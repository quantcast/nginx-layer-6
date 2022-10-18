#include <nginx.h>
#include <ngx_core.h>
#include "presentation_http_server.h"

#include "presentation_module_configuration.h"

ngx_uint_t presentation_max_module_count;

const int PORT = 0xB822; /* PORT 8888 (decimal to hex) in little endian */

char* presentation_block(
    ngx_conf_t *configuration, 
    ngx_command_t *command, 
    void *base_configuration
) {
    char                                *rv;
    ngx_uint_t                           m, module_index;
    ngx_conf_t                           pcf;
    presentation_module_t                *module;
    presentation_configuration_context_t *context;

    /* the main http context */
    context = ngx_pcalloc(configuration->pool, sizeof(presentation_configuration_context_t));
    if (context == NULL) {
        return NGX_CONF_ERROR;
    }

    *(presentation_configuration_context_t **) base_configuration = context;

    /* count all modules and set up their indices */
    presentation_max_module_count = 0;
    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != PRESENTATION_MODULE) {
            continue;
        }

        ngx_modules[m]->ctx_index = presentation_max_module_count++;
    }

    /* the presentation module main context */
    context->main_configuration = ngx_pcalloc(configuration->pool, presentation_max_module_count * sizeof(void*));
    if (context->main_configuration == NULL) {
        return NGX_CONF_ERROR;
    }

    /* create the main configuration */
    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != PRESENTATION_MODULE) {
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

    configuration->module_type = PRESENTATION_MODULE;
    configuration->cmd_type = PRESENTATION_MAIN_CONFIGURATION;
    rv = ngx_conf_parse(configuration, NULL);

    if (rv != NGX_CONF_OK) {
        *configuration = pcf;
        return rv;
    }

    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != PRESENTATION_MODULE) {
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

    if (presentation_http_server_init_listening(configuration, PORT) != NGX_OK) {
        printf("Failed to init connection\n");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}