#ifndef PRESENTATION_UPSTREAM_H
#define PRESENTATION_UPSTREAM_H

#include <nginx.h>
#include <ngx_string.h>

#include "presentation_http_request.h";

typedef struct {
    ngx_int_t port;
    ngx_str_t address;
} presentation_upstream_t;

presentation_upstream_t *create_upstream(ngx_str_t address, ngx_int_t port);
void write_to_upstream(presentation_upstream_t *upstream, 
    presentation_request_t *request);
void free_upstream(presentation_upstream_t* upstream);

#endif