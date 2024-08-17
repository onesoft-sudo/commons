#ifndef PLIBC_STDLIB_H
#define PLIBC_STDLIB_H

#include "malloc.h"
#include "stddef.h"
#include "utils.h"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void __noreturn exit (int status);
void __noreturn _exit (int status);
void atexit (void (*func) (void));

#endif /* PLIBC_STDLIB_H */