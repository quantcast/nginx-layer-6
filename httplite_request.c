#include <nginx.h>
#include <ngx_core.h>
#include <ngx_string.h>
#include <ngx_event.h>

#include "httplite_request.h"
#include "httplite_upstream.h"

#define LENGTH_HEADER "\nContent-Length: "
#define LENGTH_HEADER_SIZE strlen(LENGTH_HEADER)
#define HEADER_BODY_SEPARATOR "\r\n\r\n"
#define HEADER_BODY_SEPARATOR_SIZE strlen(HEADER_BODY_SEPARATOR)

httplite_request_list_t *httplite_init_list(ngx_connection_t *connection) {
    httplite_request_list_t *list = ngx_pcalloc(connection->pool, sizeof(httplite_request_list_t));

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

    list->head = head;
    list->tail = head;
    list->connection = connection;
    list->curr = head;

    return list;
}

httplite_request_slab_t *httplite_add_slab(httplite_request_list_t *list) {
    httplite_request_slab_t *new_slab = ngx_pcalloc(list->connection->pool, sizeof(httplite_request_slab_t));

    if (!new_slab) {
        ngx_log_error(NGX_ERROR_ALERT, list->connection->log, 0, "unable to create new slab on connection's memory pool.");
        return NULL;
    }

    new_slab->size = 0;
    new_slab->buffer = ngx_pnalloc(list->connection->pool, SLAB_SIZE);

    if (!new_slab->buffer) {
        ngx_log_error(NGX_ERROR_ALERT, list->connection->log, 0, "unable to the string buffer on connection's memory pool.");
        return NULL;
    }

    list->tail->next = new_slab;
    list->tail = new_slab;
    list->tail->next = NULL;

    return new_slab;
}

void httplite_free_list(httplite_request_list_t *list) {
    httplite_request_slab_t *slab;
    slab = list->head;
    while (slab) {
        void *free = slab;
        slab = slab->next;
        ngx_pfree(list->connection->pool, free);
    }

    ngx_pfree(list->connection->pool, list);
}

void httplite_close_connection(ngx_connection_t *c)
{
    ngx_pool_t  *pool;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "close http connection: %d", c->fd);

    c->destroyed = 1;

    pool = c->pool;

    ngx_close_connection(c);

    if (pool) {
        ngx_destroy_pool(pool);
    }
}

/* Assumes incoming slab is empty (writes to buffer pointer, overwriting anything there) */
size_t recv_wrapper(ngx_connection_t *c, httplite_request_list_t *list, ngx_event_t *rev) {
    int n;
    n = c->recv(c, list->tail->buffer, SLAB_SIZE);

    if (n == NGX_AGAIN) {
        if (!rev->timer_set) {
            ngx_add_timer(rev, DEFAULT_SERVER_TIMEOUT);
            ngx_reusable_connection(c, 1);
        }

        if (ngx_handle_read_event(rev, 0) != NGX_OK) {
            httplite_close_connection(c);
            return n;
        }

        return n;
    }

    if (n == NGX_ERROR) {
        httplite_close_connection(c);
        return n;
    }

    if (n == NGX_OK) {
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                      "client closed connection");
        httplite_close_connection(c);
        return n;
    }

    list->tail->size += n;

    c->log->action = "reading client request line";

    ngx_reusable_connection(c, 0);

    return n;
}

// TODO: update this function to reflect new request structure
void httplite_request_handler(ngx_event_t *rev) {
    ssize_t                    n;
    httplite_request_list_t   *list;
    httplite_request_slab_t   *curr;
    ngx_connection_t          *c;

    c = rev->data;

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
    
    if (list->head == NULL) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to allocate space for the request slab.");
        httplite_close_connection(c);
        return;
    }

    if (list->head->buffer == NULL) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to allocate space for the request string.");
        httplite_close_connection(c);
        return;
    }

    n = recv_wrapper(c, list, rev);

    if (n <= 0) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "failed recv.");
        return;
    }

    /* get request length */
    ssize_t request_length = find_request_length(list->curr);
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
        m = recv_wrapper(c, list, rev);

        if (m < 0) { 
            ngx_log_error(NGX_LOG_ALERT, c->log, 0, "failed recv.");
            return; 
        }

        n += m;
    }

    httplite_event_data_t *ev_data = ngx_pcalloc(c->pool, sizeof(httplite_event_data_t));
    if (!ev_data) {
        fprintf(stderr, "Unable to instantiate httplite_event_data_t pointer.\n");
        return;
    }
    ev_data->client = c;

    c->data = ev_data;

    httplite_send_request_to_upstream(list);
}

ssize_t find_request_length(httplite_request_slab_t *slab) {
    u_char *str, *header, *end_ptr;
    size_t header_size, l;
    ssize_t body_size;

    str = slab->buffer;
    header = ngx_strlcasestrn(str, str + slab->size, (u_char*) LENGTH_HEADER, LENGTH_HEADER_SIZE - 1);
    /* From nginx documentation:
    *       ngx_strlcasestrn() is intended to search for static substring
    *       with known length in string until the argument last. The argument n
    *       must be length of the second substring - 1.
    */

    if (header == NULL) {
        return NGX_ERROR;
    }

    header += LENGTH_HEADER_SIZE;
    l = 0;

    /* find size of substring containing value after the header */
    while (*(header + l) != '\r' && *(header + l) != '\n') {
        ++l;
    }

    body_size = ngx_atosz(header, l);

    /* find index of header/body separator */ 
    end_ptr = (u_char*) ngx_strstr(header + l, HEADER_BODY_SEPARATOR);
            
    if (end_ptr == NULL) {
        return NGX_ERROR;
    }
    else {
        header_size = end_ptr + HEADER_BODY_SEPARATOR_SIZE - str;
        return body_size + header_size;
    }

    return NGX_ERROR;
}
