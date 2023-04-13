#include <nginx.h>
#include <ngx_core.h>

#include "httplite_http_module.h"
#include "httplite_request.h"
#include "httplite_upstream.h"
#include "httplite_upstream_module_configuration.h"

static void httplite_empty_upstream_handler() {}

httplite_upstream_t *httplite_create_upstream(ngx_array_t *arr, char *address, ngx_int_t port, ngx_pool_t *pool) {
    httplite_upstream_t *upstream;
    struct sockaddr_in *socket_address;
    size_t socket_length;
    ngx_str_t *name;
    
    upstream = ngx_array_push(arr);

    if (!upstream) {
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

    upstream->pool = pool;
    upstream->peer.sockaddr = (struct sockaddr*)socket_address;
    upstream->peer.socklen = socket_length;
    upstream->peer.name = name;
    upstream->peer.get = ngx_event_get_peer;
    upstream->peer.log = pool->log;
    upstream->peer.log_error = NGX_ERROR_ERR;

    upstream->active = 0;
    upstream->busy = 0;

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

void httplite_keepalive_write_handler(ngx_event_t *wev) {
    ngx_connection_t *c = wev->data;
    httplite_event_connection_t *connections;
    httplite_upstream_t *u;

    if (c->destroyed) {
        return;
    }

    if (httplite_check_broken_connection(c) != NGX_OK) {
        httplite_close_connection(c);
        return;
    }

    if (wev->timedout) {
        ngx_log_error(NGX_ERROR_ALERT, wev->log, 0, "the request timed out\n");
        httplite_close_connection(c);
        return;
    }

    if (!wev->ready) {
        return;
    }

    connections = c->data;
    u->active = 1;
}

void httplite_keepalive_read_handler(ngx_event_t *rev) {
    ngx_connection_t *c = rev->data;

    if (c->destroyed) {
        return;
    }

    if (httplite_check_broken_connection(c) != NGX_OK) {
        httplite_close_connection(c);
        return;
    }
}

int httplite_check_broken_connection(ngx_connection_t *c) {
    ngx_event_t *rev = c->read;

    if (!rev->pending_eof) {
        return NGX_OK;
    }

    ngx_log_debug0(NGX_LOG_INFO, c->log, 0, "broken connection found. closing.\n");

    rev->eof = 1;
    c->error = 1;

    return NGX_ERROR;
}

void httplite_refresh_upstream_connection(httplite_upstream_t *upstream, void *upstream_data) {
    // TODO: Add testing logic to check if the connection is already made
    ngx_int_t result = ngx_event_connect_peer(&upstream->peer);
    ngx_event_t *wev = upstream->peer.connection->write;
    ngx_event_t *rev = upstream->peer.connection->read;

    if (result == NGX_AGAIN) {
        // if the upstream has a data field, then set the write handler to
        // the proper handler to forward connections
        if (upstream->request) {
            upstream->peer.connection->data = upstream_data;
            wev->handler = httplite_upstream_write_handler;
            rev->handler = httplite_upstream_read_handler;
            return;
        }
        wev->handler = httplite_keepalive_write_handler;
        rev->handler = httplite_keepalive_read_handler;
        ngx_add_timer(wev, 10000);
    }

    if (result != NGX_OK && result != NGX_AGAIN) {
        fprintf(stderr, "Something went wrong when creating connection.\n");
        ngx_pfree(upstream->pool, upstream);
    }
}

void httplite_upstream_write_handler(ngx_event_t *wev) {
    ngx_connection_t     *c;
    httplite_request_list_t *list;
    httplite_request_slab_t *r;
    httplite_upstream_t *u;
    int n;

    c = wev->data;

    if (httplite_check_broken_connection(c) != NGX_OK) {
        httplite_close_connection(c);
        return;
    }

    u = ((httplite_event_connection_t*) c->data)->upstream;
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
        ngx_add_timer(wev, 30000);
        return;
    }
}

void httplite_upstream_read_handler(ngx_event_t *rev) {
    ngx_connection_t *c = rev->data;
    httplite_event_connection_t *connections;
    ngx_connection_t *client;
    httplite_upstream_t *upstream;
    httplite_request_slab_t *response_slab;
    int n;

    if (httplite_check_broken_connection(c) != NGX_OK) {
        httplite_close_connection(c);
        return;
    }

    connections = c->data;
    client = connections->client_connection;
    upstream = connections->upstream;

    response_slab = ngx_pcalloc(client->pool, sizeof(httplite_request_slab_t));
    if (!response_slab) {
        fprintf(stderr, "Unable to initialize response slab in httplite_upstream_read_handler.\n");
        return;
    }

    response_slab->buffer = ngx_pnalloc(client->pool, SLAB_SIZE);
    if (!response_slab->buffer) {
        fprintf(stderr, "Unable to initialize buffer space in httplite_upstream_read_handler.\n");
        return;
    }

    // read the content from the upstream and store it on the current connection so as to prevent blocking on the upstream connection
    n = c->recv(c, response_slab->buffer, SLAB_SIZE);
    response_slab->size += n;

    upstream->response = response_slab;

    // wait until client is write ready to send to client
    if (!client->write->ready) {
        printf("client not ready to write yet\n");
        ngx_add_timer(rev, DEFAULT_CLIENT_WRITE_TIMEOUT);
        return;
    }

    int rc = client->send(client, response_slab->buffer, response_slab->size);

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
    if (rc < response_slab->size) {
        response_slab->buffer += rc;
        response_slab->size -= rc;
        ngx_handle_write_event(client->write, 0);
        return;
    }

    // all data sent, reset upstream write handler
    c->write->handler = httplite_empty_upstream_handler;

    if (c->read->ready) {
        // upstream has more data to send, add read event
        ngx_handle_read_event(c->read, 0);
    }
}

httplite_upstream_t *httplite_fetch_inactive_upstream(httplite_connection_pool_t *c_pool) {
    httplite_upstream_pool_t *upstream_pool;
    httplite_upstream_t *upstream;

    ngx_atomic_t upstream_pool_index, upstream_index;
    ngx_uint_t num_upstream_pools, num_upstreams;

    upstream_pool_index = c_pool->pool_index;
    num_upstream_pools = c_pool->upstream_pools->nelts;
    upstream_pool = &((httplite_upstream_pool_t *)(c_pool->upstream_pools->elts))[upstream_pool_index % num_upstream_pools];
    num_upstreams = upstream_pool->upstreams->nelts;

    for (int i = 0; i < num_upstreams; i++) {
        upstream_index = (upstream_pool->upstream_index + i) % num_upstreams;
        upstream = &((httplite_upstream_t*)(upstream_pool->upstreams->elts))[upstream_index];
        if (!upstream->active) {
            ngx_atomic_fetch_add(&upstream_pool->upstream_index, i); 
            return upstream;
        }
    }

    return NULL;
}

void httplite_send_request_to_upstream(httplite_request_list_t *request) {
    ngx_connection_t *client_connection = request->connection;
    httplite_upstream_configuration_t *cucf = httplite_get_upstream_conf(client_connection);
    httplite_upstream_t *upstream = fetch_upstream(cucf->connection_pool);
    
    if (upstream == NULL) {
        upstream = httplite_fetch_inactive_upstream(cucf->connection_pool);

        if (upstream == NULL)  {
            printf("All connections are busy\n");
            return;
        }

        ((httplite_event_connection_t*)client_connection->data)->upstream = upstream;
        upstream->busy = 1;
        upstream->request = request;

        httplite_refresh_upstream_connection(upstream, client_connection->data);
        return;
    } 

    ((httplite_event_connection_t*)client_connection->data)->upstream = upstream;
    upstream->busy = 1;
    upstream->request = request;

    upstream->peer.connection->data = client_connection->data;
    upstream->peer.connection->write->handler = httplite_upstream_write_handler;
    upstream->peer.connection->read->handler = httplite_upstream_read_handler;
}

httplite_upstream_t *fetch_upstream(httplite_connection_pool_t *c_pool) {
    httplite_upstream_pool_t *upstream_pool;
    httplite_upstream_t *upstream;

    ngx_atomic_t upstream_pool_index, upstream_index;
    ngx_uint_t num_upstream_pools, num_upstreams;

    /* Get the upstream pool currently pointed to */
    upstream_pool_index = ngx_atomic_fetch_add(&c_pool->pool_index, 1);
    num_upstream_pools = c_pool->upstream_pools->nelts;
    upstream_pool = &((httplite_upstream_pool_t *)(c_pool->upstream_pools->elts))[upstream_pool_index % num_upstream_pools];

    num_upstreams = upstream_pool->upstreams->nelts;
    int i = 1; 
    while (i < num_upstreams + 1) {
        int upstream_index_to_check = (upstream_pool->upstream_index + i) % num_upstreams;
        httplite_upstream_t *upstream_to_check = &((httplite_upstream_t*)upstream_pool->upstreams->elts)[upstream_index_to_check];
        if (upstream_to_check->active && !upstream_to_check->busy) {
            upstream_index = ngx_atomic_fetch_add(&upstream_pool->upstream_index, i); 
            upstream = &((httplite_upstream_t*)(upstream_pool->upstreams->elts))[upstream_index % num_upstreams];
            return upstream;
        }
        i++;
    }

    return NULL;
}
