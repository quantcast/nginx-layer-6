#include <nginx.h>
#include <ngx_core.h>

#include "httplite_http_module.h"
#include "httplite_request.h"
#include "httplite_upstream.h"
#include "httplite_upstream_module_configuration.h"

#define HTTP_INACTIVE_UPSTREAM_RESPONSE "HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\nContent-Length: 41\r\n\r\nTrying to send to an inactive upstream.\r\n"
#define HTTP_UNABLE_TO_FIND_UPSTREAM_RESPONSE "HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\nContent-Length: 36\r\n\r\nUnable to find an active upstream.\r\n"
#define HTTP_503_RESPONSE "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n"
#define HTTPLITE_TRACE_ON 0
#define TRACEME(fmt, ...) if (HTTPLITE_TRACE_ON) printf("[%s @ %s:%d]\n"fmt, __func__, __FILE__, __LINE__, __VA_ARGS__)

void httplite_empty_handler() {}

httplite_upstream_t *httplite_create_upstream(ngx_pool_t *pool, ngx_array_t *arr, char *address, ngx_int_t port) {
    httplite_upstream_t *u;
    struct sockaddr_in *socket_address;
    size_t socket_length;
    ngx_str_t *name;
    
    u = ngx_array_push(arr);

    if (!u) {
        return NULL;
    }

    socket_length = sizeof(struct sockaddr_in);
    socket_address = ngx_pcalloc(pool, socket_length);

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

    name = ngx_pcalloc(pool, sizeof(ngx_str_t));
    name->data = ngx_pnalloc(pool, INET_ADDRSTRLEN);
    name->data = (u_char *) address;
    name->len = strlen(address) - 1;

    u->pool = pool;
    u->log = pool->log;
    u->peer.sockaddr = (struct sockaddr*)socket_address;
    u->peer.socklen = socket_length;
    u->peer.name = name;
    u->peer.get = ngx_event_get_peer;
    u->peer.log = pool->log;
    u->peer.log_error = NGX_ERROR_ERR;

    u->active = 0;
    u->pending_active = 0;
    u->busy = 0;
    u->keep_alive = 0;
    u->request = NULL;
    u->response = NULL;
    u->timer = ngx_pcalloc(pool, sizeof(ngx_event_t));
    u->data = ngx_pcalloc(pool, sizeof(httplite_event_data_t));

    return u;
}

int httplite_check_broken_connection(ngx_connection_t *c) {
    ngx_event_t *rev = c->read;

    if (!rev->pending_eof) {
        return NGX_OK;
    }

    ngx_log_debug0(NGX_LOG_INFO, c->log, 0, "broken connection found. closing.");

    rev->eof = 1;
    c->error = 1;

    return NGX_ERROR;
}

void httplite_deactivate_upstream(httplite_upstream_t *u) {
    static int i = 0;
    TRACEME("  deactivating upstream %p.\n  call #%d\n\n", u, ++i);

    if (!u) {
        fprintf(stderr, "trying to free an upstream that is null.\n");
        return;
    }

    u->active = 0;
    u->pending_active = 0;
    u->busy = 0;

    if (u->request) {
        ngx_pfree(u->request->connection->pool, u->request);
        u->request = NULL;
    }

    if (u->timer->timer_set) {
        ngx_del_timer(u->timer);
    }

    httplite_close_connection(u->peer.connection);
}

int httplite_send_request_to_upstream(httplite_request_list_t *request) {
    ngx_connection_t *peer_c;

    ngx_connection_t *client = request->connection;
    httplite_upstream_configuration_t *cucf = httplite_get_upstream_conf(client);
    httplite_upstream_t *u = fetch_upstream(cucf->connection_pool);

    if (httplite_check_broken_connection(client) != NGX_OK) {
        ngx_log_debug0(NGX_LOG_WARN, client->log, 0, "Client was closed during request processing.");
        httplite_close_connection(client);
        return NGX_ERROR;
    }
    
    if (u == NULL || ngx_event_ident(u->peer.connection) == -1) {
        return httplite_fetch_upstream_and_send_request(request);
    }

    peer_c = u->peer.connection;

    ((httplite_event_data_t*)u->data)->client = client;
    ((httplite_event_data_t*)u->data)->upstream = u;
    peer_c->data = u->data;

    u->pending_active = 0;
    u->busy = 1;
    u->request = request;

    TRACEME(
        "  receieved upstream: %p\n"
        "  setting request to: %p\n\n",
        u, request
    );

    // the connection is already established so we can delete the timer
    if (u->timer->timer_set) {
        ngx_del_timer(u->timer);
    }

    if (peer_c->write->ready) {
        httplite_upstream_write_handler(peer_c->write);
    } else {
        peer_c->write->handler = httplite_upstream_write_handler;
    }
    peer_c->read->handler = httplite_upstream_read_handler;

    return NGX_OK;
}

int httplite_fetch_upstream_and_send_request(httplite_request_list_t *request) {
    ngx_connection_t *client = request->connection;
    httplite_upstream_configuration_t *cucf = httplite_get_upstream_conf(client);
    httplite_upstream_t *u = httplite_fetch_inactive_upstream(cucf->connection_pool);

    if (httplite_check_broken_connection(client) != NGX_OK) {
        ngx_log_debug0(NGX_LOG_WARN, client->log, 0, "Client was closed during request processing.");
        httplite_close_connection(client);
        return NGX_ERROR;
    }

    if (u == NULL)  {
        ngx_log_debug0(NGX_LOG_WARN, client->log, 0, "all connections are busy. will try to send request later");
        return NGX_ERROR;
    }

    ((httplite_event_data_t*)u->data)->client = client;
    ((httplite_event_data_t*)u->data)->upstream = u;

    ngx_event_t *timer = u->timer;
    timer->data = u->data;
    timer->log = u->peer.log;
    timer->cancelable = 1;
    timer->handler = httplite_find_upstream_timeout_handler;
    ngx_add_timer(timer, MAX_RETRY_TIME);

    u->pending_active = 1;
    u->busy = 1;
    
    u->request = request;
    u->keep_alive = cucf->keep_alive;

    TRACEME(
        "  receieved upstream: %p\n"
        "  setting request to: %p\n\n",
        u, request
    );

    return httplite_refresh_upstream_connection(u);
}

int httplite_refresh_upstream_connection(httplite_upstream_t *u) {
    // TODO: Add testing logic to check if the connection is already made
    ngx_int_t result = ngx_event_connect_peer(&u->peer);
    ngx_connection_t *peer_c = u->peer.connection;
    ngx_event_t *wev = peer_c->write;
    ngx_event_t *rev = peer_c->read;

    if (result == NGX_AGAIN) {
        // if the upstream has a data field, then set the write handler to
        // the proper handler to forward connections
        peer_c->data = u->data;

        TRACEME(
            "  creating upstream connection\n"
            "  upstream in peer.connection->data: %p; actual upstream: %p\n"
            "  connection data: %d (%p)\n\n", 
            ((httplite_event_data_t*)peer_c->data)->upstream, u, ngx_event_ident(peer_c), peer_c
        );

        rev->handler = httplite_keepalive_read_handler;
        wev->handler = httplite_keepalive_write_handler;

        if (u->request) {
            rev->handler = httplite_upstream_read_handler;
            wev->handler = httplite_upstream_write_handler;
        }

        ngx_add_timer(wev, u->keep_alive);
    }

    if (result != NGX_OK && result != NGX_AGAIN) {
        fprintf(stderr, "Something went wrong when creating connection.\n");
        return NGX_ERROR;
    }

    return NGX_OK;
}

void httplite_send_client_error(ngx_connection_t *client, char *message) {
    if (httplite_check_broken_connection(client) != NGX_OK) {
        ngx_log_debug0(NGX_LOG_WARN, client->log, 0, "Client was closed while trying to send an error.");
        httplite_close_connection(client);
        return;
    }
    
    if (client->write->ready) {
        int n = client->send(client, message, strlen(message));
        
        if (n == NGX_ERROR) {
            ngx_log_error(NGX_ERROR_ALERT, client->log, 0, "unable to send error response to client!");
        }

        client->write->handler = httplite_empty_handler;
        return;
    }

    client->data = ngx_pcalloc(client->pool, sizeof(message));
    memcpy(client->data, message, sizeof(message));

    client->write->handler = httplite_send_client_error_handler;
}

void httplite_send_client_error_handler(ngx_event_t *wev) {
    ngx_connection_t *client;
    char *message;
    int n;

    if (httplite_check_broken_connection(client) != NGX_OK) {
        ngx_log_debug0(NGX_LOG_WARN, client->log, 0, "Client was closed during error handling.");
        httplite_close_connection(client);
        return;
    }
    
    if (wev->timedout) {
        ngx_log_error(NGX_ERROR_ALERT, wev->log, 0, "timed out while sending response error to client.");
        return;
    }

    if (!wev->ready) {
        ngx_add_timer(wev, DEFAULT_CLIENT_WRITE_TIMEOUT);
        return;
    }

    wev->handler = httplite_empty_handler;

    client = wev->data;
    message = client->data;
    
    n = client->send(client, message, strlen(message));
    client->write->handler = httplite_empty_handler;

    if (n == NGX_ERROR) {
        ngx_log_error(NGX_ERROR_ALERT, wev->log, 0, "unable to send error response to client!");
    }
}

void httplite_find_upstream_timeout_handler(ngx_event_t *ev) {
    httplite_event_data_t *ev_data = ev->data;
    httplite_upstream_t *u = ev_data->upstream;
    ngx_connection_t *peer_c = u->peer.connection;
    ngx_connection_t *client = ev_data->client;

    ngx_log_debug0(NGX_LOG_WARN, u->pool->log, 0, "reached max timeout for finding upstream. dropping request");

    // remove the write event keep alive
    if (peer_c) {
        peer_c->write->handler = httplite_keepalive_write_handler;
        peer_c->read->handler = httplite_keepalive_read_handler;
    }

    if (u->request) {
        httplite_free_list(u->request);
    }

    httplite_send_client_error(client, HTTP_UNABLE_TO_FIND_UPSTREAM_RESPONSE);
}

void httplite_keepalive_read_handler(ngx_event_t *rev) {
    ngx_connection_t *c = rev->data;
    httplite_upstream_t *u;

    if (c->destroyed) {
        return;
    }

    u = ((httplite_event_data_t*) c->data)->upstream;

    if (httplite_check_broken_connection(c) != NGX_OK) {
        httplite_deactivate_upstream(u);
        return;
    }

    u->pending_active = 0;
    u->busy = 0;
    
    // if we invoke this handler, then we have a valid connection
    // so we can delete the timer
    if (u->timer->timer_set) {
        ngx_del_timer(u->timer);
    }
}

void httplite_keepalive_write_handler(ngx_event_t *wev) {
    ngx_connection_t *c = wev->data;
    httplite_event_data_t *ev_data;
    httplite_upstream_t *u;

    if (c->destroyed) {
        return;
    }

    ev_data = c->data;
    u = ev_data->upstream;

    if (httplite_check_broken_connection(c) != NGX_OK) {
        httplite_deactivate_upstream(u);
        return;
    }

    if (wev->timedout) {
        ngx_log_debug2(NGX_LOG_INFO, c->log, 0, "keep alive time out has been hit on write event: %d (%p). closing connection\n", ngx_event_ident(wev->data), wev->data);
        TRACEME(
            "  creating upstream connection\n"
            "  upstream in peer.connection->data: %p; actual upstream: %p\n"
            "  connection data: %d (%p)\n\n", 
            ((httplite_event_data_t*)u->peer.connection->data)->upstream, u, ngx_event_ident(u->peer.connection), u->peer.connection
        );

        httplite_deactivate_upstream(u);
        return;
    }

    u->active = 1;
    u->pending_active = 0;
    u->busy = 0;

    // if we invoke this handler, then we have a valid connection
    // so we can delete the timer
    if (u->timer->timer_set) {
        ngx_del_timer(u->timer);
    }
}

void httplite_send_response_to_client(ngx_event_t *ev) {
    httplite_event_data_t *ev_data = ev->data;
    httplite_upstream_t *u = ev_data->upstream;
    ngx_connection_t *client = ev_data->client;
    ngx_connection_t *c = u->peer.connection;
    httplite_request_slab_t *response = u->response;

    if (!client->write->ready) {
        ngx_add_timer(ev, 1000);
        return;
    }

    int rc = client->send(client, response->buffer_pos, response->size);

    if (rc <= 0) {
        // error or client closed the connection
        httplite_close_connection(client);
        httplite_deactivate_upstream(u);
        ngx_pfree(u->pool, ev);
        return;
    }

    // data was fully sent, check if there's more to send
    if (rc < (int) response->size) {
        response->buffer_pos += rc;
        response->size -= rc;
        ngx_add_timer(ev, 1000);
        return;
    }

    // all data sent, reset upstream write handler
    c->write->handler = httplite_keepalive_write_handler;
    c->read->handler = httplite_keepalive_read_handler;
    u->busy = 0;

    ngx_add_timer(c->write, u->keep_alive);
    ngx_log_debug2(NGX_LOG_INFO, c->log, 0, "added time to %d (%p)\n", ngx_event_ident(c->read->data), u);

    // free memory associated with event
    ngx_pfree(u->pool, ev);

    if (c->read->ready) {
        // upstream has more data to send, add read event
        ngx_handle_read_event(c->read, 0);
    }
}

void httplite_upstream_read_handler(ngx_event_t *rev) {
    ngx_connection_t *c = rev->data;
    httplite_event_data_t *ev_data;
    ngx_connection_t *client;
    httplite_upstream_t *u;
    httplite_request_slab_t *response;
    int n;

    if (c->destroyed) {
        return;
    }

    ev_data = c->data;
    client = ev_data->client;
    u = ev_data->upstream;

    if (httplite_check_broken_connection(c) != NGX_OK) {
        httplite_send_client_error(client, HTTP_INACTIVE_UPSTREAM_RESPONSE);
        ngx_log_debug0(NGX_LOG_WARN, client->log, 0, "Upstream was closed during request processing.");
        httplite_deactivate_upstream(u);
        return;
    }

    if (httplite_check_broken_connection(client) != NGX_OK) {
        ngx_log_debug0(NGX_LOG_WARN, client->log, 0, "Client was closed during request processing.");
        httplite_close_connection(client);
        return;
    }

    if (rev->timedout) {
        ngx_log_debug1(NGX_LOG_INFO, c->log, 0, "timed out on connection %d.\n", ngx_event_ident(rev->data));
        httplite_deactivate_upstream(u);
        return;
    }

    // if we invoke this handler, then we have a valid connection
    // so we can delete the timer
    if (u->timer->timer_set) {
        ngx_del_timer(u->timer);
    }

    u->active = 1;
    u->pending_active = 0;

    if (client->destroyed) {
        TRACEME("  trying to access connection pool that has been destroyed on client connection %p.\n", client);
        ngx_log_debug0(NGX_LOG_WARN, u->log, 0, "trying to access connection pool that has been destroyed.");
        return;
    }

    response = ngx_pcalloc(client->pool, sizeof(httplite_request_slab_t));
    if (!response) {
        fprintf(stderr, "Unable to initialize response slab in httplite_upstream_read_handler.\n");
        return;
    }

    response->buffer_start = ngx_pnalloc(client->pool, SLAB_SIZE);
    if (!response->buffer_start) {
        fprintf(stderr, "Unable to initialize buffer space in httplite_upstream_read_handler.\n");
        return;
    }
    response->buffer_pos = response->buffer_start;

    // read the content from the upstream and store it on the current connection so as to prevent blocking on the upstream connection
    n = c->recv(c, response->buffer_pos, SLAB_SIZE);
    response->size += n;

    u->response = response;

    ngx_event_t *event = ngx_pcalloc(u->pool, sizeof(ngx_event_t));
    event->data = u->data;
    event->log = u->peer.log;
    event->cancelable = 1;

    // wait until client is write ready to send to client
    if (!client->write->ready) {
        event->handler = httplite_send_response_to_client;
        ngx_add_timer(event, DEFAULT_CLIENT_WRITE_TIMEOUT);
        return;
    }

    httplite_send_response_to_client(event);
}

void httplite_upstream_write_handler(ngx_event_t *wev) {
    ngx_connection_t        *c;
    httplite_event_data_t   *ev_data;
    httplite_request_list_t *list;
    httplite_request_slab_t *r;
    httplite_upstream_t     *u;
    int n;

    c = wev->data;
    ev_data = c->data;
    u = ev_data->upstream;

    if (httplite_check_broken_connection(c) != NGX_OK) {
        httplite_deactivate_upstream(u);
        return;
    }


    if (wev->timedout) {
        ngx_log_debug1(NGX_LOG_INFO, c->log, 0, "timed out on connection %d.\n", ngx_event_ident(wev->data));
        httplite_deactivate_upstream(u);
        return;
    }

    u->pending_active = 0;
    u->active = 1;

    // if we invoke this handler, then we have a valid connection
    // so we can delete the timer
    if (u->timer->timer_set) {
        ngx_del_timer(u->timer);
    }

    TRACEME(
        "  receieved upstream: %p\n"
        "  request set to    : %p\n\n",
        u, u->request
    );

    if (!(u->request && u->request->curr)) {
        TRACEME(
            "  u: %p\n"
            "  list: %p\n"
            "  curr (may not exist): %p\n\n",
            u, u->request, u->request ? u->request->curr : NULL
        );
        ngx_log_debug0(NGX_LOG_WARN, wev->log, 0, "found null request.");
        wev->handler = httplite_keepalive_write_handler;
        ngx_add_timer(wev, u->keep_alive);
        return;
    }

    list = u->request;
    r = list->curr;

    n = c->send(c, r->buffer_pos, r->size);

    if (n == NGX_ERROR) {
        ngx_log_error(NGX_LOG_WARN, wev->log, 0, "unable to send request to upstream %s!", u->peer.name->data);
        httplite_deactivate_upstream(u);
        return;
    }

    if (n != (int) r->size) {
        list->curr->buffer_pos += n;
        list->curr->size -= n;
        return;
    }

    list->curr = list->curr->next;

    if (list->curr) {
        return;
    }

    httplite_free_list(u->request);
    u->request = NULL;

    wev->handler = httplite_keepalive_write_handler;
    TRACEME(
        "  upstream write handler called\n"
        "  connection id: %d (%p)\n"
        "  connection->data: %p\n"
        "  upstream: %p\n"
        "  ev_data: %p\n\n",
        ngx_event_ident(u->peer.connection), u->peer.connection, u->peer.connection->data, u, ev_data
    );
    ngx_add_timer(wev, u->keep_alive);
}

httplite_upstream_t *fetch_upstream(httplite_connection_pool_t *c_pool) {
    httplite_upstream_pool_t    *upstream_pool;
    httplite_upstream_t         *u;

    int *upstream_pool_index, upstream_index;
    ngx_uint_t num_upstream_pools, num_upstreams;

    /* Get the upstream pool currently pointed to */
    upstream_pool_index = &c_pool->pool_index;
    (*upstream_pool_index)++;

    num_upstream_pools = c_pool->upstream_pools->nelts;
    upstream_pool = &((httplite_upstream_pool_t *)(c_pool->upstream_pools->elts))[*upstream_pool_index % num_upstream_pools];
    num_upstreams = upstream_pool->upstreams->nelts;

    if (num_upstreams == 1) {
        u = (httplite_upstream_t*)(upstream_pool->upstreams->elts);
        return (u->active && !u->pending_active && !u->busy) ? u : NULL;
    }

    for (ngx_uint_t i = 1; i <= num_upstreams; i++) {
        upstream_index = (upstream_pool->upstream_index + i) % num_upstreams;
        u = &((httplite_upstream_t*)upstream_pool->upstreams->elts)[upstream_index];
        if (u->active && !u->pending_active && !u->busy) {
            upstream_pool->upstream_index += i;
            return u;
        }
    }

    return NULL;
}

httplite_upstream_t *httplite_fetch_inactive_upstream(httplite_connection_pool_t *c_pool) {
    httplite_upstream_pool_t    *upstream_pool;
    httplite_upstream_t         *u;

    int upstream_pool_index, upstream_index;
    ngx_uint_t num_upstream_pools, num_upstreams;

    upstream_pool_index = c_pool->pool_index;
    num_upstream_pools = c_pool->upstream_pools->nelts;
    upstream_pool = &((httplite_upstream_pool_t *)(c_pool->upstream_pools->elts))[upstream_pool_index % num_upstream_pools];
    num_upstreams = upstream_pool->upstreams->nelts;

    if (num_upstreams == 1) {
        u = (httplite_upstream_t*)(upstream_pool->upstreams->elts);
        return (u->active || u->pending_active) ? NULL : u;
    }

    for (ngx_uint_t i = 1; i < num_upstreams; i++) {
        upstream_index = (upstream_pool->upstream_index + i) % num_upstreams;
        u = &((httplite_upstream_t*)(upstream_pool->upstreams->elts))[upstream_index];
        if (!u->active && !u->pending_active) {
            upstream_pool->upstream_index += i;
            return u;
        }
    }

    return NULL;
}
