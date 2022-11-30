#ifndef HTTPLITE_SERVER_CONFIGURATION_H
#define HTTPLITE_SERVER_CONFIGURATION_H

#include <nginx.h>
#include <ngx_core.h>

typedef struct {
    ngx_flag_t  enable; //TODO: replace flag
    ngx_str_t   server_name;
}   httplite_server_main_conf_t;

#define HTTPLITE_SRV_CONF_OFFSET   offsetof(ngx_http_conf_ctx_t, srv_conf)

ngx_int_t httplite_server_initialization(ngx_conf_t *configuration);
void* httplite_server_block_create_main_configuration(ngx_conf_t* configuration);

// typedef struct ngx_command_s ngx_command_t;

// struct ngx_command_s {
//     ngx_str_t       
// }



#endif