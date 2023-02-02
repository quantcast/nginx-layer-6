#include <nginx.h>
#include <ngx_core.h>
#include <ngx_string.h>
#include <ngx_event.h>

#include "httplite_request.h"

#define LENGTH_HEADER "\nContent-Length: "
#define LENGTH_HEADER_SIZE strlen(LENGTH_HEADER)
#define HEADER_BODY_SEPARATOR "\r\n\r\n"
#define HEADER_BODY_SEPARATOR_SIZE strlen(HEADER_BODY_SEPARATOR)

#include "httplite_upstream.h"
#include "httplite_load_balancer.h"

typedef struct {
    ngx_connection_t *client_connection;
    ngx_connection_t *upstream_connection;
    httplite_request_slab_t *request;
} httplite_event_connection_t;

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

void httplite_empty_read_handler() {
    printf("we are ready to read from the client!\n");
}

void httplite_empty_write_handler() {
    printf("we are ready to write to the client!\n");
}

void httplite_client_handle_wakeup(ngx_event_t *event) {
    httplite_event_connection_t *connections = event->data;
    ngx_connection_t *client = connections->client_connection;
    ngx_connection_t *upstream = connections->upstream_connection;

    if (!upstream->read->ready) {
        ngx_add_timer(event, 1000);
        return;
    }

    ssize_t                    n;
    httplite_request_list_t    list;
    httplite_request_slab_t   *curr;

    list = httplite_init_list(client);

    curr = list.head;
    
    if (list.head == NULL) {
        ngx_log_error(NGX_LOG_ALERT, client->log, 0, "unable to allocate space for the request slab.");
        httplite_close_connection(client);
        return;
    }

    if (list.head->buffer == NULL) {
        ngx_log_error(NGX_LOG_ALERT, client->log, 0, "unable to allocate space for the request string.");
        httplite_close_connection(client);
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
    n = upstream->recv(upstream, curr->buffer, SLAB_SIZE);
    curr->size = n;

    client->send(client, curr->buffer, curr->size);

    upstream->read->handler = httplite_empty_read_handler;
}

void httplite_client_write_handler(ngx_event_t *event) {
    printf("we are inside the handler\n");
    httplite_event_connection_t *connections = event->data;
    ngx_connection_t *client = connections->client_connection;
    ngx_connection_t *upstream = connections->upstream_connection;

    if (!upstream->read->ready) {
        printf("inside the not read ready clause\n");
        ngx_event_t *timer_event = ngx_pcalloc(client->pool, sizeof(ngx_event_t));
        if (timer_event == NULL) {
            fprintf(stderr, "Unable to allocate space for timer event.\n");
            return;
        }

        timer_event->handler = httplite_client_handle_wakeup;
        timer_event->data = event->data;
        timer_event->log = client->log;

        ngx_add_timer(timer_event, 1000);
        return;
    }

    ssize_t                    n;
    httplite_request_list_t    list;
    httplite_request_slab_t   *curr;

    list = httplite_init_list(client);

    curr = list.head;
    
    if (list.head == NULL) {
        ngx_log_error(NGX_LOG_ALERT, client->log, 0, "unable to allocate space for the request slab.");
        httplite_close_connection(client);
        return;
    }

    if (list.head->buffer == NULL) {
        ngx_log_error(NGX_LOG_ALERT, client->log, 0, "unable to allocate space for the request string.");
        httplite_close_connection(client);
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
    n = upstream->recv(upstream, curr->buffer, SLAB_SIZE);
    curr->size = n;

    client->send(client, curr->buffer, curr->size);

    upstream->read->handler = httplite_empty_read_handler;
}

/* Assumes incoming slab is empty (writes to buffer pointer, overwriting anything there) */
size_t recv_wrapper(ngx_connection_t *c, httplite_request_slab_t *slab, ngx_event_t *rev) {
    int n;

    const int NUM_UPSTREAMS = ((httplite_upstream_configuration_t*)(c->listening->servers))->upstreams.nelts;
    static int upstream_index = 0;
    
    n = c->recv(c, slab->buffer, SLAB_SIZE);

    httplite_upstream_configuration_t *upstream_configuration = c->listening->servers;
    httplite_upstream_t *upstream_elements = upstream_configuration->upstreams.elts;
    
    // NOTE: Possible race condition below
    httplite_upstream_t *upstream = &upstream_elements[upstream_index++ % NUM_UPSTREAMS];

    httplite_refresh_upstream_connection(upstream);
    ngx_connection_t *upstream_connection = upstream->peer.connection;

    httplite_event_connection_t *connections = ngx_pcalloc(c->pool, sizeof(httplite_event_connection_t));
    if (!connections) {
        fprintf(stderr, "Unable to instantiate httplite_event_connection_t pointer.\n");
        return NGX_ERROR;
    }

    connections->client_connection = c;
    connections->upstream_connection = upstream_connection;

    c->data = connections;

    c->read->handler = httplite_empty_read_handler;
    c->write->handler = httplite_empty_write_handler;

    httplite_send_request_to_upstream(upstream, slab);

    if (n == NGX_AGAIN) {
        if (!rev->timer_set) {
            ngx_add_timer(rev, 60 * 1000);
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

    slab->size += n;

    c->log->action = "reading client request line";

    ngx_reusable_connection(c, 0);

    return n;
}

// TODO: update this function to reflect new request structure
void httplite_request_handler(ngx_event_t *rev) {
    ssize_t                    n;
    httplite_request_list_t    list;
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
