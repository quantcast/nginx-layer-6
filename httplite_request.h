#ifndef HTTPLITE_HTTP_REQUEST_H
#define HTTPLITE_HTTP_REQUEST_H

#include <nginx.h>
#include <ngx_core.h>
#include <ngx_event.h>

#include "httplite_upstream.h"

#define SLAB_SIZE 1500                              /* MTU size */
#define DEFAULT_SERVER_TIMEOUT          (60*1000)   /* Default timeout for server to be read ready */
#define DEFAULT_CLIENT_WRITE_TIMEOUT    (60*1000)   /* Default timeout for server to be read ready */

typedef struct httplite_request_slab_s {
    u_char *buffer;                         /* A pointer to the memory holding the request string */
    struct httplite_request_slab_s *next;   /* A pointer to the next slab in the linked list */
    httplite_upstream_t *upstream;          /* An (optional) pointer to the upstream to be sent to */
    size_t size;                            /* The number of bytes that have been filled in the slab */
} httplite_request_slab_t;

typedef struct httplite_request_list_s {
    httplite_request_slab_t *head;
    httplite_request_slab_t *tail;
    ngx_connection_t *connection;           /* A pointer to the parent connection */
} httplite_request_list_t;

typedef struct {
    ngx_connection_t *client_connection;
    ngx_connection_t *upstream_connection;
    httplite_request_slab_t *request;
    httplite_request_slab_t *response;
} httplite_event_connection_t;

/**
 * @returns new httplite linked list of slabs, where each slab contains a
 * pointer to a SLAB_SIZE string buffer, using the given connection.
*/
httplite_request_list_t httplite_init_list(ngx_connection_t *connection);

/**
 * Adds a node to the given list given the httplite_request_list
 * 
 * @returns A pointer to the new slab in the list
*/
httplite_request_slab_t *httplite_add_slab(httplite_request_list_t list);

void httplite_request_handler(ngx_event_t *rev);
void httplite_close_connection(ngx_connection_t *c);

void httplite_send_request_to_upstream(httplite_upstream_t *upstream, httplite_request_slab_t *request);

/**
 * Given a slab, looks at the buffer (assumed to contain all the headers of a request)
 * 
 * @returns Total request length = size of headers + size of body 
*/
ssize_t find_request_length(httplite_request_slab_t *slab);

/**
 * Reads from a connection into a slab buffer (overwrites contents)
 * 
 * @returns Number of bytes read
*/
size_t recv_wrapper(ngx_connection_t *c, httplite_request_slab_t *request, ngx_event_t *rev, ngx_atomic_t upstream_index);

void httplite_client_handle_wakeup(ngx_event_t *event);

#endif
