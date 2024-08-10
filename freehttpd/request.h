#ifndef FREEHTTPD_REQUEST_H
#define FREEHTTPD_REQUEST_H

#include "freehttpd.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct freehttpd_header
{
    char *name;
    char *value;
} freehttpd_header_t;

typedef struct freehttpd_request
{
    char *method;
    size_t method_length;
    char *uri;
    size_t uri_length;
    char *version;
    size_t version_length;
    freehttpd_header_t **headers;
    size_t headers_count;
    char *body;
    size_t body_length;
} freehttpd_request_t;

freehttpd_request_t *freehttpd_request_init (const char *method,
                                             const char *uri,
                                             const char *version);
void freehttpd_request_free (freehttpd_request_t *request);
freehttpd_request_t *freehttpd_request_parse (freehttpd_t *freehttpd,
                                              int sockfd, ecode_t *error);

#endif