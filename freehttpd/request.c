#include "request.h"
#include "freehttpd.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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
freehttpd_header_init (const char *name, const char *value, size_t name_length,
                       size_t value_length)
{
    freehttpd_header_t *header = calloc (1, sizeof (freehttpd_header_t));

    if (header == NULL)
        return NULL;

    freehttpd_header_t on_stack
        = freehttpd_header_init_stack (name, value, name_length, value_length);
    memcpy (header, &on_stack, sizeof (freehttpd_header_t));

    return header;
}

freehttpd_header_t
freehttpd_header_init_stack (const char *name, const char *value,
                             size_t name_length, size_t value_length)
{
    freehttpd_header_t header = { 0 };

    if (name != NULL)
        {
            header.name = strdup (name);
            header.name_length = name_length == 0 ? strlen (name) : name_length;
        }

    if (value != NULL)
        {
            header.value = strdup (value);
            header.value_length
                = value_length == 0 ? strlen (value) : value_length;
        }

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
        free (request->version - 5);

    if (request->path != NULL)
        free (request->path);

    if (request->query != NULL)
        free (request->query);

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

static char *
urldecode (const char *input, size_t *len)
{
    char *output = calloc (strlen (input) + 1, 1);

    if (output == NULL)
        return NULL;

    size_t i = 0, j = 0;

    for (; input[i] != 0; i++, j++)
        {
            if (input[i] == '%')
                {
                    if (input[i + 1] == 0 || input[i + 2] == 0)
                        {
                            free (output);
                            return NULL;
                        }

                    char c1 = tolower (input[i + 1]);
                    char c2 = tolower (input[i + 2]);
                    int c1n = isdigit (c1) ? c1 - '0' : c1 - 'a' + 10;
                    int c2n = isdigit (c2) ? c2 - '0' : c2 - 'a' + 10;
                    int code = c1n * 16 + c2n;
                    output[j] = (char) code;
                    i += 2;
                }
            else
                output[j] = input[i];
        }

    output[j] = 0;

    printf ("output: %s\n", output);

    if (len != NULL)
        *len = j;

    return output;
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

            /* TODO: instead of always calling read with size 1, we could
               optimize this by reading more bytes at once and then parsing
               the buffer. */
            while (recv (sockfd, &c, 1, 0) == 1)
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

    if (memcmp (request->version, "HTTP/", 5) != 0)
        {
            freehttpd_request_free (request);
            *error = E_MALFORMED_REQUEST;
            return NULL;
        }

    request->version += 5;
    request->version_length -= 5;

    char *path = strchr (request->uri, '?');

    if (path != NULL)
        {
            path = strndup (request->uri, path - request->uri);
            request->query = strdup (path + 1);
            request->query_length
                = request->uri_length - request->path_length - 1;
        }
    else
        path = strdup (request->uri);

    request->path = urldecode (path, &request->path_length);
    free (path);
    *error = E_OK;
    return request;
}