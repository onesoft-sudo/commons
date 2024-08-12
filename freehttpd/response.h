#ifndef FREEHTTPD_RESPONSE_H
#define FREEHTTPD_RESPONSE_H

#include <stdint.h>
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
    size_t version_length;
    freehttpd_status_info_t status;
    freehttpd_header_t *headers;
    size_t headers_length;
    FILE *stream;
    uint8_t *buffer;
    size_t buffer_length;
} freehttpd_response_t;

freehttpd_response_t *freehttpd_response_init (FILE *stream,
                                               const char *version,
                                               size_t version_len,
                                               freehttpd_status_t status);
const char *freehttpd_response_status_text (freehttpd_status_t status);
void freehttpd_response_set_status (freehttpd_response_t *response,
                                    freehttpd_status_t status);
ecode_t freehttpd_response_head_send (const freehttpd_response_t *response);
freehttpd_header_t *
freehttpd_response_add_header (freehttpd_response_t *response, const char *name,
                               size_t name_len, const char *value_fmt, ...);
void freehttpd_response_free (freehttpd_response_t *response);
void freehttpd_response_add_default_headers (freehttpd_response_t *response);
ecode_t freehttpd_response_printf (freehttpd_response_t *response,
                                   const char *format, ...);
ecode_t freehttpd_response_bprintf (freehttpd_response_t *response,
                                    const char *format, ...);
ecode_t freehttpd_response_flush (freehttpd_response_t *response);
ecode_t freehttpd_response_begin_body (freehttpd_response_t *response);
ecode_t freehttpd_response_begin_end (freehttpd_response_t *response);
size_t freehttpd_response_write (freehttpd_response_t *response,
                                 const void *data, size_t size, size_t n);
const char *freehttpd_response_status_description (freehttpd_status_t status);

#endif /* FREEHTTPD_RESPONSE_H */