.text

.globl write
.type write, @function

write:
    mov $1, %rax
    syscall
    ret

.globl exit
.type exit, @function
exit:
    mov $60, %rax
    syscall
    ret

.globl sbrk
.type sbrk, @function
sbrk:
    mov $12, %rax
    syscall
    ret

.globl brk
.type brk, @function
brk:
    mov $0, %rdi
    call sbrk
    ret

.globl mmap
.type mmap, @function

mmap:
    mov $9, %rax
    syscall
    ret

.globl munmap
.type munmap, @function

munmap:
    mov $11, %rax
    syscall
    ret

.globl sysinfo
.type sysinfo, @function

sysinfo:
    mov $99, %rax
    syscall
    ret

.section .note.GNU-stack,"",@progbits
