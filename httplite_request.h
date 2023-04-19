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
    u_char *start;                         /* A pointer that points to the beginning of the request string */
    u_char *buffer;                            /* A pointer to the memory holding the current position in the request string */
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

typedef struct httplite_client_data_s {
    httplite_request_list_t *read_list;
    httplite_request_list_t *write_list_head;
    httplite_request_list_t *write_list;
    httplite_request_list_t *staging_list;
    size_t bytes_remaining;
    size_t step_number;
    /*
    0 = start
    1 = find separator
    2 = find Content Length header, value, and request type
    3 = go to end of body
    */
} httplite_request_data_t;

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
void copy_to_list_2(httplite_request_list_t *write_list, size_t read_size, httplite_request_slab_t *read_slab, u_char **read_start_ptr);


/**
 * Adds a node to the given list given the httplite_request_list
 * 
 * @returns A pointer to the new slab in the list
*/
httplite_request_slab_t *httplite_add_slab(httplite_request_list_t *list);

void httplite_request_handler(ngx_event_t *rev);
void httplite_close_connection(ngx_connection_t *c);

void httplite_send_request_to_upstream(httplite_upstream_t *upstream, httplite_request_slab_t *request);

httplite_request_list_t *split_request (httplite_request_list_t *read_list, httplite_request_list_t *write_list,
                                        ngx_connection_t *c);
void split_request_2(httplite_request_data_t *request_data, ngx_connection_t *c);

httplite_request_list_t *httplite_add_list_to_chain(httplite_request_list_t *list, ngx_connection_t *c);

void printRequests (httplite_request_list_t *requests);
void printRequest(httplite_request_list_t *request);
void httplite_client_handle_wakeup(ngx_event_t *event);

#endif
