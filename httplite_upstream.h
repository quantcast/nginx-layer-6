#ifndef HTTPLITE_UPSTREAM_H
#define HTTPLITE_UPSTREAM_H

#include <nginx.h>
#include <ngx_string.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <ngx_string.h>

#include "httplite_upstream_module_configuration.h"

typedef struct httplite_request_slab_s {
    u_char *buffer;                         /* A pointer to the memory holding the request string */
    struct httplite_request_slab_s *next;   /* A pointer to the next slab in the linked list */
    size_t size;                            /* The number of bytes that have been filled in the slab */
} httplite_request_slab_t;

typedef struct httplite_request_list_s {
    httplite_request_slab_t *head;
    httplite_request_slab_t *tail;
    httplite_request_slab_t *curr;
    ngx_connection_t *connection;           /* A pointer to the parent connection */
} httplite_request_list_t;

typedef struct httplite_upstream_s {
    ngx_peer_connection_t       peer;
    ngx_pool_t                 *pool;
    httplite_request_list_t    *request;
    httplite_request_slab_t    *response;
    int                         active  : 1;
    int                         busy    : 1;
} httplite_upstream_t;

typedef struct {
    ngx_connection_t    *client_connection;
    httplite_upstream_t *upstream;
} httplite_event_connection_t;

httplite_upstream_t* httplite_create_upstream(ngx_array_t *arr, char *address, ngx_int_t port, ngx_pool_t *pool);
ngx_int_t httplite_free_upstream(httplite_upstream_t* upstream);
void httplite_refresh_upstream_connection(httplite_upstream_t *upstream, void *upstream_data);
httplite_upstream_t* fetch_upstream(httplite_connection_pool_t *c_pool);
int httplite_check_broken_connection(ngx_connection_t *c);
httplite_upstream_t *httplite_fetch_inactive_upstream(httplite_connection_pool_t *c_pool);
void httplite_send_request_to_upstream(httplite_request_list_t *request);

void httplite_upstream_read_handler(ngx_event_t *rev);
void httplite_upstream_write_handler(ngx_event_t *wev);

#endif
