#include <nginx.h>
#include <ngx_core.h>

#include "httplite_http_module.h"
#include "httplite_request.h"
#include "httplite_upstream.h"
#include "httplite_upstream_module_configuration.h"

#define HTTP_503_RESPONSE "HTTP/1.1 500 Service Unavailable\r\nContent-Length: 0\r\n\r\n"

static void httplite_empty_handler() {}

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
    u->peer.sockaddr = (struct sockaddr*)socket_address;
    u->peer.socklen = socket_length;
    u->peer.name = name;
    u->peer.get = ngx_event_get_peer;
    u->peer.log = pool->log;
    u->peer.log_error = NGX_ERROR_ERR;

    u->active = 0;
    u->busy = 0;
    u->keep_alive = 0;
    ngx_pfree(u->pool, u->timer);
    u->timer = NULL;

    return u;
}

ngx_int_t httplite_free_upstream(httplite_upstream_t* u) {
    ngx_pfree(u->pool, u->peer.name->data);
    ngx_pfree(u->pool, u->peer.name);

    if (ngx_pfree(u->pool, u) != NGX_OK) {
        fprintf(stderr, "Failed to deallocate httplite upstream\n");
        return NGX_DECLINED;
    }

    httplite_deactivate_upstream(u);

    return NGX_OK;
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
    u->active = 0;
    u->busy = 0;
    httplite_close_connection(u->peer.connection);
}

void httplite_refresh_upstream_connection(httplite_upstream_t *u, void *data) {
    // TODO: Add testing logic to check if the connection is already made
    ngx_int_t result = ngx_event_connect_peer(&u->peer);
    ngx_event_t *wev = u->peer.connection->write;
    ngx_event_t *rev = u->peer.connection->read;

    if (result == NGX_AGAIN) {
        // if the upstream has a data field, then set the write handler to
        // the proper handler to forward connections
        u->peer.connection->data = data;

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
        ngx_pfree(u->pool, u);
    }
}

void httplite_send_request_to_upstream(httplite_request_list_t *request) {
    ngx_connection_t *client = request->connection;
    httplite_upstream_configuration_t *cucf = httplite_get_upstream_conf(client);
    httplite_upstream_t *u = fetch_upstream(cucf->connection_pool);
    
    if (u == NULL) {
        u = httplite_fetch_inactive_upstream(cucf->connection_pool);

        if (u == NULL)  {
            printf("All connections are busy\n");
            return;
        }

        ngx_event_t *timer = ngx_pcalloc(u->pool, sizeof(ngx_event_t));
        if (!timer) {
            ngx_log_debug0(NGX_LOG_WARN, u->peer.log, 0, "the request timed out");
            return;
        }

        timer->data = client->data;
        timer->handler = httplite_find_upstream_timeout_handler;
        timer->log = u->peer.log;
        ngx_add_timer(timer, 1000);

        ((httplite_event_data_t*)client->data)->upstream = u;
        u->busy = 1;
        u->request = request;
        u->keep_alive = cucf->keep_alive;

        httplite_refresh_upstream_connection(u, client->data);
        return;
    } 

    ((httplite_event_data_t*)client->data)->upstream = u;
    u->busy = 1;
    u->request = request;

    // the connection is already established so we can delete the timer
    if (u->timer) {
        ngx_del_timer(u->timer);
        ngx_pfree(u->pool, u->timer);
        u->timer = NULL;
    }

    u->peer.connection->data = client->data;
    u->peer.connection->write->handler = httplite_upstream_write_handler;
    u->peer.connection->read->handler = httplite_upstream_read_handler;
}

void httplite_send_client_error(ngx_event_t *wev) {
    printf("here!\n");
    ngx_connection_t *client;
    char *message;
    int n;
    
    if (wev->timedout) {
        ngx_log_error(NGX_ERROR_ALERT, wev->log, 0, "timed out while sending response error to client.");
        return;
    }

    if (!wev->ready) {
        ngx_add_timer(wev, 30000);
        return;
    }

    wev->handler = httplite_empty_handler;

    client = wev->data;
    message = client->data;
    ngx_str_t response = ngx_string(message);
    
    n = client->send(client, response.data, response.len);

    if (n == NGX_ERROR) {
        ngx_log_error(NGX_ERROR_ALERT, wev->log, 0, "unable to send error response to client!");
    }
}

void httplite_find_upstream_timeout_handler(ngx_event_t *ev) {
    httplite_event_data_t *ev_data = ev->data;
    httplite_upstream_t *u = ev_data->upstream;
    ngx_connection_t *peer_c = u->peer.connection;
    ngx_connection_t *client = ev_data->client;

    ngx_pfree(u->pool, u->timer);
    u->timer = NULL;

    ngx_log_debug0(NGX_LOG_WARN, u->pool->log, 0, "reached max timeout for finding upstream. dropping request");

    // remove the write event keep alive
    peer_c->write->handler = httplite_keepalive_write_handler;
    peer_c->read->handler = httplite_keepalive_read_handler;
    httplite_free_list(u->request);

    if (client->write->ready) {
        ngx_str_t message = ngx_string(HTTP_503_RESPONSE);
        int n = client->send(client, message.data, message.len);

        if (n == NGX_ERROR) {
            ngx_log_error(NGX_ERROR_ALERT, peer_c->log, 0, "unable to send error response to client!");
        }

        client->write->handler = httplite_empty_handler;
        return;
    }

    client->data = HTTP_503_RESPONSE;
    client->write->handler = httplite_send_client_error;
}

void httplite_keepalive_read_handler(ngx_event_t *rev) {
    ngx_connection_t *c = rev->data;
    httplite_upstream_t *u;

    if (c->destroyed) {
        return;
    }

    if (httplite_check_broken_connection(c) != NGX_OK) {
        httplite_close_connection(c);
        return;
    }

    u = ((httplite_event_data_t*) c->data)->upstream;
    
    // if we invoke this handler, then we have a valid connection
    // so we can delete the timer
    if (u->timer) {
        ngx_del_timer(u->timer);
        ngx_pfree(u->pool, u->timer);
        u->timer = NULL;
    }
}

void httplite_keepalive_write_handler(ngx_event_t *wev) {
    ngx_connection_t *c = wev->data;
    httplite_event_data_t *ev_data;
    httplite_upstream_t *u;

    if (c->destroyed) {
        return;
    }

    if (httplite_check_broken_connection(c) != NGX_OK) {
        httplite_close_connection(c);
        return;
    }

    ev_data = c->data;
    u = ev_data->upstream;

    if (wev->timedout) {
        httplite_deactivate_upstream(u);
        httplite_close_connection(c);

        printf("keep alive time out has been hit. closing connection\n");
        fflush(stdin);

        ngx_log_error(NGX_ERROR_ALERT, wev->log, 0, "the request timed out\n");

        httplite_refresh_upstream_connection(u, u->peer.connection->data);
        return;
    }

    u->active = 1;

    // if we invoke this handler, then we have a valid connection
    // so we can delete the timer
    if (u->timer) {
        ngx_del_timer(u->timer);
        ngx_pfree(u->pool, u->timer);
        u->timer = NULL;
    }
}

void httplite_upstream_read_handler(ngx_event_t *rev) {
    ngx_connection_t *c = rev->data;
    httplite_event_data_t *ev_data;
    ngx_connection_t *client;
    httplite_upstream_t *u;
    httplite_request_slab_t *response;
    int n;

    if (httplite_check_broken_connection(c) != NGX_OK) {
        httplite_close_connection(c);
        return;
    }

    ev_data = c->data;
    client = ev_data->client;
    u = ev_data->upstream;

    if (rev->timedout) {
        printf("timed out!\n");
        httplite_close_connection(c);
        httplite_deactivate_upstream(u);
        return;
    }

    // if we invoke this handler, then we have a valid connection
    // so we can delete the timer
    if (u->timer) {
        ngx_del_timer(u->timer);
        ngx_pfree(u->pool, u->timer);
        u->timer = NULL;
    }

    response = ngx_pcalloc(client->pool, sizeof(httplite_request_slab_t));
    if (!response) {
        fprintf(stderr, "Unable to initialize response slab in httplite_upstream_read_handler.\n");
        return;
    }

    response->buffer = ngx_pnalloc(client->pool, SLAB_SIZE);
    if (!response->buffer) {
        fprintf(stderr, "Unable to initialize buffer space in httplite_upstream_read_handler.\n");
        return;
    }

    // read the content from the upstream and store it on the current connection so as to prevent blocking on the upstream connection
    n = c->recv(c, response->buffer, SLAB_SIZE);
    response->size += n;

    u->response = response;

    // wait until client is write ready to send to client
    if (!client->write->ready) {
        printf("client not ready to write yet\n");
        ngx_add_timer(rev, DEFAULT_CLIENT_WRITE_TIMEOUT);
        return;
    }

    int rc = client->send(client, response->buffer, response->size);

    if (rc == NGX_AGAIN) {
        // data was only partially sent, add write event
        ngx_handle_write_event(client->write, 0);
        return;
    }

    if (rc <= 0) {
        // error or client closed the connection
        httplite_close_connection(client);
        httplite_close_connection(c);
        return;
    }

    // data was fully sent, check if there's more to send
    if (rc < response->size) {
        response->buffer += rc;
        response->size -= rc;
        ngx_handle_write_event(client->write, 0);
        return;
    }

    // all data sent, reset upstream write handler
    c->write->handler = httplite_empty_handler;

    if (c->read->ready) {
        // upstream has more data to send, add read event
        ngx_handle_read_event(c->read, 0);
    }
}

void httplite_upstream_write_handler(ngx_event_t *wev) {
    ngx_connection_t        *c;
    httplite_request_list_t *list;
    httplite_request_slab_t *r;
    httplite_upstream_t     *u;
    int n;

    c = wev->data;

    if (httplite_check_broken_connection(c) != NGX_OK) {
        httplite_close_connection(c);
        return;
    }

    u = ((httplite_event_data_t*) c->data)->upstream;

    if (!u->active) {
        ngx_log_error(NGX_LOG_ERR, wev->log, 0, "trying to access inactive usptream %s!", u->peer.name->data);
        httplite_close_connection(c);
        return;
    }

    if (wev->timedout) {
        printf("timed out!\n");
        httplite_close_connection(c);
        httplite_deactivate_upstream(u);
        return;
    }

    // if we invoke this handler, then we have a valid connection
    // so we can delete the timer
    if (u->timer) {
        ngx_del_timer(u->timer);
        ngx_pfree(u->pool, u->timer);
        u->timer = NULL;
    }

    list = u->request;

    r = list->curr;

    n = c->send(c, r->buffer, r->size);

    if (n == NGX_ERROR) {
        ngx_log_error(NGX_LOG_WARN, wev->log, 0, "unable to send request to upstream %s!", u->peer.name->data);
        httplite_close_connection(c);
        return;
    }

    if (n != r->size) {
        list->curr->buffer += n;
        return;
    }

    list->curr = list->curr->next;
    if (!list->curr) {
        u->busy = 0;
        wev->handler = httplite_keepalive_write_handler;
        ngx_add_timer(wev, u->keep_alive);
        return;
    }
}

httplite_upstream_pool_t *httplite_next_upstream_pool(httplite_connection_pool_iterator_t *connection_pool_iterator) {
    httplite_connection_pool_t *connection_pool; 
    int *upstream_pool_index; 
    ngx_uint_t total_upstream_pools;

    connection_pool = connection_pool_iterator->connection_pool;
    upstream_pool_index = &connection_pool_iterator->upstream_pool_index;
    total_upstream_pools = connection_pool->upstream_pools->nelts;

    *upstream_pool_index = (*upstream_pool_index + 1) % total_upstream_pools;
    connection_pool->pool_index = *upstream_pool_index;

    return &((httplite_upstream_pool_t *)
        (connection_pool->upstream_pools->elts))[*upstream_pool_index];
}

int httplite_has_next_upstream(httplite_connection_pool_iterator_t *connection_pool_iterator) {
    ngx_uint_t num_upstreams;
    httplite_upstream_pool_iterator_t upstream_pool_iterator;
    httplite_upstream_pool_t* upstream_pool;
    
    upstream_pool_iterator = ((httplite_upstream_pool_iterator_t*) connection_pool_iterator->
        upstream_pool_iterators->elts)[connection_pool_iterator->upstream_pool_index];
    upstream_pool = upstream_pool_iterator.upstream_pool;
    num_upstreams = upstream_pool->upstreams->nelts;

    return upstream_pool->upstream_index % num_upstreams == upstream_pool_iterator.start_index;
}

httplite_upstream_t *httplite_next_upstream(httplite_connection_pool_iterator_t *connection_pool_iterator) {
    ngx_uint_t num_upstreams;
    httplite_upstream_pool_iterator_t upstream_pool_iterator;
    httplite_upstream_pool_t* upstream_pool;

    upstream_pool_iterator = ((httplite_upstream_pool_iterator_t*) connection_pool_iterator->
        upstream_pool_iterators->elts)[connection_pool_iterator->upstream_pool_index];
    upstream_pool = upstream_pool_iterator.upstream_pool;
    num_upstreams = upstream_pool->upstreams->nelts;

    upstream_pool_iterator.upstream_index = (upstream_pool->upstream_index + 1) % num_upstreams;
    upstream_pool->upstream_index = upstream_pool_iterator.upstream_index;

    return &((httplite_upstream_t*)upstream_pool->upstreams->elts)[upstream_pool->upstream_index];
}

httplite_connection_pool_iterator_t *httplite_create_connection_pool_iterator(httplite_connection_pool_t *connection_pool) {
    httplite_connection_pool_iterator_t *connection_pool_iterator;
    httplite_upstream_pool_iterator_t* upstream_pool_iterator;
    httplite_upstream_pool_t* upstream_pool;
    ngx_array_t *upstream_pool_iterators;

    upstream_pool_iterators = ngx_array_create(connection_pool->pool, 32, sizeof(httplite_upstream_pool_iterator_t));
    connection_pool_iterator = ngx_pcalloc(connection_pool->pool, sizeof(httplite_connection_pool_iterator_t));
    connection_pool_iterator->connection_pool = connection_pool;
    connection_pool_iterator->upstream_pool_index = connection_pool->pool_index,
    connection_pool_iterator->upstream_pool_iterators = upstream_pool_iterators;

    for (int i = 0; i < connection_pool->upstream_pools->nelts; i++) {
        upstream_pool = &((httplite_upstream_pool_t*)connection_pool->upstream_pools->elts)[i];
        upstream_pool_iterator = ngx_array_push(upstream_pool_iterators);
        upstream_pool_iterator->start_index = upstream_pool->upstream_index;
        upstream_pool->upstream_index = upstream_pool_iterator->start_index;
        upstream_pool_iterator->upstream_pool = upstream_pool;
    }

    return connection_pool_iterator;
}

httplite_upstream_t *fetch_upstream(httplite_connection_pool_t *c_pool) {
    httplite_upstream_t         *u;
    httplite_connection_pool_iterator_t* connection_pool_iterator;

    connection_pool_iterator = httplite_create_connection_pool_iterator(c_pool);
    httplite_next_upstream_pool(connection_pool_iterator);

    while (httplite_has_next_upstream(connection_pool_iterator)) {
        u = httplite_next_upstream(connection_pool_iterator);

        if (u->active && !u->busy) {
            return u;
        }
    }

    return NULL;
}

httplite_upstream_t *httplite_fetch_inactive_upstream(httplite_connection_pool_t *c_pool) {
    httplite_upstream_t *u;
    httplite_connection_pool_iterator_t* connection_pool_iterator;

    connection_pool_iterator = httplite_create_connection_pool_iterator(c_pool);

    while (httplite_has_next_upstream(connection_pool_iterator)) {
        u = httplite_next_upstream(connection_pool_iterator);
        if (!u->active) {
            return u;
        }
    }

    return NULL;
}
