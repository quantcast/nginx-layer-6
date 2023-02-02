#ifndef HTTPLITE_UPSTREAM_H
#define HTTPLITE_UPSTREAM_H

#include <nginx.h>
#include <ngx_string.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <ngx_string.h>

#include "httplite_upstream_module_configuration.h"

typedef struct httplite_upstream_s {
    ngx_peer_connection_t peer;
    ngx_pool_t *pool;
    // httplite_upstream_handler_t *handler;
} httplite_upstream_t;

// typedef void *httplite_upstream_handler_t(httplite_upstream_t *upstream, httplite_request_slab_t* slab);

httplite_upstream_t* httplite_create_upstream(httplite_upstream_configuration_t *uscf, char *address, ngx_int_t port);
ngx_int_t httplite_free_upstream(httplite_upstream_t* upstream);
void httplite_initialize_upstream_connection(httplite_upstream_t *upstream);

#endif
