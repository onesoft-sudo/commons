.text

.globl plibc_init
.type plibc_init, @function

plibc_init:
    call get_errno_location
    xorl %edi, %edi
    movl %edi, (%rax)
    call plibc_init_resources
    ret

.globl plibc_deinit
.type plibc_deinit, @function

plibc_deinit:
    call plibc_deinit_resources
    ret

.globl plibc_deinit_sfds
.type plibc_deinit_sfds, @function

plibc_deinit_sfds:
    push %rbp
    mov %rsp, %rbp
    call plibc_sfds_status     
    push %rax   
    mov $3, %rcx
.plibc_deinit_sfds.loop_start:
    mov %rcx, %rdi
    dec %rdi
    mov -8(%rbp), %rax
    movb (%rax, %rdi), %al
    test %al, %al
    jz .plibc_deinit_sfds.loop_repeat
    push %rcx
    call close
    pop %rcx
.plibc_deinit_sfds.loop_repeat:
    loop .plibc_deinit_sfds.loop_start
.plibc_deinit_sfds.loop_end:
    mov %rbp, %rsp
    pop %rbp
    ret

.section .note.GNU-stack,"",@progbits
