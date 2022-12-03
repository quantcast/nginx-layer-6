#ifndef HTTPLITE_LOAD_BALANCER_H
#define HTTPLITE_LOAD_BALANCER_H

#include <nginx.h>
#include <ngx_core.h>

#include "httplite_request.h"

ngx_int_t httplite_load_balance(
    httplite_request_slab_t *request,
    char* body, 
    char* load_balancing_algorithm,
    ngx_array_t *upstreams 
);

#endif
