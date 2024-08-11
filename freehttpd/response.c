#include "response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

freehttpd_response_t *
freehttpd_response_init (const char *version, size_t version_len,
                         freehttpd_status_t status)
{
    freehttpd_response_t *response = calloc (1, sizeof (freehttpd_response_t));

    if (response == NULL)
        return NULL;

    response->version = version == NULL ? NULL : strdup (version);
    response->version_length = version == NULL    ? 0
                               : version_len == 0 ? strlen (version)
                                                  : version_len;
    response->status.code = status;
    response->status.text
        = status != 0 ? freehttpd_response_status_text (status) : NULL;
    response->status.status_text_length = 0;
    response->headers = NULL;
    response->headers_length = 0;
    response->body = NULL;
    response->body_length = 0;

    return response;
}

const char *
freehttpd_response_status_text (freehttpd_status_t status)
{
    switch (status)
        {
        case FREEHTTPD_STATUS_OK:
            return "OK";
        case FREEHTTPD_STATUS_BAD_REQUEST:
            return "Bad Request";
        case FREEHTTPD_STATUS_NOT_FOUND:
            return "Not Found";
        case FREEHTTPD_STATUS_METHOD_NOT_ALLOWED:
            return "Method Not Allowed";
        case FREEHTTPD_STATUS_INTERNAL_SERVER_ERROR:
            return "Internal Server Error";
        case FREEHTTPD_STATUS_NOT_IMPLEMENTED:
            return "Not Implemented";
        case FREEHTTPD_STATUS_FORBIDDEN:
            return "Forbidden";
        default:
            return "Unknown Status";
        }
}

void
freehttpd_response_set_status (freehttpd_response_t *response,
                               freehttpd_status_t status)
{
    response->status.code = status;
    response->status.text = freehttpd_response_status_text (status);
}

void
freehttpd_response_add_default_headers (freehttpd_response_t *response)
{
    response->headers
        = realloc (response->headers, sizeof (freehttpd_header_t *)
                                          * (response->headers_length + 3));
    response->headers[response->headers_length++]
        = freehttpd_header_init ("Server", "freehttpd", 6, 9);
    response->headers[response->headers_length++]
        = freehttpd_header_init ("Connection", "close", 10, 5);

    struct tm *time_info;
    time_t raw_time;
    time (&raw_time);
    time_info = gmtime (&raw_time);

    char time_str[64];
    size_t time_strlen = strftime (time_str, sizeof (time_str),
                                   "%a, %d %b %Y %H:%M:%S GMT", time_info);

    response->headers[response->headers_length++]
        = freehttpd_header_init ("Date", time_str, 4, time_strlen);
}

freehttpd_header_t *
freehttpd_response_add_header (freehttpd_response_t *response, const char *name,
                               const char *value, size_t name_len,
                               size_t value_len)
{
    freehttpd_header_t *header
        = freehttpd_header_init (name, value, name_len, value_len);

    if (header == NULL)
        return NULL;

    response->headers
        = realloc (response->headers, sizeof (freehttpd_header_t *)
                                          * (response->headers_length + 1));
    response->headers[response->headers_length++] = header;
    return header;
}

ecode_t
freehttpd_response_send (const freehttpd_response_t *response, FILE *stream)
{
    int ret = 0;

    ret = fprintf (stream, "HTTP/%s %d %s\r\n", response->version,
                   response->status.code, response->status.text);

    if (ret < 0)
        return E_SYSCALL_WRITE;

    for (size_t i = 0; i < response->headers_length; i++)
        {
            freehttpd_header_t *header = response->headers[i];
            ret = fprintf (stream, "%s: %s\r\n", header->name, header->value);

            if (ret < 0)
                return E_SYSCALL_WRITE;
        }

    if (response->body != NULL)
        {
            ret = fprintf (stream, "\r\n");

            if (ret < 0)
                return E_SYSCALL_WRITE;
            if (fwrite (response->body, 1, response->body_length, stream)
                != response->body_length)
                return E_SYSCALL_WRITE;
        }

    fflush (stream);
    return E_OK;
}

void
freehttpd_response_free (freehttpd_response_t *response)
{
    if (response == NULL)
        return;

    if (response->version != NULL)
        free (response->version);

    if (response->headers != NULL)
        {
            for (size_t i = 0; i < response->headers_length; i++)
                freehttpd_header_free (response->headers[i]);

            free (response->headers);
        }

    if (response->body != NULL)
        free (response->body);

    free (response);
}