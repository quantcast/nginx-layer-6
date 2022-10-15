#include <nginx.h>
#include <ngx_core.h>
#include "presentation_module.h"
#include "presentation_module_configuration.h"

ngx_int_t presentation_http_block_initialization(ngx_conf_t *configuration) {
    // associating configuration with module
    presentation_main_configuration_t* http_main_configuration =
        presentation_conf_get_module_main_conf(
            configuration, 
            presentation_module
        );
    
    printf("initalization: %lu\n", http_main_configuration->port);

    return NGX_OK;
}

void* presentation_http_block_create_main_configuration(ngx_conf_t* configuration) {
    presentation_main_configuration_t* my_http_configuration;
    my_http_configuration = ngx_pcalloc(
        configuration->pool, sizeof(presentation_main_configuration_t)
    );
    
    if (my_http_configuration == NULL) {
        return NULL;
    }

    my_http_configuration->port = NGX_CONF_UNSET_UINT;

    return my_http_configuration;
}
