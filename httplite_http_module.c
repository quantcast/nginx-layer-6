#include <nginx.h>
#include <ngx_core.h>
#include <ngx_config.h>
#include "httplite_module_configuration.h"
#include "httplite_http_module_configuration.h"

#include "httplite_http_module.h"

ngx_command_t httplite_http_commands[] = {
    {
        ngx_string(PORT_KEYWORD),
        HTTPLITE_MAIN_CONFIGURATION | NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        HTTPLITE_MAIN_CONFIGURATION_OFFSET,
        0,
        NULL 
    }
};

httplite_module_t httplite_http_module_context = {
  NULL,                                     /* preconfiguration */
  httplite_http_block_initialization,                         /* postconfiguration */
  httplite_http_block_create_main_configuration,             /* create main configuration */
  NULL,                                     /* init main configuration */
};

/* options for the module */
ngx_module_t httplite_http_module = {
  NGX_MODULE_V1,
  &httplite_http_module_context,  /* module context */
  httplite_http_commands,         /* module directives */
  HTTPLITE_MODULE,                /* module type */
  NULL,                               /* init master */
  NULL,                               /* init module */
  NULL,                               /* init process */
  NULL,                               /* init thread */
  NULL,                               /* exit thread */
  NULL,                               /* exit process */
  NULL,                               /* exit master */
  NGX_MODULE_V1_PADDING
};
