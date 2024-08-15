#define _PLIBC_INTERNALS

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
_c_start (int argc __attribute_maybe_unused__,
          char **argv __attribute_maybe_unused__)
{
    return 0;
}