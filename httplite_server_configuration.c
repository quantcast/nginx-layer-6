#include <nginx.h>
#include <ngx_core.h>
#include "httplite_server.h"
#include "httplite_server_configuration.h"



static void *httplite_server_create_conf(ngx_cycle_t *cycle);
static char *httplite_server_init_conf(ngx_cycle_t *cycle, void *conf);

ngx_command_t   httplite_server_commands[] = {

    { ngx_string("server"),
      HTTPLITE_SRV_CONF|NGX_CONF_BLOCK|NGX_CONF_TAKE1,
      httplite_server,
      HTTPLITE_SRV_CONF_OFFSET,
      0,    // TODO: verify offset
      NULL  // TODO: verify post field
    },

    ngx_null_command
}

ngx_int_t httplite_server_block_initialization(ngx_conf_t *configuration) {
    // associating configuration with module
    return NGX_OK;
}

void* httplite_server_block_create_conf(ngx_conf_t* configuration) {
    httplite_main_configuration_t* httplite_main_configuration;
    httplite_main_configuration = ngx_pcalloc(
        configuration->pool, sizeof(httplite_main_configuration_t)
    );
    
    if (httplite_main_configuration == NULL) {
        return NULL;
    }

    httplite_main_configuration->port = NGX_CONF_UNSET_UINT;

    return httplite_main_configuration;
}
