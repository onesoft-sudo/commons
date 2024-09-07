.section .data

.globl __uar_data_start
.globl __uar_data_end
.globl __uar_data_size

__uar_data_start:
    .incbin "test-archive.uar"
__uar_data_end:
    .byte 0x00
__uar_data_size:
    .quad __uar_data_end - __uar_data_start

.text

.globl main
.type main, @function
main:
    call uar_init
    call uar_main
    push %rax
    call uar_deinit
    pop %rax
    ret

.globl uar_get_data_start
.type uar_get_data_start, @function

uar_get_data_start:
    lea __uar_data_start(%rip), %rax
    ret

.globl _start
_start:
    call plibc_init
    mov $0, %rdi
    mov %rsp, %rsi
    mov $0, %rdx
    call main
    push %rax
    call _plibc_call_exit_handlers
    call plibc_deinit
    pop %rdi
    call _exit

.section .note.GNU-stack,"",@progbits
