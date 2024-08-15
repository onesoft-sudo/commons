#ifndef PLIBC_MALLOC_H
#define PLIBC_MALLOC_H

#include <stddef.h>

void *malloc (size_t size);
void *calloc (size_t nmemb, size_t size);
void *realloc (void *ptr, size_t size);
void free (void *ptr);

void bzero (void *ptr, size_t size);

#ifdef _PLIBC_INTERNALS
void __plibc_heap_reset (void);
#endif /* _PLIBC_INTERNALS */

#endif /* PLIBC_MALLOC_H */