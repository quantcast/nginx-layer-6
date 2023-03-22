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
    ngx_connection_t *connection;
   struct httplite_request_list_s *next;                 /* Points to next request in pipeline queue */
} httplite_request_list_t;

typedef struct httplite_connection_s {
    ngx_connection_t *ngx_connection;
    ngx_queue_t request_q;                  /* maintains order of pipelined requests */
} httplite_connection_t;

// typedef struct httplite_request_queue_s {
//     httplite_request_list_t *head;
// } httplite_request_queue_t;


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
void ngx_httplite_close_connection(ngx_connection_t *c);

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

/**
 * Determines if given string is an HTTP method name.
 * 
 * @returns 1 if str is an HTTP method, 0 if str is not an HTTP method
*/
size_t check_http_method(u_char *str);
httplite_request_list_t *split_request (httplite_request_list_t *read_list, httplite_request_list_t *write_list);
/**
 * Used to free a list that is not needed anymore by using ngx_free() instead of ngx_pfree() which is used to
 * free large allocations
 * @cite: https://www.nginx.com/resources/wiki/extending/api/alloc/
*/
void httplite_free_list (httplite_request_list_t *list);

/**
 * Used to free a slab that is not needed anymore by using ngx_free() instead of ngx_pfree() which is used to
 * free large allocations
 * @cite: https://www.nginx.com/resources/wiki/extending/api/alloc/
*/
void httplite_free_slab (httplite_request_slab_t *slab);
void printRequests (httplite_request_list_t *requests);
void printRequest(httplite_request_list_t *request);

#endif
