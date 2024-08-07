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
#include <time.h>
#include <unistd.h>

#if defined(__linux__)
#    include <linux/limits.h>
#elif defined(__APPLE__)
#    include <sys/syslimits.h>
#else
#    include <limits.h>
#endif

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
    time_t mtime;
    int __pad2;
};

struct uar_archive
{
    struct uar_header header;
    struct uar_file **files;
    struct uar_file *root;
    enum uar_error ecode;
    bool is_stream;
    uint8_t *buffer; /* Deprecated */
    FILE *stream;
    int last_errno;
    char *err_file;
};

static void
uar_set_error (struct uar_archive *uar, enum uar_error ecode,
               const char *err_file)
{
    uar->ecode = ecode;
    uar->last_errno = errno;
    free (uar->err_file);
    uar->err_file = err_file == NULL ? NULL : strdup (err_file);
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
        case UAR_SYSTEM_ERROR:
            return "system error";
        case UAR_SYSCALL_ERROR:
            return strerror (uar->last_errno);
        default:
            return "unknown error";
        }
}

bool
uar_has_error (const struct uar_archive *restrict uar)
{
    return uar->ecode != UAR_SUCCESS;
}

/* TODO: Use static storage for path */
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
    uar->stream = NULL;
    uar->root = NULL;
    uar->last_errno = 0;
    uar->err_file = NULL;

    return uar;
}

static struct uar_file *
uar_initialize_root (struct uar_archive *uar)
{
    struct uar_file *root = uar_file_create ("/", 1, 0, 0);

    if (root == NULL)
        {
            uar_set_error (uar, UAR_OUT_OF_MEMORY, "/");
            return NULL;
        }

    root->type = UF_DIR;
    root->mode = S_IFDIR | 0755;
    root->mtime = time (NULL);

    if (!uar_add_file_entry (uar, root))
        {
            uar_file_destroy (root);
            return NULL;
        }

    uar->root = root;

    return root;
}

static bool
uar_initialize (struct uar_archive *uar)
{
    if (uar_initialize_root (uar) == NULL)
        return false;

    return true;
}

bool
uar_stream_write (struct uar_archive *uar, const char *filename)
{
    if (uar == NULL || !uar->is_stream || uar->stream == NULL)
        return false;

    FILE *stream = fopen (filename, "wb");

    if (fwrite (&uar->header, 1, sizeof (struct uar_header), stream)
        != sizeof (struct uar_header))
        {
            uar_set_error (uar, UAR_SYSCALL_ERROR, NULL);
            fclose (stream);
            return false;
        }

    for (uint64_t i = 0; i < uar->header.nfiles; i++)
        {
            struct uar_file *file = uar->files[i];

            if (fwrite (file, 1, sizeof (struct uar_file), stream)
                != sizeof (struct uar_file))
                {
                    uar_set_error (uar, UAR_SYSCALL_ERROR, file->name);
                    fclose (stream);
                    return false;
                }

            if (fwrite (file->name, 1, file->namelen, stream) != file->namelen)
                {
                    uar_set_error (uar, UAR_SYSCALL_ERROR, file->name);
                    fclose (stream);
                    return false;
                }
        }

    uar->stream = freopen (NULL, "rb", uar->stream);

    if (uar->stream == NULL)
        {
            uar_set_error (uar, UAR_SYSCALL_ERROR, NULL);
            fclose (stream);
            return false;
        }

    size_t buf_size
        = uar->header.size >= (1024 * 1024) ? 1024 * 1024 : uar->header.size;
    uint8_t *buf = malloc (buf_size);

    if (buf == NULL)
        {
            uar_set_error (uar, UAR_OUT_OF_MEMORY, NULL);
            fclose (stream);
            return false;
        }

    uint64_t size = uar->header.size;

    while (size > 0 && !feof (uar->stream))
        {
            if (size < buf_size)
                buf_size = size;

            if (fread (buf, 1, buf_size, uar->stream) != buf_size)
                {
                    uar_set_error (uar, UAR_SYSCALL_ERROR, NULL);
                    fclose (stream);
                    free (buf);
                    return false;
                }

            if (fwrite (buf, 1, buf_size, stream) != buf_size)
                {
                    uar_set_error (uar, UAR_SYSCALL_ERROR, NULL);
                    fclose (stream);
                    free (buf);
                    return false;
                }

            if (size < buf_size)
                buf_size = size;
            else
                size -= buf_size;
        }

    fclose (stream);
    return true;
}

struct uar_archive *
uar_create_stream (void)
{
    struct uar_archive *uar = uar_create ();
    int cerrno;

    if (uar == NULL)
        return NULL;

    uar->is_stream = true;
    uar->stream = tmpfile ();

    if (uar->stream == NULL)
        goto uar_create_stream_error;

    if (!uar_initialize (uar))
        goto uar_create_stream_error;

    goto uar_create_stream_ret;

uar_create_stream_error:
    cerrno = errno;
    uar_close (uar);
    errno = cerrno;
uar_create_stream_ret:
    return uar;
}

struct uar_file *
uar_stream_add_file (struct uar_archive *uar, const char *uar_filename,
                     const char *fs_filename, struct stat *stinfo)
{
    assert (uar != NULL && "uar is NULL");
    assert (uar->is_stream && "uar is not in stream mode");
    assert (uar_filename != NULL && "uar_filename is NULL");
    assert (fs_filename != NULL && "fs_filename is NULL");

    if (uar->root == NULL)
        {
            if (uar_initialize_root (uar) == NULL)
                return NULL;
        }

    struct stat custom_stinfo = { 0 };
    enum uar_error ecode = UAR_SUCCESS;
    void *buffer = NULL;
    struct uar_file *file = NULL;

    if (stinfo == NULL)
        {
            if (lstat (fs_filename, &custom_stinfo) != 0)
                {
                    uar_set_error (uar, UAR_SYSCALL_ERROR, fs_filename);
                    perror ("uar_stream_add_file::lstat");
                    return NULL;
                }
            else
                stinfo = &custom_stinfo;
        }

    FILE *stream = fopen (fs_filename, "rb");

    if (stream == NULL)
        {
            uar_set_error (uar, UAR_SYSCALL_ERROR, fs_filename);
            perror ("uar_stream_add_file::fopen");
            return NULL;
        }

    fseek (stream, 0, SEEK_END);
    long size = ftell (stream);

    if (size < 0)
        {
            ecode = UAR_SYSCALL_ERROR;
            goto uar_stream_add_file_end;
        }

    fseek (stream, 0, SEEK_SET);

    file = uar_file_create (uar_filename, 0, size, uar->header.size);

    if (file == NULL)
        {
            ecode = UAR_SYSCALL_ERROR;
            perror ("uar_stream_add_file::uar_file_create");
            goto uar_stream_add_file_end;
        }

    file->mode = stinfo->st_mode;
    file->data.size = size;
    file->mtime = stinfo->st_mtime;
    uar->header.size += size;
    uar->root->data.size += size;

    if (!uar_add_file_entry (uar, file))
        {
            perror ("uar_stream_add_file::uar_add_file_entry");
            uar_file_destroy (file);
            fclose (stream);
            return NULL;
        }

    buffer = malloc (size);

    if (buffer == NULL)
        {
            ecode = UAR_OUT_OF_MEMORY;
            goto uar_stream_add_file_end;
        }

    if (size != 0 && fread (buffer, 1, size, stream) != (size_t) size)
        {
            ecode = UAR_SYSCALL_ERROR;
            goto uar_stream_add_file_end;
        }

    if (fwrite (buffer, 1, size, uar->stream) != (size_t) size)
        {
            ecode = UAR_SYSCALL_ERROR;
            goto uar_stream_add_file_end;
        }

uar_stream_add_file_end:
    if (ecode != UAR_SUCCESS && file != NULL)
        uar_file_destroy (file);

    if (buffer != NULL)
        free (buffer);

    uar_set_error (uar, ecode, fs_filename);
    fclose (stream);
    return ecode == UAR_SUCCESS ? file : NULL;
}

struct uar_file *
uar_stream_add_dir (struct uar_archive *uar, const char *uar_dirname,
                    const char *fs_dirname, struct stat *stinfo,
                    uar_callback_t callback)
{
    struct stat custom_stinfo = { 0 };
    enum uar_error ecode = UAR_SUCCESS;
    struct uar_file *file = NULL;
    uint64_t size = 0;
    DIR *dir = NULL;

    if (stinfo == NULL)
        {
            if (lstat (fs_dirname, &custom_stinfo) != 0)
                {
                    uar_set_error (uar, UAR_SYSCALL_ERROR, fs_dirname);
                    return NULL;
                }
            else
                stinfo = &custom_stinfo;
        }

    file = uar_file_create (uar_dirname, 0, 0, uar->header.size);

    if (file == NULL)
        {
            ecode = UAR_OUT_OF_MEMORY;
            goto uar_stream_add_dir_error;
        }

    file->type = UF_DIR;
    file->mode = stinfo->st_mode;
    file->mtime = stinfo->st_mtime;

    if (!uar_add_file_entry (uar, file))
        {
            ecode = UAR_OUT_OF_MEMORY;
            goto uar_stream_add_dir_error;
        }

    if (!callback (uar, file, uar_dirname, fs_dirname))
        {
            ecode = UAR_SUCCESS;
            goto uar_stream_add_dir_error;
        }

    dir = opendir (fs_dirname);

    if (dir == NULL)
        {
            ecode = UAR_SYSCALL_ERROR;
            goto uar_stream_add_dir_error;
        }

    struct dirent *entry = NULL;

    while ((entry = readdir (dir)) != NULL)
        {
            if (strcmp (entry->d_name, ".") == 0
                || strcmp (entry->d_name, "..") == 0)
                continue;

            size_t dname_len = strlen (entry->d_name);
            char *fs_fullpath = path_concat (fs_dirname, entry->d_name,
                                             strlen (fs_dirname), dname_len);
            char *uar_fullpath = path_concat (uar_dirname, entry->d_name,
                                              strlen (uar_dirname), dname_len);

            struct uar_file *entry_file = uar_stream_add_entry (
                uar, uar_fullpath, fs_fullpath, NULL, callback);

            if (entry_file == NULL)
                {
                    ecode = UAR_SYSCALL_ERROR;
                    goto uar_stream_add_dir_ret;
                }

            size += entry_file->data.size;
            free (fs_fullpath);
            free (uar_fullpath);
        }

    file->data.size = size;

uar_stream_add_dir_error:
    uar_set_error (uar, ecode, fs_dirname);
uar_stream_add_dir_ret:
    if (dir != NULL)
        closedir (dir);

    if (ecode != UAR_SUCCESS && file != NULL)
        uar_file_destroy (file);

    return ecode == UAR_SUCCESS ? file : NULL;
}

struct uar_file *
uar_stream_add_link (struct uar_archive *uar, const char *uar_name,
                     const char *fs_name, struct stat *stinfo)
{
    struct stat custom_stinfo = { 0 };
    struct uar_file *file = NULL;

    if (stinfo == NULL)
        {
            if (lstat (fs_name, &custom_stinfo) != 0)
                {
                    uar_set_error (uar, UAR_SYSCALL_ERROR, fs_name);
                    return NULL;
                }
            else
                stinfo = &custom_stinfo;
        }

    file = uar_file_create (uar_name, 0, 0, uar->header.size);

    if (file == NULL)
        {
            uar_set_error (uar, UAR_OUT_OF_MEMORY, fs_name);
            return NULL;
        }

    file->type = UF_LINK;
    file->mode = stinfo->st_mode;
    file->mtime = stinfo->st_mtime;

    char link_buf[PATH_MAX] = { 0 };

    ssize_t link_len = readlink (fs_name, link_buf, PATH_MAX);

    if (link_len == -1)
        {
            uar_set_error (uar, UAR_SYSCALL_ERROR, fs_name);
            uar_file_destroy (file);
            return NULL;
        }

    file->data.linkinfo.loclen = link_len;
    file->data.linkinfo.loc = malloc (link_len + 1);

    if (file->data.linkinfo.loc == NULL)
        {
            uar_set_error (uar, UAR_OUT_OF_MEMORY, fs_name);
            uar_file_destroy (file);
            return NULL;
        }

    memcpy (file->data.linkinfo.loc, link_buf, link_len);
    file->data.linkinfo.loc[link_len] = 0;

    if (!uar_add_file_entry (uar, file))
        {
            uar_file_destroy (file);
            return NULL;
        }

    return file;
}

struct uar_file *
uar_stream_add_entry (struct uar_archive *uar, const char *uar_name,
                      const char *fs_name, struct stat *stinfo,
                      uar_callback_t callback)
{
    assert (uar != NULL && "uar is NULL");
    assert (uar->is_stream && "uar is not in stream mode");
    assert (uar_name != NULL && "uar_name is NULL");
    assert (fs_name != NULL && "fs_name is NULL");

    struct stat custom_stinfo = { 0 };
    struct uar_file *file = NULL;

    if (stinfo == NULL)
        {
            if (lstat (fs_name, &custom_stinfo) != 0)
                {
                    uar_set_error (uar, UAR_SYSCALL_ERROR, fs_name);
                    return NULL;
                }
            else
                stinfo = &custom_stinfo;
        }

    if (S_ISREG (stinfo->st_mode))
        {
            file = uar_stream_add_file (uar, uar_name, fs_name, stinfo);

            if (file == NULL)
                {
                    return NULL;
                }

            if (!callback (uar, file, uar_name, fs_name))
                {
                    uar_set_error (uar, UAR_SUCCESS, fs_name);
                    return NULL;
                }
        }
    else if (S_ISDIR (stinfo->st_mode))
        {
            file
                = uar_stream_add_dir (uar, uar_name, fs_name, stinfo, callback);
        }
    else
        {
            file = uar_stream_add_link (uar, uar_name, fs_name, stinfo);
        }

    return file;
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
            uar_set_error (uar, UAR_IO_ERROR, NULL);
            goto uar_open_ret;
        }

    fseek (stream, 0, SEEK_END);
    size_t size = ftell (stream);
    fseek (stream, 0, SEEK_SET);

    if (fread (&uar->header, sizeof (struct uar_header), 1, stream) != 1)
        {
            uar_set_error (uar, UAR_IO_ERROR, NULL);
            goto uar_open_ret;
        }

    if (memcmp (uar->header.magic, UAR_MAGIC, 4) != 0)
        {
            uar_set_error (uar, UAR_INVALID_MAGIC, NULL);
            goto uar_open_ret;
        }

    uint64_t filearr_size = uar->header.nfiles * sizeof (struct uar_file);

    if (filearr_size > size)
        {
            uar_set_error (uar, UAR_IO_ERROR, NULL);
            goto uar_open_ret;
        }

    if (uar->header.size > size)
        {
            uar_set_error (uar, UAR_IO_ERROR, NULL);
            goto uar_open_ret;
        }

    uar->files = calloc (uar->header.nfiles, sizeof (struct uar_file *));

    if (uar->files == NULL)
        {
            uar_set_error (uar, UAR_OUT_OF_MEMORY, NULL);
            goto uar_open_ret;
        }

    for (uint64_t i = 0; i < uar->header.nfiles; i++)
        {
            struct uar_file *file = malloc (sizeof (struct uar_file));

            if (file == NULL)
                {
                    uar_set_error (uar, UAR_OUT_OF_MEMORY, NULL);
                    goto uar_open_ret;
                }

            if (fread (file, sizeof (struct uar_file), 1, stream) != 1)
                {
                    uar_set_error (uar, UAR_IO_ERROR, NULL);
                    goto uar_open_ret;
                }

            if (file->namelen > PATH_MAX)
                {
                    uar_set_error (uar, UAR_INVALID_PATH, NULL);
                    goto uar_open_ret;
                }

            file->name = malloc (file->namelen + 1);

            if (file->name == NULL)
                {
                    uar_set_error (uar, UAR_OUT_OF_MEMORY, NULL);
                    goto uar_open_ret;
                }

            if (fread (file->name, 1, file->namelen, stream) != file->namelen)
                {
                    uar_set_error (uar, UAR_IO_ERROR, file->name);
                    goto uar_open_ret;
                }

            file->name[file->namelen] = 0;
            uar->files[i] = file;
        }

    uar->buffer = malloc (uar->header.size + 1);

    if (uar->buffer == NULL)
        {
            uar_set_error (uar, UAR_OUT_OF_MEMORY, NULL);
            goto uar_open_ret;
        }

    if (fread (uar->buffer, 1, uar->header.size, stream) != uar->header.size)
        {
            uar_set_error (uar, UAR_IO_ERROR, NULL);
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

    if (file->type == UF_LINK)
        free (file->data.linkinfo.loc);

    free (file->name);
    free (file);
}

void
uar_close (struct uar_archive *uar)
{
    if (uar == NULL)
        return;

    if (uar->is_stream)
        fclose (uar->stream);
    else
        free (uar->buffer);

    for (uint64_t i = 0; i < uar->header.nfiles; i++)
        {
            struct uar_file *file = uar->files[i];
            uar_file_destroy (file);
        }

    free (uar->err_file);
    free (uar->files);
    free (uar);
}

bool
uar_add_file_entry (struct uar_archive *restrict uar, struct uar_file *file)
{
    if (uar == NULL || file == NULL)
        {
            uar_set_error (uar, UAR_INVALID_ARGUMENT, NULL);
            return false;
        }

    uar->files = realloc (uar->files,
                          (uar->header.nfiles + 1) * sizeof (struct uar_file));

    if (uar->files == NULL)
        {
            uar_set_error (uar, UAR_OUT_OF_MEMORY, NULL);
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
    bool abs = false;

    if (namelen == 0)
        namelen = strlen (name);

    if (namelen >= PATH_MAX)
        {
            errno = ENAMETOOLONG;
            return NULL;
        }

    abs = name[0] == '/';
    namelen += (abs ? 0 : 1);

    file = malloc (sizeof (struct uar_file));

    if (file == NULL)
        return NULL;

    bzero (file, sizeof (struct uar_file));

    file->type = UF_FILE;
    file->mode = 0644;
    file->mtime = 0;
    file->name = malloc (namelen + 1);

    if (file->name == NULL)
        {
            cerrno = errno;
            free (file);
            errno = cerrno;
            return NULL;
        }

    if (!abs)
        file->name[0] = '/';

    strncpy (file->name + (abs ? 0 : 1), name, namelen);
    file->name[namelen] = 0;
    file->namelen = namelen;
    file->data.size = size;
    file->offset = offset;

    return file;
}

struct uar_file *
uar_add_file (struct uar_archive *restrict uar, const char *name,
              const char *path, struct stat *stinfo)
{
    assert (uar != NULL && "uar is NULL");
    assert (name != NULL && "name is NULL");
    assert (path != NULL && "path is NULL");
    assert (!uar->is_stream && "uar in non-stream mode is not supported yet");

    uint64_t namelen = strlen (name);
    struct stat *file_stinfo = stinfo, st_stinfo = { 0 };

    if (namelen >= PATH_MAX)
        {
            uar_set_error (uar, UAR_INVALID_PATH, path);
            return NULL;
        }

    if (file_stinfo == NULL)
        {
            if (lstat (path, &st_stinfo) != 0)
                {
                    uar_set_error (uar, UAR_IO_ERROR, path);
                    return NULL;
                }

            file_stinfo = &st_stinfo;
        }

    FILE *stream = fopen (path, "rb");

    if (stream == NULL)
        {
            uar_set_error (uar, UAR_IO_ERROR, path);
            return NULL;
        }

    fseek (stream, 0, SEEK_END);
    long size = ftell (stream);

    if (size < 0)
        {
            uar_set_error (uar, UAR_IO_ERROR, path);
            fclose (stream);
            return NULL;
        }

    fseek (stream, 0, SEEK_SET);

    struct uar_file *file
        = uar_file_create (name, namelen, size, uar->header.size);

    if (file == NULL)
        {
            uar_set_error (uar, UAR_OUT_OF_MEMORY, path);
            fclose (stream);
            return NULL;
        }

    file->mtime = file_stinfo->st_mtime;
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
            uar_set_error (uar, UAR_OUT_OF_MEMORY, path);
            fclose (stream);
            return NULL;
        }

    if (size != 0 && fread (uar->buffer + file->offset, size, 1, stream) != 1)
        {
            uar_set_error (uar, UAR_IO_ERROR, path);
            fclose (stream);
            return NULL;
        }

    fclose (stream);
    return file;
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
            name = strdup ("/");
            free_name = true;
            namelen = 1;
        }
    else
        namelen = strlen (name);

    if (namelen >= PATH_MAX)
        {
            uar_set_error (uar, UAR_INVALID_PATH, path);
            return NULL;
        }

    DIR *dir = opendir (path);
    struct dirent *entry = NULL;

    if (dir == NULL)
        {
            uar_set_error (uar, UAR_INVALID_FILE, path);
            return NULL;
        }

    struct uar_file *dir_file
        = uar_file_create (name, namelen, 0, uar->header.size);
    uint64_t dir_size = 0;

    dir_file->type = UF_DIR;

    if (callback != NULL && !callback (dir_file, name, path))
        {
            uar_set_error (uar, UAR_SUCCESS, NULL);
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
                    uar_set_error (uar, UAR_INVALID_PATH, path);
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

            if (lstat (fullpath, &stinfo) != 0)
                {
                    uar_set_error (uar, UAR_IO_ERROR, fullpath);
                    goto uar_add_dir_error;
                }

            if (S_ISREG (stinfo.st_mode))
                {
                    struct uar_file *file
                        = uar_add_file (uar, fullname, fullpath, &stinfo);

                    if (file == NULL)
                        {
                            goto uar_add_dir_error;
                        }

                    if (callback != NULL
                        && !callback (file, fullname, fullpath))
                        {
                            uar_set_error (uar, UAR_SUCCESS, NULL);
                            goto uar_add_dir_error;
                        }

                    file->mode = stinfo.st_mode;
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

                    direntry->mode = stinfo.st_mode;
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
uar_debug_print_file_contents (const struct uar_archive *uar,
                               struct uar_file *file)
{
    printf ("    contents:\n");
    printf ("==================\n");
    fflush (stdout);

    ssize_t size
        = write (STDOUT_FILENO, uar->buffer + file->offset, file->data.size);

    if (size == -1 || ((uint64_t) size) != file->data.size)
        {
            perror ("write");
            return;
        }

    putchar ('\n');
    printf ("==================\n");
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
            printf ("    name: \033[1m%s%s\033[0m\n", uar_file_get_name (file),
                    file->type == UF_DIR
                        ? file->name[0] == '/' && file->namelen == 1 ? "" : "/"
                    : file->type == UF_LINK ? "@"
                                            : "");
            printf ("    offset: %lu\n", file->offset);
            printf ("    mode: %04o\n", file->mode);

            if (file->type == UF_LINK)
                printf ("    points to: %s\n", file->data.linkinfo.loc);
            else
                printf ("    size: %lu\n", file->data.size);

            if (file->type == UF_FILE && print_file_contents)
                uar_debug_print_file_contents (uar, file);
        }
}

bool
uar_write (struct uar_archive *uar, const char *filename)
{
    FILE *stream = fopen (filename, "wb");

    if (stream == NULL)
        {
            uar_set_error (uar, UAR_IO_ERROR, filename);
            return false;
        }

    if (fwrite (&uar->header, sizeof (struct uar_header), 1, stream) != 1)
        {
            uar_set_error (uar, UAR_IO_ERROR, filename);
            fclose (stream);
            return false;
        }

    for (uint64_t i = 0; i < uar->header.nfiles; i++)
        {
            struct uar_file *file = uar->files[i];

            if (fwrite (file, sizeof (struct uar_file), 1, stream) != 1)
                {
                    uar_set_error (uar, UAR_IO_ERROR, file->name);
                    fclose (stream);
                    return false;
                }

            if (fwrite (file->name, 1, file->namelen, stream) != file->namelen)
                {
                    uar_set_error (uar, UAR_IO_ERROR, file->name);
                    fclose (stream);
                    return false;
                }
        }

    if (fwrite (uar->buffer, 1, uar->header.size, stream) != uar->header.size)
        {
            uar_set_error (uar, UAR_IO_ERROR, NULL);
            fclose (stream);
            return false;
        }

    fclose (stream);
    return true;
}

bool
uar_extract (struct uar_archive *uar, const char *cwd,
             bool (*callback) (struct uar_file *file))
{
    if (cwd != NULL && chdir (cwd) != 0)
        {
            uar_set_error (uar, UAR_SYSTEM_ERROR, NULL);
            return false;
        }

    for (uint64_t i = 0; i < uar->header.nfiles; i++)
        {
            struct uar_file *file = uar->files[i];

            if (callback != NULL && !callback (file))
                return false;

            char *name = file->name;

            if (name[0] == '/')
                name += 2;

            switch (file->type)
                {
                case UF_FILE:
                    {
                        FILE *stream = fopen (name, "wb");

                        if (stream == NULL)
                            {
                                uar_set_error (uar, UAR_IO_ERROR, name);
                                return false;
                            }

                        if (fwrite (uar->buffer + file->offset, 1,
                                    file->data.size, stream)
                            != file->data.size)
                            {
                                uar_set_error (uar, UAR_IO_ERROR, name);
                                return false;
                            }

                        fchmod (fileno (stream), file->mode & 07777);
                        fclose (stream);
                    }
                    break;

                case UF_DIR:
                    if (file->namelen == 1 && file->name[0] == '/')
                        continue;

                    if (mkdir (name, file->mode) != 0)
                        {
                            uar_set_error (uar, UAR_SYSTEM_ERROR, name);
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

bool
uar_iterate (struct uar_archive *uar,
             bool (*callback) (struct uar_file *file, void *data), void *data)
{
    for (uint64_t i = 0; i < uar->header.nfiles; i++)
        {
            struct uar_file *file = uar->files[i];

            if (!callback (file, data))
                return false;
        }

    return true;
}

const char *
uar_file_get_name (const struct uar_file *file)
{
    return file->name;
}

enum uar_file_type
uar_file_get_type (const struct uar_file *file)
{
    return file->type;
}

mode_t
uar_file_get_mode (const struct uar_file *file)
{
    return file->mode;
}

uint64_t
uar_file_get_size (const struct uar_file *file)
{
    if (file->type == UF_LINK)
        return 0;

    return file->data.size;
}

uint64_t
uar_file_get_namelen (const struct uar_file *file)
{
    return file->namelen;
}

uint64_t
uar_get_file_count (const struct uar_archive *restrict uar)
{
    return uar->header.nfiles;
}

time_t
uar_file_get_mtime (const struct uar_file *file)
{
    return file->mtime;
}

const char *
uar_get_error_file (const struct uar_archive *uar)
{
    return uar->err_file;
}