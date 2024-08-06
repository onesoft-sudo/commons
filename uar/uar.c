#define _XOPEN_SOURCE 500

#include "uar.h"
#include "malloc.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__linux__)
#    include <linux/limits.h>
#elif defined(__APPLE__)
#    include <sys/syslimits.h>
#else
#    include <limits.h>
#endif

#define UAR_ROOT_DIR_NAME 0x01

const unsigned char UAR_MAGIC[] = { 0x99, 'U', 'A', 'R' };

struct uar_header
{
    uint8_t magic[4];
    uint16_t version;
    uint32_t flags;
    uint64_t nfiles;
    uint64_t size;
} __attribute__ ((packed));

struct uar_file
{
    enum uar_file_type type;
    int __pad1;
    char *name;
    uint64_t namelen;
    uint64_t offset;
    union
    {
        uint64_t size;
        struct
        {
            char *loc;
            uint64_t loclen;
        } linkinfo;
    } data;
    mode_t mode;
    int __pad2;
};

struct uar_archive
{
    struct uar_header header;
    struct uar_file **files;
    enum uar_error ecode;
    bool is_stream;
    uint8_t *buffer;
};

static void *
uar_set_error (struct uar_archive *uar, enum uar_error ecode)
{
    uar->ecode = ecode;
    return NULL;
}

const char *
uar_strerror (const struct uar_archive *restrict uar)
{
    switch (uar->ecode)
        {
        case UAR_SUCCESS:
            return "success";
        case UAR_INVALID_MAGIC:
            return "invalid archive magic";
        case UAR_INVALID_FILE:
            return "invalid file";
        case UAR_INVALID_PATH:
            return "invalid path string";
        case UAR_IO_ERROR:
            return "archive I/O error";
        case UAR_OUT_OF_MEMORY:
            return "out of memory";
        case UAR_INVALID_ARGUMENT:
            return "invalid argument";
        case UAR_INVALID_OPERATION:
            return "invalid operation";
        default:
            return "unknown error";
        }
}

bool
uar_has_error (const struct uar_archive *restrict uar)
{
    return uar->ecode != UAR_SUCCESS;
}

struct uar_archive *
uar_create (void)
{
    struct uar_archive *uar = malloc (sizeof (struct uar_archive));

    if (uar == NULL)
        return NULL;

    uar->is_stream = false;
    uar->buffer = NULL;
    uar->ecode = UAR_SUCCESS;
    uar->header.size = 0;
    memcpy (uar->header.magic, UAR_MAGIC, 4);
    uar->header.version = 1;
    uar->header.flags = 0;
    uar->header.nfiles = 0;
    uar->files = NULL;

    return uar;
}

struct uar_archive *
uar_open (const char *filename)
{
    struct uar_archive *uar = NULL;
    FILE *stream = NULL;
    int cerrno;

    errno = 0;
    uar = uar_create ();

    if (uar == NULL)
        return NULL;

    stream = fopen (filename, "rb");

    if (stream == NULL)
        {
            uar_set_error (uar, UAR_IO_ERROR);
            goto uar_open_ret;
        }

    fseek (stream, 0, SEEK_END);
    size_t size = ftell (stream);
    fseek (stream, 0, SEEK_SET);

    if (fread (&uar->header, sizeof (struct uar_header), 1, stream) != 1)
        {
            uar_set_error (uar, UAR_IO_ERROR);
            goto uar_open_ret;
        }

    if (memcmp (uar->header.magic, UAR_MAGIC, 4) != 0)
        {
            uar_set_error (uar, UAR_INVALID_MAGIC);
            goto uar_open_ret;
        }

    uint64_t filearr_size = uar->header.nfiles * sizeof (struct uar_file);

    if (filearr_size > size)
        {
            uar_set_error (uar, UAR_IO_ERROR);
            goto uar_open_ret;
        }

    if (uar->header.size > size)
        {
            uar_set_error (uar, UAR_IO_ERROR);
            goto uar_open_ret;
        }

    uar->files = calloc (uar->header.nfiles, sizeof (struct uar_file *));

    if (uar->files == NULL)
        {
            uar_set_error (uar, UAR_OUT_OF_MEMORY);
            goto uar_open_ret;
        }

    for (uint64_t i = 0; i < uar->header.nfiles; i++)
        {
            struct uar_file *file = malloc (sizeof (struct uar_file));

            if (file == NULL)
                {
                    uar_set_error (uar, UAR_OUT_OF_MEMORY);
                    goto uar_open_ret;
                }

            if (fread (file, sizeof (struct uar_file), 1, stream) != 1)
                {
                    uar_set_error (uar, UAR_IO_ERROR);
                    goto uar_open_ret;
                }

            if (file->namelen > PATH_MAX)
                {
                    uar_set_error (uar, UAR_INVALID_PATH);
                    goto uar_open_ret;
                }

            file->name = malloc (file->namelen + 1);

            if (file->name == NULL)
                {
                    uar_set_error (uar, UAR_OUT_OF_MEMORY);
                    goto uar_open_ret;
                }

            if (fread (file->name, 1, file->namelen, stream) != file->namelen)
                {
                    uar_set_error (uar, UAR_IO_ERROR);
                    goto uar_open_ret;
                }

            file->name[file->namelen] = 0;
            uar->files[i] = file;
        }

    uar->buffer = malloc (uar->header.size + 1);

    if (uar->buffer == NULL)
        {
            uar_set_error (uar, UAR_OUT_OF_MEMORY);
            goto uar_open_ret;
        }

    if (fread (uar->buffer, 1, uar->header.size, stream) != uar->header.size)
        {
            uar_set_error (uar, UAR_IO_ERROR);
            goto uar_open_ret;
        }

uar_open_ret:
    cerrno = errno;
    fclose (stream);
    errno = cerrno;
    return uar;
}

void
uar_file_destroy (struct uar_file *file)
{
    if (file == NULL)
        return;

    free (file->name);
    free (file);
}

void
uar_close (struct uar_archive *uar)
{
    if (uar == NULL)
        return;

    free (uar->buffer);

    for (uint64_t i = 0; i < uar->header.nfiles; i++)
        {
            struct uar_file *file = uar->files[i];
            uar_file_destroy (file);
        }

    free (uar->files);
    free (uar);
}

bool
uar_add_file_entry (struct uar_archive *restrict uar, struct uar_file *file)
{
    if (uar == NULL || file == NULL)
        {
            uar_set_error (uar, UAR_INVALID_ARGUMENT);
            return false;
        }

    uar->files = realloc (uar->files,
                          (uar->header.nfiles + 1) * sizeof (struct uar_file));

    if (uar->files == NULL)
        {
            uar_set_error (uar, UAR_OUT_OF_MEMORY);
            return false;
        }

    uar->files[uar->header.nfiles] = file;
    uar->header.nfiles++;
    return true;
}

struct uar_file *
uar_file_create (const char *name, uint64_t namelen, uint64_t size,
                 uint32_t offset)
{
    struct uar_file *file;
    int cerrno;
    assert (namelen < PATH_MAX);

    file = malloc (sizeof (struct uar_file));

    if (file == NULL)
        return NULL;

    bzero (file, sizeof (struct uar_file));

    file->type = UF_FILE;
    file->mode = 0644;
    file->name = malloc (namelen + 1);

    if (file->name == NULL)
        {
            cerrno = errno;
            free (file);
            errno = cerrno;
            return NULL;
        }

    file->name[namelen] = 0;
    file->namelen = namelen;
    file->data.size = size;
    file->offset = offset;

    strncpy (file->name, name, namelen);
    return file;
}

struct uar_file *
uar_add_file (struct uar_archive *restrict uar, const char *name,
              const char *path)
{
    assert (uar != NULL && "uar is NULL");
    assert (name != NULL && "name is NULL");
    assert (path != NULL && "path is NULL");
    assert (!uar->is_stream && "uar in non-stream mode is not supported yet");

    uint64_t namelen = strlen (name);

    if (namelen >= PATH_MAX)
        {
            uar_set_error (uar, UAR_INVALID_PATH);
            return NULL;
        }

    FILE *stream = fopen (path, "rb");

    if (stream == NULL)
        {
            uar_set_error (uar, UAR_IO_ERROR);
            return NULL;
        }

    fseek (stream, 0, SEEK_END);
    long size = ftell (stream);

    if (size < 0)
        {
            uar_set_error (uar, UAR_IO_ERROR);
            fclose (stream);
            return NULL;
        }

    fseek (stream, 0, SEEK_SET);

    struct uar_file *file
        = uar_file_create (name, namelen, size, uar->header.size);

    if (file == NULL)
        {
            uar_set_error (uar, UAR_OUT_OF_MEMORY);
            fclose (stream);
            return NULL;
        }

    uar->header.size += size;

    if (!uar_add_file_entry (uar, file))
        {
            uar_file_destroy (file);
            fclose (stream);
            return NULL;
        }

    uar->buffer = realloc (uar->buffer, uar->header.size);

    if (uar->buffer == NULL)
        {
            uar_set_error (uar, UAR_OUT_OF_MEMORY);
            fclose (stream);
            return NULL;
        }

    if (size != 0 && fread (uar->buffer + file->offset, size, 1, stream) != 1)
        {
            uar_set_error (uar, UAR_IO_ERROR);
            fclose (stream);
            return NULL;
        }

    fclose (stream);
    return file;
}

static char *
path_concat (const char *p1, const char *p2, size_t len1, size_t len2)
{
    char *path = malloc (len1 + len2 + 2);

    if (path == NULL)
        return NULL;

    strncpy (path, p1, len1);
    path[len1] = '/';
    strncpy (path + len1 + 1, p2, len2);
    path[len1 + len2 + 1] = 0;
    return path;
}

struct uar_file *
uar_add_dir (struct uar_archive *uar, const char *dname, const char *path,
             bool (*callback) (struct uar_file *file, const char *fullname,
                               const char *fullpath))
{
    assert (uar != NULL && "uar is NULL");
    assert (dname != NULL && "dname is NULL");
    assert (path != NULL && "path is NULL");
    assert (!uar->is_stream && "uar in non-stream mode is not supported yet");

    char *name = (char *) dname;
    bool free_name = false;
    int cerrno;
    uint64_t namelen;

    if (strcmp (name, ".") == 0)
        {
            name = malloc (2);
            name[0] = UAR_ROOT_DIR_NAME;
            name[1] = 0;
            free_name = true;
            namelen = 1;
        }
    else
        namelen = strlen (name);

    if (namelen >= PATH_MAX)
        {
            uar_set_error (uar, UAR_INVALID_PATH);
            return NULL;
        }

    DIR *dir = opendir (path);
    struct dirent *entry = NULL;

    if (dir == NULL)
        {
            uar_set_error (uar, UAR_INVALID_FILE);
            return NULL;
        }

    struct uar_file *dir_file
        = uar_file_create (name, namelen, 0, uar->header.size);
    uint64_t dir_size = 0;

    dir_file->type = UF_DIR;

    if (callback != NULL && !callback (dir_file, name, path))
        {
            uar_set_error (uar, UAR_SUCCESS);
            uar_file_destroy (dir_file);
            return NULL;
        }

    if (!uar_add_file_entry (uar, dir_file))
        {
            uar_file_destroy (dir_file);
            return NULL;
        }

    while ((entry = readdir (dir)) != NULL)
        {
            if (strcmp (entry->d_name, ".") == 0
                || strcmp (entry->d_name, "..") == 0)
                continue;

            struct stat stinfo = { 0 };

            if (256 + namelen >= PATH_MAX)
                {
                    uar_set_error (uar, UAR_INVALID_PATH);
                    uar_file_destroy (dir_file);
                    closedir (dir);
                    return NULL;
                }

            uint64_t dnamelen = strlen (entry->d_name);

            char *fullpath
                = path_concat (path, entry->d_name, strlen (path), dnamelen);
            assert (fullpath != NULL);

            char *fullname
                = path_concat (name, entry->d_name, namelen, dnamelen);
            assert (fullname != NULL);

            if (stat (fullpath, &stinfo) != 0)
                {
                    uar_set_error (uar, UAR_IO_ERROR);
                    goto uar_add_dir_error;
                }

            if (S_ISREG (stinfo.st_mode))
                {
                    struct uar_file *file
                        = uar_add_file (uar, fullname, fullpath);

                    if (file == NULL)
                        {
                            goto uar_add_dir_error;
                        }

                    if (callback != NULL
                        && !callback (file, fullname, fullpath))
                        {
                            uar_set_error (uar, UAR_SUCCESS);
                            goto uar_add_dir_error;
                        }

                    file->mode = stinfo.st_mode & 07777;
                    dir_size += file->data.size;
                }
            else if (S_ISDIR (stinfo.st_mode))
                {
                    struct uar_file *direntry
                        = uar_add_dir (uar, fullname, fullpath, callback);

                    if (direntry == NULL)
                        {
                            goto uar_add_dir_error;
                        }

                    direntry->mode = stinfo.st_mode & 07777;
                    dir_size += direntry->data.size;
                }
            else
                assert (false && "Not supported");

            free (fullpath);
            free (fullname);

            continue;

        uar_add_dir_error:
            cerrno = errno;
            uar_file_destroy (dir_file);
            free (fullpath);
            free (fullname);
            errno = cerrno;
            goto uar_add_dir_end;
        }

    dir_file->data.size = dir_size;

uar_add_dir_end:
    cerrno = errno;
    closedir (dir);

    if (free_name)
        free (name);

    errno = cerrno;
    return dir_file;
}

void
uar_file_set_mode (struct uar_file *file, mode_t mode)
{
    file->mode = mode;
}

static void
uar_debug_print_file (const struct uar_archive *uar, struct uar_file *file,
                      bool print_contents)
{
    printf ("    size: %lu\n", file->data.size);

    if (print_contents)
        {
            printf ("    contents:\n");
            printf ("==================\n");
            fflush (stdout);

            ssize_t size = write (STDOUT_FILENO, uar->buffer + file->offset,
                                  file->data.size);

            if (size == -1 || ((uint64_t) size) != file->data.size)
                {
                    perror ("write");
                    return;
                }

            putchar ('\n');
            printf ("==================\n");
        }
}

void
uar_debug_print (const struct uar_archive *uar, bool print_file_contents)
{
    printf ("uar_archive:\n");
    printf ("  magic: %02x %02x %02x %02x\n", uar->header.magic[0],
            uar->header.magic[1], uar->header.magic[2], uar->header.magic[3]);
    printf ("  version: %u\n", uar->header.version);
    printf ("  flags: %u\n", uar->header.flags);
    printf ("  nfiles: %lu\n", uar->header.nfiles);
    printf ("  size: %lu\n", uar->header.size);
    printf ("  stream?: %i\n", uar->is_stream);

    for (uint64_t i = 0; i < uar->header.nfiles; i++)
        {
            struct uar_file *file = uar->files[i];

            printf ("  %s[%lu]:\n",
                    file->type == UF_FILE  ? "file"
                    : file->type == UF_DIR ? "directory"
                                           : "link",
                    i);
            printf ("    name: \033[1m%s%s%s\033[0m\n",
                    file->name[0] == UAR_ROOT_DIR_NAME ? "/" : "",
                    file->name[0] == UAR_ROOT_DIR_NAME ? file->name + 2
                                                       : file->name,
                    file->type == UF_DIR    ? "/"
                    : file->type == UF_LINK ? "@"
                                            : "");
            printf ("    offset: %lu\n", file->offset);
            printf ("    mode: %04o\n", file->mode);

            switch (file->type)
                {
                case UF_FILE:
                    uar_debug_print_file (uar, file, print_file_contents);
                    break;

                case UF_DIR:
                    printf ("    size: %lu\n", file->data.size);
                    break;

                default:
                    printf ("  info: unknown file type\n");
                    break;
                }
        }
}

bool
uar_write (struct uar_archive *uar, const char *filename)
{
    FILE *stream = fopen (filename, "wb");

    if (stream == NULL)
        {
            uar_set_error (uar, UAR_IO_ERROR);
            return false;
        }

    if (fwrite (&uar->header, sizeof (struct uar_header), 1, stream) != 1)
        {
            uar_set_error (uar, UAR_IO_ERROR);
            fclose (stream);
            return false;
        }

    for (uint64_t i = 0; i < uar->header.nfiles; i++)
        {
            struct uar_file *file = uar->files[i];

            if (fwrite (file, sizeof (struct uar_file), 1, stream) != 1)
                {
                    uar_set_error (uar, UAR_IO_ERROR);
                    fclose (stream);
                    return false;
                }

            if (fwrite (file->name, 1, file->namelen, stream) != file->namelen)
                {
                    uar_set_error (uar, UAR_IO_ERROR);
                    fclose (stream);
                    return false;
                }
        }

    if (fwrite (uar->buffer, 1, uar->header.size, stream) != uar->header.size)
        {
            uar_set_error (uar, UAR_IO_ERROR);
            fclose (stream);
            return false;
        }

    fclose (stream);
    return true;
}

const char *
uar_get_file_name (const struct uar_file *file)
{
    return file->name[0] == UAR_ROOT_DIR_NAME
               ? file->namelen == 1 ? "/" : file->name + 1
               : file->name;
}

enum uar_file_type
uar_get_entry_type (const struct uar_file *file)
{
    return file->type;
}

bool
uar_extract (struct uar_archive *uar, const char *cwd,
             bool (*callback) (struct uar_file *file))
{
    if (cwd != NULL && chdir (cwd) != 0)
        {
            uar_set_error (uar, UAR_SYSTEM_ERROR);
            return false;
        }

    for (uint64_t i = 0; i < uar->header.nfiles; i++)
        {
            struct uar_file *file = uar->files[i];

            if (callback != NULL && !callback (file))
                return false;

            char *name = file->name;

            if (name[0] == UAR_ROOT_DIR_NAME)
                name += 2;

            switch (file->type)
                {
                case UF_FILE:
                    {
                        FILE *stream = fopen (name, "wb");

                        if (stream == NULL)
                            {
                                uar_set_error (uar, UAR_IO_ERROR);
                                return false;
                            }

                        if (fwrite (uar->buffer + file->offset, 1,
                                    file->data.size, stream)
                            != file->data.size)
                            {
                                uar_set_error (uar, UAR_IO_ERROR);
                                return false;
                            }

                        fchmod (fileno (stream), file->mode);
                        fclose (stream);
                    }
                    break;

                case UF_DIR:
                    if (file->namelen == 1
                        && file->name[0] == UAR_ROOT_DIR_NAME)
                        continue;

                    if (mkdir (name, file->mode) != 0)
                        {
                            uar_set_error (uar, UAR_SYSTEM_ERROR);
                            return false;
                        }

                    break;

                default:
                    assert (false && "unknown file type");
                    return false;
                }
        }

    return true;
}