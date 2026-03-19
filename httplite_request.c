#include <nginx.h>
#include <ngx_core.h>
#include <ngx_string.h>
#include <ngx_event.h>

#include "httplite_server.h"
#include "httplite_request.h"
#include "httplite_upstream.h"
#include "httplite_request_list.h"

#define LENGTH_HEADER "\nContent-Length: "
#define LENGTH_HEADER_SIZE strlen(LENGTH_HEADER)

#define HEADER_BODY_SEPARATOR "\r\n\r\n"
#define HEADER_BODY_SEPARATOR_SIZE strlen(HEADER_BODY_SEPARATOR)

#define HTTP_411_RESPONSE "HTTP/1.1 411 Length Required\r\nCache-Control: no-cache, private\r\n\r\n"

#define RETRY_READ_TIME 1000
#define READ_SLAB_MAX 5


void httplite_close_connection(ngx_connection_t *c) {
    ngx_pool_t  *pool;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "close http connection: %d", c->fd);

    c->destroyed = 1;

    pool = c->pool;

    ngx_close_connection(c);

    if (pool) {
        ngx_destroy_pool(pool);
    }
}

void *httplite_calloc(ngx_pool_t *pool, size_t size) {
    void *ret = ngx_pcalloc(pool, size);
    if (!ret) {
        ngx_log_error(NGX_LOG_ALERT, pool->log, 0, "unable to create allocate memory on connection pool.");
        return NULL;
    }

    return ret;
}

void httplite_request_handler(ngx_event_t *rev) {
    ngx_connection_t *c;
    httplite_client_data_t *request_data;
    httplite_request_list_t *read_list;

    c = rev->data;

    if (c->destroyed) {
        ngx_log_debug0(NGX_LOG_ALERT, c->log, 0, "trying to access closed connection.");
        return;
    }

    if (httplite_check_broken_connection(c) != NGX_OK) {
        ngx_log_debug0(NGX_LOG_WARN, c->log, 0, "Client was closed.");
        httplite_close_connection(c);
        return;
    }

    if (!c->data) {
        httplite_client_data_t *new_request_data = ngx_pcalloc(c->pool, sizeof(httplite_client_data_t));
        if (!new_request_data) {
            return;
        }
        
        new_request_data->read_list = httplite_init_list(c);
        if (!new_request_data->read_list) {
            return;
        }

        new_request_data->write_list = NULL;
        new_request_data->write_list_tail = NULL;

        new_request_data->staging_list = httplite_init_list(c);
        if (!new_request_data->staging_list) {
            return;
        }

        if (!httplite_add_slab(new_request_data->staging_list)) {
            return;
        }

        new_request_data->bytes_remaining = 0;
        new_request_data->step_number = 0;
        new_request_data->pending_read_slabs = 0;
        c->data = new_request_data;
    }

    request_data = c->data;

    if (rev->timedout) {
        if (request_data->write_list == request_data->write_list_tail) {
            return;
        }

        httplite_send_request_list(request_data);
        ngx_add_timer(rev, RETRY_READ_TIME);
    }

    read_list = request_data->read_list;

    if (!httplite_add_slab(request_data->read_list)) {
        return;
    }

    // TODO: fix this functionality - create limit for how many unread slabs are allowed
    // if (request_data->pending_read_slabs >= READ_SLAB_MAX) {
    //     httplite_close_connection(c);
    //     return;
    // }

    if (rev->ready) {
        ssize_t n = c->recv(c, read_list->tail->buffer_start, SLAB_SIZE);

        if (n == NGX_AGAIN) {
            return;
        }

        if (n <= 0) {
            httplite_close_connection(c);
            return;
        }

        read_list->tail->size = n;
        /* null-terminate so strstr in split_request does not read past the buffer */
        read_list->tail->buffer_start[n] = '\0';
        request_data->pending_read_slabs += 1;
    }

    httplite_split_request(request_data, c);
    request_data->pending_read_slabs -= 1;

    httplite_send_request_list(request_data);

    if (request_data->write_list != request_data->write_list_tail) {
        ngx_add_timer(rev, RETRY_READ_TIME);
    }
}

void httplite_send_request_list(httplite_client_data_t *request_data) {
    int send_failed = 0;
    httplite_request_list_t *write_list = request_data->write_list;

    // loop until we get to the tail, which is an empty list that has yet to have been populated
    while (request_data->write_list != request_data->write_list_tail) {
        send_failed = httplite_send_request_to_upstream(write_list);

        // if we are unable to send, break so we do not advance the list
        if (send_failed) {
            break;
        }

        write_list = httplite_advance_list(write_list);
        request_data->write_list = write_list;
    }
}

/*
 * Split-request helpers.
 *
 * Each helper handles one phase of the request-splitting state machine and
 * returns SPLIT_CONTINUE (keep looping) or SPLIT_DONE (exit the splitter).
 */
#define SPLIT_CONTINUE 0
#define SPLIT_DONE     1

/*
 * Step 0 — Scan for the header-body separator ("\r\n\r\n").
 *
 * Handles the case where the separator straddles the boundary between the
 * staging slab (previous recv) and the current read slab.
 */
static int
httplite_split_find_separator(httplite_client_data_t *request_data,
                              ngx_connection_t *c,
                              httplite_request_slab_t *read_slab)
{
    httplite_request_slab_t *staging_slab = request_data->staging_list->head;

    request_data->bytes_remaining = 0;

    /* check if separator is split across slabs */
    size_t split_bytes = 0;

    if (staging_slab->size != 0) {
        u_char *end   = staging_slab->buffer_start + staging_slab->size;
        u_char *start = read_slab->buffer_start;
        u_char  last  = *(end - 1);

        /* only \r (0x0d) and \n (0x0a) can border the split — fast reject */
        if ((last & 0xF8) == 0x08) {

            if (last == '\r') {
                /* staging ends \r: sep[0] (1+3) or sep[2] (3+1) */
                if (*start == '\n') {
                    if (staging_slab->size >= 3 &&
                        *(end - 3) == '\r' && *(end - 2) == '\n') {
                        /* \r\n\r | \n */
                        split_bytes = 1;
                    } else if (read_slab->size >= 3 &&
                               *(start + 1) == '\r' &&
                               *(start + 2) == '\n') {
                        /* \r | \n\r\n */
                        split_bytes = 3;
                    }
                }

            } else if (last == '\n' &&
                       staging_slab->size >= 2 && *(end - 2) == '\r' &&
                       read_slab->size >= 2 &&
                       *start == '\r' && *(start + 1) == '\n') {
                /* \r\n | \r\n */
                split_bytes = 2;
            }
        }
    }

    if (split_bytes != 0) {
        read_slab->buffer_pos = read_slab->buffer_start;
        httplite_copy_to_list(request_data->staging_list, split_bytes,
                              read_slab, &read_slab->buffer_pos);
        request_data->step_number = 1;
        return SPLIT_CONTINUE;
    }

    /* search for the separator in the current read slab */
    u_char *lookahead   = (u_char *) ngx_strstr(read_slab->buffer_pos,
                                                 HEADER_BODY_SEPARATOR);
    size_t bytes_searched = (read_slab->buffer_start + read_slab->size)
                            - read_slab->buffer_pos;

    if (lookahead != NULL) {
        size_t copy_size = (lookahead - read_slab->buffer_pos)
                           + HEADER_BODY_SEPARATOR_SIZE;
        httplite_copy_to_list(request_data->staging_list, copy_size,
                              read_slab, &read_slab->buffer_pos);
        request_data->step_number = 1;
        return SPLIT_CONTINUE;
    }

    if (bytes_searched >= SLAB_SIZE) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                      "headers span more than one packet");
        return SPLIT_DONE;
    }

    /* separator not yet found — buffer what we have and wait for more data */
    size_t copy_size = (read_slab->buffer_start + read_slab->size)
                       - read_slab->buffer_pos;
    httplite_copy_to_list(request_data->staging_list, copy_size,
                          read_slab, &read_slab->buffer_pos);
    return SPLIT_DONE;
}

/*
 * Step 1 — Identify the HTTP method and, for POST, extract Content-Length.
 *
 * GET requests are complete once the headers are in staging.
 * POST requests transition to step 2 (body accumulation).
 */
static int
httplite_split_parse_method(httplite_client_data_t *request_data,
                            ngx_connection_t *c)
{
    httplite_request_slab_t *staging_slab = request_data->staging_list->head;
    httplite_request_list_t *wl           = request_data->write_list_tail;

    /* ---- GET ---- */
    if (memcmp(staging_slab->buffer_start, "GET", 3) == 0) {
        httplite_copy_to_list(wl, staging_slab->size, staging_slab,
                              &staging_slab->buffer_start);

        request_data->staging_list->head =
            httplite_add_slab(request_data->staging_list);

        request_data->write_list_tail =
            httplite_add_list_to_chain(wl, c);

        request_data->step_number = 0;
        return SPLIT_CONTINUE;
    }

    /* ---- reject anything else that isn't POST ---- */
    if (memcmp(staging_slab->buffer_start, "POST", 4) != 0) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                      "invalid request type (only GET and POST accepted)");
        return SPLIT_DONE;
    }

    /* ---- POST: locate Content-Length header ---- */
    u_char *header_loc = ngx_strlcasestrn(
        staging_slab->buffer_start,
        staging_slab->buffer_start + staging_slab->size,
        (u_char *) LENGTH_HEADER, LENGTH_HEADER_SIZE - 1);

    if (header_loc == NULL) {
        c->send(c, (u_char *) HTTP_411_RESPONSE, strlen(HTTP_411_RESPONSE));
        ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                      "411: GET request should not have body");
        httplite_close_connection(c);
        return SPLIT_DONE;
    }

    /* parse the Content-Length value */
    u_char *runner = header_loc + LENGTH_HEADER_SIZE;
    u_char *start  = runner;

    while (*runner != '\r' && *runner != '\n') {
        ++runner;
    }

    ssize_t body_size = ngx_atosz(start, runner - start);

    if (body_size < 0) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                      "content length value invalid");
        httplite_close_connection(c);
        return SPLIT_DONE;
    }

    request_data->bytes_remaining = body_size;

    httplite_copy_to_list(wl, staging_slab->size, staging_slab,
                          &staging_slab->buffer_start);

    request_data->staging_list->head =
        httplite_add_slab(request_data->staging_list);

    /* Content-Length: 0 means no body to read */
    if (body_size == 0) {
        request_data->write_list_tail =
            httplite_add_list_to_chain(wl, c);
        request_data->step_number = 0;
        return SPLIT_CONTINUE;
    }

    request_data->step_number = 2;
    return SPLIT_CONTINUE;
}

/*
 * Step 2 — Accumulate POST body bytes according to Content-Length.
 */
static int
httplite_split_copy_body(httplite_client_data_t *request_data,
                         ngx_connection_t *c,
                         httplite_request_slab_t *read_slab)
{
    httplite_request_list_t *wl = request_data->write_list_tail;
    size_t remaining_read_space = (read_slab->buffer_start + read_slab->size)
                                  - read_slab->buffer_pos;

    if (request_data->bytes_remaining > remaining_read_space) {
        /* more body data expected on the next slab */
        httplite_copy_to_list(wl, remaining_read_space, read_slab,
                              &read_slab->buffer_pos);
        request_data->bytes_remaining -= remaining_read_space;
        return SPLIT_DONE;
    }

    /* final chunk of body — complete this request */
    httplite_copy_to_list(wl, request_data->bytes_remaining, read_slab,
                          &read_slab->buffer_pos);
    request_data->bytes_remaining = 0;

    request_data->write_list_tail =
        httplite_add_list_to_chain(wl, c);

    request_data->step_number = 0;
    return SPLIT_CONTINUE;
}

void httplite_split_request(httplite_client_data_t *request_data, ngx_connection_t *c) {

    httplite_request_slab_t *read_slab = request_data->read_list->tail;

    if (request_data->write_list_tail == NULL) {
        request_data->write_list_tail = httplite_init_list(c);
        request_data->write_list = request_data->write_list_tail;
    }

    while (read_slab->buffer_pos != read_slab->buffer_start + read_slab->size) {
        int result;

        switch (request_data->step_number) {
            case 0:
                result = httplite_split_find_separator(request_data, c, read_slab);
                break;
            case 1:
                result = httplite_split_parse_method(request_data, c);
                break;
            case 2:
                result = httplite_split_copy_body(request_data, c, read_slab);
                break;
            default:
                return;
        }

        if (result == SPLIT_DONE) {
            return;
        }
    }
}

/* Helper methods to print out requests in the queue (used for testing) */

void httplite_print_requests (httplite_request_list_t *requests) {
    printf("%s", "Printing request queue\n\n");
    httplite_request_list_t *curr = requests;
    size_t i = 1;
    if(curr == NULL){
        printf("%s\n","the list is empty");
        fflush(stdout);
    }
    while(curr != NULL){
        printf("\n---------------Printing request %zu---------------\n", i);
        httplite_print_request(curr);
        printf("\n-------------Done printing request %zu-------------\n", i);
        i++;
        curr = curr->next;
    }
    printf("%s", "Done printing request queue \n\n");
    fflush(stdout);
}

void httplite_print_request(httplite_request_list_t *request) {
    httplite_request_slab_t *curr = request->head;
    size_t i = 1;
    while(curr != NULL) {
        if (i > 1) {
            printf("\n\n****NEW SLAB****\n\n");
        }
        printf("%s", curr->buffer_pos);
        fflush(stdout);
        ++i;
        curr = curr->next;
    }
}
