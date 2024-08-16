#include "string.h"
#include "errno.h"

size_t
strlen (const char *str)
{
    size_t len = 0;

    while (str[len])
        len++;

    return len;
}

static const char *errno_lut[] = {
    [0] = "Success",
    [EPERM] = "Operation not permitted",
    [ENOENT] = "No such file or directory",
    [ESRCH] = "No such process",
    [EINTR] = "Interrupted system call",
    [EIO] = "I/O error",
    [ENXIO] = "No such device or address",
    [E2BIG] = "Argument list too long",
    [ENOEXEC] = "Exec format error",
    [EBADF] = "Bad file number",
    [ECHILD] = "No child processes",
    [EAGAIN] = "Try again",
    [ENOMEM] = "Out of memory",
    [EACCES] = "Permission denied",
    [EFAULT] = "Bad address",
    [ENOTBLK] = "Block device required",
    [EBUSY] = "Device or resource busy",
    [EEXIST] = "File exists",
    [EXDEV] = "Cross-device link",
    [ENODEV] = "No such device",
    [ENOTDIR] = "Not a directory",
    [EISDIR] = "Is a directory",
    [EINVAL] = "Invalid argument",
    [ENFILE] = "File table overflow",
    [EMFILE] = "Too many open files",
    [ENOTTY] = "Not a typewriter",
    [ETXTBSY] = "Text file busy",
    [EFBIG] = "File too large",
    [ENOSPC] = "No space left on device",
    [ESPIPE] = "Illegal seek",
    [EROFS] = "Read-only file system",
    [EMLINK] = "Too many links",
    [EPIPE] = "Broken pipe",
    [EDOM] = "Math argument out of domain of func",
    [ERANGE] = "Math result not representable",
    [EDEADLK] = "Resource deadlock would occur",
    [ENAMETOOLONG] = "File name too long",
    [ENOLCK] = "No record locks available",
    [ENOSYS] = "Function not implemented",
    [ENOTEMPTY] = "Directory not empty",
    [ELOOP] = "Too many symbolic links encountered",
    [ENOMSG] = "No message of desired type",
    [EIDRM] = "Identifier removed",
    [ECHRNG] = "Channel number out of range",
    [EL2NSYNC] = "Level 2 not synchronized",
    [EL3HLT] = "Level 3 halted",
    [EL3RST] = "Level 3 reset",
    [ELNRNG] = "Link number out of range",
    [EUNATCH] = "Protocol driver not attached",
    [ENOCSI] = "No CSI structure available",
    [EL2HLT] = "Level 2 halted",
    [EBADE] = "Invalid exchange",
    [EBADR] = "Invalid request descriptor",
    [EXFULL] = "Exchange full",
    [ENOANO] = "No anode",
    [EBADRQC] = "Invalid request code",
    [EBADSLT] = "Invalid slot",
    [EBFONT] = "Bad font file format",
};

const char *
strerror (int errnum)
{
    if (errnum < 0
        || (size_t) (errnum) >= sizeof (errno_lut) / sizeof (errno_lut[0]))
        return "Unknown error";

    return errno_lut[errnum];
}

void *
memcpy (void *dest, const void *src, size_t n)
{
    for (size_t i = 0; i < n; i++)
        ((char *) dest)[i] = ((const char *) src)[i];

    return dest;
}