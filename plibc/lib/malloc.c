#include "malloc.h"
#include "stdbool.h"
#include "stddef.h"
#include "stdio.h"
#include "unistd.h"
#include "utils.h"

struct malloc_chunk
{
    size_t size;
    void *ptr;
    struct malloc_chunk *next;
    struct malloc_chunk *prev;
    bool free;
};

static struct malloc_chunk *head = NULL;
static struct malloc_chunk *tail = NULL;

static struct malloc_chunk *free_chunk_head = NULL;
static struct malloc_chunk *free_chunk_tail = NULL;
static size_t free_chunk_count = 0;

static void *init_mbrk = NULL;

static void
malloc_init ()
{
    init_mbrk = brk ();
}

static void *
unfree (struct malloc_chunk *chunk)
{
    if (chunk->prev != NULL)
        chunk->prev->next = chunk->next;

    if (chunk->next != NULL)
        chunk->next->prev = chunk->prev;

    if (chunk == head)
        head = chunk->next;

    if (chunk == tail)
        tail = chunk->prev;

    if (free_chunk_head == chunk)
        free_chunk_head = chunk->next;

    if (free_chunk_tail == chunk)
        free_chunk_tail = chunk->prev;

    if (chunk->free)
        free_chunk_count--;

    chunk->free = false;
    return chunk->ptr;
}

static void
reduce_brk ()
{
    if (free_chunk_count <= 3)
        return;

    struct malloc_chunk *chunk = free_chunk_tail;
    intptr_t lowest_brk = 0;
    void *b = brk ();

    while (chunk != NULL)
        {
            if (tail >= chunk || !chunk->free
                || (lowest_brk != 0 && lowest_brk < (intptr_t) chunk)
                || (intptr_t) b < (intptr_t) chunk)
                {
                    chunk = chunk->prev;
                    continue;
                }

            if (chunk->prev != NULL)
                chunk->prev->next = chunk->next;

            if (chunk->next != NULL)
                chunk->next->prev = chunk->prev;

            if (chunk == free_chunk_head)
                free_chunk_head = chunk->next;

            if (chunk == free_chunk_tail)
                free_chunk_tail = chunk->prev;

            struct malloc_chunk *prev = chunk->prev;
            lowest_brk = (intptr_t) chunk;
            chunk = prev;
        }

    if (lowest_brk == 0)
        return;

    if (sbrk ((intptr_t) lowest_brk) == (void *) -1)
        {
            printf ("reduce_brk(): sbrk failed\n");
            abort ();
        }
}

void *
malloc (size_t size)
{
    reduce_brk ();

    if (size == 0)
        return NULL;

    if (free_chunk_head != NULL && free_chunk_head->free
        && free_chunk_head->size >= size)
        return unfree (free_chunk_head);

    if (free_chunk_tail != NULL && free_chunk_tail->free
        && free_chunk_tail->size >= size)
        return unfree (free_chunk_tail);

    struct malloc_chunk *chunk = free_chunk_tail;

    while (chunk != NULL)
        {
            if (chunk->free && chunk->size >= size)
                return unfree (chunk);

            chunk = chunk->prev;
        }

    if (init_mbrk == NULL)
        malloc_init ();

    size_t mc_size = sizeof (struct malloc_chunk);
    void *b = brk ();
    intptr_t new_ptr = (intptr_t) (((unsigned char *) b) + size + mc_size);

    if (sbrk (new_ptr) == (void *) -1)
        return NULL;

    chunk = (struct malloc_chunk *) b;
    chunk->size = size;
    chunk->ptr = (void *) (((unsigned char *) b) + mc_size);
    chunk->next = NULL;
    chunk->prev = tail;
    chunk->free = false;

    tail = chunk;

    if (head == NULL)
        head = chunk;

    return chunk->ptr;
}

static void __noreturn
invalid_ptr (const char *func)
{
    printf ("%s(): invalid pointer\n", func);
    abort ();
}

static void __noreturn
double_free (void *ptr, size_t size)
{
    printf ("free(): double free detected: %p (block size %zu)\n", ptr, size);
    abort ();
}

void
free (void *ptr)
{
    reduce_brk ();

    size_t mc_size = sizeof (struct malloc_chunk);
    struct malloc_chunk *chunk
        = (struct malloc_chunk *) (((unsigned char *) ptr) - mc_size);

    if (chunk == NULL)
        invalid_ptr (__func__);

    if (chunk->ptr != ptr)
        invalid_ptr (__func__);

    if (chunk->free)
        double_free (chunk->ptr, chunk->size);

    free_chunk_count++;
    chunk->free = true;

    if (chunk->prev != NULL)
        chunk->prev->next = chunk->next;

    if (chunk->next != NULL)
        chunk->next->prev = chunk->prev;

    if (chunk == head)
        head = chunk->next;

    if (chunk == tail)
        tail = chunk->prev;

    if (free_chunk_head == NULL)
        {
            free_chunk_head = chunk;
            free_chunk_tail = chunk;
            chunk->next = NULL;
            chunk->prev = NULL;
        }
    else
        {
            free_chunk_tail->next = chunk;
            chunk->prev = free_chunk_tail;
            chunk->next = NULL;
            free_chunk_tail = chunk;
        }
}

void
bzero (void *ptr, size_t size)
{
    for (size_t i = 0; i < size; i++)
        ((unsigned char *) ptr)[i] = 0;
}

void *
calloc (size_t nmemb, size_t size)
{
    size_t total_size = nmemb * size;
    void *ptr = malloc (total_size);

    if (ptr == NULL)
        return NULL;

    bzero (ptr, total_size);
    return ptr;
}

void *
realloc (void *ptr, size_t size)
{
    if (ptr == NULL)
        return malloc (size);

    size_t mc_size = sizeof (struct malloc_chunk);
    struct malloc_chunk *chunk
        = (struct malloc_chunk *) (((unsigned char *) ptr) - mc_size);

    if (chunk == NULL)
        invalid_ptr (__func__);

    if (chunk->ptr != ptr)
        invalid_ptr (__func__);

    if (chunk->size >= size)
        return ptr;

    if (tail == chunk)
        {
            reduce_brk ();

            size_t new_size = size - chunk->size;
            void *b = brk ();
            intptr_t new_ptr = (intptr_t) (((unsigned char *) b) + new_size);

            if (sbrk (new_ptr) == (void *) -1)
                return NULL;

            chunk->size = size;
            return ptr;
        }

    void *new_ptr = malloc (size);

    if (new_ptr == NULL)
        return NULL;

    for (size_t i = 0; i < chunk->size; i++)
        ((unsigned char *) new_ptr)[i] = ((unsigned char *) ptr)[i];

    free (ptr);
    return new_ptr;
}

void
__plibc_heap_reset (void)
{
    if (init_mbrk == NULL)
        return;

    if (sbrk ((intptr_t) init_mbrk) == (void *) -1)
        {
            printf ("__plibc_heap_reset(): sbrk failed\n");
            abort ();
        }

    head = NULL;
    tail = NULL;
    free_chunk_head = NULL;
    free_chunk_tail = NULL;
    free_chunk_count = 0;
}