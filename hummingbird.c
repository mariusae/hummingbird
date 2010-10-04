/*
 * hummingbird - HTTP benchmarks with periodic output.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <event.h>

#define MAX_BUCKETS 100

char http_get_request[] = "GET / HTTP/1.0\r\n\r\n";
uint32_t http_host;
uint16_t http_port;

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
  int timeouts;
} counts;

struct request {
  struct timeval starttv;
  int sock;
};

struct event reportev;
struct timeval reporttv = { 1, 0 };
int request_timeout;
struct timeval ratetv;
int ratecount = 0;

void warnx(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  fflush(stderr);
  va_end(ap);
}

void errx(int code, const char *fmt, ...)
{
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

  if (what & EVBUFFER_EOF) {
    gettimeofday(&now, NULL);
    timersub(&now, &request->starttv, &diff);
    milliseconds = diff.tv_sec * 1000 + diff.tv_usec / 1000;
    for (i = 0;
         params.buckets[i] < milliseconds &&
         params.buckets[i] != 0;
         i++)
      ;
    counts.counters[i]++;
  } else {
    if (what & EVBUFFER_ERROR)
      counts.errors++;
    if (what & EVBUFFER_TIMEOUT)
      counts.timeouts++;
  }

  counts.total++;
  counts.pending--;

  bufferevent_setcb(b, NULL, NULL, NULL, NULL);
  bufferevent_disable(b, EV_READ | EV_WRITE);
  bufferevent_free(b);
  close(request->sock);
  free(request);

  /* Queue the next one. */
  if (params.count < 0 || counts.total < params.count) {
    if (dispatch_request() < 0)
      perror("failed to dispatch request");
  }
}

void
reportcb(int fd, short what, void *arg)
{
  struct timeval now, diff;
  int i, count, milliseconds;

  printf("%d\t", (int)time(NULL));
  printf("%d\t", counts.errors);
  printf("%d\t", counts.timeouts);

  for (i = 0; params.buckets[i] != 0; i++)
    printf("%d\t", counts.counters[i]);

  /* total: */
  printf("%d\n", counts.counters[i]);

  memset(counts.counters, 0, sizeof(counts.counters));

  if (params.count < 0 || counts.total < params.count)
    evtimer_add(&reportev, &reporttv);

  if ((count = counts.total - ratecount) > 10000) {
    gettimeofday(&now, NULL);
    timersub(&now, &ratetv, &diff);

    milliseconds = diff.tv_sec * 1000 + diff.tv_usec / 1000;
    if (milliseconds > 0) {
      fprintf(stderr, "rate: %d/s\n", 1000 * count / milliseconds);

      gettimeofday(&ratetv, NULL);
      ratecount = counts.total;
    }
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

  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = http_host;  // htonl(0x7f000001);
  sin.sin_port = http_port;  // htons(8686);

  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    return (-1);
  if (evutil_make_socket_nonblocking(sock) < 0)
    return (-1);

  x = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&x, sizeof(x));
  linger.l_onoff = 1;
  linger.l_linger = 0;
  if (setsockopt(sock, SOL_SOCKET, SO_LINGER,
                 (void *)&linger, sizeof(linger)) < 0)
    perror("setsockopt");

  if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0 &&
      errno != EINPROGRESS) {
    printf("errno: %d\n", errno);
    return (-1);

  }

  if ((request = calloc(1, sizeof(*request))) == NULL)
    errx(1, "calloc");

  request->sock = sock;
  gettimeofday(&request->starttv, NULL);

  b = bufferevent_new(sock, readcb, NULL, errcb, request);
  bufferevent_enable(b, EV_READ | EV_WRITE);
  bufferevent_write(b, http_get_request, sizeof(http_get_request));
  bufferevent_settimeout(b, request_timeout, request_timeout);

  counts.pending++;

  return (0);
}

void
usage(char *cmd)
{
  fprintf(
      stderr,
      "%s: [-c CONCURRENCY] [-b BUCKETS] "
      "[-n COUNT] [-p NUMPROCS] [-r INTERVAL] [HOST] [PORT]\n",
      cmd);

  exit(0);
}

int
main(int argc, char **argv)
{
  int ch, i, nprocs = 1, status, is_parent = 1, port;
  pid_t pid;
  char *sp, *ap, *host, *cmd = argv[0];
  struct hostent *he;

  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    errx(1, "failed to ignore SIGPIPE\n");

  params.count = -1;
  params.concurrency = 1;
  memset(params.buckets, 0, sizeof(params.buckets));
  params.buckets[0] = 1;
  params.buckets[1] = 10;
  params.buckets[2] = 100;

  memset(&counts, 0, sizeof(counts));

  while ((ch = getopt(argc, argv, "c:b:n:p:r:h")) != -1) {
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
        break;

      case 'n':
        params.count = atoi(optarg);
        break;

      case 'p':
        nprocs = atoi(optarg);
        break;

      case 'r':
        reporttv.tv_sec = atoi(optarg);
        break;

      case 'h':
        usage(cmd);
        break;
    }
  }

  argc -= optind;
  argv += optind;

  host = "127.0.0.1";
  port = 80;
  switch (argc) {
    case 2:
      port = atoi(argv[1]);
    case 1:
      host = argv[0];
    case 0:
      break;
    default:
      errx(1, "only 0 or 1 (host port) pair are allowed\n");
  }

  /* Resolve the name. */
  http_port = htons(port);
  if ((he = gethostbyname(host)) == NULL) {
    herror("gethostbyname");
    exit(1);
  }

  if (he->h_length != 4 || he->h_addrtype != AF_INET)
    errx(1, "unsupported address type\n");
  if (he->h_addr_list[0] == NULL)
    errx(1, "failed to resolve address\n");
  if (he->h_addr_list[1] != NULL)
    errx(1, "hostname resolves to multiple addresses\n");

  /* Already in network byte order. */
  http_host = *((uint32_t *)he->h_addr_list[0]);

  for (i = 0; params.buckets[i] != 0; i++)
    request_timeout = params.buckets[i];

  if (params.count > 0)
    params.count /= nprocs;

  /* Report the report banner in the parent. */
  fprintf(stderr, "# ts\t\terrors\ttimeout\t");
  for (i = 0; params.buckets[i] != 0; i++)
    fprintf(stderr, "<%d\t", params.buckets[i]);

  fprintf(stderr, ">=%d\n", params.buckets[i - 1]);

  for (i = 0; i < nprocs; i++) {
    if ((pid = fork()) < 0) {
      kill(0, SIGINT);
      perror("fork");
      exit(1);
    } else if (pid != 0) {
      continue;
    }

    is_parent = 0;

    event_init();

    for (i = 0; i < params.concurrency; i++) {
      if (dispatch_request() < 0)
        perror("failed to dispatch request");
    }

    evtimer_set(&reportev, reportcb, NULL);
    evtimer_add(&reportev, &reporttv);

    gettimeofday(&ratetv, NULL);

    event_dispatch();

    /*
     * fprintf(stderr, "total[%d]: %d\n", getpid(), counts.total);
     */
    break;
  }

  if (is_parent) {
    for (i = 0; i < nprocs; i++)
      pid = waitpid(0, &status, 0);
  }

  return (0);
}
