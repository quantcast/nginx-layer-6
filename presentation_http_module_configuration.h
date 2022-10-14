#ifndef PRESENTATION_HTTP_MODULE_CONFIGURATION_H
#define PRESENTATION_HTTP_MODULE_CONFIGURATION_H

#include <nginx.h>
#include <ngx_core.h>

static ngx_int_t presentation_http_block_initialization(ngx_conf_t *configuration);
static void* presentation_http_block_create_main_configuration(ngx_conf_t* configuration);

#endif