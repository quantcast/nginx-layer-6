#include <nginx.h>
#include <ngx_core.h>
#include <ngx_event.h>

void presentation_http_request_close_connection(ngx_connection_t *c);

void presentation_http_request_handler(ngx_event_t *rev)
{
    size_t                     size;
    ssize_t                    n;
    ngx_buf_t                 *b;
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

    b = c->buffer;

    if (b == NULL) {
        b = ngx_create_temp_buf(c->pool, size);
        if (b == NULL) {
            presentation_http_request_close_connection(c);
            return;
        }

        c->buffer = b;

    } else if (b->start == NULL) {
        b->start = ngx_palloc(c->pool, size);
        if (b->start == NULL) {
            presentation_http_request_close_connection(c);
            return;
        }

        b->pos = b->start;
        b->last = b->start;
        b->end = b->last + size;
    }

    n = c->recv(c, b->last, size);

    if (n == NGX_AGAIN) {
        if (!rev->timer_set) {
            ngx_add_timer(rev, 60 * 1000);
            ngx_reusable_connection(c, 1);
        }

        if (ngx_handle_read_event(rev, 0) != NGX_OK) {
            presentation_http_request_close_connection(c);
            return;
        }

        /*
         * We are trying to not hold c->buffer's memory for an idle connection.
         */

        if (ngx_pfree(c->pool, b->start) == NGX_OK) {
            b->start = NULL;
        }

        return;
    }

    if (n == NGX_ERROR) {
        presentation_http_request_close_connection(c);
        return;
    }

    if (n == 0) {
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                      "client closed connection");
        presentation_http_request_close_connection(c);
        return;
    }

    b->last += n;

    c->log->action = "reading client request line";

    ngx_reusable_connection(c, 0);
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
