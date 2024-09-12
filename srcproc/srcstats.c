/*
 * srcstats.c -- Source code statistics generator
 *
 * This file is part of OSN Commons.
 * Copyright (C) 2024  OSN Developers.
 *
 * OSN Commons is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * OSN Commons is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OSN Commons.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#    include "config.h"
#endif

#define PROG_CANONICAL_NAME "srcstats"
#define PROG_AUTHORS "Ar Rakin <rakinar2@onesoftnet.eu.org>"

/* TODO: Add support for more file types, and
   output statistics separately for each file type. */

static const char *prog_name = NULL;

static struct option const long_options[] = {
    { "help",    no_argument, 0, 'h' },
    { "version", no_argument, 0, 'v' },
    { 0,         0,           0, 0   }
};

static const char *short_options = "hv";

struct codebase_scan_state
{
    char *filename;
    char *directory;
    char *extension;
    char *shebang_prog;
    struct codebase_report *report;
};

struct codebase_report
{
    unsigned long int files;
    unsigned long int ignored;
    unsigned long int directories;
    unsigned long int lines;
    unsigned long int blank_lines;
    unsigned long int comment_lines;
    unsigned long int code_lines;
    char *directory;
};

static void codebase_report_analyze_c (struct codebase_scan_state *state,
                                       FILE *file);
static void codebase_report_analyze_sh (struct codebase_scan_state *state,
                                        FILE *file);

struct codebase_file_handler
{
    void (*handler) (struct codebase_scan_state *, FILE *);
    const char **extensions;
    const char **filenames;
    const char **shebangs;
};

/* clang-format off */
static struct codebase_file_handler codebase_file_handlers[] = {
    { 
        .handler = &codebase_report_analyze_c,  
        .extensions = (const char *[]) { 
            "c", 
            "h", 
            "cpp", 
            "hpp", 
            "cc", 
            "hh", 
            "cxx",
            "hxx", 
            "ts", 
            "js",
            "java",
            NULL 
        },
        .filenames = NULL,
        .shebangs = NULL
    },
    { 
        .handler = &codebase_report_analyze_sh,
        .extensions = (const char *[]) { 
            "sh", 
            "bash", 
            "conf", 
            "fish", 
            "csh", 
            "zsh",
            "am",
            "ac",
            NULL
        },
        .filenames = (const char *[]) { 
            "Makefile",
            "Dockerfile",
            NULL
        },
       .shebangs = (const char *[]) { 
            "sh", 
            "bash", 
            "fish", 
            "zsh",
            "csh",
            NULL
        } 
    },
};
/* clang-format on */

static void
report_error (const char *format, ...)
{
    va_list args;
    va_start (args, format);
    fprintf (stderr, "%s: ", prog_name);
    vfprintf (stderr, format, args);
    fprintf (stderr, ": %s", strerror (errno));
    va_end (args);
    fputc ('\n', stderr);
}

static void *
xrealloc (void *ptr, size_t size)
{
    void *new_ptr = realloc (ptr, size);

    if (new_ptr == NULL)
        {
            report_error ("xrealloc(): failed to reallocate memory");
            exit (EXIT_FAILURE);
        }

    return new_ptr;
}

static char *
path_join (const char *p1, const char *p2, size_t *len)
{
    char *path = NULL;
    size_t length = 0;
    size_t index = 0;

    while (p1[index] != 0)
        {
            length++;
            path = xrealloc (path, length);
            path[index] = p1[index];
            index++;
        }

    path = xrealloc (path, length + 1);
    path[length++] = '/';

    index = 0;

    while (p2[index] != 0)
        {
            length++;
            path = xrealloc (path, length);
            path[length - 1] = p2[index];
            index++;
        }

    path = xrealloc (path, length + 1);
    path[length] = 0;

    if (len)
        *len = length;

    return path;
}

static void
codebase_report_analyze_c (struct codebase_scan_state *state, FILE *file)
{
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    bool in_comment = false;
    struct codebase_report *report = state->report;

    while ((read = getline (&line, &len, file)) != -1)
        {
            bool comment_line_incremented = false;
            ssize_t i = 0;

            report->lines++;

            if (in_comment)
                {
                    while (i < read)
                        {
                            if (i + 1 < read
                                && (line[i] == '*' && line[i + 1] == '/'))
                                {
                                    in_comment = false;
                                    i += 2;
                                    break;
                                }

                            i++;
                        }

                    report->comment_lines++;
                    comment_line_incremented = true;

                    if (i == read)
                        continue;
                }

            while (i < read && isspace (line[i]))
                i++;

            if (i == read)
                {
                    report->blank_lines++;
                    continue;
                }

            if (i + 1 < read
                && (line[i] == '\'' || line[i] == '"'
                    || ((strcmp (state->extension, "ts") == 0
                         || strcmp (state->extension, "js") == 0)
                        && line[i] == '`')))
                {
                    char quote = line[i];

                    while (i < read)
                        {
                            if (line[i] == '\\')
                                i++;

                            if (i < read && line[i] == quote)
                                {
                                    quote = 0;
                                    break;
                                }

                            if (i < read)
                                i++;
                        }

                    if (quote == '`')
                        {
                            while ((read = getline (&line, &len, file)) != -1)
                                {
                                    report->lines++;

                                    if (line[0] == quote)
                                        break;

                                    report->code_lines++;
                                }

                            continue;
                        }

                    if (i == read)
                        {
                            report->code_lines++;
                            continue;
                        }
                }

            if (i + 1 < read && line[i] == '/' && line[i + 1] == '/')
                {
                    if (!comment_line_incremented)
                        report->comment_lines++;

                    comment_line_incremented = true;
                    continue;
                }

            if (i + 1 < read && line[i] == '/' && line[i + 1] == '*')
                {
                    in_comment = true;

                    if (!comment_line_incremented)
                        report->comment_lines++;

                    comment_line_incremented = true;

                    while (i < read)
                        {
                            if (i + 1 < read
                                && (line[i] == '*' && line[i + 1] == '/'))
                                {
                                    in_comment = false;
                                    i += 2;
                                    break;
                                }

                            i++;
                        }
                }

            while (i < read && isspace (line[i]))
                i++;

            if (!in_comment && i < read)
                report->code_lines++;
        }

    free (line);
}

static void
codebase_report_analyze_sh (struct codebase_scan_state *state, FILE *file)
{
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    struct codebase_report *report = state->report;

    while ((read = getline (&line, &len, file)) != -1)
        {
            ssize_t i = 0;

            report->lines++;

            while (i < read && isspace (line[i]))
                i++;

            if (i == read)
                {
                    report->blank_lines++;
                    continue;
                }

            if (i + 1 < read && line[i] == '#')
                {
                    if (i + 2 >= read || line[i + 1] != '!')
                        report->comment_lines++;

                    continue;
                }

            if (i + 2 < read && line[i] == '<' && line[i + 1] == '<')
                {
                    char *token = NULL;
                    size_t token_len = 0;

                    while (i < read && isspace (line[i]))
                        i++;

                    while (i < read && !isspace (line[i]))
                        {
                            token = xrealloc (token, ++token_len);
                            token[token_len - 1] = line[i];
                            i++;
                        }

                    token = xrealloc (token, ++token_len);
                    token[token_len - 1] = 0;

                    while ((read = getline (&line, &len, file)) != -1)
                        {
                            report->lines++;

                            if (strcmp (line, token) == 0)
                                break;

                            report->code_lines++;
                        }

                    free (token);
                    continue;
                }

            if (i + 1 < read && (line[i] == '\'' || line[i + 1] == '"'))
                {
                    char quote = line[i];

                    while (i < read)
                        {
                            if (line[i] == '\\')
                                i++;

                            if (i < read && line[i] == quote)
                                {
                                    quote = 0;
                                    break;
                                }

                            if (i < read)
                                i++;
                        }

                    if (quote != 0)
                        {
                            while ((read = getline (&line, &len, file)) != -1)
                                {
                                    report->lines++;

                                    if (line[0] == quote)
                                        break;

                                    report->code_lines++;
                                }

                            continue;
                        }

                    if (i == read)
                        {
                            report->code_lines++;
                            continue;
                        }
                }

            while (i < read && isspace (line[i]))
                i++;

            if (i < read)
                report->code_lines++;
        }

    free (line);
}

static void
codebase_scan_state_free (struct codebase_scan_state *state)
{
    free (state->directory);
    free (state->extension);
    free (state->filename);
    free (state->shebang_prog);
}

static char *
get_file_shebang (FILE *file)
{
    char *line = NULL, *ret = NULL;
    size_t len = 0;
    ssize_t read;
    long pos = ftell (file);

    while ((read = getline (&line, &len, file)) != -1)
        {
            ssize_t index = 0;

            while (index < read && (line[index] == ' ' || line[index] == '\t'))
                index++;

            if (index + 2 >= read)
                continue;

            if (line[index] == '#' && line[index + 1] == '!')
                ret = strndup (line + 2, strlen (line + 2) - 1);

            break;
        }

    fseek (file, pos, SEEK_SET);
    free (line);
    return ret;
}

static void
codebase_report_analyze_file (struct codebase_report *report, const char *path,
                              FILE *file)
{
    const char *extension = strrchr (path, '.');
    const char *filename = strrchr (path, '/');

    extension = extension == NULL ? NULL : extension + 1;
    filename = filename == NULL ? path : filename + 1;

    for (size_t i = 0; i < sizeof (codebase_file_handlers)
                               / sizeof (codebase_file_handlers[0]);
         i++)
        {
            struct codebase_scan_state state = {
                .filename = strdup (filename),
                .directory = strdup (report->directory),
                .extension = NULL,
                .shebang_prog = NULL,
                .report = report,
            };

            if (codebase_file_handlers[i].extensions != NULL)
                {
                    for (size_t j = 0;
                         codebase_file_handlers[i].extensions[j] != NULL; j++)
                        {
                            if (extension != NULL
                                && strcmp (
                                       extension,
                                       codebase_file_handlers[i].extensions[j])
                                       == 0)
                                {
                                    state.extension = strdup (extension);
                                    codebase_file_handlers[i].handler (&state,
                                                                       file);
                                    codebase_scan_state_free (&state);
                                    report->files++;
                                    return;
                                }
                        }
                }

            if (codebase_file_handlers[i].filenames != NULL)
                {
                    for (size_t j = 0;
                         codebase_file_handlers[i].filenames[j] != NULL; j++)
                        {
                            if (strcmp (filename,
                                        codebase_file_handlers[i].filenames[j])
                                == 0)
                                {
                                    codebase_file_handlers[i].handler (&state,
                                                                       file);
                                    codebase_scan_state_free (&state);
                                    report->files++;
                                    return;
                                }
                        }
                }

            char *shebang = get_file_shebang (file);

            if (shebang == NULL || codebase_file_handlers[i].shebangs == NULL)
                {
                    free (shebang);
                    free (state.filename);
                    free (state.directory);
                    continue;
                }

            for (size_t j = 0; codebase_file_handlers[i].shebangs[j] != NULL;
                 j++)
                {
                    const char *prog = codebase_file_handlers[i].shebangs[j];
                    const char *shebang_ptr = shebang;

                    if (strncmp (shebang_ptr, "/usr/bin/", 9) == 0)
                        shebang_ptr += 9;
                    else if (strncmp (shebang_ptr, "/bin/", 5) == 0)
                        shebang_ptr += 5;
                    else if (strncmp (shebang_ptr, "/usr/bin/env ", 14) == 0)
                        {
                            shebang_ptr += 14;

                            while (*shebang_ptr == ' ' || *shebang_ptr == '\t')
                                shebang_ptr++;
                        }

                    if (strcmp (prog, shebang_ptr) == 0)
                        {
                            state.extension = strdup (prog);
                            codebase_file_handlers[i].handler (&state, file);
                            codebase_scan_state_free (&state);
                            free (shebang);
                            report->files++;
                            return;
                        }
                }

            free (shebang);
            free (state.filename);
            free (state.directory);
        }

    report->ignored++;
}

static void
codebase_report_free (struct codebase_report *report)
{
    free ((void *) report->directory);
}

static bool
codebase_report_scan (struct codebase_report *report, const char *directory)
{
    DIR *dirstream = opendir (directory);
    struct dirent *entry;
    char **entries = NULL;
    size_t entries_size = 0;

    if (dirstream == NULL)
        {
            report_error ("failed to open directory `%s'", directory);
            return false;
        }

    report->directories++;

    while ((entry = readdir (dirstream)) != NULL)
        {
            if (strcmp (entry->d_name, ".") == 0
                || strcmp (entry->d_name, "..") == 0)
                continue;

            entries = xrealloc (entries, (++entries_size) * sizeof (char *));
            entries[entries_size - 1] = strdup (entry->d_name);
        }

    entries = xrealloc (entries, (++entries_size) * sizeof (char *));
    entries[entries_size - 1] = NULL;
    closedir (dirstream);

    for (size_t i = 0; i < entries_size - 1; i++)
        {
            size_t path_len = 0;
            char *path = path_join (directory, entries[i], &path_len);
            struct stat st;

            if (lstat (path, &st) == -1)
                {
                    report_error ("failed to stat `%s'", path);
                    continue;
                }

            if (S_ISDIR (st.st_mode))
                {
                    codebase_report_scan (report, path);
                }
            else if (S_ISREG (st.st_mode))
                {
                    FILE *file = fopen (path, "r");

                    if (file == NULL)
                        {
                            report_error ("failed to open file `%s'", path);
                            continue;
                        }

                    codebase_report_analyze_file (report, path, file);
                    fclose (file);
                }

            free (path);
        }

    for (size_t i = 0; i < entries_size - 1; i++)
        free (entries[i]);

    free (entries);
    return true;
}

static bool
codebase_report_scan_r (struct codebase_report *report, const char *directory)
{
    report->directory = strdup (directory);
    return codebase_report_scan (report, directory);
}

static void
codebase_report_print (const struct codebase_report *report)
{
    const int widths[] = { 13, 14, 11, 14, 12, 13, 14 };
    const unsigned long int values[]
        = { report->files,     report->ignored,     report->directories,
            report->lines,     report->blank_lines, report->comment_lines,
            report->code_lines };

    printf ("\033[2m** Report for `%s':\033[0m\n", report->directory);

    /* clang-format off */
    printf ("+---------------+----------------+-------------+----------------+--------------+---------------+----------------+\n");
    printf ("| \033[1mFiles\033[0m         | \033[1mIgnored Files\033[0m  | \033[1mDirectories\033[0m | \033[1mLines\033[0m          | \033[1mBlank Lines\033[0m  | \033[1mComment Lines\033[0m | \033[1mCode Lines\033[0m     |\n");
    printf ("+---------------+----------------+-------------+----------------+--------------+---------------+----------------+\n|");
    /* clang-format on */

    for (size_t i = 0; i < sizeof (values) / sizeof (values[0]); i++)
        {
            const int width = widths[i];
            const long unsigned int value = values[i];
            printf (" \033[1;%sm%-*lu\033[0m |",
                    i < 3    ? "1"
                    : i == 3 ? "34"
                    : i == 4 ? "2"
                    : i == 5 ? "2"
                    : i == 6 ? "32"
                             : "1",
                    width, value);
        }

    /* clang-format off */
    printf ("\n+---------------+----------------+-------------+----------------+--------------+---------------+----------------+\n");
    /* clang-format on */
}

[[noreturn]]
static void
usage (bool error)
{
    FILE *stream = error ? stderr : stdout;
    fprintf (stream, "Usage: %s [OPTION]... <DIRECTORY>...\n", prog_name);
    fputs ("Show statistics for the given codebase.\n", stream);
    fputc ('\n', stream);
    fputs ("  -h, --help      Display this help and exit\n", stream);
    fputs ("  -v, --version   Output version information and exit\n", stream);
    fputc ('\n', stream);
    fputs ("Bug reports and feedback should be sent to \n<" PACKAGE_BUGREPORT
           ">.\n",
           stream);
    exit (error ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void
show_version (void)
{
    printf (PROG_CANONICAL_NAME " (" PACKAGE_FULLNAME ") v" PACKAGE_VERSION
                                "\n");
    fputc ('\n', stdout);
    printf ("License GPLv3+: GNU GPL version 3 or later "
            "<http://gnu.org/licenses/gpl.html>.\n");
    printf (
        "This is free software: you are free to change and redistribute it.\n");
    printf ("There is NO WARRANTY, to the extent permitted by law.\n");
    fputc ('\n', stdout);
    printf ("Written by " PROG_AUTHORS ".\n");
}

[[noreturn]]
static void
invalid_usage (const char *msg)
{
    fprintf (stderr, "%s: %s\n", prog_name, msg);
    fprintf (stderr, "Try `%s --help' for more information.\n", prog_name);
    exit (EXIT_FAILURE);
}

int
main (int argc, char **argv)
{
    prog_name = argv[0];
    int opt;

    while ((opt = getopt_long (argc, argv, short_options, long_options, NULL))
           != -1)
        {
            switch (opt)
                {
                case 'h':
                    usage (false);
                case 'v':
                    show_version ();
                    exit (EXIT_SUCCESS);
                case '?':
                    fprintf (stderr, "Try `%s --help' for more information.\n",
                             prog_name);
                    exit (EXIT_FAILURE);
                default:
                    abort ();
                }
        }

    if (optind == argc)
        invalid_usage ("missing directory operand");

    bool success = false;

    for (int i = optind; i < argc; i++)
        {
            struct codebase_report report = { 0 };

            if (!codebase_report_scan_r (&report, argv[i]))
                {
                    continue;
                }

            codebase_report_print (&report);
            codebase_report_free (&report);
            success = true;
        }

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}