#include <nginx.h>
#include <ngx_core.h>
#include "httplite_module.h"
#include "httplite_module_configuration.h"

ngx_int_t httplite_http_block_initialization(ngx_conf_t *configuration) {
    // associating configuration with module
    httplite_main_configuration_t* httplite_main_configuration =
        httplite_conf_get_module_main_conf(
            configuration, 
            httplite_module
        );
    
    printf("initalization: %lu\n", httplite_main_configuration->port);

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

    httplite_main_configuration->port = NGX_CONF_UNSET_UINT;

    return httplite_main_configuration;
}
