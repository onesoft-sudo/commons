#ifndef PLIBC_STDIO_H
#define PLIBC_STDIO_H

#include "format.h"

extern int printf (const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2)));

int puts (const char *str);
int putsnl (const char *str);
extern int putchar (int c);

#endif /* PLIBC_STDIO_H */