#include <nginx.h>
#include <ngx_core.h>
#include "httplite_module_configuration.h"

#include "httplite_module.h"

ngx_command_t httplite_module_commands[] = {
  {
    ngx_string(HTTPLITE_BLOCK_KEYWORD),
    NGX_MAIN_CONF | NGX_CONF_BLOCK | NGX_CONF_NOARGS,
    httplite_block,
    0,
    0,
    NULL
  },
  ngx_null_command
};

ngx_core_module_t httplite_module_context = {
    ngx_string(HTTPLITE_BLOCK_KEYWORD),
    NULL,
    NULL
};

ngx_module_t httplite_module = {
    NGX_MODULE_V1,
    &httplite_module_context,          /* module context */
    httplite_module_commands,          /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};
