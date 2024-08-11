#ifndef FREEHTTPD_RESPONSE_H
#define FREEHTTPD_RESPONSE_H

#include <stdio.h>

#include "request.h"

typedef enum freehttpd_status
{
    FREEHTTPD_STATUS_OK = 200,
    FREEHTTPD_STATUS_BAD_REQUEST = 400,
    FREEHTTPD_STATUS_FORBIDDEN = 403,
    FREEHTTPD_STATUS_NOT_FOUND = 404,
    FREEHTTPD_STATUS_METHOD_NOT_ALLOWED = 405,
    FREEHTTPD_STATUS_INTERNAL_SERVER_ERROR = 500,
    FREEHTTPD_STATUS_NOT_IMPLEMENTED = 501,
} freehttpd_status_t;

typedef struct freehttpd_status_info
{
    freehttpd_status_t code;
    const char *text;
    size_t status_text_length;
} freehttpd_status_info_t;

typedef struct freehttpd_response
{
    char *version;
    char *body;
    freehttpd_status_info_t status;
    freehttpd_header_t **headers;
    size_t version_length;
    size_t headers_length;
    size_t body_length;
} freehttpd_response_t;

freehttpd_response_t *freehttpd_response_init (const char *version,
                                               size_t version_len,
                                               freehttpd_status_t status);
const char *freehttpd_response_status_text (freehttpd_status_t status);
void freehttpd_response_set_status (freehttpd_response_t *response,
                                    freehttpd_status_t status);
ecode_t freehttpd_response_send (const freehttpd_response_t *response,
                                 FILE *stream);
freehttpd_header_t *
freehttpd_response_add_header (freehttpd_response_t *response, const char *name,
                               const char *value, size_t name_len,
                               size_t value_len);
void freehttpd_response_free (freehttpd_response_t *response);
void freehttpd_response_add_default_headers (freehttpd_response_t *response);

#endif /* FREEHTTPD_RESPONSE_H */