#include <nginx.h>
#include <ngx_core.h>
#include <ngx_string.h>
#include <ngx_event.h>

#include "httplite_request.h"

#define LENGTH_HEADER "Content-Length: "
#define HEADER_BODY_SEPARATOR "\r\n\r\n"

httplite_request_list_t httplite_init_list(ngx_connection_t *connection) {
    httplite_request_list_t list = { 0 };

    httplite_request_slab_t *head = ngx_pcalloc(connection->pool, sizeof(httplite_request_slab_t));
    
    if (!head) {
        ngx_log_error(NGX_ERROR_ALERT, connection->log, 0, "unable to create head slab on connection's memory pool.");
        return list;
    }

    head->buffer = ngx_pnalloc(connection->pool, SLAB_SIZE);

    if (!head->buffer) {
        ngx_log_error(NGX_ERROR_ALERT, connection->log, 0, "unable to the string buffer on connection's memory pool.");
        return list;
    }

    head->size = 0;

    list.head = head;
    list.tail = head;
    list.connection = connection;

    return list;
}

httplite_request_slab_t *httplite_add_slab(httplite_request_list_t list) {
    httplite_request_slab_t *new_slab = ngx_pcalloc(list.connection->pool, sizeof(httplite_request_slab_t));

    if (!new_slab) {
        ngx_log_error(NGX_ERROR_ALERT, list.connection->log, 0, "unable to create new slab on connection's memory pool.");
        return NULL;
    }

    new_slab->size = 0;
    new_slab->buffer = ngx_pnalloc(list.connection->pool, SLAB_SIZE);

    if (!new_slab->buffer) {
        ngx_log_error(NGX_ERROR_ALERT, list.connection->log, 0, "unable to the string buffer on connection's memory pool.");
        return NULL;
    }


    list.tail->next = new_slab;
    list.tail = new_slab;
    list.tail->next = NULL;

    return new_slab;
}

void ngx_httplite_close_connection(ngx_connection_t *c)
{
    ngx_pool_t  *pool;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "close http connection: %d", c->fd);

    c->destroyed = 1;

    pool = c->pool;

    ngx_close_connection(c);

    ngx_destroy_pool(pool);
}

// TODO: update this function to reflect new request structure
void httplite_request_handler(ngx_event_t *rev) {
    ssize_t                    n;
    httplite_request_list_t    list;
    httplite_request_slab_t   *curr;
    ngx_connection_t          *c;

    c = rev->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http wait request handler");

    if (rev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        ngx_httplite_close_connection(c);
        return;
    }

    if (c->close) {
        ngx_httplite_close_connection(c);
        return;
    }

    list = httplite_init_list(c);

    curr = list.head;
    
    if (list.head == NULL) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to allocate space for the request slab.");
        ngx_httplite_close_connection(c);
        return;
    }

    if (list.head->buffer == NULL) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to allocate space for the request string.");
        ngx_httplite_close_connection(c);
        return;
    }

    n = recv_wrapper(c, request, rev);

    if (n <= 0) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "failed recv.");
        return;
    }

    /* get request length */
    ssize_t request_length = find_request_length(request);
    if (request_length < 0) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to find request length.");
        return;
    }

    /* reallocate enough memory to hold request length */
    presentation_request_realloc(request, (ssize_t) request_length);

    /* call receive function until we have read the entire request */
    while (n < (ssize_t) request_length && rev->ready) {
        int m = recv_wrapper(c, request, rev);

        if (m < 0) { 
            ngx_log_error(NGX_LOG_ALERT, c->log, 0, "failed recv.");
            return; 
        }

        n += m;
    }
}

ssize_t find_request_length(presentation_request_t *request) {
    u_char* str;
    size_t i, str_size, length_header_size, separator_size, header_size;

    i = 0;
    str = request->start;
    str_size = request->last - request->start + 1;
    length_header_size = ngx_strlen(LENGTH_HEADER);
    separator_size = ngx_strlen(HEADER_BODY_SEPARATOR);

    /* iterate through each char in headers */
    while (i < str_size) {                                          
        if (str[i] != '\n') {
            ++i;
            continue;
        }
        ++i;

        /* at each newline, check the following header */
        if (ngx_strncmp(&str[i], LENGTH_HEADER, length_header_size) == 0) {     
            i += length_header_size;
            size_t l = 0;

            /* find size of substring containing value after the header */
            while (str[i + l] != '\r' && str[i + l] != '\n') {
                ++l;
            }

            ssize_t body_size = ngx_atosz(&str[i], l);

            /* find index of header/body separator */ 
            u_char* end_ptr = (u_char*) ngx_strstr(&str[i], HEADER_BODY_SEPARATOR);
            
            if (end_ptr == NULL) {
                return NGX_ERROR;
            }
            else {
                request->body = end_ptr + separator_size;
                header_size = request->body - str;
                return body_size + header_size;
            }
        }
    }

    return NGX_ERROR;
}