#include <nginx.h>
#include <ngx_core.h>
#include "httplite_module.h"
#include "httplite_http_module.h"
#include "httplite_module_configuration.h"

ngx_int_t httplite_http_block_initialization(ngx_conf_t *configuration) {
    // associating configuration with module
    return NGX_OK;
}

void* httplite_http_block_create_main_configuration(ngx_conf_t* configuration) {
    httplite_main_configuration_t* httplite_main_configuration;
    httplite_main_configuration = ngx_pcalloc(
        configuration->pool, sizeof(httplite_main_configuration_t)
    );
    
    if (httplite_main_configuration == NULL) {
        return NULL;
    }

    return httplite_main_configuration;
}

char *
httplite_core_server(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy)
{
    char                        *rv;
    void                        *mconf;
    ngx_uint_t                   i;
    ngx_conf_t                   pcf;
    httplite_module_t           *module;
    httplite_configuration_context_t         *ctx, *http_ctx;
    httplite_server_conf_t    *cscf;

    ctx = ngx_pcalloc(cf->pool, sizeof(httplite_configuration_context_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    http_ctx = cf->ctx;
    ctx->main_configuration = http_ctx->main_configuration;

    /* the server{}'s srv_conf */

    ctx->server_configuration = ngx_pcalloc(cf->pool, sizeof(void *));
    if (ctx->server_configuration == NULL) {
        return NGX_CONF_ERROR;
    }

    for (i = 0; cf->cycle->modules[i]; i++) {
        if (cf->cycle->modules[i]->type != HTTPLITE_MODULE) {
            continue;
        }

        module = cf->cycle->modules[i]->ctx;

        if (module->create_server_configuration) {
            mconf = module->create_server_configuration(cf);
            if (mconf == NULL) {
                return NGX_CONF_ERROR;
            }

            ctx->server_configuration[cf->cycle->modules[i]->ctx_index] = mconf;
        }
    }

    /* the server configuration context */

    cscf = ctx->server_configuration[httplite_http_module.ctx_index];
    cscf->ctx = ctx;

    /* parse inside server{} */

    pcf = *cf;
    cf->ctx = ctx;
    cf->cmd_type = HTTPLITE_SERVER_CONFIGURATION;

    rv = ngx_conf_parse(cf, NULL);

    *cf = pcf;

    printf("port: %lu\n", cscf->port);
    printf("server name: %s\n", cscf->server_name.data);
    fflush(stdout);

    return rv;
}

void *
httplite_core_create_server_configuration(ngx_conf_t *cf)
{
    httplite_server_conf_t  *cscf;

    cscf = ngx_pcalloc(cf->pool, sizeof(httplite_server_conf_t));
    if (cscf == NULL) {
        return NULL;
    }

    cscf->port = NGX_CONF_UNSET;
    cscf->server_name = (ngx_str_t)ngx_null_string;

    return cscf;
}
