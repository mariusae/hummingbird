#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <event.h>
#include <evhttp.h>

#include "u.h"

static void respond(struct evhttp_request *req, void *arg);
char content[6*1024];

void
serve(char *host, short port)
{
	struct event_base *base;
	struct evhttp *http;
	struct evhttp_bound_socket *sock;

	assert(host != nil);
	assert(port != 0);

	base = event_init();
	if (base == nil) panic("malloc");
	http = evhttp_new(base);
	if (http == nil) panic("malloc");

	if (evhttp_bind_socket(http, host, port) != 0)
		panic("failed to bind port %d", port);

	say("listening on %s:%d", host, port);

	evhttp_set_gencb(http, respond, nil);
	event_base_dispatch(base);
}

void
respond(struct evhttp_request *req, void *arg)
{
	struct evbuffer *buf;
	buf = evbuffer_new();
	evbuffer_add_reference(buf, content, sizeof(content), nil, nil);
	evhttp_send_reply(req, HTTP_OK, "nectar", buf);
	evbuffer_free(buf);
}

void
usage(char *name)
{
	panic("Usage: %s <port>", name);
}

int
main(int argc, char **argv)
{
	char *end;

	if (argc != 2) usage(argv[0]);

	int port = strtoul(argv[1], &end, 10);
	if (port == 0 && (errno == EINVAL || errno == ERANGE))
		panic("Invalid port \"%s\"", end);

	memset(content, 'Z', sizeof(content));

	serve("127.0.0.1", port);
	return 0;
}
