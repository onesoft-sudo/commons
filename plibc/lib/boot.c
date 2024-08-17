#include "internal/stdio.h"
#include "stdio.h"
#include "unistd.h"

FILE _iobuf[3] = { 0 };

void
plibc_init_resources ()
{
    for (int i = 0; i < 3; i++)
        {
            if (!_plibc_fdopen_internal (&_iobuf[i], i, i == 0 ? "r" : "w"))
                {
                    write (
                        STDERR_FILENO,
                        "plibc: failed to open standard file descriptor(s)\n",
                        50);

                    _iobuf[i].fd = -1;
                }
        }
}

void
plibc_deinit_resources ()
{
    for (int i = 0; i < 3; i++)
        {
            if (_iobuf[i].fd == -1)
                continue;

            _plibc_fclose_internal (&_iobuf[i]);
        }
}