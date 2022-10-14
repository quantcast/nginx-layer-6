#include <nginx.h>
#include <ngx_core.h>
#include <ngx_config.h>
#include "presentation_module_configuration.h";
#include "presentation_http_module_configuration.h"

#include "presentation_http_module.h";

static ngx_command_t presentation_http_block_commands[] = {
    {
        ngx_string(PORT_KEYWORD),
        PRESENTATION_MAIN_CONFIGURATION | NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        PRESENTATION_MAIN_CONFIGURATION_OFFSET,
        0,
        NULL 
    }
};

static presentation_module_t presentation_http_block_module_context = {
  NULL,                                     /* preconfiguration */
  presentation_http_block_initialization,                         /* postconfiguration */
  presentation_http_block_create_main_configuration,             /* create main configuration */
  NULL,                                     /* init main configuration */
};

/* options for the module */
ngx_module_t ngx_my_http_block_module = {
  NGX_MODULE_V1,
  &presentation_http_block_module_context,  /* module context */
  presentation_http_block_commands,         /* module directives */
  PRESENTATION_MODULE,                /* module type */
  NULL,                               /* init master */
  NULL,                               /* init module */
  NULL,                               /* init process */
  NULL,                               /* init thread */
  NULL,                               /* exit thread */
  NULL,                               /* exit process */
  NULL,                               /* exit master */
  NGX_MODULE_V1_PADDING
};
