#ifndef PLIBC_STRING_H
#define PLIBC_STRING_H

#include "stddef.h"

size_t strlen (const char *str);
const char *strerror (int errnum);
void *memcpy (void *dest, const void *src, size_t n);

#endif /* PLIBC_STRING_H */