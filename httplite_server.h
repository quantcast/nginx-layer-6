#ifndef HTTPLITE_HTTP_SERVER_H
#define HTTPLITE_HTTP_SERVER_H

#include <nginx.h>
#include <ngx_core.h>

#define httplite_get_upstream_conf(c) ((ngx_array_t*)(c)->listening->servers)->elts

ngx_int_t httplite_server_init_listening(ngx_conf_t *cf, ngx_int_t port);

#endif
