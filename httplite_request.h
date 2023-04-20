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
    u_char *start;                          /* A pointer that points to the beginning of the request string */
    u_char *buffer;                         /* A pointer to the memory holding the current position in the request string */
    struct httplite_request_slab_s *next;   /* A pointer to the next slab in the linked list */
    httplite_upstream_t *upstream;          /* An (optional) pointer to the upstream to be sent to */
    size_t size;                            /* The number of bytes that have been filled in the slab */
} httplite_request_slab_t;

typedef struct httplite_request_list_s {
    /* The request string is contained in a list of slabs */
    httplite_request_slab_t *head;              /* A pointer to the head of the request string list */
    httplite_request_slab_t *tail;              /* A pointer to the tail of the request string list */
    ngx_connection_t *connection;
    struct httplite_request_list_s *next;       /* A pointer to next request in pipeline queue */
} httplite_request_list_t;

typedef struct {
    ngx_connection_t *client_connection;
    ngx_connection_t *upstream_connection;
    httplite_request_slab_t *request;
    httplite_request_slab_t *response;
} httplite_event_connection_t;

/* Data about the connection that is maintained between calls to the request handler*/
typedef struct httplite_client_data_s {
    httplite_request_list_t *read_list;             /* Where the raw request strings are received into */
    httplite_request_list_t *write_list;            /* Head of the list where each node is a separate request */

    /* The following are utility fields used by request splitting */
    httplite_request_list_t *write_list_tail;       /* Tail node of the chain of parsed requests */
    httplite_request_list_t *staging_list;          /* Intermediate holding area for fragmented headers */
    size_t bytes_remaining;                         /* For the current request, how many bytes are left to be copied */
    size_t step_number;                             /* Keeps track of which step in the request parsing we are on */
    size_t pending_read_slabs;                      /* If this gets too large, shuts down the connection */
} httplite_request_data_t;

/* ------------------------------------------------------------------------------
                        Connection and upstream functions
   ---------------------------------------------------------------------------- */
void httplite_request_handler(ngx_event_t *rev);
void httplite_close_connection(ngx_connection_t *c);
void httplite_send_request_to_upstream(httplite_upstream_t *upstream, httplite_request_slab_t *request);
void httplite_client_handle_wakeup(ngx_event_t *event);


/* ------------------------------------------------------------------------------
                            List helper functions
   ---------------------------------------------------------------------------- */
/**
 * Adds a node to the given list given the httplite_request_list
 * 
 * @returns A pointer to the new slab in the list
*/
httplite_request_slab_t *httplite_add_slab(httplite_request_list_t *list);

/**
 * @returns new httplite linked list of slabs, where each slab contains a
 * pointer to a SLAB_SIZE string buffer, using the given connection.
*/
httplite_request_list_t *httplite_init_list(ngx_connection_t *connection);

/**
* @returns a new list pointed to by the provided list
*/
httplite_request_list_t *httplite_add_list_to_chain(httplite_request_list_t *list, ngx_connection_t *c);


/* ------------------------------------------------------------------------------
                            Request splitting functions
   ---------------------------------------------------------------------------- */
/**
* given a request_data, parses request_data->read_list into separate requests
* populates them as nodes in write_list
* there will always be one empty node at the end of write list
*/
void split_request(httplite_request_data_t *request_data, ngx_connection_t *c);

/**
 * copies "size" bytes starting from read_start_ptr (within read_slab's buffer) into write_list
 * adds slabs to write_list if necessary
 * advances read_start_ptr as needed to get all read_size bytes
 * when done, read_start_ptr will point to end of copied region
*/
void copy_to_list(httplite_request_list_t *write_list, size_t read_size, 
                        httplite_request_slab_t *read_slab, u_char **read_start_ptr);


/* ------------------------------------------------------------------------------
                                Testing functions
   ---------------------------------------------------------------------------- */
void printRequests (httplite_request_list_t *requests);
void printRequest (httplite_request_list_t *request);

#endif
