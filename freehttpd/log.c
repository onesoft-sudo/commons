#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void
log_msg (const char *fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    vfprintf (stdout, fmt, args);
    va_end (args);
}

void
log_err (const char *fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    vfprintf (stderr, fmt, args);
    va_end (args);
}