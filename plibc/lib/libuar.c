#include "malloc.h"
#include "stdio.h"
#include "syscalls.h"

void
libuar_init ()
{
}

void
libuar_deinit ()
{
}

int
_c_start (int argc, char **argv)
{
    int *p = malloc (sizeof (int));
    *p = 42;
    printf ("Hello, world! %d\n", *p);
    return 0;
}