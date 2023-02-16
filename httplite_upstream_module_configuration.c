#include <nginx.h>
#include <ngx_core.h>
#include "httplite_module.h"
#include "httplite_http_module.h"
#include "httplite_upstream.h"
#include "httplite_module_configuration.h"
#include "httplite_upstream_module_configuration.h"

#define CONNECTION_KEYWORD "connection"
#define DEFAULT_CONNECTIONS 5

char *
httplite_core_upstream(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy)
{
    char                        *rv;
    ngx_conf_t                   pcf;
    httplite_configuration_context_t         *ctx;
    httplite_upstream_configuration_t *cucf;

    
    ctx = cf->ctx;
    cucf = ctx->upstream_configuration[httplite_http_module.ctx_index];
    cucf->ctx = ctx;

    /* parse inside upstream{} */

    pcf = *cf;
    cf->ctx = ctx;
    cf->cmd_type = HTTPLITE_UPSTREAM_CONFIGURATION;

    rv = ngx_conf_parse(cf, NULL);

    *cf = pcf;

    return rv;
}

void *
httplite_create_upstream_configuration(ngx_conf_t *cf)
{
    httplite_upstream_configuration_t  *cucf;

    cucf = ngx_pcalloc(cf->pool, sizeof(httplite_upstream_configuration_t));
    if (cucf == NULL) {
        return NULL;
    }

    cucf->connection_pool = ngx_pcalloc(cf->pool, sizeof(httplite_connection_pool_t));
    if (!cucf->connection_pool) {
        fprintf(stderr, "Unable to allocate space for connection pool.\n");
        return NULL;
    }

    cucf->connection_pool->upstream_pools = ngx_array_create(cf->pool, 4, sizeof(httplite_upstream_pool_t));
    cucf->connection_pool->pool_index = 0;

    cucf->balancing_algorithm = 0;
    cucf->pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, cf->log);

    return cucf;
}

char* httplite_parse_upstream_server(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy) {
    httplite_upstream_configuration_t *cucf = httplite_conf_get_module_upstream_conf(cf, httplite_http_module);
    ngx_str_t *value;
    char *upstream_server, *port_str;
    ngx_uint_t i, port, server_len;
    ngx_uint_t num_connections = DEFAULT_CONNECTIONS;
    
    httplite_upstream_pool_t *upstream_pool;

    value = cf->args->elts;

    upstream_server = (char*)value[1].data;
    port_str = strstr(upstream_server, ":") + 1;
    port = atoi(port_str);

    /* parsing the port number from the first argument */
    server_len = port_str - upstream_server;
    char server[server_len];
    memset(server, 0, server_len);
    strncpy(server, upstream_server, server_len - 1);

    /* parsing inline arguments */
    for (i = 2; i < cf->args->nelts; i++) {
        if (ngx_strcmp("connection=", value[i].data)) {
            value[i].len -= 12;
            value[i].data += 12;

            num_connections = ngx_atoi(value[i].data, value[i].len);
        }
    }

    upstream_pool = ngx_array_push(cucf->connection_pool->upstream_pools);
    upstream_pool->upstreams = ngx_array_create(cucf->pool, num_connections, sizeof(httplite_upstream_t));
    upstream_pool->upstream_index = 0;

    for (i = 0; i < num_connections; i++)  {
        httplite_create_upstream(upstream_pool->upstreams, server, port, cucf->pool);
    }

    return NGX_CONF_OK;
}

/*
static char *
ngx_tcp_upstream_check(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) 
{
    ngx_tcp_upstream_srv_conf_t  *uscf = conf;

    ngx_str_t   *value, s;
    ngx_uint_t   i, rise, fall;
    ngx_msec_t   interval, timeout;

    rise = 2;
    fall = 5;
    interval = 30000;
    timeout = 1000;

    value = cf->args->elts;

    for (i = 1; i < cf->args->nelts; i++) {

        if (ngx_strncmp(value[i].data, "type=", 5) == 0) {
            s.len = value[i].len - 5;
            s.data = value[i].data + 5;

            uscf->check_type_conf = ngx_tcp_get_check_type_conf(&s);

            if ( uscf->check_type_conf == NULL) {
                goto invalid_check_parameter;
            }

            continue;
        }

        if (ngx_strncmp(value[i].data, "interval=", 9) == 0) {
            s.len = value[i].len - 9;
            s.data = value[i].data + 9;

            interval = ngx_atoi(s.data, s.len);
            if (interval == (ngx_msec_t) NGX_ERROR) {
                goto invalid_check_parameter;
            }

            continue;
        }

        if (ngx_strncmp(value[i].data, "timeout=", 8) == 0) {
            s.len = value[i].len - 8;
            s.data = value[i].data + 8;

            timeout = ngx_atoi(s.data, s.len);
            if (timeout == (ngx_msec_t) NGX_ERROR) {
                goto invalid_check_parameter;
            }

            continue;
        }

        if (ngx_strncmp(value[i].data, "rise=", 5) == 0) {
            s.len = value[i].len - 5;
            s.data = value[i].data + 5;

            rise = ngx_atoi(s.data, s.len);
            if (rise == (ngx_uint_t) NGX_ERROR) {
                goto invalid_check_parameter;
            }

            continue;
        }

        if (ngx_strncmp(value[i].data, "fall=", 5) == 0) {
            s.len = value[i].len - 5;
            s.data = value[i].data + 5;

            fall = ngx_atoi(s.data, s.len);
            if (fall == (ngx_uint_t) NGX_ERROR) {
                goto invalid_check_parameter;
            }

            continue;
        }

        goto invalid_check_parameter;
    }

    uscf->check_interval = interval;
    uscf->check_timeout = timeout;
    uscf->fall_count = fall;
    uscf->rise_count = rise;

    if (uscf->check_type_conf == NULL) {
        s.len = sizeof("tcp") - 1;
        s.data =(u_char *) "tcp";

        uscf->check_type_conf = ngx_tcp_get_check_type_conf(&s);
    }

    return NGX_CONF_OK;

invalid_check_parameter:

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "invalid parameter \"%V\"", &value[i]);

    return NGX_CONF_ERROR;
}
*/
