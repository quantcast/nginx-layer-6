#ifndef HTTPLITE_UPSTREAMS_MODULE_CONFIGURATION_H
#define HTTPLITE_UPSTREAMS_MODULE_CONFIGURATION_H

#include <nginx.h>
#include <ngx_core.h>

#include "httplite_module_configuration.h"

#define HTTPLITE_UPSTREAM_CONFIGURATION         0x08000000

#define HTTPLITE_UPSTREAM_CONFIGURATION_OFFSET      offsetof(httplite_configuration_context_t, upstream_configuration)

#define DEFAULT_KEEP_ALIVE                      30000

#define httplite_conf_get_module_upstream_conf(cf, module)                        \
    ((httplite_configuration_context_t *) cf->ctx)->upstream_configuration[module.ctx_index]

#define httplite_get_upstream_conf(c) ((c)->listening->servers)

typedef struct httplite_connection_pool_s {
    ngx_array_t    *upstream_pools;
    int             pool_index;
} httplite_connection_pool_t;

typedef struct httplite_upstream_pool_s {
    ngx_array_t    *upstreams;
    int             upstream_index;
} httplite_upstream_pool_t;

typedef struct {
    httplite_configuration_context_t   *ctx;
    httplite_connection_pool_t         *connection_pool;
    load_balance_method_t               balancing_algorithm;
    ngx_pool_t                         *pool;
    ngx_int_t                           keep_alive;
} httplite_upstream_configuration_t;

char* httplite_core_upstream(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy);
void* httplite_create_upstream_configuration(ngx_conf_t *cf);
char* httplite_parse_upstream_server(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy);

#endif
