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

#define READ_SLAB_MAX 5

#define str3_cmp(m, c0, c1, c2)     (m[0] == c0 && m[1] == c1 && m[2] == c2)
#define str4_cmp(m, c0, c1, c2, c3) (m[0] == c0 && m[1] == c1 && m[2] == c2 && m[3] == c3)

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
        ngx_log_error(NGX_ERROR_ALERT, pool->log, 0, "unable to create allocate memory on connection pool.");
        return NULL;
    }

    return ret;
}

void httplite_request_handler(ngx_event_t *rev) {
    ngx_connection_t *c;
    httplite_request_data_t *request_data;
    httplite_request_list_t *read_list;
    httplite_request_slab_t *next_slab;

    c = rev->data;

    if (!c->data) {
        httplite_request_data_t *new_request_data = ngx_pcalloc(c->pool, sizeof(httplite_request_data_t));
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
        printf("hit timedout\n");
        if (request_data->write_list == request_data->write_list_tail) {
            return;
        }

        httplite_send_request_list(request_data);
        ngx_add_timer(rev, DEFAULT_SERVER_TIMEOUT);
    }

    read_list = request_data->read_list;

    if (!httplite_add_slab(request_data->read_list)) {
        return;
    }

    if (request_data->pending_read_slabs >= READ_SLAB_MAX) {
        httplite_close_connection(c);
        return;
    }

    if (rev->ready) {
        read_list->tail->size = c->recv(c, read_list->tail->buffer_start, SLAB_SIZE);
        request_data->pending_read_slabs += 1;
    }

    httplite_split_request(request_data, c);
    request_data->pending_read_slabs -= 1;

    httplite_send_request_list(request_data);

    if (request_data->write_list != request_data->write_list_tail) {
        ngx_add_timer(rev, DEFAULT_SERVER_TIMEOUT);
    }
}

void httplite_send_request_list(httplite_request_data_t *request_data) {
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

void httplite_split_request(httplite_request_data_t *request_data, ngx_connection_t *c) {

    httplite_request_slab_t *read_slab = request_data->read_list->tail;

    if (request_data->write_list_tail == NULL) {
        request_data->write_list_tail = httplite_init_list(c);
        request_data->write_list = request_data->write_list_tail;
    }

    httplite_request_list_t *write_list = request_data->write_list_tail;
    httplite_request_slab_t *staging_slab = request_data->staging_list->head;
    u_char* lookahead;

    while (read_slab->buffer_pos != read_slab->buffer_start + read_slab->size) {
        switch (request_data->step_number) {
            case 0:     /* find header-body separator*/
                lookahead = NULL;
                request_data->bytes_remaining = 0;

                /* check if separator is split across slabs */
                size_t split_bytes = 0;
                if (staging_slab->size != 0) {
                    u_char* end = staging_slab->buffer_start + staging_slab->size;
                    u_char* start = read_slab->buffer_start;
                    
                    if (*(end - 3) == HEADER_BODY_SEPARATOR[0] &&
                        *(end - 2) == HEADER_BODY_SEPARATOR[1] &&
                        *(end - 1) == HEADER_BODY_SEPARATOR[2] &&
                        *(start)   == HEADER_BODY_SEPARATOR[3]) {
                        split_bytes = 1;
                    } else if (*(end - 2) == HEADER_BODY_SEPARATOR[0] &&
                            *(end - 1) == HEADER_BODY_SEPARATOR[1] &&
                            *(start) == HEADER_BODY_SEPARATOR[2] &&
                            *(start + 1)   == HEADER_BODY_SEPARATOR[3]) {
                        split_bytes = 2;
                    } else if (*(end - 1) == HEADER_BODY_SEPARATOR[0] &&
                            *(start) == HEADER_BODY_SEPARATOR[1] &&
                            *(start + 1) == HEADER_BODY_SEPARATOR[2] &&
                            *(start + 2)   == HEADER_BODY_SEPARATOR[3]) {
                        split_bytes = 3;
                    }
                }

                if (split_bytes != 0) { /* separator was split */
                    read_slab->buffer_pos = read_slab->buffer_start;
                    httplite_copy_to_list(request_data->staging_list, split_bytes, read_slab, &(read_slab->buffer_pos));
                    request_data->step_number = 1;
                    break;
                }

                /* search for header-body separator */
                lookahead = (u_char*) ngx_strstr(read_slab->buffer_pos, HEADER_BODY_SEPARATOR);
                size_t bytes_searched = read_slab->buffer_start + read_slab->size - read_slab->buffer_pos;
                /* bytes_searched is used for error checking later */

                if (lookahead != NULL) {
                    /* if found, copy up to the separator into staging and advance to next step */
                    size_t copy_size = lookahead - read_slab->buffer_pos + HEADER_BODY_SEPARATOR_SIZE;
                    httplite_copy_to_list(request_data->staging_list, copy_size, read_slab, &(read_slab->buffer_pos));
                    request_data->step_number = 1;
                    break;
                } else {
                    if (bytes_searched >= SLAB_SIZE) {
                        /* we are assuming all headers should fit on one slab */
                        ngx_log_error(NGX_ERROR_ALERT, c->log, 0, "headers span more than one packet");
                        return;
                    } else {
                        /* if separator is not found, copy up to end of slab into staging and stay on this step */
                        size_t copy_size = read_slab->buffer_start + read_slab->size - read_slab->buffer_pos;
                        httplite_copy_to_list(request_data->staging_list, copy_size, read_slab, &(read_slab->buffer_pos));
                        return;
                    }
                }
            case 1:     /* find Content Length header and parse its value */
                if (str3_cmp(staging_slab->buffer_start, 'G', 'E', 'T')) {
                    /* if we have a GET request and found the separator, we have found the whole request */
                    if (write_list == NULL) {
                        write_list = httplite_init_list(c);
                        request_data->write_list_tail = write_list;
                    }
                    /* copy staging into write list */
                    httplite_copy_to_list(write_list, staging_slab->size, staging_slab, &(staging_slab->buffer_start));
                    
                    /* reset the staging slab */
                    request_data->staging_list->head = httplite_add_slab(request_data->staging_list);
                    staging_slab = request_data->staging_list->head;

                    /* make new write list */
                    write_list = httplite_add_list_to_chain(write_list, c);
                    request_data->write_list_tail = write_list;

                    /* reset step to 0 for next request */
                    request_data->step_number = 0;
                    break;
                } else {
                    /* confirm we have a POST request*/
                    if (!str4_cmp(staging_slab->buffer_start, 'P', 'O', 'S', 'T')) {
                        ngx_log_error(NGX_ERROR_ALERT, c->log, 0, "invalid request type (only GET and POST accepted)");
                        return;
                    }
                }

                /* find "Content Length" header */
                u_char* header_loc = ngx_strlcasestrn(staging_slab->buffer_start, staging_slab->buffer_start + staging_slab->size, 
                                              (u_char*) LENGTH_HEADER, LENGTH_HEADER_SIZE - 1);

                if (header_loc != NULL) {
                    u_char* runner = header_loc;
                    runner += LENGTH_HEADER_SIZE;
                    u_char* temp = runner;

                    /* find size of substring containing value after the header */
                    while (*runner != '\r' && *runner != '\n') {
                        ++runner;
                    }

                    /* convert body size value from string to size_t */
                    size_t body_size = ngx_atosz(temp, runner - temp);

                    if (body_size < 0) {
                        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "content length value invalid");
                        httplite_close_connection(c);
                        return;
                    }

                    request_data->bytes_remaining = body_size;
                    if (write_list == NULL) {
                        write_list = httplite_init_list(c);
                        request_data->write_list_tail = write_list;
                    }
                    /* copy entire staging area into write_list */
                    httplite_copy_to_list(write_list, staging_slab->size, staging_slab, &(staging_slab->buffer_start));
                    
                    /* reset the staging slab*/
                    request_data->staging_list->head = httplite_add_slab(request_data->staging_list);
                    staging_slab = request_data->staging_list->head;

                    request_data->step_number = 2;
                    break;
                } else {
                    c->send(c, (u_char*) HTTP_411_RESPONSE, strlen(HTTP_411_RESPONSE));
                    ngx_log_error(NGX_LOG_ALERT, c->log, 0, "411: GET request should not have body");
                    httplite_close_connection(c);
                    return;
                }

            case 2:     /* copy up to the end of the body */
                size_t remaining_read_space = read_slab->buffer_start + read_slab->size - read_slab->buffer_pos;
                if (request_data->bytes_remaining > remaining_read_space) {
                    /* there's more of the body on the next slab */
                    if (write_list == NULL) {
                        write_list = httplite_init_list(c);
                        request_data->write_list_tail = write_list;
                    }
                    httplite_copy_to_list(write_list, remaining_read_space, read_slab, &(read_slab->buffer_pos));
                    request_data->bytes_remaining -= remaining_read_space;
                    return;
                } else {
                    if (write_list == NULL) {
                        write_list = httplite_init_list(c);
                        request_data->write_list_tail = write_list;
                    }
                    httplite_copy_to_list(write_list, request_data->bytes_remaining, read_slab, &(read_slab->buffer_pos));
                    request_data->bytes_remaining = 0;

                    /* make new write list */
                    write_list = httplite_add_list_to_chain(write_list, c);
                    request_data->write_list_tail = write_list;

                    /* reset step number to set up for next request */
                    request_data->step_number = 0;
                    break;
                }
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
