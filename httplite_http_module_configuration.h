#ifndef HTTPLITE_HTTP_MODULE_CONFIGURATION_H
#define HTTPLITE_HTTP_MODULE_CONFIGURATION_H

#include <nginx.h>
#include <ngx_core.h>

ngx_int_t httplite_http_block_initialization(ngx_conf_t *configuration);
void* httplite_http_block_create_main_configuration(ngx_conf_t* configuration);

#endif
