#ifndef HTTPLITE_HTTP_REQUEST_H
#define HTTPLITE_HTTP_REQUEST_H

#include <nginx.h>
#include <ngx_core.h>
#include <ngx_event.h>

#include "httplite_upstream.h"

#define SLAB_SIZE 1500                              /* MTU size */
#define DEFAULT_SERVER_TIMEOUT          (60*1000)   /* Default timeout for server to be read ready */
#define DEFAULT_CLIENT_WRITE_TIMEOUT    (60*1000)   /* Default timeout for server to be read ready */

enum HTTP_method{GET, POST};

typedef struct httplite_request_slab_s {
    u_char *buffer;                         /* A pointer to the memory holding the request string */
    struct httplite_request_slab_s *next;   /* A pointer to the next slab in the linked list */
    httplite_upstream_t *upstream;          /* An (optional) pointer to the upstream to be sent to */
    size_t size;                            /* The number of bytes that have been filled in the slab */
} httplite_request_slab_t;

typedef struct httplite_request_list_s {
    httplite_request_slab_t *head;
    httplite_request_slab_t *tail;
    ngx_connection_t *connection;
   struct httplite_request_list_s *next;                 /* Points to next request in pipeline queue */
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
httplite_request_list_t *httplite_init_list(ngx_connection_t *connection);

/**
 * recursively copies "size" bytes starting from read_start_ptr (within read_slab's buffer) into write_list
 * adds slabs to write_list if necessary
 * advances read_slab and read_start_ptr as needed to get all "size" bytes
 * when done, read_slab and read_start_ptr will point to end of copied region
 * (read_start_ptr points to within read_slab's buffer)
*/
void copy_to_list(httplite_request_list_t *write_list, size_t size, httplite_request_slab_t **read_slab, u_char **read_start_ptr);


/**
 * Adds a node to the given list given the httplite_request_list
 * 
 * @returns A pointer to the new slab in the list
*/
httplite_request_slab_t *httplite_add_slab(httplite_request_list_t *list);

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
size_t recv_wrapper(ngx_connection_t *c, httplite_request_slab_t *request, ngx_event_t *rev);

httplite_request_list_t *split_request (httplite_request_list_t *read_list, httplite_request_list_t *write_list,
                                        ngx_connection_t *c);

/**
 * Used to free a slab that is not needed anymore by using ngx_free() instead of ngx_pfree() which is used to
 * free large allocations
 * @cite: https://www.nginx.com/resources/wiki/extending/api/alloc/
*/
void httplite_free_slab (httplite_request_slab_t *slab);
void printRequests (httplite_request_list_t *requests);
void printRequest(httplite_request_list_t *request);
void httplite_client_handle_wakeup(ngx_event_t *event);

#endif
