#ifndef HTTPLITE_HTTP_REQUEST_H
#define HTTPLITE_HTTP_REQUEST_H

#include <nginx.h>
#include <ngx_core.h>
#include <ngx_event.h>

#define SLAB_SIZE 1500      /* MTU size */

typedef struct httplite_request_slab_s {
    u_char *buffer;                         /* A pointer to the memory holding the request string */
    struct httplite_request_slab_s *next;   /* A pointer to the next slab in the linked list */
    size_t size;                            /* The number of bytes that have been filled in the slab */
} httplite_request_slab_t;

typedef struct httplite_request_list_s {
    httplite_request_slab_t *head;
    httplite_request_slab_t *tail;
    ngx_connection_t *connection;           /* A pointer to the parent connection */
} httplite_request_list_t;

/**
 * Creates a new httplite linked list of slabs, where each slab contains a
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
void ngx_httplite_close_connection(ngx_connection_t *c);

ssize_t find_request_length(httplite_request_slab_t *slab);
size_t recv_wrapper(ngx_connection_t *c, httplite_request_slab_s *request, ngx_event_t *rev);

#endif
