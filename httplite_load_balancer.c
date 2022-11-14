#include <nginx.h>
#include <ngx_core.h>
#include <ngx_socket.h>

#include "httplite_load_balancer.h"
#include "httplite_upstream.h"
#include "httplite_request.h"

int httplite_round_robin_next_upstream_index = 0;

ngx_int_t httplite_load_balance(
    httplite_request_t *request,
    char* body, 
    char* load_balancing_algorithm,
    ngx_array_t *upstreams 
) {
    httplite_upstream_t *upstream_elements = upstreams->elts;
    httplite_upstream_t next_upstream = 
        upstream_elements[httplite_round_robin_next_upstream_index];
    httplite_initialize_upstream_connection(&next_upstream);
    httplite_send_request_to_upstream(&next_upstream, request);
    httplite_request_close_connection(next_upstream.peer.connection);
    if (httplite_round_robin_next_upstream_index + 1 == (int)upstreams->nelts) {
        httplite_round_robin_next_upstream_index = 0;
    } else {
        httplite_round_robin_next_upstream_index++;
    }
    return NGX_OK;
}
