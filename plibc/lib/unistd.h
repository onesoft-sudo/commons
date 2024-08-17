#ifndef PLIBC_UNISTD_H
#define PLIBC_UNISTD_H

#include "stddef.h"
#include "stdint.h"
#include "sys/types.h"

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define O_RDONLY 0x0
#define O_WRONLY 0x1
#define O_RDWR 0x2
#define O_CREAT 0x40
#define O_TRUNC 0x200
#define O_APPEND 0x400

int write (int fd, const void *buf, size_t count);
int open (const char *pathname, int flags);
int close (int fd);

void *sbrk (intptr_t increment);
void *brk (void);
void *mmap (void *addr, size_t len, int prot, int flags, int fd, off_t offset);
void *sysinfo (void *buffer);
int kill (pid_t pid, int sig);

#endif