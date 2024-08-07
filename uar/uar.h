#ifndef UAR_UAR_H
#define UAR_UAR_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

struct uar_archive;
struct uar_file;

enum uar_error
{
    UAR_SUCCESS,
    UAR_INVALID_MAGIC,
    UAR_INVALID_FILE,
    UAR_IO_ERROR,
    UAR_OUT_OF_MEMORY,
    UAR_INVALID_ARGUMENT,
    UAR_INVALID_OPERATION,
    UAR_INVALID_PATH,
    UAR_SYSTEM_ERROR,
    UAR_SYSCALL_ERROR,
};

enum uar_file_type
{
    UF_FILE,
    UF_DIR,
    UF_LINK
};

typedef bool (*uar_callback_t) (struct uar_archive *uar, struct uar_file *file,
                                const char *uar_name, const char *fs_name);

struct uar_archive *uar_create (void);
struct uar_archive *uar_open (const char *filename);
struct uar_archive *uar_create_stream (void);
void uar_close (struct uar_archive *uar);

struct uar_file *uar_stream_add_file (struct uar_archive *uar,
                                      const char *uar_filename,
                                      const char *fs_filename,
                                      struct stat *stinfo);
bool uar_stream_write (struct uar_archive *uar, const char *filename);

bool uar_has_error (const struct uar_archive *restrict uar);
const char *uar_strerror (const struct uar_archive *restrict uar);
uint64_t uar_get_file_count (const struct uar_archive *restrict uar);

struct uar_file *uar_add_file (struct uar_archive *restrict uar,
                               const char *name, const char *path,
                               struct stat *stinfo);
struct uar_file *
uar_add_dir (struct uar_archive *uar, const char *name, const char *path,
             bool (*callback) (struct uar_file *file, const char *fullname,
                               const char *fullpath));
bool uar_extract (struct uar_archive *uar, const char *cwd,
                  bool (*callback) (struct uar_file *file));
bool uar_write (struct uar_archive *uar, const char *filename);
bool uar_iterate (struct uar_archive *uar,
                  bool (*callback) (struct uar_file *file, void *data),
                  void *data);

struct uar_file *uar_file_create (const char *name, uint64_t namelen,
                                  uint64_t size, uint32_t offset);

bool uar_add_file_entry (struct uar_archive *restrict uar,
                         struct uar_file *file);

void uar_file_destroy (struct uar_file *file);

struct uar_file *uar_stream_add_entry (struct uar_archive *uar,
                                       const char *uar_name,
                                       const char *fs_name, struct stat *stinfo,
                                       uar_callback_t callback);
struct uar_file *uar_stream_add_dir (struct uar_archive *uar,
                                     const char *uar_dirname,
                                     const char *fs_dirname,
                                     struct stat *stinfo,
                                     uar_callback_t callback);

const char *uar_file_get_name (const struct uar_file *file);
enum uar_file_type uar_file_get_type (const struct uar_file *file);
uint64_t uar_file_get_size (const struct uar_file *file);
mode_t uar_file_get_mode (const struct uar_file *file);
void uar_file_set_mode (struct uar_file *file, mode_t mode);
uint64_t uar_file_get_namelen (const struct uar_file *file);
time_t uar_file_get_mtime (const struct uar_file *file);
const char *uar_get_error_file (const struct uar_archive *uar);

#ifdef UAR_PRINT_VERBOSE_IMPL_INFO
void uar_debug_print (const struct uar_archive *uar, bool print_file_contents);
#endif

#endif /* UAR_UAR_H */