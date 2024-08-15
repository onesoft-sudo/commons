.text
.globl abort
.type abort, @function

abort:
    xor %rdi, %rdi
    mov $6, %rsi
    call kill
    mov $1, %rdi
    call exit
    ret

.section .note.GNU-stack,"",@progbits