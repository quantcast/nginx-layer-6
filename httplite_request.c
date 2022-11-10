#include <nginx.h>
#include <ngx_core.h>
#include <ngx_string.h>
#include <ngx_event.h>

#include "httplite_request.h"
#define BUFFER_SIZE 1024

void httplite_http_request_close_connection(ngx_connection_t *c);

httplite_request_t *init_httplite_request(ngx_pool_t *pool, size_t size) {
    httplite_request_t *request = ngx_pcalloc(pool, sizeof(httplite_request_t));

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

int httplite_request_realloc(httplite_request_t *request) {
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

int httplite_request_free(httplite_request_t *request) {
    if (!ngx_pfree(request->pool, request->start)) {
        return NGX_DECLINED;
    }

    if (!ngx_pfree(request->pool, request)) {
        return NGX_DECLINED;
    }

    return NGX_OK;
}

void httplite_http_request_close_connection(ngx_connection_t *c)
{
    ngx_pool_t  *pool;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "close http connection: %d", c->fd);

    c->destroyed = 1;

    pool = c->pool;

    ngx_close_connection(c);

    ngx_destroy_pool(pool);
}

void httplite_request_handler(ngx_event_t *rev) {
    size_t                     size;
    ssize_t                    n;
    httplite_request_t    *request;
    ngx_connection_t          *c;

    c = rev->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http wait request handler");

    if (rev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        httplite_request_close_connection(c);
        return;
    }

    if (c->close) {
        httplite_request_close_connection(c);
        return;
    }

    // TODO: Remove and replace with config variable
    // https://github.com/quantcast/nginx-layer-6/pull/3#discussion_r1014484359
    size = BUFFER_SIZE;
    request = init_httplite_request(c->pool, size);

    if (request == NULL) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to allocate space for the httplite request struct.");
        httplite_http_request_close_connection(c);
        return;
    }
    
    if (request->start == NULL) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to allocate space for the request string.");
        httplite_http_request_close_connection(c);
        return;
    }

    /**
     * TODO: This is next line receives a single IP packet of up to size `size` bytes. The `request` is memory allocated on the heap
     * that can hold the request. The arguments are the connection (c), where to put the data (request->last, since we want to append
     * to the current request), and the upper limit on the size to read.
     * 
     * It will return an integer (n) which tells us how many bytes have been read. We want to continue to read until we have the full request.
     * Remember to clean up memory and resources as you do this (the provided `httplite_request_realloc` and `httplite_request_free`
     * functions should help you do this, but they may or may not have bugs in them).
     */
    n = c->recv(c, request->last, request->size);

    // TODO: Is this a safe assumption? 
    // https://github.com/quantcast/nginx-layer-6/pull/3#discussion_r1006215672
    if (n == NGX_AGAIN) {

        if (!rev->timer_set) {
            ngx_add_timer(rev, 60 * 1000);
            ngx_reusable_connection(c, 1);
        }

        if (ngx_handle_read_event(rev, 0) != NGX_OK) {
            httplite_request_close_connection(c);
            return;
        }

        return;
    }

    if (n == NGX_ERROR) {
        httplite_request_close_connection(c);
        return;
    }

    if (n == 0) {
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                      "client closed connection");
        httplite_request_close_connection(c);
        return;
    }

    request->last += n;

    c->log->action = "reading client request line";

    ngx_reusable_connection(c, 0);
}

void httplite_request_close_connection(ngx_connection_t *c)
{
    ngx_pool_t  *pool;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "close http connection: %d", c->fd);

    c->destroyed = 1;

    pool = c->pool;

    ngx_close_connection(c);

    ngx_destroy_pool(pool);
}

// TODO: this function should parse out the request body from the request buffer
// not sure if these are all the necessary parameters
ngx_str_t httplite_parse_http_request_body(ngx_buf_t request_buffer) {
    ngx_str_t str = ngx_string("");
    return str;
}
