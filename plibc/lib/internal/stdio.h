#ifndef PLIBC_INTERNAL_STDIO_H
#define PLIBC_INTERNAL_STDIO_H

#ifndef _NO_PLIBC_INTERNAL

#    include <stdbool.h>
#    include <stdio.h>

bool _plibc_fopen_internal (FILE *file, const char *pathname, const char *mode);
bool _plibc_fdopen_internal (FILE *file, int fd, const char *mode);
int _plibc_fclose_internal (FILE *file);

#endif /* _NO_PLIBC_INTERNAL */

#endif /* PLIBC_INTERNAL_STDIO_H */