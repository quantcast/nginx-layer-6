#ifndef HTTPLITE_UPSTREAMS_MODULE_CONFIGURATION_H
#define HTTPLITE_UPSTREAMS_MODULE_CONFIGURATION_H

#include <nginx.h>
#include <ngx_core.h>

#define HTTPLITE_UPSTREAM_CONFIGURATION      0x08000000

#define HTTPLITE_UPSTREAM_CONFIGURATION_OFFSET      offsetof(httplite_configuration_context_t, upstream_configuration)

#define httplite_conf_get_module_upstream_conf(cf, module)                        \
    ((httplite_configuration_context_t *) cf->ctx)->upstream_configuration[module.ctx_index]

typedef struct {
    httplite_configuration_context_t   *ctx;
    ngx_array_t                         upstreams;
    load_balance_method_t               balancing_algorithm;
} httplite_upstream_configuration_t;

char* httplite_core_upstream(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy);
void* httplite_create_upstream_configuration(ngx_conf_t *cf);
char* httplite_parse_upstream_server(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy);

#endif
