#ifndef HTTPLITE_HTTP_REQUEST_H
#define HTTPLITE_HTTP_REQUEST_H

#include <nginx.h>
#include <ngx_core.h>
#include <ngx_event.h>

#include "httplite_upstream.h"

#define SLAB_SIZE 1500                              /* MTU size */

/**
 * @returns new httplite linked list of slabs, where each slab contains a
 * pointer to a SLAB_SIZE string buffer, using the given connection.
*/
httplite_request_list_t *httplite_init_list(ngx_connection_t *connection);

void httplite_free_list(httplite_request_list_t *list);

/**
 * Adds a node to the given list given the httplite_request_list
 * 
 * @returns A pointer to the new slab in the list
*/
httplite_request_slab_t *httplite_add_slab(httplite_request_list_t *list);

void httplite_request_handler(ngx_event_t *rev);
void httplite_close_connection(ngx_connection_t *c);

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
size_t recv_wrapper(ngx_connection_t *c, httplite_request_list_t *request, ngx_event_t *rev);

void httplite_client_handle_wakeup(ngx_event_t *event);

#endif
