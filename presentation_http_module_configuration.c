#include <nginx.h>
#include <ngx_core.h>
#include "presentation_module.h"
#include "presentation_module_configuration.h"

ngx_int_t presentation_http_block_initialization(ngx_conf_t *configuration) {
    // associating configuration with module
    presentation_main_configuration_t* presentation_main_configuration =
        presentation_conf_get_module_main_conf(
            configuration, 
            presentation_module
        );
    
    printf("initalization: %lu\n", presentation_main_configuration->port);

    return NGX_OK;
}

void* presentation_http_block_create_main_configuration(ngx_conf_t* configuration) {
    presentation_main_configuration_t* presentation_main_configuration;
    presentation_main_configuration = ngx_pcalloc(
        configuration->pool, sizeof(presentation_main_configuration_t)
    );
    
    if (presentation_main_configuration == NULL) {
        return NULL;
    }

    presentation_main_configuration->port = NGX_CONF_UNSET_UINT;

    return presentation_main_configuration;
}
