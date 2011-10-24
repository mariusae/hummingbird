#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "u.h"

void
panic(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	fflush(stderr);
	va_end(ap);
	exit(1);
}

void
say(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	printf("\n");
	fflush(stdout);
	va_end(ap);
}

void
Scp(char *dst, char *src, size_t n)
{
	strncpy(dst, src, n);
	dst[n-1] = '\0';
}

ssize_t
atomicio(ssize_t (f)(), int fd, void *_s, size_t n)
{
	char *s = _s;
	ssize_t res, pos = 0;

	while(n>pos){
		res = (f)(fd, s + pos, n - pos);
		switch(res){
		case -1:
			if (errno == EINTR || errno == EAGAIN)
				continue;
		case 0:
			if (pos != 0)
				return pos;
			return res;
		default:
			pos += res;
		}
	}
	return pos;
}

void *
mal(size_t siz)
{
	void *p;
	p = malloc(siz);
	if(p==nil)
		panic("malloc");
	return p;
}

void *
remal(void *p, size_t siz)
{
	void *p1;
	p1 = realloc(p, siz);
	if(p==nil)
		panic("realloc");
	return p1;
}

char *
xfgetln(FILE *fp, size_t *len)
{
	static char *buf = NULL;
	static size_t bufsiz = 0;
	char *ptr;


	if (buf == NULL) {
		bufsiz = BUFSIZ;
		if ((buf = malloc(bufsiz)) == NULL)
			return NULL;
	}

	if (fgets(buf, bufsiz, fp) == NULL)
		return NULL;

	*len = 0;
	while ((ptr = strchr(&buf[*len], '\n')) == NULL) {
		size_t nbufsiz = bufsiz + BUFSIZ;
		char *nbuf = realloc(buf, nbufsiz);

		if (nbuf == NULL) {
			int oerrno = errno;
			free(buf);
			errno = oerrno;
			buf = NULL;
			return NULL;
		} else
			buf = nbuf;

		*len = bufsiz;
		if (fgets(&buf[bufsiz], BUFSIZ, fp) == NULL)
			return buf;

		bufsiz = nbufsiz;
	}

	*len = (ptr - buf) + 1;
	return buf;
}
