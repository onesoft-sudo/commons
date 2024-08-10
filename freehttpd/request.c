#include "request.h"
#include "freehttpd.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

freehttpd_request_t *
freehttpd_request_init (const char *method, const char *uri,
                        const char *version)
{
    freehttpd_request_t *request = calloc (1, sizeof (freehttpd_request_t));

    if (request == NULL)
        return NULL;

    if (method != NULL)
        {
            request->method = strdup (method);
            request->method_length = strlen (method);
        }

    if (uri != NULL)
        {
            request->uri = strdup (uri);
            request->uri_length = strlen (uri);
        }

    if (version != NULL)
        {

            request->version = strdup (version);
            request->version_length = strlen (version);
        }

    return request;
}

freehttpd_header_t *
freehttpd_header_init (const char *name, const char *value)
{
    freehttpd_header_t *header = calloc (1, sizeof (freehttpd_header_t));

    if (header == NULL)
        return NULL;

    if (name != NULL)
        header->name = strdup (name);

    if (value != NULL)
        header->value = strdup (value);

    return header;
}

void
freehttpd_header_free (freehttpd_header_t *header)
{
    if (header == NULL)
        return;

    if (header->name != NULL)
        free (header->name);

    if (header->value != NULL)
        free (header->value);

    free (header);
}

void
freehttpd_request_free (freehttpd_request_t *request)
{
    if (request == NULL)
        return;

    if (request->method != NULL)
        free (request->method);

    if (request->uri != NULL)
        free (request->uri);

    if (request->version != NULL)
        free (request->version);

    if (request->headers != NULL)
        {
            for (size_t i = 0; i < request->headers_count; i++)
                freehttpd_header_free (request->headers[i]);

            free (request->headers);
        }

    if (request->body != NULL)
        free (request->body);

    free (request);
}

const char *const SUPPORTED_METHODS[]
    = { "GET",     "POST",  "PUT",     "DELETE", "HEAD",
        "OPTIONS", "TRACE", "CONNECT", NULL };

freehttpd_request_t *
freehttpd_request_parse (freehttpd_t *freehttpd, int sockfd, ecode_t *error)
{
    freehttpd_request_t *request = freehttpd_request_init (NULL, NULL, NULL);

    if (request == NULL)
        {
            *error = E_LIBC_MALLOC;
            return NULL;
        }

    const freehttpd_config_t *config = freehttpd_get_config (freehttpd);
    char *buf[3] = { 0 };
    char **buf_ptrs[] = { &request->method, &request->uri, &request->version };
    size_t max_lengths[] = { config->max_method_len, config->max_uri_len,
                             config->max_version_len };
    size_t *length_ptrs[] = { &request->method_length, &request->uri_length,
                              &request->version_length };

    for (int i = 0; i < 3; i++)
        {
            char c;
            size_t j = 0;

            while (read (sockfd, &c, 1) == 1)
                {
                    if (isspace (c))
                        break;

                    if (j >= max_lengths[i])
                        {
                            freehttpd_request_free (request);
                            *error = E_MALFORMED_REQUEST;
                            return NULL;
                        }

                    buf[i] = realloc (buf[i], j + 1);
                    buf[i][j++] = c;
                }

            if (j == 0)
                {
                    freehttpd_request_free (request);
                    *error = E_MALFORMED_REQUEST;
                    return NULL;
                }

            buf[i] = realloc (buf[i], j + 1);
            buf[i][j] = 0;
            *length_ptrs[i] = j;
            *buf_ptrs[i] = buf[i];
        }

    for (size_t i = 0; SUPPORTED_METHODS[i] != NULL; i++)
        {
            if (strcmp (buf[0], SUPPORTED_METHODS[i]) == 0)
                {
                    request->method = buf[0];
                    break;
                }
        }

    if (request->method == NULL || request->uri == NULL
        || request->version == NULL)
        {
            freehttpd_request_free (request);
            *error = E_MALFORMED_REQUEST;
            return NULL;
        }

    *error = E_OK;
    return request;
}