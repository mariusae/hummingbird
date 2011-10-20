#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

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