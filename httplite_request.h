#ifndef HTTPLITE_HTTP_REQUEST_H
#define HTTPLITE_HTTP_REQUEST_H

#include <nginx.h>
#include <ngx_core.h>
#include <ngx_event.h>

void httplite_request_handler(ngx_event_t *rev);
void httplite_request_close_connection(ngx_connection_t *c);

#endif
