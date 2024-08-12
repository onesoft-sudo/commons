#ifndef FREEHTTPD_HTTP_ERROR_H
#define FREEHTTPD_HTTP_ERROR_H

#include "response.h"
#include <stddef.h>

#define FREEHTTPD_ERRDOC_INITIALIZER { 0, NULL, 0 }

typedef struct freehttpd_errdoc
{
    freehttpd_status_t status;
    const char *document;
    size_t document_length;
    bool auto_free;
} freehttpd_errdoc_t;

typedef struct freehttpd_errdoc_tbl
{
    freehttpd_errdoc_t *errdocs;
    size_t errdocs_count;
    size_t errdocs_cap;
} freehttpd_errdoc_tbl_t;

const freehttpd_errdoc_t *
freehttpd_error_document_get (freehttpd_errdoc_tbl_t *errdoc_tbl,
                              freehttpd_status_t status);
freehttpd_errdoc_t *
freehttpd_error_document_set (freehttpd_errdoc_tbl_t *errdoc_tbl,
                              freehttpd_status_t status, const char *document,
                              size_t document_length);
freehttpd_errdoc_tbl_t *freehttpd_error_document_tbl_init (void);
void freehttpd_error_document_tbl_free (freehttpd_errdoc_tbl_t *errdoc_tbl);
void
freehttpd_error_document_load_defaults (freehttpd_errdoc_tbl_t *errdoc_tbl);

#endif /* FREEHTTPD_HTTP_ERROR_H */