#include <nginx.h>
#include <ngx_core.h>
#include <ngx_string.h>
#include <ngx_event.h>

#include "presentation_http_request.h"

/* don't set this less than ~100 (or we might not read all the headers in the first receive call) */
/* see bug in realloc function */
#define REQUEST_SIZE 528

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
    request->body = NULL;
    request->pool = pool;
    request->size = size;

    return request;
}

int presentation_request_realloc(presentation_request_t *request, ssize_t size) {
    u_char *old_request_str = request->start;
    size_t diff = request->last - request->start;

    //request->start = ngx_pnalloc(request->pool, request->size * 2);
    request->start = ngx_pnalloc(request->pool, size);

    if (!request->start) {
        return NGX_DECLINED;
    }


    ngx_memcpy((char *)request->start, (char *)old_request_str, request->size);

    request->size *= 2;
    request->last = request->start + diff;
    request->end = request->start + request->size;

    /* pfree only reallocates "large allocations" (> NGX_MAX_ALLOC_FROM_POOL) so if current 
    request string is not a "large allocation" pfree will not find it and fail. 
    TODO: fix this 
    (current solution: keep REQUEST_SIZE >= 528) */
    if (ngx_pfree(request->pool, old_request_str) != NGX_OK) {
        return NGX_DECLINED;    // TODO: return the pfree error code instead
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

    request = init_presentation_request(c->pool, REQUEST_SIZE);

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

   /* 1. receive headers into struct */
    n = recv_wrapper(c, request, rev);

    if (n <= 0) {
        return;
    }

    /* 2. get request length */
    ssize_t request_length = find_request_length(request);
    if (request_length < 0) {
        return;
    }

    /* 3. reallocate enough memory to hold request size */
    /*    (do this before receiving to minimize number of copy calls) */
    presentation_request_realloc(request, (ssize_t) request_length);

    /* 4. call receive function until we have read the entire request */
    while (n < (ssize_t) request_length && rev->ready) {
        int m = recv_wrapper(c, request, rev);
        if (m < 0) { return; }
        n += m;
    }
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

    if (n == NGX_OK) {
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

ssize_t find_request_length(presentation_request_t *request) {
    u_char* str;
    size_t i, str_size, header_label_size, separator_size, header_size;
    char *header_label, *separator;

    i = 0;
    str = request->start;
    str_size = request->last - request->start + 1;
    header_label = "Content-Length: ";
    header_label_size = ngx_strlen(header_label);
    separator = "\r\n\r\n";
    separator_size = ngx_strlen(separator);

    while (i < str_size) {                                          /* iterate through each char in headers */
        if (str[i] != '\n') {                                       /* keep skipping until you hit a newline */
            ++i;
            continue;
        }
        ++i;

        if (ngx_strncmp(&str[i], header_label, header_label_size) == 0) {       /* at each newline, check the following header */
            i += header_label_size;                                   /* if match, skip to end of header */
            size_t l = 0;
            while (str[i + l] != '\r' && str[i + l] != '\n') {                            /* find size of substring containing value after the header */
                ++l;
            }
            ssize_t body_size = ngx_atosz(&str[i], l);

            u_char* end_ptr = (u_char*) ngx_strstr(&str[i], separator);        /* find index of header/body separator */ 
            
            if (end_ptr == NULL) {
                return NGX_ERROR;
            }
            else {
                request->body = end_ptr + separator_size;
                header_size = request->body - str;
                return body_size + header_size;
            }
        }
    }

    return NGX_ERROR;
}
