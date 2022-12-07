#include <nginx.h>
#include <ngx_core.h>
#include "httplite_module.h"
#include "httplite_http_module.h"
#include "httplite_server.h"
#include "httplite_module_configuration.h"
#include "httplite_upstream_module_configuration.h"

ngx_int_t httplite_http_block_initialization(ngx_conf_t *configuration, void* conf) {
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
    ngx_conf_t                   pcf;
    httplite_configuration_context_t         *ctx;
    httplite_server_conf_t    *cscf;

    /* the server configuration context */

    ctx = cf->ctx;
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

    if (httplite_server_init_listening(cf, cscf->port) != NGX_OK) {
        fprintf(stderr, "Failed to init connection\n");
        return NGX_CONF_ERROR;
    }

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
