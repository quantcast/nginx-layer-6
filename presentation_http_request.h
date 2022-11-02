#ifndef PRESENTATION_HTTP_REQUEST_H
#define PRESENTATION_HTTP_REQUEST_H

#include <nginx.h>
#include <ngx_core.h>
#include <ngx_event.h>

typedef struct presentation_request_s {
    ngx_pool_t  *pool;      /* The pool in which this memory lies */
    u_char      *start;     /* A pointer to the start of the request string */
    u_char      *end;       /* A pointer to the end of allocated memory for the string */
    u_char      *last;      /* A pointer to the last character of the request string */
    u_char      *body;      /* A pointer to the request body (start < body <= end <= last)*/
    size_t       size;      /* The amount of memory allocated for the request string */
} presentation_request_t;

/**
 * Allocates memory for the presentation request string on the given pool of an
 * nginx connection. The request string starts with the given size. Allocates this
 * structure in the pool as well, and hence must be freed after.
 *
 * NOTE: If the connection is closed, the pool is no longer valid.
*/
presentation_request_t *init_presentation_request(ngx_pool_t *pool, size_t size);

/**
 * Allocates more space for the string in the given request object. Increases the
 * size by the given amount. Frees the old allocated memory.
 */
int presentation_request_realloc(presentation_request_t *request, ssize_t size);

/**
 * Frees all memory associated with this request.
*/
int presentation_request_free(presentation_request_t *request);

void presentation_http_request_handler(ngx_event_t *rev);
void presentation_http_request_close_connection(ngx_connection_t *c);

/* Helper function to call the recv function with error checking */
size_t recv_wrapper(ngx_connection_t *c, presentation_request_t *request, ngx_event_t *rev);
int find_request_length(presentation_request_t *request);


#endif