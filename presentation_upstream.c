#include <nginx.h>
#include <ngx_core.h>

#include "presentation_upstream.h"

presentation_upstream_t *presentation_create_upstream(
    ngx_pool_t *pool, char *address, ngx_int_t port) {
    presentation_upstream_t *upstream = ngx_pcalloc(pool, sizeof(presentation_upstream_t));
    if (!upstream) {
        return NULL;
    }

    struct sockaddr_in *socket_address;
    size_t socket_length = sizeof(struct sockaddr_in);
    socket_address = ngx_pcalloc(pool, socket_length);
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

    ngx_str_t *name = ngx_pcalloc(pool, sizeof(ngx_str_t));
    name->data = ngx_pnalloc(pool, 7);
    name->data = (u_char *)"server";
    name->len = 6;

    upstream->pool = pool;
    upstream->peer.sockaddr = (struct sockaddr*)socket_address;
    upstream->peer.socklen = socket_length;
    upstream->peer.name = name;
    upstream->peer.get = ngx_event_get_peer;
    upstream->peer.log = NULL;
    upstream->peer.log_error = NGX_ERROR_ERR;

    return upstream;
}

ngx_int_t presentation_free_upstream(presentation_upstream_t* upstream) {
    ngx_pfree(upstream->pool, upstream->peer.name->data);
    ngx_pfree(upstream->pool, upstream->peer.name);

    if (ngx_pfree(upstream->pool, upstream) != NGX_OK) {
        fprintf(stderr, "Failed to deallocate presentation upstream\n");
        return NGX_DECLINED;
    }
    return NGX_OK;
}

void presentation_initialize_upstream_connection(presentation_upstream_t *upstream) {
    ngx_int_t result = ngx_event_connect_peer(&upstream->peer);
    if (result != NGX_OK) {
        fprintf(stderr, "Something went wrong when creating connection.\n");
        ngx_pfree(upstream->pool, upstream);
    }
}

void presentation_send_request_to_upstream(presentation_upstream_t *upstream, presentation_request_t *request) {
    ngx_connection_t *connection = upstream->peer.connection;
    connection->send(connection, request->start, request->size);
}
