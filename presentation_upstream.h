#ifndef PRESENTATION_UPSTREAM_H
#define PRESENTATION_UPSTREAM_H

#include <nginx.h>
#include <ngx_string.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <ngx_string.h>

#include "presentation_http_request.h"

typedef struct presentation_upstream_s {
    ngx_peer_connection_t peer;
    ngx_pool_t *pool;
} presentation_upstream_t;

presentation_upstream_t *presentation_create_upstream(
    ngx_pool_t *pool, char *address, ngx_int_t port);
ngx_int_t presentation_free_upstream(presentation_upstream_t* upstream);
void presentation_initialize_upstream_connection(presentation_upstream_t *upstream);
void presentation_send_request_to_upstream(presentation_upstream_t *upstream, presentation_request_t *request);

#endif