/*
 * hummingbird - HTTP benchmarks with periodic output.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <event.h>

#define MAX_BUCKETS 100

char http_get_request[] = "GET / HTTP/1.0\r\n\r\n";

struct {
  int count;
  int concurrency;
  int buckets[MAX_BUCKETS];
} params;

struct {
  int total;
  int counters[MAX_BUCKETS + 1];
  int pending;
  int errors;
} counts;

struct request {
  struct timeval starttv;
  int sock;
};

struct event reportev;
struct timeval reporttv = { 1, 0 };
struct timeval timeouttv = { 1, 0 };
struct timeval ratetv;
int ratecount = 0;

void warnx(const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  fflush(stderr);
  va_end(ap);
}

void errx(int code, const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  fflush(stderr);
  va_end(ap);
  exit(code);
}

void
readcb(struct bufferevent *b, void *arg)
{
  evbuffer_drain(b->input, EVBUFFER_LENGTH(b->input));
}

void
errcb(struct bufferevent *b, short what, void *arg)
{
  struct request *request = arg;
  struct timeval now, diff;
  int i;
  long milliseconds;

  bufferevent_setcb(b, NULL, NULL, NULL, NULL);
  bufferevent_disable(b, EV_READ | EV_WRITE);
  bufferevent_free(b);
  close(request->sock);
  free(request);

  if (what & EVBUFFER_EOF) {
    counts.total++;
    gettimeofday(&now, NULL);
    timersub(&now, &(request->starttv), &diff);
    milliseconds = diff.tv_sec * 1000 + diff.tv_usec / 1000;
    for (i = 0;
         params.buckets[i] < milliseconds &&
         params.buckets[i] != 0;
         i++)
      ;
    counts.counters[i]++;
  } else {
    counts.errors++;
  }

  counts.pending--;

  /* Queue the next one. */
  if (counts.total < params.count) {
    if (dispatch_request() < 0)
      perror("failed to dispatch request");
  }
}

void
reportcb(int fd, short what, void *arg) {
  struct timeval now, diff;
  int i, count;

  printf("%d\t", (int)time(NULL));
  printf("%d\t", counts.errors);

  /* TODO - report timeouts separately from other errors */

  for (i = 0; params.buckets[i] != 0; i++)
    printf("%d\t", counts.counters[i]);

  /* total: */
  printf("%d\n", counts.counters[i]);

  memset(counts.counters, 0, sizeof(counts.counters));

  if (counts.total < params.count)
    evtimer_add(&reportev, &reporttv);

  if ((count = counts.total - ratecount) > 10000) {
    gettimeofday(&now, NULL);
    timersub(&now, &ratetv, &diff);

    fprintf(stderr, "rate: %d/s\n", count / (int)diff.tv_sec);

    gettimeofday(&ratetv, NULL);
    ratecount = counts.total;
  }
}

int
dispatch_request()
{
  int sock, x;
  struct sockaddr_in sin;
  struct bufferevent *b;
  struct linger linger;
  struct request *request;

  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl(0x7f000001);
  sin.sin_port = htons(8686);

  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    return (-1);
  if (evutil_make_socket_nonblocking(sock) < 0)
    return (-1);

  x = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&x, sizeof(x));
  linger.l_onoff = 1;
  linger.l_linger = 0;
  if (setsockopt(sock, SOL_SOCKET, SO_LINGER,
                 (void *)&linger, sizeof(linger)) < 0) {
    perror("setsockopt");
  }

  if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0 &&
      errno != EINPROGRESS) {
    return (-1);
  }

  if ((request = malloc(sizeof(*request))) == NULL)
    errx(1, "malloc");

  request->sock = sock;
  gettimeofday(&request->starttv, NULL);

  b = bufferevent_new(sock, readcb, NULL, errcb, request);
  bufferevent_enable(b, EV_READ | EV_WRITE);
  bufferevent_write(b, http_get_request, sizeof(http_get_request));
  /* Use max bucket value (seconds). */
  bufferevent_settimeout(b, 1, 1);

  counts.pending++;

  return (0);
}

int
main(int argc, char **argv)
{
  int ch, i;
  char *sp, *ap;

  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    errx(1, "failed to ignore SIGPIPE\n");

  params.count = 10000;
  params.concurrency = 1;
  memset(params.buckets, 0, sizeof(params.buckets));
  params.buckets[0] = 1;
  params.buckets[1] = 10;
  params.buckets[2] = 100;

  memset(&counts, 0, sizeof(counts));

  while ((ch = getopt(argc, argv, "c:b:n:")) != -1) {
    switch (ch) {
      case 'b':
        sp = optarg;

        memset(params.buckets, 0, sizeof(params.buckets));

        for (i = 0; i < MAX_BUCKETS && (ap = strsep(&sp, ",")) != NULL; i++)
          params.buckets[i] = atoi(ap);

        if (params.buckets[0] == 0)
          errx(1, "first bucket must be >0\n");

        for (i = 1; params.buckets[i] != 0; i++) {
          if (params.buckets[i] < params.buckets[i-1])
            errx(1, "invalid bucket specification!\n");
        }
        break;

      case 'c':
        params.concurrency = atoi(optarg);

      case 'n':
        params.count = atoi(optarg);
        break;
    }
  }

  event_init();

  for (i = 0; i < params.concurrency; i++) {
    if (dispatch_request() < 0)
      perror("failed to dispatch request");
  }

  evtimer_set(&reportev, reportcb, NULL);
  evtimer_add(&reportev, &reporttv);

  gettimeofday(&ratetv, NULL);

  event_dispatch();

  fprintf(stderr, "total: %d\n", counts.total);

  return (0);
}
