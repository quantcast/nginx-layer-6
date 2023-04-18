#include <nginx.h>
#include <ngx_core.h>

#include "httplite_module_configuration.h"

ngx_uint_t httplite_max_module_count;

char* httplite_block(
    ngx_conf_t *cf, 
    ngx_command_t *cmd, 
    void *base_configuration
) {
    char                                *rv;
    ngx_uint_t                           m, module_index;
    ngx_conf_t                           pcf;
    httplite_module_t                   *module;
    httplite_configuration_context_t    *ctx;

    /* the main http context */
    ctx = ngx_pcalloc(cf->pool, sizeof(httplite_configuration_context_t));

    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    *(httplite_configuration_context_t **) base_configuration = ctx;

    /* count all modules and set up their indices */
    httplite_max_module_count = 0;
    for (m = 0; ngx_modules[m]; m++) {

        if (ngx_modules[m]->type != HTTPLITE_MODULE) {
            continue;
        }

        ngx_modules[m]->ctx_index = httplite_max_module_count++;
    }

    /* the httplite module main context */
    ngx_uint_t max_module_size = httplite_max_module_count * sizeof(void*);
    ctx->main_configuration = ngx_pcalloc(cf->pool, max_module_size);
    ctx->server_configuration = ngx_pcalloc(cf->pool, max_module_size);
    ctx->upstream_configuration = ngx_pcalloc(cf->pool, max_module_size);

    if (ctx->main_configuration == NULL || ctx->server_configuration == NULL || ctx->upstream_configuration == NULL) {
        return NGX_CONF_ERROR;
    }

    /* create the main cf */
    for (m = 0; ngx_modules[m]; m++) {

        if (ngx_modules[m]->type != HTTPLITE_MODULE) {
            continue;
        }

        module = ngx_modules[m]->ctx;
        module_index = ngx_modules[m]->ctx_index;

        if (module->create_main_configuration) {
            ctx->main_configuration[module_index] = module->create_main_configuration(cf);

            if (ctx->main_configuration[module_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
        if (module->create_upstream_configuration) {
            ctx->upstream_configuration[module_index] = module->create_upstream_configuration(cf);

            if (ctx->upstream_configuration[module_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
        if (module->create_server_configuration) {
            ctx->server_configuration[module_index] = module->create_server_configuration(cf);

            if (ctx->server_configuration[module_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }

    /* parse inside the httplite{} block */
    pcf = *cf;
    cf->ctx = ctx;

    cf->module_type = HTTPLITE_MODULE;
    cf->cmd_type = HTTPLITE_MAIN_CONFIGURATION;
    rv = ngx_conf_parse(cf, NULL);

    if (rv != NGX_CONF_OK) {
        *cf = pcf;
        return rv;
    }

    for (m = 0; ngx_modules[m]; m++) {

        if (ngx_modules[m]->type != HTTPLITE_MODULE) {
            continue;
        }

        module = ngx_modules[m]->ctx;
        module_index = ngx_modules[m]->ctx_index;

        /* init tcp{} main_configurationss */
        cf->ctx = ctx;

        if (module->init_main_configuration) {
            rv = module->init_main_configuration(cf, ctx->main_configuration[module_index]);

            if (rv != NGX_CONF_OK) {
                *cf = pcf;
                return rv;
            }
        }
    }

    for (m = 0; ngx_modules[m]; m++) {

        if (ngx_modules[m]->type != HTTPLITE_MODULE) {
            continue;
        }

        module = ngx_modules[m]->ctx;
        module_index = ngx_modules[m]->ctx_index;

        /* init tcp{} main_configurationss */
        cf->ctx = ctx;

        if (module->postconfiguration) {
            if (module->postconfiguration(cf) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
    }

    *cf = pcf;

    return NGX_CONF_OK;
}
