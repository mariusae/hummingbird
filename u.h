#define nil NULL

void panic(const char *fmt, ...);
void say(const char *fmt, ...);
void Scp(char *dst, char *src, size_t n);
ssize_t atomicio(ssize_t (f)(), int fd, void *_s, size_t n);

void *mal(size_t siz);
void *remal(void *p, size_t siz);
char *xfgetln(FILE *fp, size_t *len);
