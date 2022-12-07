#include <nginx.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_http.h>
#include <string.h>
#include <ngx_socket.h>

#include "httplite_http_module.h"
#include "httplite_module_configuration.h"
#include "httplite_server.h"
#include "httplite_upstream.h"
#include "httplite_upstream_module_configuration.h"

u_char* httplite_server_log_error(ngx_log_t *log, u_char *buf, size_t len)
{
    return NGX_OK;
}

void httplite_server_empty_handler(ngx_event_t *wev)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, wev->log, 0, "http empty handler");
}

void httplite_server_init_connection(ngx_connection_t *c)
{
    ngx_event_t               *rev;

    httplite_upstream_configuration_t **upstream_configuration = ((ngx_array_t*)c->listening->servers)->elts;
    
    c->log->connection = c->number;
    c->log->handler = httplite_server_log_error;
    c->log->data = NULL;
    c->log->action = "waiting for request";

    c->log_error = NGX_ERROR_INFO;

    rev = c->read;
    rev->handler = httplite_request_handler;
    c->write->handler = httplite_server_empty_handler;

    if (rev->ready) {
        /* the deferred accept(), iocp */

        if (ngx_use_accept_mutex) {
            ngx_post_event(rev, &ngx_posted_events);
            return;
        }

        rev->handler(rev);

        return;
    }

    ngx_add_timer(rev, 60 * 1000);
    ngx_reusable_connection(c, 1);

    ngx_array_t upstreams_array = (*upstream_configuration)->upstreams;

    printf("num elements: %lu\n", upstreams_array.nelts);
    for (ngx_uint_t i = 0; i < upstreams_array.nelts; i++) {
        httplite_upstream_t *upstream = ((httplite_upstream_t**)upstreams_array.elts)[i];
        httplite_initialize_upstream_connection(upstream);
    }

    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        ngx_httplite_close_connection(c);
        return;
    }
}

ngx_int_t httplite_server_init_listening(ngx_conf_t *cf, ngx_int_t port)
{
    ngx_listening_t *ls;
    struct sockaddr_in *socket_address;
    size_t socket_length = sizeof(struct sockaddr_in);

    socket_address = ngx_pcalloc(cf->pool, socket_length);
    
    if (socket_address == NULL) {
        printf("Failed to allocate socket address\n");
        return NGX_ERROR;
    }
    
    ngx_memzero(socket_address, socket_length);
    socket_address->sin_family = AF_INET;
    socket_address->sin_port = htons(port);
    socket_address->sin_len = socket_length;
    socket_address->sin_addr.s_addr = INADDR_ANY;

    
    ls = ngx_create_listening(cf, (struct sockaddr*)socket_address, socket_length);
    ls->servers = ngx_array_create(cf->pool, 4, sizeof(httplite_upstream_configuration_t*));
    httplite_upstream_configuration_t** upstream_configuration = (httplite_upstream_configuration_t**) ngx_array_push(ls->servers);
    *upstream_configuration = httplite_conf_get_module_upstream_conf(cf, httplite_http_module);
    
    if (ls == NULL) {
        printf("Failed to create listening socket\n");
        return NGX_ERROR;
    }

    ls->addr_ntop = 1;

    ls->handler = httplite_server_init_connection;
    ls->pool_size = 512;

    ls->logp = cf->log;
    ls->log.data = &ls->addr_text;
    ls->log.handler = httplite_server_log_error;

    ls->backlog = -1;
    ls->rcvbuf = SO_RCVBUF;
    ls->sndbuf = SO_SNDBUF;

    ls->keepalive = 1;

    return NGX_OK;
}
