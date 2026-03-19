#include <nginx.h>
#include <ngx_core.h>
#include "httplite_module.h"
#include "httplite_http_module.h"
#include "httplite_upstream.h"
#include "httplite_module_configuration.h"
#include "httplite_upstream_module_configuration.h"

#define CONNECTION_KEYWORD "connection"
#define DEFAULT_CONNECTIONS 5
#define DEFAULT_PORT 80

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
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                      "unable to allocate connection pool");
        return NULL;
    }
    cucf->connection_pool->pool = cf->pool;

    cucf->connection_pool->upstream_pools = ngx_array_create(cf->pool, 4, sizeof(httplite_upstream_pool_t));
    cucf->connection_pool->pool_index = 0;

    cucf->balancing_algorithm = 0;
    cucf->pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, cf->log);

    cucf->keep_alive = NGX_CONF_UNSET;

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
    port_str = strstr(upstream_server, ":");
    port = port_str ? atoi(port_str + 1) : DEFAULT_PORT;

    /* parsing the server address (everything before the ':') */
    server_len = port_str ? (ngx_uint_t)(port_str - upstream_server) : strlen(upstream_server);
    char server[server_len + 1];
    memset(server, 0, server_len + 1);
    strncpy(server, upstream_server, server_len);

    /* parsing inline arguments */
    for (i = 2; i < cf->args->nelts; i++) {
        if (ngx_strncmp("connections=", value[i].data, 12) == 0) {
            value[i].len -= 12;
            value[i].data += 12;

            num_connections = ngx_atoi(value[i].data, value[i].len);
        }
    }

    upstream_pool = ngx_array_push(cucf->connection_pool->upstream_pools);
    upstream_pool->upstreams = ngx_array_create(cucf->pool, num_connections, sizeof(httplite_upstream_t));
    upstream_pool->upstream_index = 0;

    for (i = 0; i < num_connections; i++)  {
        httplite_create_upstream(cucf->pool, upstream_pool->upstreams, server, port);
    }

    return NGX_CONF_OK;
}
