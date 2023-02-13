#include <nginx.h>
#include <ngx_core.h>
#include "httplite_module.h"
#include "httplite_http_module.h"
#include "httplite_upstream.h"
#include "httplite_module_configuration.h"

#include "httplite_upstream_module_configuration.h"

char *
httplite_core_upstream(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy)
{
    char                        *rv;
    ngx_conf_t                   pcf;
    httplite_configuration_context_t         *ctx;
    httplite_upstream_configuration_t *cucf;

    
    ctx = cf->ctx;
    cucf = ctx->upstream_configuration[httplite_http_module.ctx_index];
    cucf->ctx = ctx;

    /* parse inside upstream{} */

    pcf = *cf;
    cf->ctx = ctx;
    cf->cmd_type = HTTPLITE_UPSTREAM_CONFIGURATION;

    rv = ngx_conf_parse(cf, NULL);

    *cf = pcf;

    return rv;
}

void *
httplite_create_upstream_configuration(ngx_conf_t *cf)
{
    httplite_upstream_configuration_t  *cucf;

    cucf = ngx_pcalloc(cf->pool, sizeof(httplite_upstream_configuration_t));
    if (cucf == NULL) {
        return NULL;
    }

    cucf->balancing_algorithm = NGX_CONF_UNSET;
    ngx_array_init(&cucf->upstreams, cf->pool, 1, sizeof(httplite_upstream_t));
    cucf->pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, cf->log);
    cucf->upstream_index = 0;

    return cucf;
}

char* httplite_parse_upstream_server(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy) {
    httplite_upstream_configuration_t *cucf = httplite_conf_get_module_upstream_conf(cf, httplite_http_module);
    ngx_str_t *value;
    value = cf->args->elts;

    char *upstream_server = (char*)value[1].data;
    char *port = strstr(upstream_server, ":") + 1;

    int server_len = port - upstream_server;
    char server[server_len];
    memset(server, 0, server_len);
    strncpy(server, upstream_server, server_len - 1);

    httplite_create_upstream(cucf, server, atoi(port));

    return NGX_CONF_OK;
}
