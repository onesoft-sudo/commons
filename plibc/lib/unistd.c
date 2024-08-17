#include "unistd.h"
#include "errno.h"
#include "stdbool.h"
#include "stdio.h"
#include "syscalls.h"

volatile bool plibc_sfds_status_array[3] = { true, true, true };

static inline int
syscall_wrapper (int ret)
{
    if (ret < 0)
        {
            errno = -ret;
            return -1;
        }

    return ret;
}

int
write (int fd, const void *buf, size_t count)
{
    return syscall_wrapper (sys_write (fd, buf, count));
}

void *
sbrk (intptr_t increment)
{
    void *ptr = sys_sbrk (increment);

    if (ptr == (void *) -1)
        {
            errno = ENOMEM;
            return (void *) -1;
        }

    return ptr;
}

void *
brk (void)
{
    void *ptr = sys_brk ();

    if (ptr == (void *) -1)
        {
            errno = ENOMEM;
            return (void *) -1;
        }

    return ptr;
}

void *
mmap (void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    void *ptr = sys_mmap (addr, len, prot, flags, fd, offset);

    if (ptr == (void *) -1)
        {
            errno = ENOMEM;
            return (void *) -1;
        }

    return ptr;
}

void *
sysinfo (void *buffer)
{
    void *ptr = sys_sysinfo (buffer);

    if (ptr == (void *) -1)
        {
            errno = ENOMEM;
            return (void *) -1;
        }

    return ptr;
}

int
kill (pid_t pid, int sig)
{
    return syscall_wrapper (sys_kill (pid, sig));
}

int
open (const char *pathname, int flags)
{
    return syscall_wrapper (sys_open (pathname, flags, 0644));
}

int
close (int fd)
{
    int ret = syscall_wrapper (sys_close (fd));

    if (ret < 0)
        return ret;

    if (fd < 3)
        plibc_sfds_status_array[fd] = false;

    return ret;
}

const volatile bool *
plibc_sfds_status ()
{
    return plibc_sfds_status_array;
}