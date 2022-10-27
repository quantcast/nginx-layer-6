#include <nginx.h>
#include <ngx_core.h>

#include "presentation_load_balancer.h"
#include "presentation_upstream.h"

int presentation_round_robin_next_upstream_index = 0;

ngx_int_t presentation_load_balance(
    presentation_request_t *request,
    char* body, 
    char* load_balancing_algorithm,
    ngx_array_t *upstreams 
) {
    presentation_upstream_t *upstream_elements = upstreams->elts;
    presentation_upstream_t next_upstream = 
        upstream_elements[presentation_round_robin_next_upstream_index];
    presentation_initialize_upstream_connection(&next_upstream);
    presentation_send_request_to_upstream(&next_upstream, request);
    if (presentation_round_robin_next_upstream_index + 1 == (int)upstreams->nelts) {
        presentation_round_robin_next_upstream_index = 0;
    } else {
        presentation_round_robin_next_upstream_index++;
    }
    return NGX_OK;
}