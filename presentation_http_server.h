#ifndef PRESENTATION_HTTP_SERVER_H
#define PRESENTATION_HTTP_SERVER_H

#include <nginx.h>
#include <ngx_core.h>

#define LOCALHOST ngx_string("127.0.0.1")

ngx_int_t presentation_http_server_init_listening(ngx_conf_t *cf, ngx_int_t port);

#endif