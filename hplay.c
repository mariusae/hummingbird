/*
	Play HTTP requests from a stdin. Note that HTTP parsing is
	fairly robust to accomodate for packet dumps, etc.
*/

#include <event.h>
#include <string.h>
#include <stdlib.h>
#include "u.h"

struct Header {
	char key[1024];
	char value[1024*4];
};
typedef struct Header Header;


enum {
	Nfbuf = 1<<20,
};

struct Request {
	char action[5];
	char uri[1024];
	char httpversion[20];
	Header headers[10];
	int nheader;
	int nbody;
};
typedef struct Request Request;

/*
	IO
*/

static struct {
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

	if(io.peeking) {
		io.peeking = 0;
		io.nread += io.buflen;
	} else {
		io.buf = fgetln(io.file, &io.buflen);
		if (io.buf != nil) {
			/*
		 	strip trailing \n and \rs. XXX note that this may not
		 	be valid for the last string read, but we're actually
		 	not going to care.
			 */
			for(n = io.buflen-1; n >= 0 &&
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
	if(rv != nil) {
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
nullrequest(Request *r)
{
	int i, nhdr;
	Header *h;
	
	nhdr = sizeof(r->headers)/sizeof(r->headers[0]);

	r->action[0] = r->uri[0] = r->httpversion[0] = '\0';
	for(i=0; i<nhdr; i++) {
		h = &r->headers[i];
		h->key[0] = h->value[0] = '\0';
	}
	
	r->nheader = r->nbody = 0;
}

int
isvalidaction(char *action)
{
	static char* actions[] = {
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
	while((fld = strsep(&line, " ")) != nil) {
		if(*fld == '\0') continue;
		n = strlen(fld) + 1;
		switch(pos++) {
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

	if(pos == 3) {
		readline();
		return 1;
	} else
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
	Scp(hdr->key, line, sizeof(hdr->key));
	Scp(hdr->value, sep + 2, sizeof(hdr->value));
	return 1;
}

int
readrequest(Request *r)
{
	int i, clen, nhdr;
	char *line;
	
	clen = 0;

	nullrequest(r);
	if(!findfirstline(r)) return 0;
	
	nhdr = sizeof(r->headers)/sizeof(r->headers[0]);

	for(i=0; readheader(&r->headers[i]) && i<nhdr; i++) {
		if(strcasecmp(r->headers[i].key, "content-length"))
			continue;

		r->nbody = atoi(r->headers[i].value);
	}
	r->nheader = i;

	for(i=0; i<2; i++) {
		line = readline();
		if(line == nil || *line != '\0')
			return 0;
	}

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

int
main(int argc, char **argv)
{
	char *host;
	int port, pos, fail, ok;
	Request r;

	setfile(stdin);

	if(argc != 3) panic("usage: %s HOST PORT", argv[0]);
	host = argv[1];
	port = atoi(argv[2]);
	if(port == 0) panic("invalid port \"%s\"", argv[2]);

	fail = ok = 0;

	while(!eof())
		if (readrequest(&r)) ok++; else fail++;
	
	say("ok %d", ok);
	say("fail %d", fail);
	
	return 0;
}
