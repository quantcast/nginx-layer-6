#ifndef PRESENTATION_LOAD_BALANCER_H
#define PRESENTATION_LOAD_BALANCER_H

#include <nginx.h>
#include <ngx_core.h>

#include "presentation_http_request.h"

ngx_int_t presentation_load_balance(
    presentation_request_t *request,
    char* body, 
    char* load_balancing_algorithm,
    ngx_array_t *upstreams 
);

#endif