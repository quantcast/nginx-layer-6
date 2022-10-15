#ifndef PRESENTATION_LOAD_BALANCER_H
#define PRESENTATION_LOAD_BALANCER_H

#include <nginx.h>
#include <ngx_core.h>

// will execute load balancing and pass message to upstreams
ngx_int_t presentation_load_balance(
    ngx_str_t message, // todo: this may also need headers to be passed in so this type may need to be adjusted
    ngx_str_t load_balancing_algorithm,
    void* upstreams // todo: this should be the type of the upstream
);

#endif