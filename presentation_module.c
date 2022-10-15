#include <nginx.h>
#include <ngx_core.h>
#include "presentation_module_configuration.h"

#include "presentation_module.h"

ngx_command_t presentation_module_commands[] = {
  {
    ngx_string(PRESENTATION_BLOCK_KEYWORD),
    NGX_MAIN_CONF | NGX_CONF_BLOCK | NGX_CONF_NOARGS,
    presentation_block,
    0,
    0,
    NULL
  },
  ngx_null_command
};

ngx_core_module_t presentation_module_context = {
    ngx_string(PRESENTATION_BLOCK_KEYWORD),
    NULL,
    NULL
};

ngx_module_t presentation_module = {
    NGX_MODULE_V1,
    &presentation_module_context,          /* module context */
    presentation_module_commands,          /* module directives */
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
