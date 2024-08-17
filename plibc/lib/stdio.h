#ifndef PLIBC_STDIO_H
#define PLIBC_STDIO_H

#include "format.h"
#include "stddef.h"

#define EOF (-1)

typedef struct
{
    int fd;
    void *buf;
    size_t buf_size;
    int mode;
} FILE;

#define stdin ((FILE *) _iobuf)
#define stdout ((FILE *) (_iobuf + 1))
#define stderr ((FILE *) (_iobuf + 2))

extern int printf (const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2)));

int puts (const char *str);
int putsnl (const char *str);
int putchar (int c);

FILE *fopen (const char *pathname, const char *mode);
size_t fwrite (const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fclose (FILE *stream);
int fflush (FILE *stream);
int fputs (const char *str, FILE *stream);
FILE *fdopen (int fd, const char *mode);

extern FILE _iobuf[3];

#endif /* PLIBC_STDIO_H */