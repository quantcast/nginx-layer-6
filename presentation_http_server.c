#include <nginx.h>
#include <ngx_core.h>
#include <ngx_config.h>
#include <ngx_event.h>
#include <ngx_http.h>
#include <string.h>
#include <ngx_socket.h>

#include <presentation_http_server.h>

void presentation_http_server_close_connection(ngx_connection_t *c);
static void presentation_http_server_wait_request_handler(ngx_event_t *rev);
void presentation_http_server_empty_handler(ngx_event_t *wev);
u_char* presentation_http_server_log_error(ngx_log_t *log, u_char *buf, size_t len);
void presentation_http_server_init_connection(ngx_connection_t *c);

void presentation_http_server_close_connection(ngx_connection_t *c)
{
    ngx_pool_t  *pool;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "close http connection: %d", c->fd);

    c->destroyed = 1;

    pool = c->pool;

    ngx_close_connection(c);

    ngx_destroy_pool(pool);
}

static void presentation_http_server_wait_request_handler(ngx_event_t *rev)
{
    size_t                     size;
    ssize_t                    n;
    ngx_buf_t                 *b;
    ngx_connection_t          *c;

    c = rev->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http wait request handler");

    if (rev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        presentation_http_server_close_connection(c);
        return;
    }

    if (c->close) {
        presentation_http_server_close_connection(c);
        return;
    }

    size = 1024;

    b = c->buffer;

    if (b == NULL) {
        b = ngx_create_temp_buf(c->pool, size);
        if (b == NULL) {
            presentation_http_server_close_connection(c);
            return;
        }

        c->buffer = b;

    } else if (b->start == NULL) {
        b->start = ngx_palloc(c->pool, size);
        if (b->start == NULL) {
            presentation_http_server_close_connection(c);
            return;
        }

        b->pos = b->start;
        b->last = b->start;
        b->end = b->last + size;
    }

    n = c->recv(c, b->last, size);

    if (n == NGX_AGAIN) {
        if (!rev->timer_set) {
            ngx_add_timer(rev, 60 * 1000);
            ngx_reusable_connection(c, 1);
        }

        if (ngx_handle_read_event(rev, 0) != NGX_OK) {
            presentation_http_server_close_connection(c);
            return;
        }

        /*
         * We are trying to not hold c->buffer's memory for an idle connection.
         */

        if (ngx_pfree(c->pool, b->start) == NGX_OK) {
            b->start = NULL;
        }

        return;
    }

    if (n == NGX_ERROR) {
        presentation_http_server_close_connection(c);
        return;
    }

    if (n == 0) {
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                      "client closed connection");
        presentation_http_server_close_connection(c);
        return;
    }

    b->last += n;

    c->log->action = "reading client request line";

    ngx_reusable_connection(c, 0);
}

void presentation_http_server_empty_handler(ngx_event_t *wev)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, wev->log, 0, "http empty handler");

    return;
}

static u_char* presentation_http_server_log_error(ngx_log_t *log, u_char *buf, size_t len)
{
   return NGX_OK;
}

void presentation_http_server_init_connection(ngx_connection_t *c)
{
    ngx_event_t               *rev;

    c->log->connection = c->number;
    c->log->handler = presentation_http_server_log_error;
    c->log->data = NULL;
    c->log->action = "waiting for request";

    c->log_error = NGX_ERROR_INFO;

    rev = c->read;
    rev->handler = presentation_http_server_wait_request_handler;
    c->write->handler = presentation_http_server_empty_handler;

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

    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        presentation_http_server_close_connection(c);
        return;
    }
}

static ngx_int_t
presentation_http_server_init_listening(ngx_conf_t *cf, ngx_int_t port)
{
    ngx_listening_t *ls;
    struct sockaddr_in *socket_address;
    size_t socket_length = sizeof(struct sockaddr_in);

    ngx_str_t raw_address = ngx_string("127.0.0.1");

    socket_address = ngx_pcalloc(cf->pool, socket_length);
    if (socket_address == NULL) {
        printf("Failed to allocate socket address\n");
        return NGX_ERROR;
    }
    ngx_memzero(socket_address, socket_length);
    socket_address->sin_family = AF_INET;
    socket_address->sin_port = port;
    socket_address->sin_len = socket_length;
    socket_address->sin_addr.s_addr = ngx_inet_addr(raw_address.data, raw_address.len);
    
    ls = ngx_create_listening(cf, (struct sockaddr*)socket_address, socket_length);
    if (ls == NULL) {
        printf("Failed to create listening socket\n");
        return NGX_ERROR;
    }

    ls->addr_ntop = 1;

    ls->handler = presentation_http_server_init_connection;
    ls->pool_size = 512;

    ls->logp = cf->log;
    ls->log.data = &ls->addr_text;
    ls->log.handler = presentation_http_server_accept_log_error;

    ls->backlog = -1;
    ls->rcvbuf = SO_RCVBUF;
    ls->sndbuf = SO_SNDBUF;

    ls->keepalive = 1;

    return NGX_OK;
}

u_char* presentation_http_server_accept_log_error(ngx_log_t *log, u_char *buf, size_t len)
{
    printf("accepting connection\n");
    return ngx_snprintf(buf, len, " while accepting new connection on %V",
                        log->data);
}
