/*
	Play HTTP requests from a stdin. Note that HTTP parsing is
	fairly robust to accomodate for packet dumps, etc.
*/

#include <event.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <event.h>
#include <evhttp.h>

#include "u.h"

struct Header{
	char key[50];
	char value[1024];
};
typedef struct Header Header;


enum{
	Nfbuf = 1<<20,
	Nq = 100,
};

struct Request{
	char action[5];
	char uri[1024];
	char httpversion[20];
	Header headers[10];
	int nheader;
	int nbody;
};
typedef struct Request Request;

struct Run{
	Request *rs;
	int rsiz;
	struct timeval tv;
	struct event ev;
	char *host;
	short port;
	struct evhttp_connection *cachedconn;
};
typedef struct Run Run;

struct Call{
	Run *run;
	struct evhttp_connection *conn;
};
typedef struct Call Call;

/*
	IO
*/

static struct{
	char *buf;
	size_t buflen;
	int peeking;
	int nread;
	FILE *file;
	char fbuf[Nfbuf];
} io;

void
setfile(FILE *f)
{
	io.file = f;
	io.peeking = 0;
	io.nread = 0;
	io.buf = nil;

	setvbuf(io.file, io.fbuf, _IOFBF, Nfbuf);
}

char *
readline()
{
	int n;

	if(io.peeking){
		io.peeking = 0;
		io.nread += io.buflen;
	}else{
		io.buf = fgetln(io.file, &io.buflen);
		if (io.buf != nil){
			/*
		 	strip trailing \n and \rs. XXX note that this may not
		 	be valid for the last string read, but we're actually
		 	not going to care.
			 */
			for(n=io.buflen-1; n>=0 &&
				(io.buf[n] == '\r' || io.buf[n] == '\n'); n--);
			io.buf[n+1] = '\0';
		}
		io.nread += io.buflen;
	}

	return io.buf;
}

char *
peekline()
{
	char *rv;

	if(io.peeking)
		return io.buf;

	rv = readline();
	if(rv != nil){
		io.peeking = 1;
		io.nread -= io.buflen;
	}

	return rv;
}

int
eof()
{
	return feof(io.file);
}

int
tell()
{
	return io.nread;
}

/*
	HTTP
*/

void
zerorequest(Request *r)
{
	int i, nhdr;
	Header *h;

	nhdr = sizeof(r->headers)/sizeof(r->headers[0]);

	r->action[0] = r->uri[0] = r->httpversion[0] = '\0';
	for(i=0; i<nhdr; i++){
		h = &r->headers[i];
		h->key[0] = h->value[0] = '\0';
	}

	r->nheader = r->nbody = 0;
}

int
isvalidaction(char *action)
{
	static char* actions[] ={
		"GET", "POST", "PUT"
	};
	int i;

	for(i=0; i<sizeof(actions)/sizeof(*actions); i++)
		if(strcasecmp(actions[i], action) == 0)
			return 1;

	return 0;
}

int
readfirstline(Request *r)
{
	char *line, *fld;
	int pos;
	long n;

	line = peekline();
	if(line == nil) return 0;

	/* XXX: note that we modify a peek'd line, which is 
	not good behavior. however, our caller will always
	skip any failed line. obviously this does not generalize */
	pos = 0;
	while((fld = strsep(&line, " ")) != nil){
		if(*fld == '\0') continue;
		n = strlen(fld) + 1;
		switch(pos++){
		case 0:
			if(!isvalidaction(fld))
				return 0;
			strcpy(r->action, fld);
			break;
		case 1:
			if (n > sizeof(r->uri)) return 0;
			memcpy(r->uri, fld, n);
			break;
		case 2:
			if (n > sizeof(r->httpversion)) return 0;
			memcpy(r->httpversion, fld, n);
			break;
		default:
			return 0;
		}
	}

	if(pos == 3){
		readline();
		return 1;
	}else
		return 0;
}

int
findfirstline(Request *r)
{
	while(!eof())
		if(readfirstline(r)) return 1;
		else readline(); /* skip */

	return 0;
}

int
readheader(Header *hdr)
{
	char *line, *sep;

	line = peekline();
	if(line == nil) return 0;
	sep = strstr(line, ":");
	if(sep == nil || sep[1] == '\0') return 0;
	readline();

	*sep = '\0';
	// okay so here we're smashing some stacks.
	Scp(hdr->key, line, sizeof(hdr->key));
	Scp(hdr->value, sep + 2, sizeof(hdr->value));
	return 1;
}

int
readrequest(Request *r)
{
	int i, clen, nhdr;
	
	clen = 0;

	zerorequest(r);
	if(!findfirstline(r))
		return 0;

	nhdr = sizeof(r->headers)/sizeof(r->headers[0]);
	for(i=0; i < nhdr && readheader(&r->headers[i]); i++){
		if(strcasecmp(r->headers[i].key, "content-length"))
			continue;

		r->nbody = atoi(r->headers[i].value);
	}
	r->nheader = i;

/*
	for(i=0; i<2; i++){
		line = readline();
		if(line == nil || *line != '\0'){
			say("sad");
			return 0;
		}
	}
*/

	return 1;
}

void
sayrequest(Request *r)
{
	int i;

	say("action: %s", r->action);
	say("uri: %s", r->uri);
	say("httpversion: %s", r->httpversion);
	for(i=0; i<r->nheader; i++)
		say("%s: %s", r->headers[i].key, r->headers[i].value);
	say("body = %d bytes", r->nbody);
}

struct evhttp_connection *
mkconn(char *host, short port)
{
	struct evhttp_connection *conn;

	if((conn = evhttp_connection_new(host, port)) == nil)
		panic("evhttp_connection_new");
		
	return conn;
}

void
donecb(struct evhttp_request *req, void *arg)
{
	Call *call;
	Run *run;

	call = (Call*)arg;
	run = call->run;

	if(run->cachedconn == nil)
		run->cachedconn = call->conn;
	else
		evhttp_connection_free(call->conn);
}

void
runcb(int fd, short what, void *arg)
{
	Run *run;
	Request *r;
	Call *c;
	Header *h;
	struct evhttp_connection *conn;
	struct evhttp_request *req;
	enum evhttp_cmd_type cmd;
	int i;

	run = (Run*)arg;
	r = &run->rs[rand() % run->rsiz];

	if(run->cachedconn!=nil){
		conn = run->cachedconn;
		run->cachedconn = nil;
	}else
		conn = mkconn(run->host, run->port);

	c = mal(sizeof(*c));
	c->run = run;
	c->conn = conn;

	req = evhttp_request_new(&donecb, c);

	if(strcasecmp(r->action, "get")==0)
		cmd = EVHTTP_REQ_GET;
	else if(strcasecmp(r->action, "post")==0)
		cmd = EVHTTP_REQ_POST;
	else
		panic("invalid action \"%s\"", r->action);

	for(i=0;i<r->nheader;i++){
		h = &r->headers[i];
		evhttp_add_header(
		    req->output_headers,
		    h->key, h->value);
	}

	evhttp_make_request(conn, req, cmd, r->uri);

	event_add(&run->ev, &run->tv);
}

int
main(int argc, char **argv)
{
	char *host;
	int port, fail;
	Request *rs;
	Run run;
	int n, i, qps;
	FILE **fs, *f;

	if(argc < 4)
		panic("usage: %s host port qps [file ...]", argv[0]);
	host = argv[1];
	port = atoi(argv[2]);
	if(port == 0)
		panic("invalid port \"%s\"", argv[2]);
	qps = atoi(argv[3]);
	if(qps==0)
		panic("invalid QPS \"%s\"", argv[3]);

	fail = 0;

	i = 0;
	n = 1000;
	rs = mal(n*sizeof(*rs));
	
	if(argc > 4){
		fs = alloca((argc-3)*sizeof(FILE*));
		for(i=0;i<argc-4;i++){
			fs[i] = fopen(argv[i+4], "r");
			if(fs[i] == nil)
				panic("failed to open \"%s\"", argv[i+3]);
		}
		fs[i] = nil;
	}else{
		fs = alloca(2*sizeof(FILE*));
		fs[0] = stdin;
		fs[1] = nil;
	}

	while((f=*(fs++)) != nil){
		setfile(f);
		while(!eof()){
			if(i == n){
				n *= 2;
				rs = remal(rs, n*sizeof(*rs));
			}
			if(readrequest(&rs[i]))
				i++;
			else
				fail++;
		}
	}

	say("parsed %d requests, failed %d", i, fail);

	event_init();
	
	run.rs = rs;
	run.rsiz = i;
	run.tv.tv_sec = 0;
	run.tv.tv_usec = 1000000/qps;
	run.host = host;
	run.port = port;
	run.cachedconn = nil;

	evtimer_set(&run.ev, runcb, &run);
	evtimer_add(&run.ev, &run.tv);

	event_dispatch();

	return 0;
}
