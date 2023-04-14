#include <nginx.h>
#include <ngx_core.h>
#include <ngx_config.h>
#include "httplite_module_configuration.h"
#include "httplite_http_module_configuration.h"
#include "httplite_upstream_module_configuration.h"

#include "httplite_http_module.h"

ngx_command_t httplite_http_commands[] = {
    {
        ngx_string("server"),
        HTTPLITE_MAIN_CONFIGURATION | NGX_CONF_BLOCK | NGX_CONF_NOARGS,
        httplite_core_server,
        HTTPLITE_MAIN_CONFIGURATION_OFFSET,
        0,
        NULL 
    },

    {
        ngx_string("listen"),
        HTTPLITE_SERVER_CONFIGURATION | NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        HTTPLITE_SERVER_CONFIGURATION_OFFSET,
        offsetof(httplite_server_conf_t, port),
        NULL 
    },

    {
        ngx_string("server_name"),
        HTTPLITE_SERVER_CONFIGURATION | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        HTTPLITE_SERVER_CONFIGURATION_OFFSET,
        offsetof(httplite_server_conf_t, server_name),
        NULL 
    },

    {
        ngx_string("upstreams"),
        HTTPLITE_MAIN_CONFIGURATION | NGX_CONF_BLOCK | NGX_CONF_NOARGS,
        httplite_core_upstream,
        HTTPLITE_MAIN_CONFIGURATION_OFFSET,
        0,
        NULL 
    },

    {
        ngx_string("server"),
        HTTPLITE_UPSTREAM_CONFIGURATION | NGX_CONF_1MORE,
        httplite_parse_upstream_server,
        HTTPLITE_UPSTREAM_CONFIGURATION_OFFSET,
        0,
        NULL
    },

    {
        ngx_string("keep_alive"),
        HTTPLITE_UPSTREAM_CONFIGURATION | NGX_CONF_1MORE,
        ngx_conf_set_num_slot,
        HTTPLITE_UPSTREAM_CONFIGURATION_OFFSET,
        offsetof(httplite_upstream_configuration_t, keep_alive),
        NULL
    },

    ngx_null_command
};

httplite_module_t httplite_http_module_context = {
  NULL,                                          /* preconfiguration */
  httplite_http_block_initialization,            /* postconfiguration */
  httplite_http_block_create_main_configuration, /* create main configuration */
  NULL,                                          /* init main configuration */
  httplite_core_create_server_configuration,     /* create server configuration */
  NULL,                                          /* merge server configuration */
  httplite_create_upstream_configuration,        /* create upstream configuration */
  NULL,                                          /* merge location configuration */
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
