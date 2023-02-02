#include <nginx.h>
#include <ngx_core.h>
#include <ngx_string.h>
#include <ngx_event.h>

#include "httplite_request.h"
#include "httplite_upstream.h"
#include "httplite_load_balancer.h"

httplite_request_list_t httplite_init_list(ngx_connection_t *connection) {
    httplite_request_list_t list = { 0 };

    httplite_request_slab_t *head = ngx_pcalloc(connection->pool, sizeof(httplite_request_slab_t));
    
    if (!head) {
        ngx_log_error(NGX_ERROR_ALERT, connection->log, 0, "unable to create head slab on connection's memory pool.");
        return list;
    }

    head->buffer = ngx_pnalloc(connection->pool, SLAB_SIZE);

    if (!head->buffer) {
        ngx_log_error(NGX_ERROR_ALERT, connection->log, 0, "unable to the string buffer on connection's memory pool.");
        return list;
    }

    head->size = 0;

    list.head = head;
    list.tail = head;
    list.connection = connection;

    return list;
}

httplite_request_slab_t *httplite_add_slab(httplite_request_list_t list) {
    httplite_request_slab_t *new_slab = ngx_pcalloc(list.connection->pool, sizeof(httplite_request_slab_t));

    if (!new_slab) {
        ngx_log_error(NGX_ERROR_ALERT, list.connection->log, 0, "unable to create new slab on connection's memory pool.");
        return NULL;
    }

    new_slab->size = 0;
    new_slab->buffer = ngx_pnalloc(list.connection->pool, SLAB_SIZE);

    if (!new_slab->buffer) {
        ngx_log_error(NGX_ERROR_ALERT, list.connection->log, 0, "unable to the string buffer on connection's memory pool.");
        return NULL;
    }

    list.tail->next = new_slab;
    list.tail = new_slab;
    list.tail->next = NULL;

    return new_slab;
}

void httplite_close_connection(ngx_connection_t *c)
{
    ngx_pool_t  *pool;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "close http connection: %d", c->fd);

    c->destroyed = 1;

    pool = c->pool;

    ngx_close_connection(c);

    ngx_destroy_pool(pool);
}

// TODO: update this function to reflect new request structure
void httplite_request_handler(ngx_event_t *rev) {
    ssize_t                    n;
    httplite_request_list_t    list;
    httplite_request_slab_t   *curr;
    ngx_connection_t          *c;

    c = rev->data;

    const uint NUM_UPSTREAMS = ((httplite_upstream_configuration_t*)(c->listening->servers))->upstreams.nelts;
    static int upstream_index = 0;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http wait request handler");

    if (rev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        httplite_close_connection(c);
        return;
    }

    if (c->close) {
        httplite_close_connection(c);
        return;
    }

    list = httplite_init_list(c);

    curr = list.head;
    
    if (list.head == NULL) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to allocate space for the request slab.");
        httplite_close_connection(c);
        return;
    }

    if (list.head->buffer == NULL) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to allocate space for the request string.");
        httplite_close_connection(c);
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
    n = c->recv(c, curr->buffer, SLAB_SIZE);
    curr->size = n;
    httplite_upstream_configuration_t *upstream_configuration = c->listening->servers;
    httplite_upstream_t *upstream_elements = upstream_configuration->upstreams.elts;

    // NOTE: Possible race condition below
    httplite_send_request_to_upstream(
        &upstream_elements[upstream_index++ % NUM_UPSTREAMS],
        curr
    );

    // TODO: Is this a safe assumption? 
    // https://github.com/quantcast/nginx-layer-6/pull/3#discussion_r1006215672
    if (n == NGX_AGAIN) {

        if (!rev->timer_set) {
            ngx_add_timer(rev, 60 * 1000);
            ngx_reusable_connection(c, 1);
        }

        if (ngx_handle_read_event(rev, 0) != NGX_OK) {
            httplite_close_connection(c);
            return;
        }

        return;
    }

    if (n == NGX_ERROR) {
        httplite_close_connection(c);
        return;
    }

    if (n == 0) {
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                      "client closed connection");
        httplite_close_connection(c);
        return;
    }

    c->log->action = "reading client request line";

    ngx_reusable_connection(c, 0);
}

// TODO: this function should parse out the request body from the request buffer
// not sure if these are all the necessary parameters
ngx_str_t httplite_parse_http_request_body(ngx_buf_t request_buffer) {
    ngx_str_t str = ngx_string("");
    return str;
}
