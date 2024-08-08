/*
 * main.c -- entry point and argument parsing for the UAR program
 *
 * This program is part of the UAR (Universal Archive) utility suite.
 * Copyright (C) 2024  OSN, Inc.
 * Author:  Ar Rakin <rakinar2@onesoftnet.eu.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _XOPEN_SOURCE 500

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "uar.h"
#include "xmalloc.h"

#ifdef HAVE_CONFIG_H
#    include "config.h"
#else
#    define PACKAGE_NAME "uar"
#    define PACKAGE_VERSION "1.0"
#    define PACKAGE_BUGREPORT "uar@onesoftnet.eu.org"
#endif

#ifndef NDEBUG
#    define debug(...) pdebug (__FILE__, __LINE__, __VA_ARGS__)
#else
#    define debug(...)
#endif

/* Command line options. */
static struct option const long_options[] = {
    { "create",         no_argument,       NULL, 'c' },
    { "extract",        no_argument,       NULL, 'x' },
    { "human-readable", no_argument,       NULL, 'm' },
    { "list",           no_argument,       NULL, 't' },
    { "verbose",        no_argument,       NULL, 'v' },
    { "file",           required_argument, NULL, 'f' },
    { "directory",      required_argument, NULL, 'C' },
    { "help",           no_argument,       NULL, 'h' },
    { "version",        no_argument,       NULL, 'V' },
    { NULL,             0,                 NULL, 0   },
};

static char const short_options[] = "cxtvmf:C:hV";

/* Program name. */
static char *progname = NULL;

/* Flags for the command line options. */
enum uar_mode
{
    MODE_NONE = 0,
    MODE_CREATE,
    MODE_EXTRACT,
    MODE_LIST
};

struct uar_params
{
    enum uar_mode mode;
    bool verbose;
    bool hr_sizes;
    char *file;
    char *cwd;
    char **targets;
    char **rtargets;
    size_t ntargets;
};

static struct uar_params params = { 0 };

/* Print usage information. */
static void
usage (void)
{
    printf ("Usage:\n");
    printf ("  uar [OPTION]... [FILE]...\n");
    printf ("\n");
    printf ("Universal Archive utility.\n");
    printf ("\n");
    printf ("Options:\n");
    printf ("  -c, --create            Create a new archive\n");
    printf ("  -x, --extract           Extract files from an archive\n");
    printf ("  -t, --list              List the contents of an archive\n");
    printf ("  -m, --human-readable    Print human-readable sizes\n");
    printf ("  -v, --verbose           Verbose mode\n");
    printf (
        "  -f, --file=ARCHIVE      Use archive file or directory ARCHIVE\n");
    printf ("  -C, --directory=DIR     Change to directory DIR\n");
    printf ("  -h, --help              Display this help and exit\n");
    printf ("  -V, --version           Output version information and exit\n");
    printf ("\n");
    printf ("Report bugs to: <" PACKAGE_BUGREPORT ">\n");
}

/* Print version information. */
static void
show_version (void)
{
    printf ("OSN %s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
    printf ("\n");
    printf ("Copyright (C) 2024 OSN, Inc.\n");
    printf ("License GPLv3+: GNU GPL version 3 or later "
            "<http://gnu.org/licenses/gpl.html>\n");
    printf (
        "This is free software: you are free to change and redistribute it.\n");
    printf ("There is NO WARRANTY, to the extent permitted by law.\n");
    printf ("\n");
    printf ("Written by Ar Rakin <rakinar2@onesoftnet.eu.org>\n");
}

#ifndef NDEBUG
/* Print a debug message. */
static void
pdebug (char const *file, int line, char const *format, ...)
{
    if (!params.verbose)
        return;

    va_list args;
    va_start (args, format);
    fprintf (stderr, "%s(verbose): %s:%i: ", progname, file, line);
    vfprintf (stderr, format, args);
    va_end (args);
}
#endif

/* Print a message. */
static void
pinfo (char const *format, ...)
{
    va_list args;
    va_start (args, format);
    fprintf (stdout, "%s: ", progname);
    vfprintf (stdout, format, args);
    va_end (args);
}

/* Print an error message. */
static void
perr (char const *format, ...)
{
    va_list args;
    va_start (args, format);
    fprintf (stderr, "%s: ", progname);
    vfprintf (stderr, format, args);
    va_end (args);
}

/* Cleanup memory. */
static void
cleanup ()
{
    for (size_t i = 0; i < params.ntargets; i++)
        free (params.targets[i]);

    if (params.targets != NULL)
        free (params.targets);

    if (params.cwd != NULL)
        free (params.cwd);

    if (params.file != NULL)
        free (params.file);
}

/* Initialize the program. */
static void
initialize (char *argv0)
{
    atexit (&cleanup);
    progname = argv0;
}

/* Create archive callback. */
static bool
create_archive_callback (struct uar_archive *uar,
                         struct uar_file *file __attribute__ ((unused)),
                         const char *uar_name __attribute__ ((unused)),
                         const char *fs_name, enum uar_error_level level,
                         const char *message)
{
    if (level == UAR_ELEVEL_NONE)
        {
            if (params.verbose)
                fprintf (stdout, "%s\n", fs_name);
        }
    else if (level == UAR_ELEVEL_WARNING)
        perr ("warning: %s: %s\n", fs_name,
              message != NULL ? message : uar_strerror (uar));
    else if (level == UAR_ELEVEL_ERROR)
        perr ("error: %s: %s\n", fs_name,
              message != NULL ? message : uar_strerror (uar));

    return true;
}

/* Create an archive. */
static void
create_archive (void)
{
    assert (params.mode == MODE_CREATE);
    assert (params.ntargets > 0);
    assert (params.targets != NULL);

    if (params.verbose)
        pinfo ("creating archive: %s\n", params.file);

    struct uar_archive *uar = uar_create_stream ();

    if (uar == NULL)
        {
            pinfo ("failed to create archive: %s\n", strerror (errno));
            return;
        }

    uar_set_create_callback (uar, &create_archive_callback);

    for (size_t i = 0; i < params.ntargets; i++)
        {
            struct stat stinfo = { 0 };

            if (stat (params.targets[i], &stinfo) != 0)
                {
                    perr ("cannot stat '%s': %s\n", params.targets[i],
                          strerror (errno));
                    uar_close (uar);
                    return;
                }

            struct uar_file *file
                = uar_stream_add_entry (uar, basename (params.rtargets[i]),
                                        params.rtargets[i], &stinfo);

            if (file == NULL || uar_has_error (uar))
                {
                    const char *error_file = uar_get_error_file (uar);
                    perr ("failed to add '%s': %s\n",
                          error_file == NULL ? params.targets[i] : error_file,
                          uar_strerror (uar));
                    exit (1);
                }
        }

    if (!uar_stream_write (uar, params.file))
        {
            const char *error_file = uar_get_error_file (uar);
            pinfo ("failed to write archive: %s%s%s\n",
                   error_file == NULL ? "" : error_file,
                   error_file == NULL ? "" : ": ", uar_strerror (uar));
            return;
        }

#ifdef UAR_PRINT_VERBOSE_IMPL_INFO
    uar_debug_print (uar, false);
#endif
    uar_close (uar);
}

/* Archive extraction callback. */
static bool
extract_archive_callback (struct uar_file *file)
{
    pinfo ("extracting: %s\n", uar_file_get_name (file));
    return true;
}

/* Extract an archive. */
static void
extract_archive (void)
{
    assert (params.mode == MODE_EXTRACT);

    pinfo ("extracting archive: %s\n", params.file);

    struct uar_archive *uar = uar_open (params.file);

    if (uar == NULL || uar_has_error (uar))
        {
            pinfo ("failed to open archive: %s\n", strerror (errno));
            return;
        }

#ifdef UAR_PRINT_VERBOSE_IMPL_INFO
    uar_debug_print (uar, false);
#endif

    if (!uar_extract (uar, params.cwd, &extract_archive_callback))
        {
            pinfo ("failed to extract archive: %s\n", strerror (errno));
            return;
        }

    uar_close (uar);
}

static const char *
stringify_mode (mode_t mode)
{
    static char str[11];

    str[0] = S_ISDIR (mode) ? 'd' : S_ISLNK (mode) ? 'l' : '-';

    for (int i = 1; i < 10; i++)
        str[i] = mode & (1 << (9 - i)) ? "rwxrwxrwx"[i - 1] : '-';

    return str;
}

static int
count_dec_numlen (uint64_t num)
{
    int len = 0;

    do
        {
            num /= 10;
            len++;
        }
    while (num > 0);

    return len;
}

static char *
format_iec_size (uint64_t size)
{
    static char buf[32] = { 0 };
    const char suffix[] = { ' ', 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y' };
    size_t i = 0;
    long double computed = size;

    while (computed > 1024 && i < sizeof (suffix) / sizeof (suffix[0]))
        {
            computed /= 1024;
            i++;
        }

    snprintf (buf, sizeof (buf), "%.02Lf%c", computed, suffix[i]);
    return buf;
}

struct archive_file_info
{
    mode_t mode;
    const char *name;
    time_t mtime;
    union
    {

        uint64_t bytes;
        char str[64];
    } size;
};

struct archive_file_table
{
    struct archive_file_info *files;
    size_t nfiles;
    int widths[1];
};

#define TABLE_WIDTH_SIZE 1

static bool
list_archive_callback_analyze (struct uar_file *file, void *data)
{
    struct archive_file_table *table = (struct archive_file_table *) data;
    struct archive_file_info info = { 0 };

    mode_t mode = uar_file_get_mode (file);
    const char *name = uar_file_get_name (file);
    uint64_t size = uar_file_get_size (file);
    int size_len = 0;

    info.mode = mode;
    info.name = name;
    info.mtime = uar_file_get_mtime (file);

    if (params.hr_sizes)
        {
            char *str = format_iec_size (size);
            size_len = strlen (str);
            strncpy (info.size.str, str, 32);
        }
    else
        {
            info.size.bytes = size;
            size_len = count_dec_numlen (size);
        }

    if (size_len > table->widths[TABLE_WIDTH_SIZE])
        table->widths[TABLE_WIDTH_SIZE] = size_len;

    table->files[table->nfiles++] = info;
    return true;
}

static void
list_archive (void)
{
    assert (params.mode == MODE_LIST);
    struct archive_file_table *table = NULL;
    struct uar_archive *uar = uar_open (params.file);

    if (uar == NULL || uar_has_error (uar))
        {
            pinfo ("failed to open archive: %s\n", strerror (errno));
            goto list_archive_end;
        }

    uint64_t nfiles = uar_get_file_count (uar);

    table = xcalloc (1, sizeof (struct archive_file_table));
    table->files = xcalloc (nfiles, sizeof (struct archive_file_info));
    table->nfiles = 0;

    if (!uar_iterate (uar, &list_archive_callback_analyze, (void *) table))
        {
            pinfo ("failed to read archive: %s\n", strerror (errno));
            goto list_archive_end;
        }

    for (size_t i = 0; i < nfiles; i++)
        {
            struct archive_file_info info = table->files[i];
            struct tm *tm = localtime (&info.mtime);
            char mtime_str[10] = "none";
            const char *mode_str = stringify_mode (info.mode);

            if (tm == NULL)
                {
                    fprintf (stderr,
                             "%s: warning: failed to convert time: %s\n",
                             progname, strerror (errno));
                }
            else
                {
                    strftime (mtime_str, sizeof (mtime_str), "%b %d", tm);
                }

            if (params.hr_sizes)
                {

                    fprintf (stdout, "%s %*s %s %s\n", mode_str,
                             table->widths[TABLE_WIDTH_SIZE], info.size.str,
                             mtime_str, info.name);
                }
            else
                {

                    fprintf (stdout, "%s %*lu %s %s\n", mode_str,
                             table->widths[TABLE_WIDTH_SIZE], info.size.bytes,
                             mtime_str, info.name);
                }
        }

list_archive_end:
    free (table->files);
    free (table);
    uar_close (uar);
}

int
main (int argc, char **argv)
{
    initialize (argv[0]);
    assert (progname != NULL && "progname is NULL");

    while (true)
        {
            int opt
                = getopt_long (argc, argv, short_options, long_options, NULL);

            if (opt == -1)
                break;

            if ((opt == 'c' || opt == 'x' || opt == 't')
                && params.mode != MODE_NONE)
                {
                    perr ("only one mode can be specified\n");
                    exit (1);
                }

            switch (opt)
                {
                case 'c':
                    params.mode = MODE_CREATE;
                    break;

                case 'x':
                    params.mode = MODE_EXTRACT;
                    break;

                case 't':
                    params.mode = MODE_LIST;
                    break;

                case 'v':
                    params.verbose = true;
                    debug ("Verbose mode enabled\n", progname);
                    break;

                case 'm':
                    params.hr_sizes = true;
                    break;

                case 'f':
                    params.file = optarg;
                    break;

                case 'C':
                    params.cwd = optarg;
                    break;

                case 'h':
                    usage ();
                    exit (0);

                case 'V':
                    show_version ();
                    exit (0);

                case '?':
                default:
                    debug ("Unknown/Unhandled option: %c\n", opt);
                    bzero (&params, sizeof (params));
                    exit (1);
                }
        }

    if (params.file != NULL)
        {
            char *file = params.file;

            if ((params.mode == MODE_EXTRACT || params.mode == MODE_LIST))
                {
                    params.file = realpath (file, NULL);

                    if (params.file == NULL)
                        {
                            perr ("failed to read '%s': %s\n", file,
                                  strerror (errno));
                            exit (1);
                        }
                }
            else
                {
                    params.file = strdup (file);
                }
        }

    if (params.cwd != NULL)
        {
            if (params.mode == MODE_LIST)
                {
                    params.cwd = NULL;
                    perr ("option '-C' or '--directory' does not make sense in "
                          "list mode\n");
                    exit (1);
                }

            char *dir = params.cwd;
            params.cwd = realpath (dir, NULL);

            if (params.cwd == NULL)
                {
                    perr ("failed to change working directory to '%s': %s\n",
                          dir, strerror (errno));
                    exit (1);
                }
        }

    if (params.verbose)
        {
            debug ("Summary of options:\n");
            debug ("  mode: %s\n", params.mode == MODE_CREATE    ? "create"
                                   : params.mode == MODE_EXTRACT ? "extract"
                                                                 : "list");
            debug ("  verbose: %s\n", params.verbose ? "yes" : "no");
            debug ("  file: %s\n", params.file);
            debug ("  working directory: %s\n", params.cwd);
        }

    switch (params.mode)
        {
        case MODE_CREATE:
            if (params.file == NULL)
                {
                    perr ("no archive file name specified\n");
                    exit (1);
                }

            for (int i = optind; i < argc; i++)
                {
                    char *path = realpath (argv[i], NULL);

                    if (path == NULL)
                        {
                            perr ("failed to read '%s': %s\n", argv[i],
                                  strerror (errno));
                            exit (1);
                        }

                    params.targets
                        = xrealloc (params.targets,
                                    (params.ntargets + 1) * sizeof (char *));
                    params.targets[params.ntargets] = path;
                    params.ntargets++;
                }

            params.rtargets = argv + optind;

            if (params.ntargets == 0)
                {
                    perr ("no files or directories specified\n");
                    exit (1);
                }

            create_archive ();
            break;

        case MODE_EXTRACT:
            if (params.file == NULL)
                {
                    perr ("no archive file specified\n");
                    exit (1);
                }

            extract_archive ();
            break;

        case MODE_LIST:
            if (params.file == NULL)
                {
                    perr ("no archive file specified\n");
                    exit (1);
                }

            list_archive ();
            break;

        default:
            usage ();
            exit (1);
        }

    return 0;
}