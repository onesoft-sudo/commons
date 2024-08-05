#include "xmalloc.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *
xmalloc (size_t size)
{
    void *ptr = malloc (size);

    if (ptr == NULL)
        {
            fprintf (stderr, "xmalloc(%zu): failed to allocate memory: %s",
                     size, strerror (errno));
            abort ();
        }

    return ptr;
}

void *
xcalloc (size_t nmemb, size_t size)
{
    void *ptr = calloc (nmemb, size);

    if (ptr == NULL)
        {
            fprintf (stderr, "xcalloc(%zu, %zu): failed to allocate memory: %s",
                     nmemb, size, strerror (errno));
            abort ();
        }

    return ptr;
}

void *
xrealloc (void *ptr, size_t size)
{
    void *new_ptr = realloc (ptr, size);

    if (new_ptr == NULL)
        {
            fprintf (stderr,
                     "xrealloc(%p, %zu): failed to re-allocate memory: %s", ptr,
                     size, strerror (errno));
            abort ();
        }

    return new_ptr;
}