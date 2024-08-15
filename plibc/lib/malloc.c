#include "malloc.h"
#include "stdio.h"
#include "syscalls.h"
#include <stdbool.h>
#include <stddef.h>

#define __noreturn __attribute__ ((noreturn))

struct malloc_chunk
{
    size_t size;
    void *ptr;
    struct malloc_chunk *next;
    struct malloc_chunk *prev;
};

static struct malloc_chunk *head = NULL;
static struct malloc_chunk *tail = NULL;

static void *init_mbrk = NULL;
static void *mbrk = NULL;

static void
malloc_init ()
{
    mbrk = brk ();
    init_mbrk = mbrk;
}

void __noreturn
abort ()
{
    write (1, "Aborted\n", 8);
    exit (-1);
}

#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
#define MMAP_FAILED ((void *) -1)

static long long int
get_page_size ()
{
    unsigned char *sysinfo_struct;

    if (sysinfo (&sysinfo_struct) < 0)
        abort ();

    return *(long long int *) (sysinfo_struct + 8);
}

static struct malloc_chunk *
new_chunk (size_t size, void *ptr)
{
    size_t aligned_size = 4096;
    printf ("Size: %lu\n", aligned_size);

    char *data
        = mmap (NULL, aligned_size * sizeof (struct malloc_chunk),
                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (data == MMAP_FAILED || data == NULL)
        return NULL;

    printf ("new_chunk: mmap() successful\n");
    printf ("ptr: %p\n", ptr);

    data[0] = 0x41;

    printf ("accessed: %p\n", ptr);
    return NULL;
}

void *
malloc (size_t size)
{
    if (mbrk == NULL)
        malloc_init ();

    void *b = brk ();

    if (sbrk ((intptr_t) (((unsigned char *) b) + size)) == (void *) -1)
        return NULL;

    puts ("malloc: brk() successful");
    struct malloc_chunk *chunk = new_chunk (size, b);
    puts ("malloc: new_chunk() successful");

    if (chunk == NULL)
        return NULL;

    if (head == NULL)
        head = chunk;
    else
        {
            tail->next = chunk;
            chunk->prev = tail;
        }

    tail = chunk;
    return b;
}

void
free (void *ptr)
{
    if (ptr == NULL)
        return;

    struct malloc_chunk *chunk = head;
    bool found = false;

    while (chunk != NULL)
        {
            if (chunk->ptr == ptr)
                {
                    if (chunk->prev != NULL)
                        chunk->prev->next = chunk->next;

                    if (chunk->next != NULL)
                        chunk->next->prev = chunk->prev;

                    if (chunk == tail)
                        tail = chunk->prev;

                    if (chunk == head)
                        head = chunk->next;

                    found = true;
                    break;
                }

            chunk = chunk->next;
        }

    if (!found)
        {
            write (1, "free(): invalid pointer\n", 24);
            abort ();
        }
}