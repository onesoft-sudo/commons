#include "freehttpd.h"
#include "log.h"
#include "request.h"
#include "response.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <magic.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

struct freehttpd
{
    int sockfd;
    magic_t magic;
    freehttpd_config_t *config;
};

static freehttpd_config_t *
freehttpd_config_init ()
{
    struct freehttpd_config *config = calloc (1, sizeof (freehttpd_config_t));

    if (config == NULL)
        return NULL;

    config->port = 80;
    config->addr = NULL;
    config->max_listen_queue = 5;
    config->max_method_len = 16;
    config->max_uri_len = 8192;
    config->max_version_len = 16;
    config->docroot = NULL;

    return config;
}

static void
freehttpd_config_free (freehttpd_config_t *config)
{
    if (config == NULL)
        return;

    free (config->docroot);
    free (config->addr);
    free (config);
}

freehttpd_t *
freehttpd_init (magic_t magic)
{
    freehttpd_t *freehttpd = calloc (1, sizeof (freehttpd_t));

    if (freehttpd == NULL)
        return NULL;

    freehttpd->sockfd = -1;
    freehttpd->config = freehttpd_config_init ();
    freehttpd->magic = magic;
    return freehttpd;
}

ecode_t
freehttpd_setopt (freehttpd_t *freehttpd, freehttpd_opt_t opt, void *value)
{
    switch (opt)
        {
        case FREEHTTPD_CONFIG_PORT:
            freehttpd->config->port = *(unsigned int *) value;
            break;

        case FREEHTTPD_CONFIG_ADDR:
            freehttpd->config->addr
                = value == NULL ? NULL : strdup ((const char *) value);
            break;

        case FREEHTTPD_CONFIG_MAX_LISTEN_QUEUE:
            freehttpd->config->max_listen_queue = *(unsigned int *) value;
            break;

        case FREEHTTPD_CONFIG_MAX_METHOD_LEN:
            freehttpd->config->max_method_len = *(size_t *) value;
            break;

        case FREEHTTPD_CONFIG_MAX_URI_LEN:
            freehttpd->config->max_uri_len = *(size_t *) value;
            break;

        case FREEHTTPD_CONFIG_MAX_VERSION_LEN:
            freehttpd->config->max_version_len = *(size_t *) value;
            break;

        case FREEHTTPD_CONFIG_DOCROOT:
            freehttpd->config->docroot
                = value == NULL ? NULL : strdup ((const char *) value);

            if (value != NULL)
                freehttpd->config->_docroot_length
                    = strlen (freehttpd->config->docroot);
            break;

        default:
            return E_UNKNOWN_OPT;
        }

    return E_OK;
}

void
freehttpd_free (freehttpd_t *freehttpd)
{
    if (freehttpd == NULL)
        return;

    freehttpd_config_free (freehttpd->config);
    free (freehttpd);
}

static ecode_t
freehttpd_create_socket (freehttpd_t *freehttpd)
{
    int sockfd = socket (AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
        return E_SYSCALL_SOCKET;

    int opt = 1;

    if (setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt)) < 0)
        return E_SYSCALL_SETSOCKOPT;

    freehttpd->sockfd = sockfd;
    return E_OK;
}

static struct sockaddr_in
freehttpd_setup_addrinfo (freehttpd_t *freehttpd)
{
    struct sockaddr_in addr = { 0 };
    const char *addr_host = freehttpd->config->addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons (freehttpd->config->port);
    addr.sin_addr.s_addr
        = addr_host == NULL ? INADDR_ANY : inet_addr (addr_host);

    return addr;
}

static ecode_t
freehttpd_bind (freehttpd_t *freehttpd, struct sockaddr_in *addr_in)
{
    if (bind (freehttpd->sockfd, (struct sockaddr *) addr_in, sizeof (*addr_in))
        < 0)
        return E_SYSCALL_BIND;

    return E_OK;
}

static ecode_t
freehttpd_listen (freehttpd_t *freehttpd)
{
    unsigned int max_listen_queue = freehttpd->config->max_listen_queue;

    if (listen (freehttpd->sockfd, (int) max_listen_queue) < 0)
        return E_SYSCALL_LISTEN;

    return E_OK;
}

static ecode_t
freehttpd_send_error (FILE *stream, int sockfd, freehttpd_status_t status)
{
    freehttpd_response_t *response = freehttpd_response_init ("1.1", 3, status);

    if (response == NULL)
        return E_LIBC_MALLOC;

    if (stream == NULL)
        stream = fdopen (sockfd, "w");

    freehttpd_response_add_default_headers (response);
    freehttpd_response_add_header (response, "Content-Type",
                                   "text/html; charset=\"utf-8\"", 12, 26);

    response->body = NULL;
    (void) (asprintf (&response->body,
                      "<center><h1>%d %s</h1><hr><p>freehttpd</p></center>\r\n",
                      response->status.code, response->status.text)
            + 1);

    response->body_length = strlen (response->body);

    char *len = NULL;
    (void) (asprintf (&len, "%lu", response->body_length) + 1);

    freehttpd_response_add_header (response, "Content-Length", len, 14,
                                   strlen (len));

    ecode_t ret = freehttpd_response_send (response, stream);

    if (ret != E_OK)
        log_err (LOG_ERR "failed to send error response: %i\n", ret);

    fflush (stream);
    free (len);
    freehttpd_response_free (response);
    return ret;
}

static long
fsize (FILE *file)
{
    long size;
    long pos = ftell (file);
    fseek (file, 0, SEEK_END);
    size = ftell (file);
    fseek (file, pos, SEEK_SET);
    return size;
}

static void
iasprintf (char **strp, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    (void) (vasprintf (strp, fmt, ap) + 1);
    va_end (ap);
}

static ecode_t
freehttpd_respond_dindex (freehttpd_t *freehttpd, freehttpd_request_t *request,
                          freehttpd_response_t *response, FILE *stream,
                          const char *rpath)
{
    freehttpd_response_set_status (response, FREEHTTPD_STATUS_OK);
    ecode_t code = freehttpd_response_send (response, stream);

    if (code != E_OK)
        return code;

    bool is_root = strcmp (freehttpd->config->docroot, rpath) == 0;
    bool http1_0 = strcmp (request->version, "1.0") == 0;

    fprintf (stream, "Content-Type: text/html; charset=\"utf-8\"\r\n");

    if (http1_0)
        fprintf (stream, "Content-Length: -1\r\n");
    else
        fprintf (stream, "Transfer-Encoding: chunked\r\n");

    fprintf (stream, "\r\n");

    DIR *dir = opendir (rpath);

    if (dir == NULL)
        return E_SYSCALL_READ;

    struct dirent *entry = NULL;
    char *out_buf = NULL;

    iasprintf (
        &out_buf,
        "<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 3.2//EN\">\r\n"
        "<html>\r\n"
        "<head>\r\n"
        "<title>Index of %s</title>\r\n"
        "</head>\r\n"
        "<body>\r\n"
        "<h1>Index of %s</h1>\r\n"
        "<table>\r\n"
        "<tr>\r\n"
        "    <th>\r\n"
        "        <img\r\n"
        "            src=\"https://httpd.apache.org/icons/blank.gif\"\r\n"
        "            alt=\"[ICO]\"\r\n"
        "        />\r\n"
        "    </th>\r\n"
        "    <th><a href=\"?C=N;O=D\">Name</a></th>\r\n"
        "    <th><a href=\"?C=M;O=A\">Last modified</a></th>\r\n"
        "    <th><a href=\"?C=S;O=A\">Size</a></th>\r\n"
        "    <th><a href=\"?C=D;O=A\">Description</a></th>\r\n"
        "</tr>\r\n"
        "<tr>\r\n"
        "    <th colspan=\"5\"><hr /></th>\r\n"
        "</tr>\r\n",
        request->uri, request->uri);

    fprintf (stream, "%lx\r\n%s\r\n", strlen (out_buf), out_buf);
    free (out_buf);

    if (!is_root)
        {
            iasprintf (&out_buf,
                       "<tr>\r\n"
                       "<td valign=\"top\">\r\n"
                       "<img\r\n"
                       "src=\"https://httpd.apache.org/icons/back.gif\"\r\n"
                       "alt=\"[DIR]\"\r\n"
                       "/>\r\n"
                       "</td>\r\n"
                       "<td><a href=\"..\">Parent Directory</a></td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td align=\"right\"> - </td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "</tr>\r\n");

            fprintf (stream, "%lx\r\n%s\r\n", strlen (out_buf), out_buf);
            free (out_buf);
        }

    while ((entry = readdir (dir)) != NULL)
        {
            if (strcmp (entry->d_name, ".") == 0
                || strcmp (entry->d_name, "..") == 0)
                continue;

            struct stat st = { 0 };
            char path[PATH_MAX] = { 0 };
            char mtime_str[64] = { 0 };

            strcpy (path, rpath);
            strcat (path, "/");
            strcat (path, entry->d_name);

            if (lstat (path, &st) < 0)
                continue;

            struct tm *mtime = localtime (&st.st_mtime);
            strftime (mtime_str, sizeof (mtime_str), "%Y-%m-%d %H:%M", mtime);

            iasprintf (&out_buf,
                       "<tr>\r\n"
                       "<td valign=\"top\">\r\n"
                       "<img\r\n"
                       "src=\"https://httpd.apache.org/icons/%s.gif\"\r\n"
                       "alt=\"[DIR]\"\r\n"
                       "/>\r\n"
                       "</td>\r\n",
                       S_ISDIR (st.st_mode) ? "folder" : "unknown");

            fprintf (stream, "%lx\r\n%s\r\n", strlen (out_buf), out_buf);
            free (out_buf);

            const char *leading_slash = S_ISDIR (st.st_mode) ? "/" : "";
            iasprintf (&out_buf,
                       "<td><a href=\"%s%s%s%s\">%s%s</a></td>\r\n"
                       "<td align=\"right\">%s</td>\r\n"
                       "<td align=\"right\"> - </td>\r\n"
                       "</tr>\r\n",
                       request->uri,
                       request->uri[request->uri_length - 1] == '/' ? "" : "/",
                       entry->d_name, leading_slash, entry->d_name,
                       leading_slash, mtime_str);

            fprintf (stream, "%lx\r\n%s\r\n", strlen (out_buf), out_buf);
            free (out_buf);
        }

    iasprintf (&out_buf,
               "<tr>\r\n"
               "    <th colspan=\"5\"><hr /></th>\r\n"
               "</tr>\r\n"
               "</table><address>freehttpd/1.0.0-beta.1 (Ubuntu 24.04 "
               "LTS) Server at localhost Port 8080</address>\r\n"
               "</body>\r\n"
               "</html>\r\n");

    fprintf (stream, "%lx\r\n%s\r\n", strlen (out_buf), out_buf);
    free (out_buf);

    fprintf (stream, "0\r\n\r\n");
    closedir (dir);
    return E_OK;
}

static ecode_t
freehttpd_respond (freehttpd_t *freehttpd, freehttpd_request_t *request,
                   freehttpd_response_t *response, FILE *stream)
{
    freehttpd_response_add_default_headers (response);

    const char *docroot = freehttpd->config->docroot;
    char *rpath = NULL;
    freehttpd_status_t status = FREEHTTPD_STATUS_OK;

    if (docroot == NULL)
        {
            status = FREEHTTPD_STATUS_INTERNAL_SERVER_ERROR;
            goto freehttpd_respond_error;
        }

    char *path = request->path;

    if (path == NULL)
        {
            status = FREEHTTPD_STATUS_INTERNAL_SERVER_ERROR;
            goto freehttpd_respond_error;
        }

    char *fs_path = NULL;

    if (asprintf (&fs_path, "%s%s", docroot, path) < 0)
        {
            status = FREEHTTPD_STATUS_INTERNAL_SERVER_ERROR;
            return E_LIBC_MALLOC;
        }

    rpath = realpath (fs_path, NULL);

    if (rpath == NULL)
        {
            status = FREEHTTPD_STATUS_NOT_FOUND;
            goto freehttpd_respond_error;
        }

    if (strncmp (docroot, rpath, freehttpd->config->_docroot_length) != 0)
        {
            status = FREEHTTPD_STATUS_FORBIDDEN;
            goto freehttpd_respond_error;
        }

    log_msg (LOG_DEBUG "rpath: %s\n", rpath);
    struct stat st = { 0 };

    if (lstat (rpath, &st) < 0)
        {
            status = FREEHTTPD_STATUS_INTERNAL_SERVER_ERROR;
            goto freehttpd_respond_error;
        }

    if (S_ISDIR (st.st_mode))
        {
            ecode_t code = freehttpd_respond_dindex (freehttpd, request,
                                                     response, stream, rpath);

            free (rpath);
            free (fs_path);
            return code;
        }

    FILE *file = fopen (rpath, "r");

    if (file == NULL)
        {
            switch (errno)
                {
                case EACCES:
                case EISDIR:
                    status = FREEHTTPD_STATUS_FORBIDDEN;
                    break;
                case ENOENT:
                    status = FREEHTTPD_STATUS_NOT_FOUND;
                    break;
                default:
                    status = FREEHTTPD_STATUS_INTERNAL_SERVER_ERROR;
                    break;
                }

            goto freehttpd_respond_error;
        }

    const char *content_type = NULL;
    size_t rpath_len = strlen (rpath);

    if (rpath_len > 4 && strcmp (rpath + rpath_len - 4, ".css") == 0)
        content_type = "text/css";
    else if (rpath_len > 5 && strcmp (rpath + rpath_len - 5, ".html") == 0)
        content_type = "text/html";
    else if (rpath_len > 4 && strcmp (rpath + rpath_len - 3, ".js") == 0)
        content_type = "application/javascript";
    else
        {
            magic_descriptor (freehttpd->magic, fileno (file));

            if (content_type == NULL)
                content_type = "application/octet-stream";
        }

    fseek (file, 0, SEEK_SET);

    long file_size = fsize (file);
    char buffer[1024] = { 0 };

    if (file_size == -1)
        {
            status = FREEHTTPD_STATUS_INTERNAL_SERVER_ERROR;
            goto freehttpd_respond_error;
        }

    freehttpd_response_set_status (response, status);
    ecode_t code = freehttpd_response_send (response, stream);

    if (code != E_OK)
        {
            free (rpath);
            free (fs_path);
            fclose (file);
            return code;
        }

    char etag[128] = { 0 };
    bool http1_0 = strcmp (request->version, "1.0") == 0;
    snprintf (etag, sizeof (etag), "\"%lx-%lx\"", st.st_mtime, file_size);

    fprintf (stream, "Content-Type: %s\r\n", content_type);

    if (http1_0)
        fprintf (stream, "Content-Length: %ld\r\n", file_size);
    else
        fprintf (stream, "Transfer-Encoding: chunked\r\n");

    fprintf (stream, "ETag: %s\r\n", etag);
    fprintf (stream, "\r\n");

    while (file_size > 0)
        {
            size_t read_size = 0;

            errno = 0;

            if ((read_size = fread (buffer, 1, sizeof (buffer), file)) == 0
                && errno != 0)
                {
                    freehttpd_response_set_status (
                        response, FREEHTTPD_STATUS_INTERNAL_SERVER_ERROR);
                    free (rpath);
                    free (fs_path);
                    fclose (file);
                    return E_SYSCALL_READ;
                }

            if (!http1_0)
                fprintf (stream, "%lx\r\n", read_size);

            if (fwrite (buffer, 1, read_size, stream) != read_size)
                {
                    free (rpath);
                    free (fs_path);
                    fclose (file);
                    return E_SYSCALL_WRITE;
                }

            if (!http1_0)
                fprintf (stream, "\r\n");

            if (file_size >= (long int) read_size)
                file_size -= (long int) read_size;
            else
                file_size = 0;
        }

    if (!http1_0)
        fprintf (stream, "0\r\n\r\n");

    fclose (file);
freehttpd_respond_error:
    freehttpd_response_set_status (response, status);

    if (status != FREEHTTPD_STATUS_OK)
        freehttpd_send_error (stream, fileno (stream), status);

    free (rpath);
    free (fs_path);
    return E_OK;
}

static ecode_t
freehttpd_loop (freehttpd_t *freehttpd)
{
    while (true)
        {
            struct sockaddr_in client_addr = { 0 };
            socklen_t client_addr_len = sizeof (client_addr);
            int client_sockfd
                = accept (freehttpd->sockfd, (struct sockaddr *) &client_addr,
                          &client_addr_len);

            if (client_sockfd < 0)
                return E_SYSCALL_ACCEPT;

            ecode_t code = E_OK;
            freehttpd_request_t *request
                = freehttpd_request_parse (freehttpd, client_sockfd, &code);

            if (code != E_OK)
                {
                    log_err (LOG_ERR "failed to parse request: %i\n", code);
                    freehttpd_send_error (NULL, client_sockfd,
                                          FREEHTTPD_STATUS_BAD_REQUEST);
                    continue;
                }

            freehttpd_response_t *response = freehttpd_response_init (
                request->version, request->version_length, FREEHTTPD_STATUS_OK);

            if (response == NULL)
                {
                    log_err (LOG_ERR "failed to init response\n");
                    freehttpd_send_error (
                        NULL, client_sockfd,
                        FREEHTTPD_STATUS_INTERNAL_SERVER_ERROR);
                    freehttpd_request_free (request);
                    close (client_sockfd);
                    continue;
                }

            FILE *stream = fdopen (client_sockfd, "w");

            code = freehttpd_respond (freehttpd, request, response, stream);

            log_msg (LOG_INFO "%s %s HTTP/%s - %d %s\n", request->method,
                     request->uri, request->version, response->status.code,
                     response->status.text);

            if (code != E_OK)
                log_err (LOG_ERR "failed to send response: %i\n", code);

            fclose (stream);
            freehttpd_response_free (response);
            freehttpd_request_free (request);
        }

    return E_OK;
}

ecode_t
freehttpd_start (freehttpd_t *restrict freehttpd)
{
    ecode_t code = E_OK;
    struct sockaddr_in addr_in;

    if ((code = freehttpd_create_socket (freehttpd)) != E_OK)
        return code;

    addr_in = freehttpd_setup_addrinfo (freehttpd);

    if ((code = freehttpd_bind (freehttpd, &addr_in)) != E_OK)
        return code;

    if ((code = freehttpd_listen (freehttpd)) != E_OK)
        return code;

    return freehttpd_loop (freehttpd);
}

const freehttpd_config_t *
freehttpd_get_config (freehttpd_t *freehttpd)
{
    return freehttpd->config;
}