.text

.globl sys_write
.type sys_write, @function

sys_write:
    mov $1, %rax
    syscall
    ret

.globl sys_exit
.type sys_exit, @function
sys_exit:
    mov $60, %rax
    syscall
    ret

.globl sys_sbrk
.type sys_sbrk, @function
sys_sbrk:
    mov $12, %rax
    syscall
    ret

.globl sys_brk
.type sys_brk, @function
sys_brk:
    mov $0, %rdi
    call sbrk
    ret

.globl sys_mmap
.type sys_mmap, @function

sys_mmap:
    mov $9, %rax
    syscall
    ret

.globl sys_munmap
.type sys_munmap, @function

sys_munmap:
    mov $11, %rax
    syscall
    ret

.globl sys_sysinfo
.type sys_sysinfo, @function

sys_sysinfo:
    mov $99, %rax
    syscall
    ret

.global sys_kill
.type sys_kill, @function

sys_kill:
    mov $62, %rax
    syscall
    ret

.globl sys_open
.type sys_open, @function

sys_open:
    mov $2, %rax
    syscall
    ret

.globl sys_close
.type sys_close, @function

sys_close:
    mov $3, %rax
    syscall
    ret

.section .note.GNU-stack,"",@progbits
