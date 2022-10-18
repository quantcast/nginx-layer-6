#ifndef PRESENTATION_HTTP_REQUEST_H
#define PRESENTATION_HTTP_REQUEST_H

#include <nginx.h>
#include <ngx_core.h>
#include <ngx_event.h>

typedef struct presentation_request_s {
    ngx_pool_t  *pool;
    u_char      *start;
    u_char      *end;
    u_char      *last;
    size_t      len;
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
 * size of the request by a factor of 2. Frees the old allocated memory.
 */
int presentation_request_realloc(presentation_request_t *request);

/**
 * Frees all memory associated with this request.
*/
int presentation_request_free(presentation_request_t *request);

void presentation_http_request_handler(ngx_event_t *rev);
void presentation_http_request_close_connection(ngx_connection_t *c);

#endif