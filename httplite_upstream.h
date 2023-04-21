#ifndef HTTPLITE_UPSTREAM_H
#define HTTPLITE_UPSTREAM_H

#include <nginx.h>
#include <ngx_string.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <ngx_string.h>

#include "httplite_upstream_module_configuration.h"
#include "httplite_request_list.h"

#define DEFAULT_CLIENT_WRITE_TIMEOUT    (30*1000)   /* Default timeout for server to be write ready */
#define MAX_RETRY_TIME                  10000

typedef struct httplite_upstream_s {
    ngx_peer_connection_t       peer;
    ngx_pool_t                 *pool;
    httplite_request_list_t    *request;
    httplite_request_slab_t    *response;
    ngx_event_t                *timer;
    void                       *data;
    int                         keep_alive;
    int                         active;
    int                         pending_active;
    int                         busy;
} httplite_upstream_t;

typedef struct {
    ngx_connection_t    *client;
    httplite_upstream_t *upstream;
} httplite_event_data_t;

void httplite_empty_handler();

httplite_upstream_t *httplite_create_upstream(ngx_pool_t *pool, ngx_array_t *arr, char *address, ngx_int_t port);

int httplite_check_broken_connection(ngx_connection_t *c);
void httplite_deactivate_upstream(httplite_upstream_t *u);

int httplite_send_request_to_upstream(httplite_request_list_t *request);
int httplite_fetch_upstream_and_send_request(httplite_request_list_t *request);
int httplite_refresh_upstream_connection(httplite_upstream_t *upstream);

void httplite_send_client_error(ngx_connection_t *client, char *message);
void httplite_send_client_error_handler(ngx_event_t *wev);
void httplite_find_upstream_timeout_handler(ngx_event_t *ev);

void httplite_keepalive_read_handler(ngx_event_t *rev);
void httplite_keepalive_write_handler(ngx_event_t *wev);

void httplite_upstream_read_handler(ngx_event_t *rev);
void httplite_upstream_write_handler(ngx_event_t *wev);

httplite_upstream_t *fetch_upstream(httplite_connection_pool_t *c_pool);
httplite_upstream_t *httplite_fetch_inactive_upstream(httplite_connection_pool_t *c_pool);

#endif
