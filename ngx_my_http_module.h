#ifndef NGX_MY_HTTP_MODULE_H
#define NGX_MY_HTTP_MODULE_H

#include <nginx.h>
#include <ngx_core.h>
#include <ngx_config.h>

// macros

// bitmask constants
#define NGX_MY_HTTP_MODULE                      0x3659414C /* "LAY6" */
#define NGX_MY_HTTP_MAIN_CONFIGURATION          0x02000000
#define NGX_MY_HTTP_SERVER_CONFIGURATION        0x04000000
#define NGX_MY_HTTP_LOCATION_CONFIGURATION      0x08000000

// config macros
#define NGX_MY_HTTP_MAIN_CONFIGURATION_OFFSET       offsetof(ngx_my_http_configuration_context_t, main_configuration)

// function to set main configuration to module
#define ngx_my_http_conf_get_module_main_conf(cf, module)                        \
    ((ngx_my_http_configuration_context_t *) cf->ctx)->main_configuration[module.ctx_index]

// structs

// configuration context
typedef struct {
    void        **main_configuration;
} ngx_my_http_configuration_context_t;


typedef struct {
    ngx_int_t   (*preconfiguration)(ngx_conf_t *configuration);
    ngx_int_t   (*postconfiguration)(ngx_conf_t *configuration);

    void       *(*create_main_configuration)(ngx_conf_t *configuration);
    char       *(*init_main_configuration)(ngx_conf_t *configuration, void *base_configuration);
} ngx_my_http_module_t;

typedef struct {
    ngx_uint_t port;
} ngx_my_http_main_configuration_t;

typedef struct {
    /* ngx_http_in_addr_t or ngx_http_in6_addr_t */
    void                      *addrs;
    ngx_uint_t                 naddrs;
} ngx_my_http_port_t;

extern ngx_module_t ngx_my_http_module;

#endif
