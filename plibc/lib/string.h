#ifndef PLIBC_STRING_H
#define PLIBC_STRING_H

#include <stddef.h>

size_t
strlen (const char *str)
{
    size_t len = 0;

    while (str[len])
        len++;

    return len;
}

#endif /* PLIBC_STRING_H */