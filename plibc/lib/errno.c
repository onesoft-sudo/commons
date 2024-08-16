#include "errno.h"

static int _errno = 0;

int *
get_errno_location (void)
{
    return &_errno;
}