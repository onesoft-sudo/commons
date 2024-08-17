#ifndef PLIBC_SYSCALLS_H
#define PLIBC_SYSCALLS_H

#include "stddef.h"
#include "stdint.h"
#include <sys/types.h>

extern int sys_write (int fd, const void *buf, size_t count);
extern void *sys_sbrk (intptr_t increment);
extern void *sys_brk (void);
extern void __attribute__ ((noreturn)) sys_exit (int status);
extern void *sys_mmap (void *addr, size_t len, int prot, int flags, int fd,
                       off_t offset);
extern void *sys_sysinfo (void *buffer);
extern int sys_kill (int pid, int sig);
extern int sys_open (const char *pathname, int flags, mode_t mode);
extern int sys_close (int fd);

#endif /* PLIBC_SYSCALLS_H */