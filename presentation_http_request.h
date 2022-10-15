#ifndef PRESENTATION_HTTP_REQUEST_H
#define PRESENTATION_HTTP_REQUEST_H

#include <nginx.h>
#include <ngx_core.h>
#include <ngx_event.h>

void presentation_http_request_handler(ngx_event_t *rev);
void presentation_http_request_close_connection(ngx_connection_t *c);

#endif