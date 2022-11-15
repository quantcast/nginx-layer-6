#include <nginx.h>
#include <ngx_core.h>
#include "httplite_server.h"

#include "httplite_module_configuration.h"

ngx_uint_t httplite_max_module_count;

char* httplite_block(
    ngx_conf_t *configuration, 
    ngx_command_t *command, 
    void *base_configuration
) {
    char                                *rv;
    ngx_uint_t                           m, module_index;
    ngx_conf_t                           pcf;
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
    for (m = 0; ngx_modules[m]; m++) {

        if (ngx_modules[m]->type != HTTPLITE_MODULE) {
            continue;
        }

        ngx_modules[m]->ctx_index = httplite_max_module_count++;
    }

    /* the httplite module main context */
    context->main_configuration = ngx_pcalloc(configuration->pool, httplite_max_module_count * sizeof(void*));

    if (context->main_configuration == NULL) {
        return NGX_CONF_ERROR;
    }

    /* create the main configuration */
    for (m = 0; ngx_modules[m]; m++) {

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

    }

    /* parse inside the myhttp{} block */
    pcf = *configuration;
    configuration->ctx = context;

    configuration->module_type = HTTPLITE_MODULE;
    configuration->cmd_type = HTTPLITE_MAIN_CONFIGURATION;
    rv = ngx_conf_parse(configuration, NULL);

    if (rv != NGX_CONF_OK) {
        *configuration = pcf;
        return rv;
    }

    for (m = 0; ngx_modules[m]; m++) {

        if (ngx_modules[m]->type != HTTPLITE_MODULE) {
            continue;
        }

        module = ngx_modules[m]->ctx;
        module_index = ngx_modules[m]->ctx_index;

        /* init tcp{} main_configurationss */
        configuration->ctx = context;

        if (module->init_main_configuration) {
            rv = module->init_main_configuration(configuration, context->main_configuration[module_index]);

            if (rv != NGX_CONF_OK) {
                *configuration = pcf;
                return rv;
            }
        }
    }

    *configuration = pcf;

    if (httplite_http_server_init_listening(configuration, 0xB822) != NGX_OK) {
        printf("Failed to init connection\n");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}
