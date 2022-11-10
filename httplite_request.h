#ifndef HTTPLITE_HTTP_REQUEST_H
#define HTTPLITE_HTTP_REQUEST_H

#include <nginx.h>
#include <ngx_core.h>
#include <ngx_event.h>

typedef struct httplite_request_s {
    ngx_pool_t  *pool;      /* The pool in which this memory lies */
    u_char      *start;     /* A pointer to the start of the request string */
    u_char      *end;       /* A pointer to the end of allocated memory for the string */
    u_char      *last;      /* A pointer to the last character of the request string */
    size_t       size;      /* The amount of memory allocated for the request string */
} httplite_request_t;

/**
 * Allocates memory for the httplite request string on the given pool of an
 * nginx connection. The request string starts with the given size. Allocates this
 * structure in the pool as well, and hence must be freed after.
 *
 * NOTE: If the connection is closed, the pool is no longer valid.
*/
httplite_request_t *init_httplite_request(ngx_pool_t *pool, size_t size);

/**
 * Allocates more space for the string in the given request object. Increases the
 * size of the request by a factor of 2. Frees the old allocated memory.
 */
int httplite_request_realloc(httplite_request_t *request);

/**
 * Frees all memory associated with this request.
*/
int httplite_request_free(httplite_request_t *request);

void httplite_request_handler(ngx_event_t *rev);
void httplite_request_close_connection(ngx_connection_t *c);

#endif
