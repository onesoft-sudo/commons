#ifndef UAR_XMALLOC_H
#define UAR_XMALLOC_H

#include <stddef.h>

/* Allocate memory and abort process on error. */
void *xmalloc (size_t size);

/* Allocate 'n' zeroed memory block of 'size' and abort process on error. */
void *xcalloc (size_t nmemb, size_t size);

/* Reallocate memory and abort process on error. */
void *xrealloc (void *ptr, size_t size);

#endif /* UAR_XMALLOC_H */