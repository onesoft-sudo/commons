#include "freehttpd.h"
#include <errno.h>
#include <getopt.h>
#include <magic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct option const long_options[] = {
    { "help",    no_argument,       NULL, 'h' },
    { "version", no_argument,       NULL, 'v' },
    { "config",  required_argument, NULL, 'c' },
    { NULL,      0,                 NULL, 0   }
};

static const char *const short_options = "hvc:";

struct config
{
    const char *progname;
    const char *config_file;
};

static struct config config = { 0 };

static void
usage (void)
{
    printf ("Usage: %s [OPTIONS]\n", config.progname);
    printf ("Options:\n");
    printf ("  -h, --help     Print this help message\n");
    printf ("  -v, --version  Print version information\n");
    printf ("  -c, --config   Specify configuration file\n");
}

static void
version (void)
{
    printf ("freehttpd 0.0.0\n");
}

static bool
validate_settings (void)
{
    return true;
}

static void
initialize (const char *progname)
{
    config.progname = progname;
    config.config_file = "freehttpd.conf";
}

int
main (int argc, char **argv)
{
    initialize (argv[0]);

    while (true)
        {
            int option_index = 0;
            int c = getopt_long (argc, argv, short_options, long_options,
                                 &option_index);

            if (c == -1)
                break;

            switch (c)
                {
                case 'h':
                    usage ();
                    exit (EXIT_SUCCESS);

                case 'v':
                    version ();
                    exit (EXIT_SUCCESS);

                case 'c':
                    config.config_file = optarg;
                    break;

                case '?':
                default:
                    exit (EXIT_FAILURE);
                }
        }

    if (!validate_settings ())
        exit (EXIT_FAILURE);

    magic_t magic = magic_open (MAGIC_MIME);
    unsigned int port = 8080;
    size_t uri_len = 1024;
    ecode_t code = E_OK;

    if (magic == NULL)
        {
            fprintf (stderr, "%s: failed to initialize magic\n",
                     config.progname);
            exit (EXIT_FAILURE);
        }

    if (magic_load (magic, NULL) != 0)
        {
            fprintf (stderr, "%s: failed to load magic database\n",
                     config.progname);
            magic_close (magic);
            exit (EXIT_FAILURE);
        }

    freehttpd_t *freehttpd = freehttpd_init (magic);

    code = freehttpd_setopt (freehttpd, FREEHTTPD_CONFIG_PORT, &port);

    if (code != E_OK)
        goto error;

    code = freehttpd_setopt (freehttpd, FREEHTTPD_CONFIG_ADDR, NULL);

    if (code != E_OK)
        goto error;

    code = freehttpd_setopt (freehttpd, FREEHTTPD_CONFIG_MAX_URI_LEN, &uri_len);

    if (code != E_OK)
        goto error;

    char *docroot = getenv ("FREEHTTPD_DOCROOT");
    code = freehttpd_setopt (freehttpd, FREEHTTPD_CONFIG_DOCROOT,
                             docroot ? docroot : "/var/www");

    if (code != E_OK)
        goto error;

    code = freehttpd_start (freehttpd);

    if (code != E_OK)
        {
        error:
            fprintf (stderr, "%s: failed to start: %d: %s\n", config.progname,
                     code, strerror (errno));
            freehttpd_free (freehttpd);
            exit (EXIT_FAILURE);
        }

    freehttpd_free (freehttpd);
    magic_close (magic);
    return 0;
}