#ifndef PRESENTATION_MODULE_H
#define PRESENTATION_MODULE_H

#include <nginx.h>
#include <ngx_core.h>
#include <ngx_config.h>

// macros

// bitmask constants
#define PRESENTATION_MODULE                      0x3659414C /* "LAY6" */
#define PRESENTATION_MAIN_CONFIGURATION          0x02000000
#define PRESENTATION_SERVER_CONFIGURATION        0x04000000
#define PRESENTATION_LOCATION_CONFIGURATION      0x08000000

// config macros
#define PRESENTATION_MAIN_CONFIGURATION_OFFSET       offsetof(presentation_configuration_context_t, main_configuration)

// function to set main configuration to module
#define presentation_conf_get_module_main_conf(cf, module)                        \
    ((presentation_configuration_context_t *) cf->ctx)->main_configuration[module.ctx_index]

// structs

// configuration context
typedef struct {
    void        **main_configuration;
} presentation_configuration_context_t;


typedef struct {
    ngx_int_t   (*preconfiguration)(ngx_conf_t *configuration);
    ngx_int_t   (*postconfiguration)(ngx_conf_t *configuration);

    void       *(*create_main_configuration)(ngx_conf_t *configuration);
    char       *(*init_main_configuration)(ngx_conf_t *configuration, void *base_configuration);
} presentation_module_t;

typedef struct {
    ngx_uint_t port;
} presentation_main_configuration_t;

typedef struct {
    /* ngx_http_in_addr_t or ngx_http_in6_addr_t */
    void                      *addrs;
    ngx_uint_t                 naddrs;
} presentation_port_t;

extern ngx_module_t presentation_module;

#endif
