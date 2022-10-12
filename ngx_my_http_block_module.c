#include <ngx_config.h>
#include <ngx_core.h>
#include <nginx.h>

#include "ngx_my_http_module.h"

static ngx_int_t ngx_my_http_block_initialization(ngx_conf_t *configuration);
static void* ngx_my_http_block_create_main_configuration(ngx_conf_t* configuration);

static ngx_command_t ngx_my_http_block_commands[] = {
    {
        ngx_string("mylisten"),
        NGX_MY_HTTP_MAIN_CONFIGURATION | NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_MY_HTTP_MAIN_CONFIGURATION_OFFSET,
        0,
        NULL 
    }
};

static ngx_my_http_module_t ngx_my_http_block_module_context = {
  NULL,                                     /* preconfiguration */
  ngx_my_http_block_initialization,                         /* postconfiguration */
  ngx_my_http_block_create_main_configuration,             /* create main configuration */
  NULL,                                     /* init main configuration */
};

/* options for the module */
ngx_module_t ngx_my_http_block_module = {
  NGX_MODULE_V1,
  &ngx_my_http_block_module_context,  /* module context */
  ngx_my_http_block_commands,         /* module directives */
  NGX_MY_HTTP_MODULE,                 /* module type */
  NULL,                               /* init master */
  NULL,                               /* init module */
  NULL,                               /* init process */
  NULL,                               /* init thread */
  NULL,                               /* exit thread */
  NULL,                               /* exit process */
  NULL,                               /* exit master */
  NGX_MODULE_V1_PADDING
};

static ngx_int_t ngx_my_http_block_initialization(ngx_conf_t *configuration) {
    // associating configuration with module
    ngx_my_http_main_configuration_t* http_main_configuration =
        ngx_my_http_conf_get_module_main_conf(
            configuration, 
            ngx_my_http_module
        );
    
    printf("initalization: %lu\n", http_main_configuration->port);

    return NGX_OK;
}

static void* ngx_my_http_block_create_main_configuration(ngx_conf_t* configuration) {
    ngx_my_http_main_configuration_t* my_http_configuration;
    my_http_configuration = ngx_pcalloc(
        configuration->pool, sizeof(ngx_my_http_main_configuration_t)
    );
    
    if (my_http_configuration == NULL) {
        return NULL;
    }

    my_http_configuration->port = NGX_CONF_UNSET_UINT;

    return my_http_configuration;
}
