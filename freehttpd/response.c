#include "response.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

freehttpd_response_t *
freehttpd_response_init (FILE *stream, const char *version, size_t version_len,
                         freehttpd_status_t status)
{
    freehttpd_response_t *response = calloc (1, sizeof (freehttpd_response_t));

    if (response == NULL)
        return NULL;

    response->stream = stream;
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
    response->buffer = NULL;
    response->buffer_length = 0;

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

const char *
freehttpd_response_status_description (freehttpd_status_t status)
{
    switch (status)
        {
        case FREEHTTPD_STATUS_BAD_REQUEST:
            return "Your browser sent a request that this server could not "
                   "understand.";
        case FREEHTTPD_STATUS_NOT_FOUND:
            return "The requested URL was not found on this server.";
        case FREEHTTPD_STATUS_METHOD_NOT_ALLOWED:
            return "The requested method is not allowed for the URL.";
        case FREEHTTPD_STATUS_NOT_IMPLEMENTED:
            return "The server does not support the action requested by the "
                   "browser.";
        case FREEHTTPD_STATUS_FORBIDDEN:
            return "You don't have permission to access the requested URL on "
                   "this server.";
        default:
            return "The server encountered an internal error or "
                   "misconfiguration and was unable to complete your request.";
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
        = realloc (response->headers, sizeof (freehttpd_header_t)
                                          * (response->headers_length + 3));
    response->headers[response->headers_length++]
        = freehttpd_header_init_stack ("Server", "freehttpd", 6, 9);
    response->headers[response->headers_length++]
        = freehttpd_header_init_stack ("Connection", "close", 10, 5);

    struct tm *time_info;
    time_t raw_time;
    time (&raw_time);
    time_info = gmtime (&raw_time);

    char time_str[64];
    size_t time_strlen = strftime (time_str, sizeof (time_str),
                                   "%a, %d %b %Y %H:%M:%S GMT", time_info);

    response->headers[response->headers_length++]
        = freehttpd_header_init_stack ("Date", time_str, 4, time_strlen);
}

freehttpd_header_t *
freehttpd_response_add_header (freehttpd_response_t *response, const char *name,
                               size_t name_len, const char *value_fmt, ...)
{
    va_list args;
    char *final_value = NULL;
    freehttpd_header_t *header;

    va_start (args, value_fmt);
    (void) (vasprintf (&final_value, value_fmt, args) + 1);
    va_end (args);

    response->headers
        = realloc (response->headers, sizeof (freehttpd_header_t)
                                          * (response->headers_length + 1));
    response->headers[response->headers_length++]
        = freehttpd_header_init_stack (name, NULL, name_len, 0);

    header = &response->headers[response->headers_length - 1];
    header->value = final_value;
    header->value_length = strlen (final_value);

    return response->headers + (response->headers_length - 1);
}

ecode_t
freehttpd_response_head_send (const freehttpd_response_t *response)
{
    int ret = 0;

    ret = fprintf (response->stream, "HTTP/%s %d %s\r\n", response->version,
                   response->status.code, response->status.text);

    if (ret < 0)
        return E_SYSCALL_WRITE;

    for (size_t i = 0; i < response->headers_length; i++)
        {
            ret = fprintf (response->stream, "%s: %s\r\n",
                           response->headers[i].name,
                           response->headers[i].value);

            if (ret < 0)
                return E_SYSCALL_WRITE;
        }

    fflush (response->stream);
    return E_OK;
}

ecode_t
freehttpd_response_begin_body (freehttpd_response_t *response)
{
    if (fwrite ("\r\n", 1, 2, response->stream) != 2)
        return E_SYSCALL_WRITE;

    fflush (response->stream);
    return E_OK;
}

ecode_t
freehttpd_response_begin_end (freehttpd_response_t *response)
{
    return freehttpd_response_begin_body (response);
}

size_t
freehttpd_response_write (freehttpd_response_t *response, const void *data,
                          size_t size, size_t n)
{
    size_t wsize = fwrite (data, size, n, response->stream);

    if (wsize > 0)
        {
            int old_errno = errno;
            fflush (response->stream);
            errno = old_errno;
        }

    return wsize;
}

ecode_t
freehttpd_response_printf (freehttpd_response_t *response, const char *format,
                           ...)
{
    va_list args;
    va_start (args, format);
    int ret = vfprintf (response->stream, format, args);
    va_end (args);

    if (ret < 0)
        return E_SYSCALL_WRITE;

    return E_OK;
}

ecode_t
freehttpd_response_bprintf (freehttpd_response_t *response, const char *format,
                            ...)
{
    va_list args;
    va_start (args, format);
    char *str = NULL;
    int ret = vasprintf (&str, format, args);
    va_end (args);

    if (ret < 0)
        return E_SYSCALL_WRITE;

    size_t new_len = strlen (str) + 1;
    response->buffer_length += new_len;
    response->buffer = realloc (response->buffer, response->buffer_length);
    memcpy (response->buffer + response->buffer_length - new_len, str, new_len);
    free (str);

    return E_OK;
}

ecode_t
freehttpd_response_flush (freehttpd_response_t *response)
{
    if (response->buffer == NULL)
        return E_OK;

    size_t written = 0;

    while (written < response->buffer_length)
        {
            ssize_t ret
                = fwrite (response->buffer + written, 1,
                          response->buffer_length - written, response->stream);

            if (ret < 0)
                return E_SYSCALL_WRITE;

            written += ret;
        }

    fflush (response->stream);
    free (response->buffer);
    response->buffer = NULL;
    response->buffer_length = 0;

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
                {
                    free (response->headers[i].name);
                    free (response->headers[i].value);
                }
        }

    free (response->buffer);
    free (response);
}