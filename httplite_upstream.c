#include <nginx.h>
#include <ngx_core.h>

#include "httplite_http_module.h"
#include "httplite_request.h"
#include "httplite_upstream.h"
#include "httplite_upstream_module_configuration.h"

static void httplite_empty_upstream_handler() { }

httplite_upstream_t *httplite_create_upstream(httplite_upstream_configuration_t *uscf, char *address, ngx_int_t port) {
    httplite_upstream_t *upstream;
    struct sockaddr_in *socket_address;
    size_t socket_length;
    ngx_str_t *name;
    
    // upstream = ngx_array_push(&uscf->upstreams);

    // if (!upstream) {
    //     return NULL;
    // }

    socket_length = sizeof(struct sockaddr_in);
    socket_address = ngx_pcalloc(uscf->pool, socket_length);

    if (socket_address == NULL) {
        fprintf(stderr, "Failed to allocate socket address\n");
        return NULL;
    }

    socket_address->sin_family = AF_INET;
#if (NGX_DARWIN)
    socket_address->sin_len = socket_length;
#endif
    socket_address->sin_port = htons(port);

    inet_pton(AF_INET, address, &socket_address->sin_addr);

    name = ngx_pcalloc(uscf->pool, sizeof(ngx_str_t));
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

void httplite_refresh_upstream_connection(httplite_upstream_t *upstream) {
    // TODO: Add testing logic to check if the connection is already made
    ngx_int_t result = ngx_event_connect_peer(&upstream->peer);

    if (result != NGX_OK && result != NGX_AGAIN) {
        fprintf(stderr, "Something went wrong when creating connection.\n");
        ngx_pfree(upstream->pool, upstream);
    }
}

void httplite_handle_send_request_to_upstream(ngx_event_t *event) {
    ngx_connection_t     *c;
    httplite_request_slab_t *r;
    httplite_upstream_t *u;

    c = event->data;
    r = ((httplite_event_connection_t*) c->data)->request;

    u = r->upstream;
    c = u->peer.connection;

    c->send(c, r->buffer, r->size);

    // after sending the message, prevent more of the same messages from sending
    c->write->handler = httplite_empty_upstream_handler;
}

void httplite_send_request_to_upstream(httplite_upstream_t *upstream, httplite_request_slab_t *request) {
    ngx_connection_t *connection = upstream->peer.connection;

    if (!connection) {
        fprintf(stderr, "the connection is null!\n");
        // reinitialize the connection
        // add an event so that after reinitialization we resend to upstream
        return;
    }
    
    ((httplite_event_connection_t*) connection->data)->request = request;
    request->upstream = upstream;
    
    connection->write->handler = httplite_handle_send_request_to_upstream;
    if (connection->write->ready) {
        connection->send(connection, request->buffer, request->size);
    }
}
