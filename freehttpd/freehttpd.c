#include "freehttpd.h"
#include "request.h"
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

struct freehttpd
{
    int sockfd;
    freehttpd_config_t *config;
};

static freehttpd_config_t *
freehttpd_config_init ()
{
    struct freehttpd_config *config = calloc (1, sizeof (freehttpd_config_t));

    if (config == NULL)
        return NULL;

    config->port = 80;
    config->addr = NULL;
    config->max_listen_queue = 5;
    config->max_method_len = 16;
    config->max_uri_len = 8192;
    config->max_version_len = 16;

    return config;
}

static void
freehttpd_config_free (freehttpd_config_t *config)
{
    if (config == NULL)
        return;

    free (config->addr);
    free (config);
}

freehttpd_t *
freehttpd_init ()
{
    freehttpd_t *freehttpd = calloc (1, sizeof (freehttpd_t));

    if (freehttpd == NULL)
        return NULL;

    freehttpd->sockfd = -1;
    freehttpd->config = freehttpd_config_init ();
    return freehttpd;
}

ecode_t
freehttpd_setopt (freehttpd_t *freehttpd, freehttpd_opt_t opt, void *value)
{
    if (opt >= CONFIG_OPTION_COUNT)
        return E_UNKNOWN_OPT;

    switch (opt)
        {
        case FREEHTTPD_CONFIG_PORT:
            freehttpd->config->port = *(unsigned int *) value;
            break;

        case FREEHTTPD_CONFIG_ADDR:
            freehttpd->config->addr
                = value == NULL ? NULL : strdup ((const char *) value);
            break;

        case FREEHTTPD_CONFIG_MAX_LISTEN_QUEUE:
            freehttpd->config->max_listen_queue = *(unsigned int *) value;
            break;

        case FREEHTTPD_CONFIG_MAX_METHOD_LEN:
            freehttpd->config->max_method_len = *(size_t *) value;
            break;

        case FREEHTTPD_CONFIG_MAX_URI_LEN:
            freehttpd->config->max_uri_len = *(size_t *) value;
            break;

        case FREEHTTPD_CONFIG_MAX_VERSION_LEN:
            freehttpd->config->max_version_len = *(size_t *) value;
            break;

        default:
            return E_UNKNOWN_OPT;
        }

    return E_OK;
}

void
freehttpd_free (freehttpd_t *freehttpd)
{
    if (freehttpd == NULL)
        return;

    freehttpd_config_free (freehttpd->config);
    free (freehttpd);
}

static ecode_t
freehttpd_create_socket (freehttpd_t *freehttpd)
{
    int sockfd = socket (AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
        return E_SYSCALL_SOCKET;

    int opt = 1;

    if (setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt)) < 0)
        return E_SYSCALL_SETSOCKOPT;

    freehttpd->sockfd = sockfd;
    return E_OK;
}

static struct sockaddr_in
freehttpd_setup_addrinfo (freehttpd_t *freehttpd)
{
    struct sockaddr_in addr = { 0 };
    const char *addr_host = freehttpd->config->addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons (freehttpd->config->port);
    addr.sin_addr.s_addr
        = addr_host == NULL ? INADDR_ANY : inet_addr (addr_host);

    return addr;
}

static ecode_t
freehttpd_bind (freehttpd_t *freehttpd, struct sockaddr_in *addr_in)
{
    if (bind (freehttpd->sockfd, (struct sockaddr *) addr_in, sizeof (*addr_in))
        < 0)
        return E_SYSCALL_BIND;

    return E_OK;
}

static ecode_t
freehttpd_listen (freehttpd_t *freehttpd)
{
    unsigned int max_listen_queue = freehttpd->config->max_listen_queue;

    if (listen (freehttpd->sockfd, (int) max_listen_queue) < 0)
        return E_SYSCALL_LISTEN;

    return E_OK;
}

static ecode_t
freehttpd_loop (freehttpd_t *freehttpd)
{
    while (true)
        {
            struct sockaddr_in client_addr = { 0 };
            socklen_t client_addr_len = sizeof (client_addr);
            int client_sockfd
                = accept (freehttpd->sockfd, (struct sockaddr *) &client_addr,
                          &client_addr_len);

            if (client_sockfd < 0)
                return E_SYSCALL_ACCEPT;

            ecode_t code = E_OK;
            freehttpd_request_t *request
                = freehttpd_request_parse (freehttpd, client_sockfd, &code);

            if (code != E_OK)
                {
                    close (client_sockfd);
                    continue;
                }

            fprintf (stdout, "Incoming: %s %s %s\n", request->method,
                     request->uri, request->version);

            FILE *client = fdopen (client_sockfd, "w");

            if (client == NULL)
                {
                    freehttpd_request_free (request);
                    continue;
                }

            fprintf (client, "%s 200 OK\r\n", request->version);
            fprintf (client, "Server: FreeHTTPD\r\n");
            fprintf (client, "Content-Type: text/html; charset=\"utf-8\"\r\n");
            fprintf (client, "Content-Length: 24\r\n");
            fprintf (client, "Connection: close\r\n");
            fprintf (client, "\r\n");
            fprintf (client, "<h1>Hello, World!</h1>\r\n");
            fflush (client);
            freehttpd_request_free (request);
            fclose (client);
        }

    return E_OK;
}

ecode_t
freehttpd_start (freehttpd_t *restrict freehttpd)
{
    ecode_t code = E_OK;
    struct sockaddr_in addr_in;

    if ((code = freehttpd_create_socket (freehttpd)) != E_OK)
        return code;

    addr_in = freehttpd_setup_addrinfo (freehttpd);

    if ((code = freehttpd_bind (freehttpd, &addr_in)) != E_OK)
        return code;

    if ((code = freehttpd_listen (freehttpd)) != E_OK)
        return code;

    return freehttpd_loop (freehttpd);
}

const freehttpd_config_t *
freehttpd_get_config (freehttpd_t *freehttpd)
{
    return freehttpd->config;
}