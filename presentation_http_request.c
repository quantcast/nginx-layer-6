#include <nginx.h>
#include <ngx_core.h>
#include <ngx_string.h>
#include <ngx_event.h>

#include "presentation_http_request.h"

presentation_request_t *init_presentation_request(ngx_pool_t *pool, size_t size) {
    presentation_request_t *request = ngx_pcalloc(pool, sizeof(presentation_request_t));

    if (!request) {
        return NULL;
    }

    request->start = ngx_pnalloc(pool, size);

    if (!request->start) {
        ngx_pfree(pool, request);
        return NULL;
    }

    request->last = request->start;
    request->end = request->start + size;
    request->pool = pool;
    request->size = size;

    return request;
}

int presentation_request_realloc(presentation_request_t *request) {
    u_char *old_request_str = request->start;
    size_t diff = request->last - request->start;

    request->start = ngx_pnalloc(request->pool, request->size * 2);

    if (!request->start) {
        return NGX_DECLINED;
    }

    strncpy((char *)request->start, (char *)old_request_str, request->size);

    request->size *= 2;
    request->last = request->start + diff;
    request->end = request->start + request->size;

    if (!ngx_pfree(request->pool, old_request_str)) {
        return NGX_DECLINED;
    }

    return NGX_OK;
}

int presentation_request_free(presentation_request_t *request) {
    if (!ngx_pfree(request->pool, request->start)) {
        return NGX_DECLINED;
    }

    if (!ngx_pfree(request->pool, request)) {
        return NGX_DECLINED;
    }

    return NGX_OK;
}

void presentation_http_request_close_connection(ngx_connection_t *c)
{
    ngx_pool_t  *pool;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "close http connection: %d", c->fd);

    c->destroyed = 1;

    pool = c->pool;

    ngx_close_connection(c);

    ngx_destroy_pool(pool);
}

void presentation_http_request_handler(ngx_event_t *rev) {
    size_t                     size;
    ssize_t                    n;
    presentation_request_t    *request;
    ngx_connection_t          *c;

    c = rev->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http wait request handler");

    if (rev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        presentation_http_request_close_connection(c);
        return;
    }

    if (c->close) {
        presentation_http_request_close_connection(c);
        return;
    }

    size = 1024;
    request = init_presentation_request(c->pool, size);

    if (request == NULL) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to allocate space for the presentation request struct.");
        presentation_http_request_close_connection(c);
        return;
    }
    
    if (request->start == NULL) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to allocate space for the request string.");
        presentation_http_request_close_connection(c);
        return;
    }

    /**
     * TODO: This is next line receives a single IP packet of up to size `size` bytes. The `request` is memory allocated on the heap
     * that can hold the request. The arguments are the connection (c), where to put the data (request->last, since we want to append
     * to the current request), and the upper limit on the size to read.
     * 
     * It will return an integer (n) which tells us how many bytes have been read. We want to continue to read until we have the full request.
     * Remember to clean up memory and resources as you do this (the provided `presentation_request_realloc` and `presentation_request_free`
     * functions should help you do this, but they may or may not have bugs in them).
     * 
     * 
     */

    /* Plan of action:
        1. receive header into struct
        2. get content length from header
        3. realloc the correct number of times
        4. call recv the correct number of times
    */

   /* 1. receive header into struct */
    n = recv_wrapper(c, request, rev);

    if (n <= 0) {
        return;
    }

    /* 2. get content length from header */
    printf("\n\ncontent length: %zu", find_content_length((char*) request->start));
    fflush(stdout);
}

// TODO: this function should parse out the request body from the request buffer
// not sure if these are all the necessary parameters
ngx_str_t presentation_parse_http_request_body(ngx_buf_t request_buffer) {
    ngx_str_t str = ngx_string("");
    return str;
}

size_t recv_wrapper(ngx_connection_t *c, presentation_request_t *request, ngx_event_t *rev)
{
    int n;
    
    n = c->recv(c, request->last, request->size);

    if (n == NGX_AGAIN) {
        if (!rev->timer_set) {
            ngx_add_timer(rev, 60 * 1000);
            ngx_reusable_connection(c, 1);
        }

        if (ngx_handle_read_event(rev, 0) != NGX_OK) {
            presentation_http_request_close_connection(c);
            return n;
        }

        return n;
    }

    if (n == NGX_ERROR) {
        presentation_http_request_close_connection(c);
        return n;
    }

    if (n == 0) {
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                      "client closed connection");
        presentation_http_request_close_connection(c);
        return n;
    }

    request->last += n;

    c->log->action = "reading client request line";

    ngx_reusable_connection(c, 0);

    return n;
}

size_t find_content_length(char *str) {
    size_t i, str_size, header_size;
    char* header;

    i = 0;
    str_size = strlen(str);
    header = "Content-Length: ";
    header_size = strlen(header);

    while (i < str_size) {                          /* iterate through each char in headers */
        if (str[i] != '\n') {
            ++i;
            continue;
        }
        ++i;
        for (size_t j = 0; j < header_size; ++j) {     /* check for "Content-length" header */
            if (str[i + j] != header[j]) {
                break;                              /* if mismatch, break */
            }
            if (j == header_size - 1) {                 /* ensure we have read the whole header name */
                i += j;
                size_t l;
                l = 0;
                while (str[i + l] != '\n') {        /* go to the end of the length value (until newline) */
                    ++l;
                }
                char size[l];
                memcpy(size, &str[i+1], l-1);            /* put the length value string into size variable */
                return (size_t) atoi(size);
            }
        }
    }

    return 0;
}