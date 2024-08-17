#include "stdlib.h"
#include "syscalls.h"

static void (*atexit_funcs[32]) (void);

void
atexit (void (*func) (void))
{
    static int i = 0;

    if (i < 32)
        atexit_funcs[i++] = func;
}

void
_plibc_call_exit_handlers ()
{
    for (int i = 0; i < 32; i++)
        if (atexit_funcs[i] != NULL)
            atexit_funcs[i]();
}

void
exit (int status)
{
    _plibc_call_exit_handlers ();
    _exit (status);
}

void
_exit (int status)
{
    sys_exit (status);
}