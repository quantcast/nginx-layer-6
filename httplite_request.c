#include <nginx.h>
#include <ngx_core.h>
#include <ngx_string.h>
#include <ngx_event.h>

#include "httplite_request.h"
#include "httplite_upstream.h"

#define LENGTH_HEADER "\nContent-Length: "
#define LENGTH_HEADER_SIZE strlen(LENGTH_HEADER)

#define HEADER_BODY_SEPARATOR "\r\n\r\n"
#define HEADER_BODY_SEPARATOR_SIZE strlen(HEADER_BODY_SEPARATOR)

#define HTTP_411_RESPONSE "HTTP/1.1 411 Length Required\r\nCache-Control: no-cache, private\r\n\r\n"

#define str3_cmp(m, c0, c1, c2)     (m[0] == c0 && m[1] == c1 && m[2] == c2)
#define str4_cmp(m, c0, c1, c2, c3) (m[0] == c0 && m[1] == c1 && m[2] == c2 && m[3] == c3)

httplite_request_list_t *httplite_init_list(ngx_connection_t *connection) {
    /* does not add a slab to the list, i.e. list->head is NULL. This must be done manually */
    httplite_request_list_t *list = ngx_pcalloc(connection->pool, sizeof(httplite_request_list_t));
    if (!list) {
        ngx_log_error(NGX_ERROR_ALERT, connection->log, 0, "unable to allocate memory for list.");
        return NULL;
    }
    
    list->connection = connection;
    list->next = NULL;

    return list;
}

httplite_request_list_t *httplite_add_list_to_chain(httplite_request_list_t *list, ngx_connection_t *c) {
    if (!list) {
        return httplite_init_list(c);
    }

    list->next = httplite_init_list(list->connection);
    return list->next;
}

int httplite_advance_slab_iterator(httplite_request_slab_t *slab, size_t size) {
    if (slab->size + size > SLAB_SIZE) {
        return NGX_ERROR;
    }

    slab->buffer += size;
    slab->size -= size;
    return NGX_OK;
}

httplite_request_slab_t *httplite_add_slab(httplite_request_list_t *list) {
    httplite_request_slab_t *new_slab = ngx_pcalloc(list->connection->pool, sizeof(httplite_request_slab_t));

    if (!new_slab) {
        ngx_log_error(NGX_ERROR_ALERT, list->connection->log, 0, "unable to create new slab on connection's memory pool.");
        return NULL;
    }

    new_slab->size = 0;
    new_slab->start = ngx_pnalloc(list->connection->pool, SLAB_SIZE);
    if (!new_slab->start) {
        ngx_log_error(NGX_ERROR_ALERT,list->connection->log, 0, "unable to the string buffer on connection's memory pool.");
        return NULL;
    }

    new_slab->buffer = new_slab->start;

    if (!list->head) {
        list->head = new_slab;
        list->tail = new_slab;
        list->next = NULL;
        return new_slab;
    }

    // add the slab to the tail of the list
    list->tail->next= new_slab;
    list->tail = new_slab;

    return new_slab;
}

void httplite_close_connection(ngx_connection_t *c)
{
    ngx_pool_t  *pool;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "close http connection: %d", c->fd);

    c->destroyed = 1;

    pool = c->pool;

    ngx_close_connection(c);

    ngx_destroy_pool(pool);
}

void httplite_empty_handler() {}

void httplite_upstream_read_handler(ngx_event_t *event) {
    ngx_connection_t *connection = event->data;
    httplite_event_connection_t *connections = connection->data;
    ngx_connection_t *client = connections->client_connection;
    ngx_connection_t *upstream = connections->upstream_connection;

    int n;

    httplite_request_slab_t *response_slab = ngx_pcalloc(client->pool, sizeof(httplite_request_slab_t));
    if (!response_slab) {
        fprintf(stderr, "Unable to initialize response slab in httplite_upstream_read_handler.\n");
        return;
    }

    response_slab->buffer = ngx_pnalloc(client->pool, SLAB_SIZE);
    if (!response_slab->buffer) {
        fprintf(stderr, "Unable to initialize buffer space in httplite_upstream_read_handler.\n");
        return;
    }

    // read the content from the upstream and store it on the current connection so as to prevent blocking on the upstream connection
    n = upstream->recv(upstream, response_slab->buffer, SLAB_SIZE);
    response_slab->size += n;

    // make sure that client has copy of the data as well
    ((httplite_event_connection_t*)(upstream->data))->response = response_slab;
    client->data = upstream->data;

    // wait until client is write ready to send to client
    if (!client->write->ready) {
        ngx_add_timer(event, DEFAULT_CLIENT_WRITE_TIMEOUT);
        return;
    }

    client->send(client, response_slab->buffer, response_slab->size);
    upstream->read->handler = httplite_empty_handler;
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
    read_list = request_data->read_list;

    if (!httplite_add_slab(request_data->read_list)) {
        return;
    }

    if (rev->ready) {
        read_list->tail->size = c->recv(c, read_list->tail->start, SLAB_SIZE);
        request_data->pending_read_slabs += 1;
    }

    split_request(request_data, c);
    request_data->pending_read_slabs -= 1;
}

void split_request (httplite_request_data_t *request_data, ngx_connection_t *c) {

    httplite_request_slab_t *read_slab = request_data->read_list->tail;

    if (request_data->write_list_tail == NULL) {
        request_data->write_list_tail = httplite_init_list(c);
        request_data->write_list = request_data->write_list_tail;
    }

    httplite_request_list_t *write_list = request_data->write_list_tail;
    httplite_request_slab_t *staging_slab = request_data->staging_list->head;
    u_char* lookahead;


    while (read_slab->buffer != read_slab->start + read_slab->size) {
        switch (request_data->step_number) {
            case 0:     /* find header-body separator*/
                lookahead = NULL;
                request_data->bytes_remaining = 0;

                /* check if separator is split across slabs */
                size_t split_bytes = 0;
                if (staging_slab->size != 0) {
                    u_char* end = staging_slab->start + staging_slab->size;
                    u_char* start = read_slab->start;
                    
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
                    read_slab->buffer = read_slab->start;
                    copy_to_list(request_data->staging_list, split_bytes, read_slab, &(read_slab->buffer));
                    request_data->step_number = 1;
                    break;
                }

                /* search for header-body separator */
                lookahead = (u_char*) ngx_strstr(read_slab->buffer, HEADER_BODY_SEPARATOR);
                size_t bytes_searched = read_slab->start + read_slab->size - read_slab->buffer;
                /* bytes_searched is used for error checking later */

                if (lookahead != NULL) {
                    /* if found, copy up to the separator into staging and advance to next step */
                    size_t copy_size = lookahead - read_slab->buffer + HEADER_BODY_SEPARATOR_SIZE;
                    copy_to_list(request_data->staging_list, copy_size, read_slab, &(read_slab->buffer));
                    request_data->step_number = 1;
                    break;
                } else {
                    if (bytes_searched >= SLAB_SIZE) {
                        /* we are assuming all headers should fit on one slab */
                        ngx_log_error(NGX_ERROR_ALERT, c->log, 0, "headers span more than one packet");
                        return;
                    } else {
                        /* if separator is not found, copy up to end of slab into staging and stay on this step */
                        size_t copy_size = read_slab->start + read_slab->size - read_slab->buffer;
                        copy_to_list(request_data->staging_list, copy_size, read_slab, &(read_slab->buffer));
                        return;
                    }
                }
            case 1:     /* find Content Length header and parse its value */
                if (str3_cmp(staging_slab->start, 'G', 'E', 'T')) {
                    /* if we have a GET request and found the separator, we have found the whole request */
                    if (write_list == NULL) {
                        write_list = httplite_init_list(c);
                        request_data->write_list_tail = write_list;
                    }
                    /* copy staging into write list */
                    copy_to_list(write_list, staging_slab->size, staging_slab, &(staging_slab->start));
                    
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
                    if (!str4_cmp(staging_slab->start, 'P', 'O', 'S', 'T')) {
                        ngx_log_error(NGX_ERROR_ALERT, c->log, 0, "invalid request type (only GET and POST accepted)");
                        return;
                    }
                }

                /* find "Content Length" header */
                u_char* header_loc = ngx_strlcasestrn(staging_slab->start, staging_slab->start + staging_slab->size, 
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
                    copy_to_list(write_list, staging_slab->size, staging_slab, &(staging_slab->start));
                    
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
                size_t remaining_read_space = read_slab->start + read_slab->size - read_slab->buffer;
                if (request_data->bytes_remaining > remaining_read_space) {
                    /* there's more of the body on the next slab */
                    if (write_list == NULL) {
                        write_list = httplite_init_list(c);
                        request_data->write_list_tail = write_list;
                    }
                    copy_to_list(write_list, remaining_read_space, read_slab, &(read_slab->buffer));
                    request_data->bytes_remaining -= remaining_read_space;
                    return;
                } else {
                    if (write_list == NULL) {
                        write_list = httplite_init_list(c);
                        request_data->write_list_tail = write_list;
                    }
                    copy_to_list(write_list, request_data->bytes_remaining, read_slab, &(read_slab->buffer));
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

void copy_to_list (httplite_request_list_t *write_list, size_t read_size, 
                    httplite_request_slab_t *read_slab, u_char **read_start_ptr) {

    size_t available_read_space = read_slab->start + read_slab->size - *read_start_ptr;
    if (read_size > available_read_space) {
        ngx_log_error(NGX_LOG_ALERT, write_list->connection->log, 0, "copy size out of bounds");
        return;
    }

    if (write_list->head == NULL) {
        httplite_add_slab(write_list);
    }
    
    while (read_size > 0) { /* each iteration will fill up to a single write slab */
        httplite_request_slab_t *write_slab = write_list->tail;

        /* how much we can write on the current slab */
        size_t write_space = SLAB_SIZE - write_slab->size;
    
        /* we will copy up to MIN(read_length, write_space) */
        size_t copy_bytes;
        if (read_size <= write_space) {
            copy_bytes = read_size;
        } else {
            copy_bytes = write_space;
        }

        memcpy(write_slab->buffer + write_slab->size, *read_start_ptr, copy_bytes);

        /* decrement read_size */
        read_size -= copy_bytes;
        /* update the buffer size */
        write_slab->size += copy_bytes;
        /* advance read pointer */
        *read_start_ptr += copy_bytes;

        /* if we have filled up the current write slab and still needs to write, make a new one */
        if (write_space - copy_bytes == 0 && read_size > 0) {
            httplite_add_slab(write_list);
        }
    }

}

/* Helper methods to print out requests in the queue (used for testing) */

void printRequests (httplite_request_list_t *requests) {
    printf("%s", "Printing request queue\n\n");
    httplite_request_list_t *curr = requests;
    size_t i = 1;
    if(curr == NULL){
        printf("%s\n","the list is empty");
        fflush(stdout);
    }
    while(curr != NULL){
        printf("\n---------------Printing request %zu---------------\n", i);
        printRequest(curr);
        printf("\n-------------Done printing request %zu-------------\n", i);
        i++;
        curr = curr->next;
    }
    printf("%s", "Done printing request queue \n\n");
    fflush(stdout);
}

void printRequest(httplite_request_list_t *request) {
    httplite_request_slab_t *curr = request->head;
    size_t i = 1;
    while(curr != NULL) {
        if (i > 1) {
            printf("\n\n****NEW SLAB****\n\n");
        }
        printf("%s", curr->buffer);
        fflush(stdout);
        ++i;
        curr = curr->next;
    }
}
