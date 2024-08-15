.text

.globl printf
.type printf, @function

printf:
    push %rbp
    mov %rsp, %rbp

    push %r9
    push %r8
    push %rcx
    push %rdx
    push %rsi
    
    /* The first argument is the format string, which is already loaded in 
       %rdi.
       The second argument is a pointer to a region in the stack where 
       arguments passed to the registers are pushed in reverse order for easy
       access. */
    lea -40(%rbp), %rsi

    /* Since the System V ABI requires that the remaining arguments are pushed 
       to the stack, we need to make sure we have a way to access them.
       We can go back 16 bytes from the current base pointer to access the
       first argument, since the last 8 bytes before the base pointer are
       the return address and the first 8 bytes are base pointer of the 
       previous frame. */
    lea 16(%rbp), %rdx
    
    call printf_internal
    mov %rbp, %rsp
    pop %rbp
    ret

.section .note.GNU-stack,"",@progbits
