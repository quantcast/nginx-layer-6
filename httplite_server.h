#ifndef HTTPLITE_HTTP_SERVER_H
#define HTTPLITE_HTTP_SERVER_H

#include <nginx.h>
#include <ngx_core.h>

#define DEFAULT_SERVER_TIMEOUT          (60*1000)   /* Default timeout for server to be read ready */

ngx_int_t httplite_server_init_listening(ngx_conf_t *cf, ngx_int_t port);

#endif
