#include <nginx.h>
#include <ngx_core.h>
#include "httplite_upstream.h"
#include "httplite_module.h"
#include "httplite_http_module.h"
#include "httplite_server.h"
#include "httplite_module_configuration.h"
#include "httplite_upstream_module_configuration.h"

ngx_int_t httplite_http_block_initialization(ngx_conf_t *configuration) {
    httplite_server_conf_t *server_configuration = httplite_conf_get_module_server_conf(configuration, httplite_http_module);

    // associating configuration with module
    if (httplite_server_init_listening(configuration, server_configuration->port) != NGX_OK) {
        fprintf(stderr, "Failed to init connection\n");
        return NGX_ERROR;
    }
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
httplite_core_server(ngx_conf_t *configuration, ngx_command_t *command, void *dummy)
{
    char                        *parse_result;
    ngx_conf_t                   previous_configuration;
    httplite_configuration_context_t         *context;
    httplite_server_conf_t    *server_configuration;

    /* the server configuration context */

    context = configuration->ctx;
    server_configuration = context->server_configuration[httplite_http_module.ctx_index];
    server_configuration->ctx = context;

    /* parse inside server{} */

    previous_configuration = *configuration;
    configuration->ctx = context;
    configuration->cmd_type = HTTPLITE_SERVER_CONFIGURATION;

    parse_result = ngx_conf_parse(configuration, NULL);

    *configuration = previous_configuration;

    return parse_result;
}

void *
httplite_core_create_server_configuration(ngx_conf_t *configuration)
{
    httplite_server_conf_t  *server_configuration;

    server_configuration = ngx_pcalloc(configuration->pool, sizeof(httplite_server_conf_t));
    if (server_configuration == NULL) {
        return NULL;
    }

    server_configuration->port = NGX_CONF_UNSET;
    server_configuration->server_name = (ngx_str_t)ngx_null_string;

    return server_configuration;
}
