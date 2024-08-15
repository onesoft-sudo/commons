#ifndef PLIBC_SYSCALLS_H
#define PLIBC_SYSCALLS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>

extern int write (int fd, const void *buf, size_t count);
extern void *sbrk (intptr_t increment);
extern void *brk (void);
extern void __attribute__ ((noreturn)) exit (int status);
extern void *mmap (void *addr, size_t len, int prot, int flags, int fd,
                   off_t offset);
extern void *sysinfo (void *buffer);
extern void kill (int pid, int sig);

#endif /* PLIBC_SYSCALLS_H */