#ifndef UAR_UAR_H
#define UAR_UAR_H

#include <stdbool.h>
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
    UAR_SYSTEM_ERROR
};

enum uar_file_type
{
    UF_FILE,
    UF_DIR,
    UF_LINK
};

struct uar_archive *uar_open (const char *filename);
bool uar_has_error (const struct uar_archive *restrict uar);
void uar_close (struct uar_archive *uar);
const char *uar_strerror (const struct uar_archive *restrict uar);
struct uar_archive *uar_create (void);
struct uar_file *uar_add_file (struct uar_archive *restrict uar,
                               const char *name, const char *path);
void uar_file_set_mode (struct uar_file *file, mode_t mode);
bool uar_write (struct uar_archive *uar, const char *filename);
struct uar_file *
uar_add_dir (struct uar_archive *uar, const char *name, const char *path,
             bool (*callback) (struct uar_file *file, const char *fullname,
                               const char *fullpath));
bool uar_extract (struct uar_archive *uar, const char *cwd,
                  bool (*callback) (struct uar_file *file));
const char *uar_get_file_name (const struct uar_file *file);
enum uar_file_type uar_get_entry_type (const struct uar_file *file);

#ifndef NDEBUG
void uar_debug_print (const struct uar_archive *uar, bool print_file_contents);
#endif

#endif /* UAR_UAR_H */