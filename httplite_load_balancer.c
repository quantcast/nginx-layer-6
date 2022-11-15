#include <nginx.h>
#include <ngx_core.h>

#include "httplite_load_balancer.h"

// See header for implementation hints
ngx_int_t httplite_load_balance(
    ngx_str_t message, 
    ngx_str_t load_balancing_algorithm,
    void* upstreams 
) {
    return NGX_ERROR;
}
