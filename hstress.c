/*
 * hstress - HTTP load generator with periodic output.
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
#include <evhttp.h>

#include "u.h"

#define NBUFFER 10
#define MAX_BUCKETS 100

char *http_hostname;
uint16_t http_port;
char http_hosthdr[2048];

struct{
	int count;
	int concurrency;
	int buckets[MAX_BUCKETS];
	int nbuckets;
	int rpc;
}params;

struct{
	int successes;
	int counters[MAX_BUCKETS + 1];
	int errors;
	int timeouts;
	int closes;
}counts;

struct request{
	struct timeval 			starttv;
	struct event			timeoutev;
	int 					sock;
	struct evhttp_connection *evcon;
	struct evhttp_request	*evreq;
	int					evcon_reqno;
};

enum{
	Success,
	Closed,
	Error,
	Timeout
};

struct event 	reportev;
struct timeval 	reporttv ={ 1, 0 };
struct timeval	timeouttv ={ 1, 0 };
struct timeval 	lastreporttv;
int 			request_timeout;
struct timeval 	ratetv;
int 			ratecount = 0;
int			nreport = 0;
int 			nreportbuf[NBUFFER];
int 			*reportbuf[NBUFFER];

void recvcb(struct evhttp_request *req, void *arg);
void timeoutcb(int fd, short what, void *arg);
struct evhttp_connection *mkhttp();
void closecb(struct evhttp_connection *evcon, void *arg);
void report();
void sigint(int which);

/*
	Reporting.
*/

int
mkrate(struct timeval *tv, int count)
{
	int milliseconds;
	struct timeval now, diff;

	gettimeofday(&now, nil);
	timersub(&now, tv, &diff);
	milliseconds = diff.tv_sec * 1000 + diff.tv_usec / 1000;
	*tv = now;

	return(1000 * count / milliseconds);
}

void
reportcb(int fd, short what, void *arg)
{
	struct timeval now, diff;
	int i, count, milliseconds;

	printf("%d\t", nreport++);
	printf("%d\t", counts.errors);
	printf("%d\t", counts.timeouts);
	printf("%d\t", counts.closes);
	
	counts.errors = counts.timeouts = counts.closes = 0;

	for(i=0; params.buckets[i]!=0; i++)
		printf("%d\t", counts.counters[i]);

	printf("%d\n", counts.counters[i]);
	fflush(stdout);

	memset(counts.counters, 0, sizeof(counts.counters));

	if(params.count<0 || counts.successes<params.count)
		evtimer_add(&reportev, &reporttv);
}

/*
	HTTP, via libevent's HTTP support.
*/

struct evhttp_connection *
mkhttp()
{
	struct evhttp_connection *evcon;

	evcon = evhttp_connection_new(http_hostname, http_port);
	if(evcon == nil)
		panic("evhttp_connection_new");

	evhttp_connection_set_closecb(evcon, &closecb, nil);
	/*
		note: we manage our own per-request timeouts, since the underlying
		library does not give us enough error reporting fidelity
	*/
	
	/* also set some socket options manually. */
	

	return(evcon);
}

void
dispatch(struct evhttp_connection *evcon, int reqno)
{
	struct evhttp_request *evreq;
	struct request *req;

	if((req = calloc(1, sizeof(*req))) == nil)
		panic("calloc");

	req->evcon = evcon;
	req->evcon_reqno = reqno;

	evreq = evhttp_request_new(&recvcb, req);
	if(evreq == nil)
		panic("evhttp_request_new");

	req->evreq = evreq;

	evreq->response_code = -1;
	evhttp_add_header(evreq->output_headers, "Host", http_hosthdr);

	gettimeofday(&req->starttv, nil);
	evtimer_set(&req->timeoutev, timeoutcb,(void *)req);
	evtimer_add(&req->timeoutev, &timeouttv);

	evhttp_make_request(evcon, evreq, EVHTTP_REQ_GET, "/");
}

void
complete(int how, struct request *req)
{
	struct timeval now, diff;
	int i, total;
	long milliseconds;

	evtimer_del(&req->timeoutev);

	switch(how){
	case Success:
		gettimeofday(&now, nil);
		timersub(&now, &req->starttv, &diff);
		milliseconds = diff.tv_sec * 1000 + diff.tv_usec / 1000;
		for(i=0; params.buckets[i]<milliseconds &&
		    params.buckets[i]!=0; i++);
		counts.counters[i]++;
		counts.successes++;
		break;
	case Error:
		counts.errors++;
		break;
	case Timeout:
		counts.timeouts++;
		break;
	}

	total =
	    counts.successes + counts.errors + 
	    counts.timeouts /*+ counts.closes*/;
	/* enqueue the next one */
	if(params.count<0 || total<params.count){
		if(params.rpc<0 || params.rpc>req->evcon_reqno){
			dispatch(req->evcon, req->evcon_reqno + 1);
		}else{
			evhttp_connection_free(req->evcon);
			dispatch(mkhttp(), 1);
		}
	}else{
		/* We'll count this as a close. I guess that's ok. */
		evhttp_connection_free(req->evcon);
		if(--params.concurrency == 0){
			evtimer_del(&reportev);
			reportcb(0, 0, nil);  /* issue a last report */
		}
	}

	
	free(req);
}

void
recvcb(struct evhttp_request *evreq, void *arg)
{

	int status = Success;

	/* 
		It seems that, under certain circumstances, 
		evreq may be null on failure.  

		we'll count it as an error.
	*/
		 
	if(evreq == nil || evreq->response_code < 0)
		status = Error;

	complete(status,(struct request *)arg);
}

void
timeoutcb(int fd, short what, void *arg)
{
	struct request *req =(struct request *)arg;
	
	/* re-establish the connection */
	evhttp_connection_free(req->evcon);
	req->evcon = mkhttp();

	complete(Timeout, req);
}

void
closecb(struct evhttp_connection *evcon, void *arg)
{
	counts.closes++;
}


/*
	Aggregation.
*/

void
chldreadcb(struct bufferevent *b, void *arg)
{
	char *line, *sp, *ap;
	int n, i, total, nprocs = *(int *)arg;

	if((line=evbuffer_readline(b->input)) != nil){
		sp = line;

		if((ap = strsep(&sp, "\t")) == nil)
			panic("report error\n");
		n = atoi(ap);
		if(n - nreport > NBUFFER)
			panic("a process fell too far behind\n");

		n %= NBUFFER;

		for(i=0; i<params.nbuckets + 3 && (ap=strsep(&sp, "\t")) != nil; i++)
			reportbuf[n][i] += atoi(ap);

		if(++nreportbuf[n] >= nprocs){
			/* Timestamp it.  */
			printf("%d\t",(int)time(nil));
			for(i = 0; i < params.nbuckets + 3; i++)
				printf("%d\t", reportbuf[n][i]);

			/* Compute the total rate of succesful requests. */
			total = 0;
			for(i=3; i<params.nbuckets+1; i++)
				total += reportbuf[n][i];

			printf("%d\n", mkrate(&lastreporttv, total));
			
			/* Aggregate. */
			counts.errors += reportbuf[n][0];
			counts.timeouts += reportbuf[n][1];
			counts.closes += reportbuf[n][2];
			for(i=0; i<params.nbuckets; i++){
				counts.successes += reportbuf[n][i + 3];
				counts.counters[i] += reportbuf[n][i + 3];
			}

			/* Clear it. Advance nreport. */
			memset(reportbuf[n], 0,(params.nbuckets + 3) * sizeof(int));
			nreportbuf[n] = 0;
			nreport++;
		}

		free(line);
	}

	bufferevent_enable(b, EV_READ);
}

void
chlderrcb(struct bufferevent *b, short what, void *arg)
{
	int *nprocs =(int *)arg;

	bufferevent_setcb(b, nil, nil, nil, nil);
	bufferevent_disable(b, EV_READ | EV_WRITE);
	bufferevent_free(b);
	
	/*if(--(*nprocs) == 0)
		event_loopbreak();*/
}

void
parentd(int nprocs, int *sockets)
{
	int *fdp, i, status, size;
	pid_t pid;
	struct bufferevent *b;
	
	signal(SIGINT, sigint);

	gettimeofday(&ratetv, nil);
	gettimeofday(&lastreporttv, nil);
	memset(nreportbuf, 0, sizeof(nreportbuf));
	for(i=0; i<NBUFFER; i++){
		if((reportbuf[i] = calloc(params.nbuckets + 3, sizeof(int))) == nil)
			panic("calloc");
	}

	event_init();

	for(fdp=sockets; *fdp!=-1; fdp++){
		b = bufferevent_new(
		    *fdp, chldreadcb, nil, 
		    chlderrcb,(void *)&nprocs);
		bufferevent_enable(b, EV_READ);
	}

	event_dispatch();

	for(i=0; i<nprocs; i++)
		pid = waitpid(0, &status, 0);
		
	report();
}

void
sigint(int which)
{
	report();
	exit(0);
}

void
printcount(const char *name, int total, int count)
{
	fprintf(stderr, "# %s", name);
	if(total > 0)
		fprintf(stderr, "\t%d\t%.02f", count,(1.0f*count) /(1.0f*total));
	
	fprintf(stderr, "\n");
}

void
report()
{
	char buf[128];
	int i, total = counts.successes + counts.errors + counts.timeouts;

	printcount("successes", total, counts.successes);
	printcount("errors", total, counts.errors);
	printcount("timeouts", total, counts.timeouts);
	printcount("closes", total, counts.closes);
	for(i=0; params.buckets[i]!=0; i++){
		snprintf(buf, sizeof(buf), "<%d\t", params.buckets[i]);
		printcount(buf, total, counts.counters[i]);
	}
	
	snprintf(buf, sizeof(buf), ">=%d\t", params.buckets[i - 1]);
	printcount(buf, total, counts.counters[i]);
	
	/* no total */
	fprintf(stderr, "# hz\t\t%d\n", mkrate(&ratetv, counts.successes));
}

/*
	Main, dispatch.
*/

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
	int ch, i, nprocs = 1, is_parent = 1, port, *sockets, fds[2];
	pid_t pid;
	char *sp, *ap, *host, *cmd = argv[0];
	struct hostent *he;

	/* Defaults */
	params.count = -1;
	params.rpc = -1;
	params.concurrency = 1;
	memset(params.buckets, 0, sizeof(params.buckets));
	params.buckets[0] = 1;
	params.buckets[1] = 10;
	params.buckets[2] = 100;
	params.nbuckets = 4;

	memset(&counts, 0, sizeof(counts));

	while((ch = getopt(argc, argv, "c:b:n:p:r:i:h")) != -1){
		switch(ch){
		case 'b':
			sp = optarg;

			memset(params.buckets, 0, sizeof(params.buckets));

			for(i=0; i<MAX_BUCKETS && (ap=strsep(&sp, ",")) != nil; i++)
				params.buckets[i] = atoi(ap);

			params.nbuckets = i;

			if(params.buckets[0] == 0)
				panic("first bucket must be >0\n");

			for(i=1; params.buckets[i]!=0; i++){
				if(params.buckets[i]<params.buckets[i-1])
					panic("invalid bucket specification!\n");
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

		case 'i':
			reporttv.tv_sec = atoi(optarg);
			break;

		case 'r':
			params.rpc = atoi(optarg);
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
	switch(argc){
	case 2:
		port = atoi(argv[1]);
	case 1:
		host = argv[0];
	case 0:
		break;
	default:
		panic("only 0 or 1(host port) pair are allowed\n");
	}
	
	http_hostname = host;
	http_port = port;
	if(snprintf(http_hosthdr, sizeof(http_hosthdr), "%s:%d", host, port) > sizeof(http_hosthdr))
		panic("snprintf");

	for(i = 0; params.buckets[i] != 0; i++)
		request_timeout = params.buckets[i];

	if(params.count > 0)
		params.count /= nprocs;

#if 0
	event_init();
	dispatch(mkhttp(), 1);
	event_dispatch(); exit(0);
#endif

	fprintf(stderr, "# params: c=%d p=%d n=%d r=%d\n", 
	    params.concurrency, nprocs, params.count, params.rpc);

	fprintf(stderr, "# ts\t\terrors\ttimeout\tcloses\t");
	for(i=0; params.buckets[i]!=0; i++)
		fprintf(stderr, "<%d\t", params.buckets[i]);

	fprintf(stderr, ">=%d\thz\n", params.buckets[i - 1]);

	if((sockets = calloc(nprocs + 1, sizeof(int))) == nil)
		panic("malloc\n");

	sockets[nprocs] = -1;

	for(i=0; i<nprocs; i++){
		if(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0){
			perror("socketpair");
			exit(1);
		}

		sockets[i] = fds[0];

		if((pid = fork()) < 0){
			kill(0, SIGINT);
			perror("fork");
			exit(1);
		}else if(pid != 0){
			close(fds[1]);
			continue;
		}

		is_parent = 0;

		event_init();

		/* Set up output. */
		if(dup2(fds[1], STDOUT_FILENO) < 0){
			perror("dup2");
			exit(1);
		}

		close(fds[1]);

		for(i=0; i<params.concurrency; i++)
			dispatch(mkhttp(), 1);

		evtimer_set(&reportev, reportcb, nil);
		evtimer_add(&reportev, &reporttv);

		event_dispatch();

		break;
	}

	if(is_parent)
		parentd(nprocs, sockets);

	return(0);
}
