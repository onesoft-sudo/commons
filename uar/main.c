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
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
    { "create",    no_argument,       NULL, 'c' },
    { "extract",   no_argument,       NULL, 'x' },
    { "list",      no_argument,       NULL, 't' },
    { "verbose",   no_argument,       NULL, 'v' },
    { "file",      required_argument, NULL, 'f' },
    { "directory", required_argument, NULL, 'C' },
    { "help",      no_argument,       NULL, 'h' },
    { "version",   no_argument,       NULL, 'V' },
    { NULL,        0,                 NULL, 0   },
};

static char const short_options[] = "cxtvf:C:hV";

/* Program name. */
static char *progname = NULL;

/* Flags for the command line options. */
enum uar_mode
{
    MODE_NONE,
    MODE_CREATE,
    MODE_EXTRACT,
    MODE_LIST
};

struct uar_params
{
    enum uar_mode mode;
    bool verbose;
    char *file;
    char *cwd;
    union
    {
        struct
        {
            char **targets;
            size_t ntargets;
        } create;
    } params;
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
    if (params.params.create.targets != NULL)
        free (params.params.create.targets);

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

    progname = strrchr (argv0, '/');

    if (progname != NULL)
        progname++;
    else
        progname = argv0;
}

/* Create an archive. */
static void
create_archive (void)
{
    assert (params.mode == MODE_CREATE);
    assert (params.params.create.ntargets > 0);
    assert (params.params.create.targets != NULL);

    pinfo ("creating archive: %s\n", params.file);

    struct uar_archive *uar = uar_create ();

    if (uar == NULL || uar_has_error (uar))
        {
            pinfo ("failed to create archive: %s\n", strerror (errno));
            return;
        }

    for (size_t i = 0; i < params.params.create.ntargets; i++)
        {
            struct stat stinfo = { 0 };

            if (stat (params.params.create.targets[i], &stinfo) != 0)
                {
                    perr ("cannot stat '%s': %s\n",
                          params.params.create.targets[i], strerror (errno));
                    uar_close (uar);
                    return;
                }

            struct uar_file *file = NULL;

            if (S_ISREG (stinfo.st_mode))
                {
                    pinfo ("adding file: %s\n",
                           params.params.create.targets[i]);
                    file = uar_add_file (
                        uar, basename (params.params.create.targets[i]),
                        params.params.create.targets[i]);

                    if (file == NULL)
                        {
                            perr ("failed to add file: %s\n", strerror (errno));
                            uar_close (uar);
                            return;
                        }
                }
            else if (S_ISDIR (stinfo.st_mode))
                {
                    pinfo ("adding directory: %s\n",
                           params.params.create.targets[i]);
                    file = uar_add_dir (
                        uar, basename (params.params.create.targets[i]),
                        params.params.create.targets[i]);

                    if (file == NULL)
                        {
                            perr ("failed to add directory: %s\n",
                                  strerror (errno));
                            uar_close (uar);
                            return;
                        }
                }
            else if (S_ISLNK (stinfo.st_mode))
                {
                    assert (false && "Not implemented");
                }
            else
                {
                    perr ("failed to add file: %s: file type not supported\n",
                          params.params.create.targets[i]);
                    uar_close (uar);
                    return;
                }

            assert (file != NULL);
            uar_file_set_mode (file, stinfo.st_mode & 07777);
        }

    pinfo ("writing archive: %s\n", params.file);

    if (!uar_write (uar, params.file))
        {
            perr ("failed to write archive: %s\n", strerror (errno));
            uar_close (uar);
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
    pinfo ("extracting: %s\n", uar_get_file_name (file));
    return true;
}

/* Extract an archive. */
static void
extract_archive ()
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
                    usage ();
                    exit (1);

                default:
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
                            perr ("failed to read '%s': %s", file,
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
            char *dir = params.cwd;
            params.cwd = realpath (dir, NULL);

            if (params.cwd == NULL)
                {
                    perr ("failed to change working directory to '%s': %s", dir,
                          strerror (errno));
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
            for (int i = optind; i < argc; i++)
                {
                    params.params.create.targets = xrealloc (
                        params.params.create.targets,
                        (params.params.create.ntargets + 1) * sizeof (char *));
                    params.params.create.targets[params.params.create.ntargets]
                        = argv[i];
                    params.params.create.ntargets++;
                }

            create_archive ();
            break;

        case MODE_EXTRACT:
            extract_archive ();
            break;

        case MODE_LIST:
            assert (false && "Not implemented yet");
            break;

        default:
            usage ();
            exit (1);
        }

    return 0;
}