#ifndef HTTPLITE_MODULE_CONFIGURATION_H
#define HTTPLITE_MODULE_CONFIGURATION_H


#include <nginx.h>
#include <ngx_core.h>
#include <ngx_config.h>

// macros


// bitmask constants
#define HTTPLITE_MODULE                      0x3659414C /* "LAY6" */
#define HTTPLITE_MAIN_CONFIGURATION          0x02000000
#define HTTPLITE_SERVER_CONFIGURATION        0x04000000

// config macros
#define HTTPLITE_MAIN_CONFIGURATION_OFFSET          offsetof(httplite_configuration_context_t, main_configuration)
#define HTTPLITE_SERVER_CONFIGURATION_OFFSET        offsetof(httplite_configuration_context_t, server_configuration)

// function to set main configuration to module
#define httplite_conf_get_module_main_conf(cf, module)                        \
    ((httplite_configuration_context_t *) cf->ctx)->main_configuration[module.ctx_index]

#define httplite_conf_get_module_server_conf(cf, module)                        \
    ((httplite_configuration_context_t *) cf->ctx)->server_configuration[module.ctx_index]

// structs

// configuration context
typedef struct {
    void        **main_configuration;
    void        **server_configuration;
    void        **upstream_configuration;
} httplite_configuration_context_t;

typedef struct {
    ngx_int_t   (*preconfiguration)(ngx_conf_t *configuration);
    ngx_int_t   (*postconfiguration)(ngx_conf_t *configuration);

    void       *(*create_main_configuration)(ngx_conf_t *configuration);
    char       *(*init_main_configuration)(ngx_conf_t *configuration, void *base_configuration);

    void       *(*create_server_configuration)(ngx_conf_t *configuration);
    char       *(*merge_server_configuration)(ngx_conf_t *configuration, void *prev, void *base_configuration);

    void       *(*create_upstream_configuration)(ngx_conf_t *configuration);
    char       *(*merge_upstream_configuration)(ngx_conf_t *configuration, void *prev, void *base_configuration);        
} httplite_module_t;

typedef struct {
} httplite_main_configuration_t;

typedef struct {
    /* ngx_http_in_addr_t or ngx_http_in6_addr_t */
    void                      *addrs;
    ngx_uint_t                 naddrs;
} httplite_port_t;

typedef struct {
    /* server ctx */
    httplite_configuration_context_t        *ctx;
    ngx_str_t                   server_name;
    ngx_uint_t                  port;
} httplite_server_conf_t;

typedef enum load_balance_method_e { round_robin } load_balance_method_t;

// functions 

char* httplite_block(
    ngx_conf_t *configuration, 
    ngx_command_t *command, 
    void *base_configuration
);

char * httplite_core_server(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy);

#endif
