#include "stdio.h"
#include "malloc.h"
#include "stdbool.h"
#include "string.h"
#include "unistd.h"

enum printf_size
{
    PS_DEFAULT,
    PS_LONG,
    PS_LONG_LONG,
    PS_SIZE_T,
};

int
printf_internal (const char *fmt, long long int *argp,
                 long long int *stack_argp)
{
    size_t argc = 0;

    while (*fmt)
        {
            char c = *fmt++;

            if (c != '%')
                {
                    putchar (c);
                    continue;
                }

            long long int *p;

            if (argc < 5)
                p = argp + argc;
            else
                p = stack_argp + argc - 5;

            int length = -1;
            enum printf_size size_modifier = PS_DEFAULT;
            bool zero_pad = false;

            if (*fmt == '0')
                {
                    zero_pad = true;
                    fmt++;
                }

            while (*fmt >= '0' && *fmt <= '9')
                {
                    if (length == -1)
                        length = 0;

                    length = length * 10 + *fmt++ - '0';
                }

            if (*fmt == 'L')
                {
                    size_modifier = PS_LONG_LONG;
                    fmt++;
                }
            else if (*fmt == 'l')
                {
                    size_modifier = PS_LONG;
                    fmt++;
                }
            else if (*fmt == 'z')
                {
                    size_modifier = PS_SIZE_T;
                    fmt++;
                }

            if (*fmt == 'l' && size_modifier == PS_LONG)
                {
                    size_modifier = PS_LONG_LONG;
                    fmt++;
                }

            switch (*fmt)
                {
                case '%':
                    putchar ('%');
                    break;

                case 'c':
                    putchar (*(int *) p);
                    argc++;
                    break;

                case 's':
                    putsnl (*(char **) p);
                    argc++;
                    break;

                case 'd':
                case 'i':
                    {
                        long long int num;
                        char buf[32] = { 0 };
                        int j = 0;

                        switch (size_modifier)
                            {
                            case PS_DEFAULT:
                                num = *(int *) p;
                                break;

                            case PS_LONG:
                                num = *(long *) p;
                                break;

                            case PS_LONG_LONG:
                            case PS_SIZE_T:
                                num = *(long long *) p;
                                break;
                            }

                        argc++;

                        if (num < 0)
                            {
                                putchar ('-');
                                num = -num;
                            }

                        do
                            {
                                if (j >= 32)
                                    return -1;

                                buf[j++] = num % 10 + '0';
                                num /= 10;
                            }
                        while (num);

                        if (j > length && length >= 0)
                            j = length;

                        while (length != -1 && length > j && zero_pad)
                            {
                                putchar ('0');
                                length--;
                            }

                        while (j--)
                            putchar (buf[j]);
                    }
                    break;

                case 'u':
                    {
                        unsigned long long int num;
                        char buf[32] = { 0 };
                        int j = 0;

                        switch (size_modifier)
                            {
                            case PS_DEFAULT:
                                num = *(unsigned int *) p;
                                break;

                            case PS_LONG:
                                num = *(unsigned long *) p;
                                break;

                            case PS_LONG_LONG:
                                num = *(unsigned long long *) p;
                                break;

                            case PS_SIZE_T:
                                num = *(size_t *) p;
                                break;
                            }

                        argc++;

                        do
                            {
                                if (j >= 32)
                                    return -1;

                                buf[j++] = num % 10 + '0';
                                num /= 10;
                            }
                        while (num);

                        if (j > length && length >= 0)
                            j = length;

                        while (length != -1 && length > j && zero_pad)
                            {
                                putchar ('0');
                                length--;
                            }

                        while (j--)
                            putchar (buf[j]);
                    }
                    break;

                case 'x':
                case 'X':
                case 'p':
                    {
                        unsigned long long int num;
                        char buf[32] = { 0 };
                        int j = 0;

                        if (*fmt == 'p')
                            num = *(unsigned long long int *) p;
                        else
                            {
                                switch (size_modifier)
                                    {
                                    case PS_DEFAULT:
                                        num = *(unsigned int *) p;
                                        break;

                                    case PS_LONG:
                                        num = *(unsigned long *) p;
                                        break;

                                    case PS_LONG_LONG:
                                    case PS_SIZE_T:
                                        num = *(unsigned long long *) p;
                                        break;
                                    }
                            }

                        argc++;

                        do
                            {
                                if (j >= 32)
                                    return -1;

                                long long int rem = num % 16;
                                buf[j++] = rem < 10 ? rem % 16 + '0'
                                                    : rem % 16 - 10
                                                          + (*fmt == 'X' ? 'A'
                                                                         : 'a');
                                num /= 16;
                            }
                        while (num);

                        if (*fmt == 'p')
                            {
                                putchar ('0');
                                putchar ('x');
                            }

                        if (j > length && length >= 0)
                            j = length;

                        while (length != -1 && length > j && zero_pad)
                            {
                                putchar ('0');
                                length--;
                            }

                        while (j--)
                            putchar (buf[j]);
                    }
                    break;

                default:
                    return -1;
                }

            fmt++;
        }

    return 0;
}

int
puts (const char *str)
{
    int ret = putsnl (str);

    if (ret < 0)
        return ret;

    return putchar ('\n');
}

int
putsnl (const char *str)
{
    return write (STDOUT_FILENO, str, strlen (str));
}

int
putchar (int c)
{
    return write (STDOUT_FILENO, &c, 1);
}

#define FBUFSIZ 1024

static int
fmode_to_flags (const char *mode)
{
    int flags = 0;

    if (mode[0] == 'r')
        flags = O_RDONLY;
    else if (mode[0] == 'w')
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a')
        flags = O_WRONLY | O_CREAT | O_APPEND;
    else
        return -1;

    if (mode[1] == '+')
        {
            flags &= ~(O_RDONLY | O_WRONLY);
            flags |= O_RDWR;
        }

    return flags;
}

bool
_plibc_fopen_internal (FILE *file, const char *pathname, const char *mode)
{
    int flags = fmode_to_flags (mode);

    if (flags == -1)
        return false;

    int fd = open (pathname, flags);

    if (fd < 0)
        return false;

    file->fd = fd;
    file->buf = malloc (FBUFSIZ);
    file->buf_size = 0;
    file->mode = flags;

    return true;
}

FILE *
fopen (const char *pathname, const char *mode)
{

    FILE *file = malloc (sizeof (FILE));

    if (file == NULL)
        return NULL;

    if (!_plibc_fopen_internal (file, pathname, mode))
        {
            free (file);
            return NULL;
        }

    return file;
}

int
_plibc_fclose_internal (FILE *file)
{
    fflush (file);

    int ret = close (file->fd);

    if (ret < 0)
        return ret;

    free (file->buf);
    return 0;
}

int
fclose (FILE *file)
{
    if (file == NULL)
        return -1;

    int ret = _plibc_fclose_internal (file);
    free (file);
    return ret;
}

size_t
fwrite (const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    size_t total = size * nmemb;
    size_t written = 0;
    char buf[FBUFSIZ];

    while (total)
        {
            size_t to_write = total;

            if (to_write > FBUFSIZ)
                to_write = FBUFSIZ;

            memcpy (buf, ptr, to_write);

            int ret = write (stream->fd, buf, to_write);

            if (ret < 0)
                return ret;

            total -= to_write;
            written += to_write;
        }

    return written;
}

int
fputs (const char *str, FILE *stream)
{
    while (*str)
        {
            ((unsigned char *) stream->buf)[stream->buf_size++] = *str;

            if (stream->buf_size >= FBUFSIZ || *str == '\n')
                {
                    int ret = fwrite (stream->buf, 1, stream->buf_size, stream);

                    if (ret < 0)
                        return ret;

                    stream->buf_size = 0;
                }

            str++;
        }

    return 0;
}

int
fflush (FILE *stream)
{
    if (stream->buf_size == 0)
        return 0;

    return fwrite (stream->buf, 1, stream->buf_size, stream);
}

bool
_plibc_fdopen_internal (FILE *file, int fd, const char *mode)
{
    int flags = fmode_to_flags (mode);

    if (flags == -1)
        return false;

    file->fd = fd;
    file->buf = malloc (FBUFSIZ);
    file->buf_size = 0;
    file->mode = flags;

    return true;
}

FILE *
fdopen (int fd, const char *mode)
{
    FILE *file = malloc (sizeof (FILE));

    if (file == NULL)
        return NULL;

    if (!_plibc_fdopen_internal (file, fd, mode))
        {
            free (file);
            return NULL;
        }

    return file;
}