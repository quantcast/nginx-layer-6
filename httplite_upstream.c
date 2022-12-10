#include <nginx.h>
#include <ngx_core.h>

#include "httplite_http_module.h"
#include "httplite_upstream.h"
#include "httplite_upstream_module_configuration.h"

httplite_upstream_t *httplite_create_upstream(httplite_upstream_configuration_t *uscf, char *address, ngx_int_t port) {
    httplite_upstream_t *upstream = ngx_array_push(&uscf->upstreams);

    if (!upstream) {
        return NULL;
    }

    struct sockaddr_in *socket_address;
    size_t socket_length = sizeof(struct sockaddr_in);
    socket_address = ngx_pcalloc(uscf->pool, socket_length);
    if (socket_address == NULL) {
        fprintf(stderr, "Failed to allocate socket address\n");
        return NULL;
    }
    ngx_memzero(socket_address, socket_length);
    socket_address->sin_family = AF_INET;
    #if __APPLE__
        socket_address->sin_len = socket_length;
    #endif
    socket_address->sin_port = htons(port);
    inet_pton(AF_INET, address, &socket_address->sin_addr);

    ngx_str_t *name = ngx_pcalloc(uscf->pool, sizeof(ngx_str_t));
    name->data = ngx_pnalloc(uscf->pool, 7);
    name->data = (u_char *)"server";
    name->len = 6;

    upstream->pool = uscf->pool;
    upstream->peer.sockaddr = (struct sockaddr*)socket_address;
    upstream->peer.socklen = socket_length;
    upstream->peer.name = name;
    upstream->peer.get = ngx_event_get_peer;
    upstream->peer.log = NULL;
    upstream->peer.log_error = NGX_ERROR_ERR;

    return upstream;
}

ngx_int_t httplite_free_upstream(httplite_upstream_t* upstream) {
    ngx_pfree(upstream->pool, upstream->peer.name->data);
    ngx_pfree(upstream->pool, upstream->peer.name);

    if (ngx_pfree(upstream->pool, upstream) != NGX_OK) {
        fprintf(stderr, "Failed to deallocate httplite upstream\n");
        return NGX_DECLINED;
    }
    return NGX_OK;
}

void httplite_initialize_upstream_connection(httplite_upstream_t *upstream) {
    ngx_int_t result = ngx_event_connect_peer(&upstream->peer);
    if (result != NGX_OK) {
        fprintf(stderr, "Something went wrong when creating connection.\n");
        ngx_pfree(upstream->pool, upstream);
    }
}

void httplite_send_request_to_upstream(httplite_upstream_t *upstream, httplite_request_slab_t *request) {
    ngx_connection_t *connection = upstream->peer.connection;
    connection->send(connection, request->buffer, request->size);
}
