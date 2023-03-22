#include <nginx.h>
#include <ngx_core.h>

#include "httplite_module_configuration.h"

ngx_uint_t httplite_max_module_count;

char* httplite_block(
    ngx_conf_t *configuration, 
    ngx_command_t *command, 
    void *base_configuration
) {
    char                                *parse_result;
    ngx_uint_t                           module_index;
    ngx_conf_t                           previous_configuration;
    httplite_module_t                *module;
    httplite_configuration_context_t *context;

    /* the main http context */
    context = ngx_pcalloc(configuration->pool, sizeof(httplite_configuration_context_t));

    if (context == NULL) {
        return NGX_CONF_ERROR;
    }

    *(httplite_configuration_context_t **) base_configuration = context;

    /* count all modules and set up their indices */
    httplite_max_module_count = 0;
    for (ngx_uint_t m = 0; ngx_modules[m]; m++) {

        if (ngx_modules[m]->type != HTTPLITE_MODULE) {
            continue;
        }

        ngx_modules[m]->ctx_index = httplite_max_module_count++;
    }

    /* the httplite module main context */
    ngx_uint_t max_module_size = httplite_max_module_count * sizeof(void*);
    context->main_configuration = ngx_pcalloc(configuration->pool, max_module_size);
    context->server_configuration = ngx_pcalloc(configuration->pool, max_module_size);
    context->upstream_configuration = ngx_pcalloc(configuration->pool, max_module_size);

    if (context->main_configuration == NULL || context->server_configuration == NULL || context->upstream_configuration == NULL) {
        return NGX_CONF_ERROR;
    }

    /* create the main configuration */
    for (ngx_uint_t m = 0; ngx_modules[m]; m++) {

        if (ngx_modules[m]->type != HTTPLITE_MODULE) {
            continue;
        }

        module = ngx_modules[m]->ctx;
        module_index = ngx_modules[m]->ctx_index;

        if (module->create_main_configuration) {
            context->main_configuration[module_index] = module->create_main_configuration(configuration);

            if (context->main_configuration[module_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
        if (module->create_upstream_configuration) {
            context->upstream_configuration[module_index] = module->create_upstream_configuration(configuration);

            if (context->upstream_configuration[module_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
        if (module->create_server_configuration) {
            context->server_configuration[module_index] = module->create_server_configuration(configuration);

            if (context->server_configuration[module_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }

    /* parse inside the httplite{} block */
    previous_configuration = *configuration;
    configuration->ctx = context;

    configuration->module_type = HTTPLITE_MODULE;
    configuration->cmd_type = HTTPLITE_MAIN_CONFIGURATION;
    parse_result = ngx_conf_parse(configuration, NULL);

    if (parse_result != NGX_CONF_OK) {
        *configuration = previous_configuration;
        return parse_result;
    }

    for (ngx_uint_t m = 0; ngx_modules[m]; m++) {

        if (ngx_modules[m]->type != HTTPLITE_MODULE) {
            continue;
        }

        module = ngx_modules[m]->ctx;
        module_index = ngx_modules[m]->ctx_index;

        /* init tcp{} main_configurationss */
        configuration->ctx = context;

        if (module->init_main_configuration) {
            parse_result = module->init_main_configuration(configuration, context->main_configuration[module_index]);

            if (parse_result != NGX_CONF_OK) {
                *configuration = previous_configuration;
                return parse_result;
            }
        }
    }

    for (ngx_uint_t m = 0; ngx_modules[m]; m++) {

        if (ngx_modules[m]->type != HTTPLITE_MODULE) {
            continue;
        }

        module = ngx_modules[m]->ctx;
        module_index = ngx_modules[m]->ctx_index;

        /* init tcp{} main_configurationss */
        configuration->ctx = context;

        if (module->postconfiguration) {
            if (module->postconfiguration(configuration) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
    }

    *configuration = previous_configuration;

    return NGX_CONF_OK;
}
