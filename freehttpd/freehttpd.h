#ifndef FREEHTTPD_H
#define FREEHTTPD_H

#include <stddef.h>

typedef struct freehttpd_config
{
    unsigned int port;
    char *addr;
    unsigned int max_listen_queue;
    size_t max_method_len;
    size_t max_uri_len;
    size_t max_version_len;
} freehttpd_config_t;

typedef enum freehttpd_error_code
{
    E_OK,
    E_UNKNOWN,
    E_UNKNOWN_OPT,
    E_MALFORMED_REQUEST,
    E_SYSCALL_SOCKET,
    E_SYSCALL_BIND,
    E_SYSCALL_LISTEN,
    E_SYSCALL_ACCEPT,
    E_SYSCALL_RECV,
    E_SYSCALL_SETSOCKOPT,
    E_LIBC_FDOPEN,
    E_LIBC_MALLOC
} ecode_t;

typedef enum freehttpd_config_option
{
    FREEHTTPD_CONFIG_PORT,
    FREEHTTPD_CONFIG_ADDR,
    FREEHTTPD_CONFIG_MAX_LISTEN_QUEUE,
    FREEHTTPD_CONFIG_MAX_METHOD_LEN,
    FREEHTTPD_CONFIG_MAX_URI_LEN,
    FREEHTTPD_CONFIG_MAX_VERSION_LEN,
    CONFIG_OPTION_COUNT
} freehttpd_opt_t;

typedef struct freehttpd freehttpd_t;

freehttpd_t *freehttpd_init ();
void freehttpd_free (freehttpd_t *freehttpd);
ecode_t freehttpd_start (freehttpd_t *restrict freehttpd);
ecode_t freehttpd_setopt (freehttpd_t *freehttpd, freehttpd_opt_t opt,
                          void *value);

const freehttpd_config_t *freehttpd_get_config (freehttpd_t *freehttpd);

#endif