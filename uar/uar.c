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
#include <utime.h>

#if defined(__linux__)
#    include <linux/limits.h>
#elif defined(__APPLE__)
#    include <sys/syslimits.h>
#else
#    include <limits.h>
#endif

const unsigned char UAR_MAGIC[] = { 0x99, 'U', 'A', 'R' };
const unsigned int UAR_MAX_SUPPORTED_VERSION = 0x01;

struct uar_header
{
    uint8_t magic[4];
    uint16_t version;
    uint32_t flags;
    uint64_t nfiles;
    uint64_t size;
} __attribute__ ((packed));

/* TODO: Fix alignment */
struct uar_file
{
    enum uar_file_type type;
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
        } link;
    } data;
    mode_t mode;
    time_t mtime;
    uid_t uid;
    gid_t gid;
};

struct uar_archive
{
    struct uar_header header;
    struct uar_file **files;
    struct uar_file *root;
    enum uar_error ecode;
    FILE *stream;
    uint64_t stream_size;
    int last_errno;
    char *err_file;
    uint64_t data_start;
    uar_create_callback_t create_callback;
    uar_extract_callback_t extract_callback;
};

void
uar_set_create_callback (struct uar_archive *uar,
                         uar_create_callback_t callback)
{
    uar->create_callback = callback;
}

void
uar_set_extract_callback (struct uar_archive *uar,
                          uar_extract_callback_t callback)
{
    uar->extract_callback = callback;
}

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
        case UAR_INVALID_ARCHIVE:
            return "invalid archive";
        case UAR_UNSUPPORTED_VERSION:
            return "archive version is not supported";
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

    uar->ecode = UAR_SUCCESS;
    uar->header.size = 0;
    memcpy (uar->header.magic, UAR_MAGIC, 4);
    uar->header.version = 1;
    uar->header.flags = 0;
    uar->header.nfiles = 0;
    uar->files = NULL;
    uar->stream = NULL;
    uar->root = NULL;
    uar->stream_size = 0;
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
    root->uid = getuid ();
    root->gid = getgid ();

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
    if (uar == NULL || uar->stream == NULL)
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

            if (file->type == UF_LINK)
                {
                    if (fwrite (file->data.link.loc, 1, file->data.link.loclen,
                                stream)
                        != file->data.link.loclen)
                        {
                            uar_set_error (uar, UAR_SYSCALL_ERROR, file->name);
                            fclose (stream);
                            return false;
                        }
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

    free (buf);
    fclose (stream);
    return true;
}

struct uar_archive *
uar_stream_create (void)
{
    struct uar_archive *uar = uar_create ();
    int cerrno;

    if (uar == NULL)
        return NULL;

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
    uar = NULL;
uar_create_stream_ret:
    return uar;
}

struct uar_file *
uar_stream_add_file (struct uar_archive *uar, const char *uar_filename,
                     const char *fs_filename, struct stat *stinfo)
{
    assert (uar != NULL && "uar is NULL");
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
    uint64_t uar_file_namelen = strlen (uar_filename);

    if (uar_file_namelen > PATH_MAX)
        {
            uar_set_error (uar, UAR_INVALID_PATH, fs_filename);
            return NULL;
        }

    bool contains_dot_dot
        = (uar_file_namelen > 3 && uar_filename[0] == '.'
           && uar_filename[1] == '.' && uar_filename[2] == '/');

    if ((uar_file_namelen > 2 && uar_filename[0] == '.'
         && uar_filename[1] == '/')
        || contains_dot_dot)
        {
            if (uar->create_callback != NULL)
                uar->create_callback (
                    uar, NULL, uar_filename, fs_filename, UAR_ELEVEL_WARNING,
                    contains_dot_dot ? "removing leading '..'"
                                     : "removing leading '.'");

            uar_filename = uar_filename + 1 + (uar_file_namelen == 1);
            uar_file_namelen -= 1 + (uar_file_namelen == 1);
        }

    if (stinfo == NULL)
        {
            if (lstat (fs_filename, &custom_stinfo) != 0)
                {
                    uar_set_error (uar, UAR_SYSCALL_ERROR, fs_filename);

                    if (uar->create_callback != NULL)
                        uar->create_callback (uar, NULL, uar_filename,
                                              fs_filename, UAR_ELEVEL_WARNING,
                                              strerror (errno));

                    return NULL;
                }
            else
                stinfo = &custom_stinfo;
        }

    FILE *stream = fopen (fs_filename, "rb");

    if (stream == NULL)
        {
            uar_set_error (uar, UAR_SYSCALL_ERROR, fs_filename);

            if (uar->create_callback != NULL)
                uar->create_callback (uar, NULL, uar_filename, fs_filename,
                                      UAR_ELEVEL_WARNING, strerror (errno));

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

    file = uar_file_create (uar_filename, uar_file_namelen, size,
                            uar->header.size);

    if (file == NULL)
        {
            ecode = UAR_SYSCALL_ERROR;
            goto uar_stream_add_file_end;
        }

    file->mode = stinfo->st_mode;
    file->data.size = size;
    file->mtime = stinfo->st_mtime;
    uar->header.size += size;
    uar->root->data.size += size;

    if (!uar_add_file_entry (uar, file))
        {
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

    if (ecode == UAR_SUCCESS && uar->create_callback != NULL)
        {
            uar->create_callback (uar, file, uar_filename, fs_filename,
                                  UAR_ELEVEL_NONE, NULL);
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
                    const char *fs_dirname, struct stat *stinfo)
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

                    if (uar->create_callback != NULL)
                        uar->create_callback (uar, NULL, uar_dirname,
                                              fs_dirname, UAR_ELEVEL_WARNING,
                                              strerror (errno));

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

    dir = opendir (fs_dirname);

    if (dir == NULL)
        {
            if (uar->create_callback != NULL)
                uar->create_callback (uar, NULL, uar_dirname, fs_dirname,
                                      UAR_ELEVEL_WARNING, strerror (errno));

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

            struct uar_file *entry_file
                = uar_stream_add_entry (uar, uar_fullpath, fs_fullpath, NULL);

            if (entry_file != NULL && entry_file->type != UF_LINK)
                size += entry_file->data.size;

            free (fs_fullpath);
            free (uar_fullpath);
        }

    file->data.size = size;

    if (ecode == UAR_SUCCESS && uar->create_callback != NULL)
        {
            uar->create_callback (uar, file, uar_dirname, fs_dirname,
                                  UAR_ELEVEL_NONE, NULL);
        }

    goto uar_stream_add_dir_ret;
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

                    if (uar->create_callback != NULL)
                        uar->create_callback (uar, NULL, uar_name, fs_name,
                                              UAR_ELEVEL_WARNING,
                                              strerror (errno));

                    return NULL;
                }
            else
                stinfo = &custom_stinfo;
        }

    uint64_t uar_file_namelen = strlen (uar_name);

    bool contains_dot_dot = (uar_file_namelen > 3 && uar_name[0] == '.'
                             && uar_name[1] == '.' && uar_name[2] == '/');

    if ((uar_file_namelen > 2 && uar_name[0] == '.' && uar_name[1] == '/')
        || contains_dot_dot)
        {
            if (uar->create_callback != NULL)
                uar->create_callback (
                    uar, NULL, uar_name, fs_name, UAR_ELEVEL_WARNING,
                    contains_dot_dot ? "removing leading '..'"
                                     : "removing leading '.'");

            uar_name = uar_name + 1 + (uar_file_namelen == 1);
            uar_file_namelen -= 1 + (uar_file_namelen == 1);
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

            if (uar->create_callback != NULL)
                uar->create_callback (uar, NULL, uar_name, fs_name,
                                      UAR_ELEVEL_WARNING, strerror (errno));

            return NULL;
        }

    file->data.link.loclen = link_len;
    file->data.link.loc = malloc (link_len + 1);

    if (file->data.link.loc == NULL)
        {
            uar_set_error (uar, UAR_OUT_OF_MEMORY, fs_name);
            uar_file_destroy (file);
            return NULL;
        }

    memcpy (file->data.link.loc, link_buf, link_len);
    file->data.link.loc[link_len] = 0;

    if (!uar_add_file_entry (uar, file))
        {
            uar_file_destroy (file);
            return NULL;
        }

    if (uar->create_callback != NULL && file != NULL)
        uar->create_callback (uar, file, uar_name, fs_name, UAR_ELEVEL_NONE,
                              NULL);

    return file;
}

struct uar_file *
uar_stream_add_entry (struct uar_archive *uar, const char *uar_name,
                      const char *fs_name, struct stat *stinfo)
{
    assert (uar != NULL && "uar is NULL");
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
        }
    else if (S_ISDIR (stinfo->st_mode))
        file = uar_stream_add_dir (uar, uar_name, fs_name, stinfo);

    else if (S_ISLNK (stinfo->st_mode))
        file = uar_stream_add_link (uar, uar_name, fs_name, stinfo);
    else
        {
            uar_set_error (uar, UAR_INVALID_FILE, fs_name);
            return NULL;
        }

    if (file != NULL)
        {
            file->mode = stinfo->st_mode;
            file->mtime = stinfo->st_mtime;
            file->uid = stinfo->st_uid;
            file->gid = stinfo->st_gid;
        }

    return file;
}

/* Validate the UAR archive header. */
bool
uar_stream_header_validate (struct uar_archive *uar)
{
    /* Compare magic to ensure it's a valid UAR archive. */
    if (memcmp (uar->header.magic, UAR_MAGIC, sizeof (UAR_MAGIC)) != 0)
        {
            uar_set_error (uar, UAR_INVALID_MAGIC, NULL);
            return false;
        }

    /* Check if the version is supported. */
    if (uar->header.version > UAR_MAX_SUPPORTED_VERSION)
        {
            uar_set_error (uar, UAR_UNSUPPORTED_VERSION, NULL);
            return false;
        }

    /* Check if the data block size is valid, to prevent buffer overflow. If
       it's larger than the stream  size, it's invalid. This could be because
       the archive is corrupted, or it's a malicious archive. */
    if (uar->header.size > (uar->stream_size - sizeof (struct uar_header)))
        {
            uar_set_error (uar, UAR_INVALID_ARCHIVE, NULL);
            return false;
        }

    /* At the moment, UAR doesn't support any flags. */
    if (uar->header.flags != 0)
        {
            uar_set_error (uar, UAR_INVALID_ARCHIVE, NULL);
            return false;
        }

    /* Check if the file is big enough to hold n number of files. */
    if (uar->header.nfiles * sizeof (struct uar_file) > uar->header.size)
        {
            uar_set_error (uar, UAR_INVALID_ARCHIVE, NULL);
            return false;
        }

    return true;
}

struct uar_archive *
uar_stream_open (const char *filename)
{
    struct uar_archive *uar;
    FILE *stream = fopen (filename, "rb");

    if (stream == NULL)
        return NULL;

    uar = uar_create ();

    if (uar == NULL)
        return NULL;

    uar->stream = stream;

    fseek (stream, 0, SEEK_END);
    long size = ftell (stream);

    if (size < 0 || size > INT64_MAX)
        {
            uar_set_error (uar, UAR_SYSCALL_ERROR, NULL);
            return uar;
        }

    fseek (stream, 0, SEEK_SET);

    if (((size_t) size) < sizeof (struct uar_header))
        {
            uar_set_error (uar, UAR_INVALID_ARCHIVE, NULL);
            return uar;
        }

    if (fread (&uar->header, 1, sizeof (struct uar_header), stream)
        != sizeof (struct uar_header))
        {
            uar_set_error (uar, UAR_SYSCALL_ERROR, NULL);
            return uar;
        }

    uar->stream_size = size;

    if (!uar_stream_header_validate (uar))
        return uar;

    uar->files = calloc (uar->header.nfiles, sizeof (struct uar_file *));

    if (uar->files == NULL)
        {
            uar_set_error (uar, UAR_OUT_OF_MEMORY, NULL);
            return uar;
        }

    uint64_t file_block_size
        = sizeof (struct uar_header)
          + (uar->header.nfiles * sizeof (struct uar_file));
    uint64_t data_block_start = file_block_size;

    for (uint64_t i = 0; i < uar->header.nfiles; i++)
        {
            struct uar_file *file = malloc (sizeof (struct uar_file));

            if (file == NULL)
                {
                    uar_set_error (uar, UAR_OUT_OF_MEMORY, NULL);
                    return uar;
                }

            if (fread (file, 1, sizeof (struct uar_file), stream)
                != sizeof (struct uar_file))
                {
                    uar_set_error (uar, UAR_SYSCALL_ERROR, NULL);
                    free (file);
                    return uar;
                }

            /* Right after the file structure, the name of the file is stored,
               with the length of the name stored in the namelen field.
               First, we need to check if the namelen is valid.
             */

            if (file->namelen > PATH_MAX || file->namelen == 0
                || file_block_size + file->namelen > ((uint64_t) size))
                {
                    /* At a later stage, we might want to rather call a callback
                       function instead. */
                    uar_set_error (uar, UAR_INVALID_ARCHIVE, NULL);
                    free (file);
                    return uar;
                }

            data_block_start += file->namelen;
            file->name = malloc (file->namelen + 1);

            if (file->name == NULL)
                {
                    uar_set_error (uar, UAR_OUT_OF_MEMORY, NULL);
                    free (file);
                    return uar;
                }

            if (fread (file->name, 1, file->namelen, stream) != file->namelen)
                {
                    uar_set_error (uar, UAR_SYSCALL_ERROR, NULL);
                    free (file->name);
                    free (file);
                    return uar;
                }

            file->name[file->namelen] = 0;

            /* Next, we need to check if the file is a link. If it is, we need
               to read the link location. */

            if (file->type == UF_LINK)
                {
                    if (file->data.link.loclen > PATH_MAX
                        || file_block_size + file->data.link.loclen
                               > ((uint64_t) size))
                        {
                            uar_set_error (uar, UAR_INVALID_ARCHIVE, NULL);
                            free (file->name);
                            free (file);
                            return uar;
                        }

                    data_block_start += file->data.link.loclen;
                    file->data.link.loc = malloc (file->data.link.loclen + 1);

                    if (file->data.link.loc == NULL)
                        {
                            uar_set_error (uar, UAR_OUT_OF_MEMORY, NULL);
                            free (file->name);
                            free (file);
                            return uar;
                        }

                    if (fread (file->data.link.loc, 1, file->data.link.loclen,
                               stream)
                        != file->data.link.loclen)
                        {
                            uar_set_error (uar, UAR_SYSCALL_ERROR, NULL);
                            free (file->name);
                            free (file->data.link.loc);
                            free (file);
                            return uar;
                        }

                    file->data.link.loc[file->data.link.loclen] = 0;
                }

            uar->files[i] = file;
        }

    uar->data_start = data_block_start;
    return uar;
}

static bool
uar_stream_extract_file (struct uar_archive *uar, struct uar_file *file,
                         const char *path)
{
    FILE *stream = fopen (path, "wb");
    uint8_t buffer[1024];
    uint64_t size = file->data.size;

    if (stream == NULL)
        {
            uar_set_error (uar, UAR_SYSCALL_ERROR, path);
            perror ("fopen");

            if (uar->extract_callback != NULL)
                uar->extract_callback (uar, file, file->name, path,
                                       UAR_ELEVEL_WARNING, strerror (errno));

            return false;
        }

    if (fseek (uar->stream, uar->data_start + file->offset, SEEK_SET) != 0)
        {
            uar_set_error (uar, UAR_SYSCALL_ERROR, path);

            if (uar->extract_callback != NULL)
                uar->extract_callback (uar, file, file->name, path,
                                       UAR_ELEVEL_WARNING, strerror (errno));

            fclose (stream);
            return false;
        }

    while (size > 0)
        {
            size_t read_size = size > sizeof (buffer) ? sizeof (buffer) : size;

            if (fread (buffer, 1, read_size, uar->stream) != read_size)
                {
                    uar_set_error (uar, UAR_SYSCALL_ERROR, path);

                    if (uar->extract_callback != NULL)
                        uar->extract_callback (uar, file, file->name, path,
                                               UAR_ELEVEL_WARNING,
                                               strerror (errno));

                    fclose (stream);
                    return false;
                }

            if (fwrite (buffer, 1, read_size, stream) != read_size)
                {
                    uar_set_error (uar, UAR_SYSCALL_ERROR, path);

                    if (uar->extract_callback != NULL)
                        uar->extract_callback (uar, file, file->name, path,
                                               UAR_ELEVEL_WARNING,
                                               strerror (errno));

                    fclose (stream);
                    return false;
                }

            size -= read_size;
        }

    if (fchmod (fileno (stream), file->mode) != 0)
        {
            uar_set_error (uar, UAR_SYSCALL_ERROR, path);

            if (uar->extract_callback != NULL)
                uar->extract_callback (uar, file, file->name, path,
                                       UAR_ELEVEL_WARNING, strerror (errno));

            fclose (stream);
            return false;
        }

    fclose (stream);
    return true;
}

bool
uar_stream_extract (struct uar_archive *uar, const char *dest)
{
    for (uint64_t i = 0; i < uar->header.nfiles; i++)
        {
            struct uar_file *file = uar->files[i];
            char *name = file->name;
            size_t diff = 0;
            bool is_root = strcmp (file->name, "/") == 0;

            if (!is_root)
                {
                    if (name[0] == '/')
                        diff += 1;

                    if (strncmp (name, "./", 2) == 0)
                        diff += 2;
                    else if (strncmp (name, "../", 3) == 0)
                        diff += 3;
                    else if (strcmp (name, "..") == 0)
                        diff += 2;
                    else if (strcmp (name, ".") == 0)
                        diff += 1;
                }

            char *path
                = is_root ? (char *) dest
                          : path_concat (dest, file->name + diff, strlen (dest),
                                         file->namelen - diff);

            if (path == NULL)
                {
                    uar_set_error (uar, UAR_OUT_OF_MEMORY, file->name);
                    return false;
                }

            if (!is_root)
                {
                    switch (file->type)
                        {
                        case UF_FILE:
                            if (!uar_stream_extract_file (uar, file, path))
                                {
                                    free (path);
                                    return false;
                                }
                            break;
                        case UF_DIR:
                            if (mkdir (path, file->mode) != 0)
                                {
                                    uar_set_error (uar, UAR_SYSCALL_ERROR,
                                                   path);

                                    if (uar->extract_callback != NULL)
                                        uar->extract_callback (
                                            uar, file, file->name, path,
                                            UAR_ELEVEL_WARNING,
                                            strerror (errno));

                                    free (path);
                                    return false;
                                }
                            break;
                        case UF_LINK:
                            if (symlink (file->data.link.loc, path) != 0)
                                {
                                    uar_set_error (uar, UAR_SYSCALL_ERROR,
                                                   path);

                                    if (uar->extract_callback != NULL)
                                        uar->extract_callback (
                                            uar, file, file->name, path,
                                            UAR_ELEVEL_WARNING,
                                            strerror (errno));

                                    free (path);
                                    return false;
                                }
                            break;
                        default:
                            uar_set_error (uar, UAR_INVALID_FILE, file->name);

                            if (uar->extract_callback != NULL)
                                uar->extract_callback (uar, file, file->name,
                                                       path, UAR_ELEVEL_WARNING,
                                                       strerror (errno));

                            free (path);
                            return false;
                        }
                }

            struct utimbuf times
                = { .actime = time (NULL), .modtime = file->mtime };

            if (utime (path, &times) != 0)
                {
                    uar_set_error (uar, UAR_SYSCALL_ERROR, path);

                    if (uar->extract_callback != NULL)
                        uar->extract_callback (uar, file, file->name, path,
                                               UAR_ELEVEL_WARNING,
                                               strerror (errno));

                    if (!is_root)
                        free (path);

                    return false;
                }

            if (chown (path, file->uid, file->gid) != 0)
                {
                    uar_set_error (uar, UAR_SYSCALL_ERROR, path);

                    if (uar->extract_callback != NULL)
                        uar->extract_callback (uar, file, file->name, path,
                                               UAR_ELEVEL_WARNING,
                                               strerror (errno));

                    if (!is_root)
                        free (path);

                    return false;
                }

            if (uar->extract_callback != NULL)
                uar->extract_callback (uar, file, file->name, path,
                                       UAR_ELEVEL_NONE, NULL);

            if (!is_root)
                free (path);
        }

    return true;
}

void
uar_file_destroy (struct uar_file *file)
{
    if (file == NULL)
        return;

    if (file->type == UF_LINK)
        free (file->data.link.loc);

    free (file->name);
    free (file);
}

void
uar_close (struct uar_archive *uar)
{
    if (uar == NULL)
        return;

    fclose (uar->stream);

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
    file->uid = 0;
    file->gid = 0;
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

    if (fseek (uar->stream, uar->data_start + file->offset, SEEK_SET) != 0)
        {
            perror ("fseek");
            return;
        }

    uint8_t buffer[1024];

    while (file->data.size > 0)
        {
            size_t size = file->data.size > sizeof (buffer) ? sizeof (buffer)
                                                            : file->data.size;

            if (fread (buffer, 1, size, uar->stream) != size)
                {
                    perror ("read");
                    return;
                }

            for (size_t i = 0; i < size; i++)
                {
                    if (buffer[i] == '\n')
                        putchar ('\n');
                    else
                        putchar (buffer[i]);
                }

            file->data.size -= size;
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
                printf ("    points to: %s\n", file->data.link.loc);
            else
                printf ("    size: %lu\n", file->data.size);

            if (file->type == UF_FILE && print_file_contents)
                uar_debug_print_file_contents (uar, file);
        }
}

bool
uar_stream_iterate (struct uar_archive *uar,
                    bool (*callback) (struct uar_file *file, void *data),
                    void *data)
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

uid_t
uar_file_get_uid (const struct uar_file *file)
{
    return file->uid;
}

gid_t
uar_file_get_gid (const struct uar_file *file)
{
    return file->gid;
}