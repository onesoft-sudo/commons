#include "http_error.h"
#include "html/default_layout.h"
#include "log.h"
#include "response.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define SERVER_SIGNATURE                                                       \
    "FreeHTTPD/1.0.0 (Ubuntu 24.04 LTS) Server at localhost"

/* clang-format off */
static unsigned int valid_statuses[] = {
    200, 201, 202, 204, 206,
    300, 301, 302, 303, 304, 307, 308, 
    400, 401, 402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412, 413,
    414, 415, 416, 417, 418, 421, 422, 423, 424, 425, 426, 428, 429, 431, 451,
    500, 501, 502, 503, 504, 505, 506, 507, 508, 510, 511,
};
/* clang-format on */

static unsigned int valid_statuses_count
    = sizeof (valid_statuses) / sizeof (valid_statuses[0]);

freehttpd_errdoc_tbl_t *
freehttpd_error_document_tbl_init (void)
{
    freehttpd_errdoc_tbl_t *errdoc_tbl
        = calloc (1, sizeof (freehttpd_errdoc_tbl_t));

    if (errdoc_tbl == NULL)
        return NULL;

    errdoc_tbl->errdocs
        = calloc (valid_statuses_count, sizeof (freehttpd_errdoc_t));

    if (errdoc_tbl->errdocs == NULL)
        return NULL;

    errdoc_tbl->errdocs_cap = valid_statuses_count;
    return errdoc_tbl;
}

void
freehttpd_error_document_tbl_free (freehttpd_errdoc_tbl_t *errdoc_tbl)
{
    if (errdoc_tbl == NULL)
        return;

    if (errdoc_tbl->errdocs != NULL)
        {
            for (size_t i = 0; i < errdoc_tbl->errdocs_count; i++)
                {
                    freehttpd_errdoc_t *errdoc = &errdoc_tbl->errdocs[i];

                    if (errdoc->auto_free == true && errdoc->document != NULL)
                        free ((void *) errdoc->document);
                }

            free (errdoc_tbl->errdocs);
        }

    free (errdoc_tbl);
}

static unsigned int
hash_function (freehttpd_status_t status)
{
    return status % valid_statuses_count;
}

const freehttpd_errdoc_t *
freehttpd_error_document_get (freehttpd_errdoc_tbl_t *errdoc_tbl,
                              freehttpd_status_t status)
{
    unsigned int hash = hash_function (status);

    if (hash >= errdoc_tbl->errdocs_cap)
        {
            log_err (LOG_ERR "Invalid status code: %d: Hash overflow", status);
            return NULL;
        }

    freehttpd_errdoc_t *errdoc = &errdoc_tbl->errdocs[hash];
    bool second_round = false;
    unsigned int index = hash;

    while (errdoc_tbl->errdocs[index].status != status)
        {
            if (errdoc_tbl->errdocs[index].status == 0)
                return NULL;

            index++;

            if (index >= errdoc_tbl->errdocs_cap)
                {
                    second_round = true;
                    index = 0;
                }

            if (second_round && index >= hash)
                return NULL;
        }

    return errdoc;
}

freehttpd_errdoc_t *
freehttpd_error_document_set (freehttpd_errdoc_tbl_t *errdoc_tbl,
                              freehttpd_status_t status, const char *document,
                              size_t document_length)
{
    unsigned int hash = hash_function (status);
    freehttpd_errdoc_t *errdoc = NULL;

    if (hash >= errdoc_tbl->errdocs_cap)
        {
            log_err (LOG_ERR "Invalid status code: %d: Hash overflow: %u\n",
                     status, hash);
            return NULL;
        }

    if (errdoc_tbl->errdocs_count >= errdoc_tbl->errdocs_cap)
        {
            log_err (LOG_ERR "Error documents table is full\n");
            return NULL;
        }

    errdoc = &errdoc_tbl->errdocs[hash];

    if (errdoc->document != NULL && errdoc->auto_free == true)
        free ((void *) errdoc->document);

    errdoc->status = status;
    errdoc->document = document;
    errdoc->document_length = document_length;
    errdoc_tbl->errdocs_count++;
    errdoc->auto_free = true;

    return errdoc;
}

void
freehttpd_error_document_load_defaults (freehttpd_errdoc_tbl_t *errdoc_tbl)
{
    const char *placeholders[] = {
        "{FREEHTTPD_STATUS}",
        "{FREEHTTPD_STATUS_TEXT}",
        "{FREEHTTPD_SIGNATURE}",
        "{FREEHTTPD_STATUS_DESCRIPTION}",
    };

    for (unsigned int i = 0;
         i < sizeof (valid_statuses) / sizeof (valid_statuses[0]); i++)
        {
            freehttpd_errdoc_t *doc = freehttpd_error_document_set (
                errdoc_tbl, valid_statuses[i],
                (const char *) default_layout_html, default_layout_html_len);

            char status_code[4];
            const char *values[] = {
                status_code,
                freehttpd_response_status_text (valid_statuses[i]),
                SERVER_SIGNATURE,
                freehttpd_response_status_description (valid_statuses[i]),
            };

            snprintf (status_code, sizeof (status_code), "%d",
                      valid_statuses[i]);

            doc->auto_free = false;

            for (unsigned int j = 0;
                 j < sizeof (placeholders) / sizeof (placeholders[0]); j++)
                {
                    char *placeholder
                        = strstr ((char *) doc->document, placeholders[j]);

                    while (placeholder != NULL)
                        {
                            size_t placeholder_len = strlen (placeholders[j]);
                            size_t value_len = strlen (values[j]);
                            size_t new_len = doc->document_length + value_len
                                             - placeholder_len;
                            char buffer[new_len + 1];
                            bzero (buffer, sizeof (buffer));

                            memcpy (buffer, doc->document,
                                    placeholder - doc->document);
                            memcpy (buffer + (placeholder - doc->document),
                                    values[j], value_len);
                            memcpy (buffer + (placeholder - doc->document)
                                        + value_len,
                                    placeholder + placeholder_len,
                                    doc->document_length
                                        - (placeholder - doc->document)
                                        - placeholder_len);

                            buffer[new_len] = 0;

                            if (doc->auto_free == true)
                                free ((void *) doc->document);
                            else
                                doc->auto_free = true;

                            doc->document_length = new_len;
                            doc->document = strdup (buffer);
                            placeholder = strstr ((char *) doc->document,
                                                  placeholders[j]);
                        }
                }
        }
}