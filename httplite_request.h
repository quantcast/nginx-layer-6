#ifndef HTTPLITE_HTTP_REQUEST_H
#define HTTPLITE_HTTP_REQUEST_H

#include <nginx.h>
#include <ngx_core.h>
#include <ngx_event.h>

#include "httplite_upstream.h"
#include "httplite_request_list.h"

enum HTTP_method{ GET, POST };

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

void httplite_request_handler(ngx_event_t *rev);
void httplite_close_connection(ngx_connection_t *c);

/* ------------------------------------------------------------------------------
                        Connection and upstream functions
   ---------------------------------------------------------------------------- */
void httplite_request_handler(ngx_event_t *rev);
void httplite_close_connection(ngx_connection_t *c);
void httplite_send_request_list(httplite_request_data_t *request_data);

/* ------------------------------------------------------------------------------
                            Request splitting function
   ---------------------------------------------------------------------------- */
/**
* given a request_data, parses request_data->read_list into separate requests
* populates them as nodes in write_list
* there will always be one empty node at the end of write list
*/
void httplite_split_request(httplite_request_data_t *request_data, ngx_connection_t *c);

/* ------------------------------------------------------------------------------
                                Testing functions
   ---------------------------------------------------------------------------- */
void httplite_print_requests (httplite_request_list_t *requests);
void httplite_print_request (httplite_request_list_t *request);

#endif
