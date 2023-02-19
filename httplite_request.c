#include <nginx.h>
#include <ngx_core.h>
#include <ngx_string.h>
#include <ngx_event.h>

#include "httplite_request.h"

#define LENGTH_HEADER "\nContent-Length: "
#define LENGTH_HEADER_SIZE strlen(LENGTH_HEADER)
#define HEADER_BODY_SEPARATOR "\r\n\r\n"
#define HEADER_BODY_SEPARATOR_SIZE strlen(HEADER_BODY_SEPARATOR)


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
    list.next = NULL;

    return list;
}

httplite_request_slab_t *httplite_add_slab(httplite_request_list_t *list) {
    httplite_request_slab_t *new_slab = ngx_pcalloc(list->connection->pool, sizeof(httplite_request_slab_t));

    if (!new_slab) {
        ngx_log_error(NGX_ERROR_ALERT, list->connection->log, 0, "unable to create new slab on connection's memory pool.");
        return NULL;
    }

    new_slab->size = 0;
    new_slab->buffer = ngx_pnalloc(list->connection->pool, SLAB_SIZE);

    if (!new_slab->buffer) {
        ngx_log_error(NGX_ERROR_ALERT,list->connection->log, 0, "unable to the string buffer on connection's memory pool.");
        return NULL;
    }
    // add a slab before the tail
    httplite_request_slab_t *temp = list->tail;
    list->tail = new_slab;
    list->tail->next = temp;

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

/* Assumes incoming slab is empty (writes to buffer pointer, overwriting anything there) */
size_t recv_wrapper(ngx_connection_t *c, httplite_request_slab_t *slab, ngx_event_t *rev) {
    int n;
    
    n = c->recv(c, slab->buffer, SLAB_SIZE);

    if (n == NGX_AGAIN) {
        if (!rev->timer_set) {
            ngx_add_timer(rev, 60 * 1000);
            ngx_reusable_connection(c, 1);
        }

        if (ngx_handle_read_event(rev, 0) != NGX_OK) {
            ngx_httplite_close_connection(c);
            return n;
        }

        return n;
    }

    if (n == NGX_ERROR) {
        ngx_httplite_close_connection(c);
        return n;
    }

    if (n == NGX_OK) {
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                      "client closed connection");
        ngx_httplite_close_connection(c);
        return n;
    }

    slab->size += n;

    c->log->action = "reading client request line";

    ngx_reusable_connection(c, 0);

    return n;
}

void httplite_request_handler(ngx_event_t *rev) {
    ssize_t                    n, m;
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

    n = recv_wrapper(c, curr, rev);

    /* read entire connection into list*/
    while (rev->ready) {
        m = 0;
        if (!httplite_add_slab(&list)) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to add slab.");
            return;
        }
        m = recv_wrapper(c, list.tail, rev);

        if (m < 0) { 
            ngx_log_error(NGX_LOG_ALERT, c->log, 0, "failed recv.");
            return; 
        }

        n += m;

        if (m < SLAB_SIZE) {
            break;
        }
    }

    if (n <= 0) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "failed recv.");
        return;
    }

    // ------------------------

    // if (n < SLAB_SIZE) {
    //     split_request(&list);
    // } else {
    //     // there might be a possibility that n > slab_size: we will test that
    //     while (n == SLAB_SIZE) {
    //         // add a new slab and read in that new slab
    //         // TODO Make the method take a pointer to the existing list

    //         httplite_request_slab_t new_slab = httplite_add_slab(&list)
    //         if (!new_slab) {
    //             ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to add slab.");
    //             return;
    //         }
    //         curr = curr->next; // point to the next slab before reading
    //         n = recv_wrapper(c,curr, rev); 

    //         if (n < 0) { 
    //             ngx_log_error(NGX_LOG_ALERT, c->log, 0, "failed recv.");
    //             return; 
    //         }
    //     }
    //     // split_request should be a void method
    //     split_request(&list);
    // }
    
    printf("%s\n", (char*) list.head->buffer);
    fflush(stdout);
    //printRequests(&list);
    
    // ----------------------

    // /* get request length */
    // ssize_t request_length = find_request_length(curr);
    // if (request_length < 0) {
    //     ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to find request length.");
    //     return;
    // }

    /* call receive function until we have read the entire request */
    // int m;
    // while (n < request_length && rev->ready) {
    //     if (!httplite_add_slab(list)) {
    //         ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to add slab.");
    //         return;
    //     }
    //     m = recv_wrapper(c, list.tail, rev);

    //     if (m < 0) { 
    //         ngx_log_error(NGX_LOG_ALERT, c->log, 0, "failed recv.");
    //         return; 
    //     }

    //     n += m;
    // }
    
}

ssize_t find_request_length(httplite_request_slab_t *slab) {
    u_char *str, *curr;
    size_t header_size, l;
    ssize_t body_size;

    str = slab->buffer;
    curr = str;
    body_size = 0;

    curr = ngx_strlcasestrn(str, str + slab->size, (u_char*) LENGTH_HEADER, LENGTH_HEADER_SIZE - 1);
    /* From nginx documentation:
    *       ngx_strlcasestrn() is intended to search for static substring
    *       with known length in string until the argument last. The argument n
    *       must be length of the second substring - 1.
    */

    if (curr != NULL) {
        curr += LENGTH_HEADER_SIZE;
        l = 0;

        /* find size of substring containing value after the header */
        while (*(curr + l) != '\r' && *(curr + l) != '\n') {
            ++l;
        }

        body_size = ngx_atosz(curr, l);

        curr += l;
    }

    /* find index of header separator */ 
    curr = (u_char*) ngx_strstr(curr, HEADER_BODY_SEPARATOR);
            
    if (curr == NULL) {
        return NGX_ERROR;
    }
    else {
        curr += HEADER_BODY_SEPARATOR_SIZE;
        header_size = curr - str;
        
        curr += body_size + 1;
        // TODO: what if end of body is in the next slab? move this to recv wrapper

        if (check_http_method(curr) == 1) {
            // TODO: process pipelined request
        }

        return body_size + header_size;
    }

    return NGX_ERROR;
}

size_t check_http_method(u_char *str) {
    if (ngx_strncmp(str, "GET", 3) == 0 ||
            ngx_strncmp(str, "POST", 4) == 0 ||
            ngx_strncmp(str, "PUT", 3) == 0 ||
            ngx_strncmp(str, "CONNECT", 7) == 0 ||
            ngx_strncmp(str, "DELETE", 6) == 0 ||
            ngx_strncmp(str, "HEAD", 4) == 0 ||
            ngx_strncmp(str, "OPTIONS", 7) == 0 ||
            ngx_strncmp(str, "TRACE", 5) == 0) {
        // TODO: check on TRACE; it is not supported by browsers
        // source: https://developer.mozilla.org/en-US/docs/Web/HTTP/Methods
        return 1;
    }
    return 0;
}

// httplite_request_list_t *make_request_queue(ngx_connection_t) {
//     // call recv
//     // if n < 
// }

/* given a list of slabs, break it up into a list of lists, each one containing
a single request */
httplite_request_list_t *split_request (httplite_request_list_t *list) {
    u_char* curr, str, start_ptr, end_ptr;
    httplite_request_slab_t* read_slab, write_slab;
    httplite_request_list_t write_list;
    size_t l, header_size; 
    ssize_t body_size;

    read_slab = list->head;
    curr = read_slab->buffer;
    write_list = httplite_init_list(list->connection);
    header_size = 0;

    while (true) { /* each iteration will find one request */
        start_ptr = curr;
        body_size = 0;

        curr = ngx_strlcasestrn(str, str + read_slab->size, (u_char*) LENGTH_HEADER, LENGTH_HEADER_SIZE - 1);

        if (curr != NULL) {
            curr += LENGTH_HEADER_SIZE;
            l = 0;

            /* find size of substring containing value after the header */
            while (*(curr + l) != '\r' && *(curr + l) != '\n') {
                ++l;
            }

            body_size = ngx_atosz(curr, l);

            curr += l;
        }

        /* find index of header separator */ 
        curr = (u_char*) ngx_strstr(curr, HEADER_BODY_SEPARATOR);

        if (curr == NULL) { /* headers continute onto next slab */

            /* we need to copy what we currently have before moving to the next slab */
            httplite_request_slab_t *slab = write_list.tail;

            /* size of current = start of slab + size of slab - end of slab*/
            size_t size = read_slab->buffer + read_slab->size - curr;

            memcpy(slab->buffer + slab->size, start_ptr, size);
        }
        else {
            curr += HEADER_BODY_SEPARATOR_SIZE;
            header_size = curr - str;
            
            curr += body_size + 1;
            end_ptr = curr;
        }

        read_slab = read_slab->next;
    }

    while (true) {

    }
}