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
    void                        *mconf;
    ngx_uint_t                   i;
    ngx_conf_t                   pcf;
    httplite_module_t           *module;
    httplite_configuration_context_t         *ctx, *http_ctx;
    httplite_upstream_configuration_t *cscf;

    ctx = ngx_pcalloc(cf->pool, sizeof(httplite_configuration_context_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    http_ctx = cf->ctx;
    ctx->main_configuration = http_ctx->main_configuration;

    /* the upstream{}'s upstream_conf */

    ctx->upstream_configuration = ngx_pcalloc(cf->pool, sizeof(void *));
    if (ctx->upstream_configuration == NULL) {
        return NGX_CONF_ERROR;
    }

    for (i = 0; cf->cycle->modules[i]; i++) {
        if (cf->cycle->modules[i]->type != HTTPLITE_MODULE) {
            continue;
        }

        module = cf->cycle->modules[i]->ctx;

        if (module->create_upstream_configuration) {
            mconf = module->create_upstream_configuration(cf);
            if (mconf == NULL) {
                return NGX_CONF_ERROR;
            }

            ctx->upstream_configuration[cf->cycle->modules[i]->ctx_index] = mconf;
        }
    }

    /* the upstream configuration context */

    cscf = ctx->upstream_configuration[httplite_http_module.ctx_index];
    cscf->ctx = ctx;

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

    cucf = ngx_pcalloc(cf->pool, sizeof(httplite_server_conf_t));
    if (cucf == NULL) {
        return NULL;
    }

    cucf->balancing_algorithm = NGX_CONF_UNSET;
    ngx_array_init(&cucf->upstreams, cf->pool, 1, sizeof(httplite_upstream_t));
    cucf->pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, cf->log);

    return cucf;
}

char* httplite_parse_upstream_server(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy) {
    // httplite_upstream_configuration_t *cucf = httplite_conf_get_module_upstream_conf(cf, httplite_http_module);
    ngx_str_t *value;
    value = cf->args->elts;

    char *upstream_server = (char*)value[1].data;
    char *port = strstr(upstream_server, ":") + 1;

    int server_len = port - upstream_server;
    char server[server_len];
    memset(server, 0, server_len);
    strncpy(server, upstream_server, server_len - 1);

    printf("server: %s\n", server);
    printf("port: %d\n", atoi(port));

    // httplite_upstream_t *upstream = ngx_array_push(&cucf->upstreams);
    // *upstream = *httplite_create_upstream(cf->pool, server, atoi(port));
    // httplite_initialize_upstream_connection(upstream);

    return NGX_CONF_OK;
}
