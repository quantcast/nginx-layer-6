#include <nginx.h>
#include <ngx_core.h>
#include <ngx_string.h>
#include <ngx_event.h>

#include "httplite_request.h"

#define LENGTH_HEADER "\nContent-Length: "
#define LENGTH_HEADER_SIZE strlen(LENGTH_HEADER)
#define HEADER_BODY_SEPARATOR "\r\n\r\n"
#define HEADER_BODY_SEPARATOR_SIZE strlen(HEADER_BODY_SEPARATOR)


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

void ngx_httplite_close_connection(ngx_connection_t *c)
{
    ngx_pool_t  *pool;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "close http connection: %d", c->fd);

    c->destroyed = 1;

    pool = c->pool;

    ngx_close_connection(c);

    ngx_destroy_pool(pool);
}

/* Assumes incoming slab is empty (writes to buffer pointer, overwriting anything there) */
size_t recv_wrapper(ngx_connection_t *c, httplite_request_slab_t *slab, ngx_event_t *rev) {
    int n;
    
    n = c->recv(c, slab->buffer, SLAB_SIZE);

    if (n == NGX_AGAIN) {
        if (!rev->timer_set) {
            ngx_add_timer(rev, 60 * 1000);
            ngx_reusable_connection(c, 1);
        }

        if (ngx_handle_read_event(rev, 0) != NGX_OK) {
            ngx_httplite_close_connection(c);
            return n;
        }

        return n;
    }

    if (n == NGX_ERROR) {
        ngx_httplite_close_connection(c);
        return n;
    }

    if (n == NGX_OK) {
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                      "client closed connection");
        ngx_httplite_close_connection(c);
        return n;
    }

    slab->size += n;

    c->log->action = "reading client request line";

    ngx_reusable_connection(c, 0);

    return n;
}

void httplite_request_handler(ngx_event_t *rev) {
    ssize_t                    n;
    httplite_request_list_t    list;
    httplite_request_slab_t   *curr;
    ngx_connection_t          *c;

    c = rev->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http wait request handler");

    if (rev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        ngx_httplite_close_connection(c);
        return;
    }

    if (c->close) {
        ngx_httplite_close_connection(c);
        return;
    }

    list = httplite_init_list(c);

    curr = list.head;
    
    if (list.head == NULL) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to allocate space for the request slab.");
        ngx_httplite_close_connection(c);
        return;
    }

    if (list.head->buffer == NULL) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to allocate space for the request string.");
        ngx_httplite_close_connection(c);
        return;
    }

    n = recv_wrapper(c, curr, rev);

    if (n <= 0) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "failed recv.");
        return;
    }

    /* get request length */
    ssize_t request_length = find_request_length(curr);
    if (request_length < 0) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to find request length.");
        return;
    }

    /* call receive function until we have read the entire request */
    int m;
    while (n < request_length && rev->ready) {
        if (!httplite_add_slab(list)) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to add slab.");
            return;
        }
        m = recv_wrapper(c, list.tail, rev);

        if (m < 0) { 
            ngx_log_error(NGX_LOG_ALERT, c->log, 0, "failed recv.");
            return; 
        }

        n += m;
    }
}

ssize_t find_request_length(httplite_request_slab_t *slab) {
    u_char *str, *curr, *end_ptr;
    size_t header_size, l;
    ssize_t body_size;

    str = slab->buffer;
    curr = str;
    body_size = 0;

    curr = ngx_strlcasestrn(str, str + slab->size, (u_char*) LENGTH_HEADER, LENGTH_HEADER_SIZE - 1);
    /* From nginx documentation:
    *       ngx_strlcasestrn() is intended to search for static substring
    *       with known length in string until the argument last. The argument n
    *       must be length of the second substring - 1.
    */

    if (curr != NULL) {
        curr += LENGTH_HEADER_SIZE;
        l = 0;

        /* find size of substring containing value after the header */
        while (*(curr + l) != '\r' && *(curr + l) != '\n') {
            ++l;
        }

        body_size = ngx_atosz(curr, l);

        curr += l;
    }

    /* find index of header separator */ 
    curr = (u_char*) ngx_strstr(curr, HEADER_BODY_SEPARATOR);
            
    if (curr == NULL) {
        return NGX_ERROR;
    }
    else {
        curr += HEADER_BODY_SEPARATOR_SIZE;
        header_size = curr - str;
        
        curr += body_size + 1;
        // TODO: what if end of body is in the next slab? move this to recv wrapper

        if (check_http_method(curr) == 1) {
            // TODO: process pipelined request
        }

        return body_size + header_size;
    }

    return NGX_ERROR;
}

size_t check_http_method(u_char *str) {
    if (ngx_strncmp(str, "GET", 3) == 0 ||
            ngx_strncmp(str, "POST", 4) == 0 ||
            ngx_strncmp(str, "PUT", 3) == 0) {
        // TODO: add the other methods
        return 1;
    }
    return 0;
}

