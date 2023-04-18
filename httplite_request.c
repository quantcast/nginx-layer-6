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

#define GET_REQUEST "GET"
#define GET_REQUEST_LEN strlen(GET_REQUEST)

#define POST_REQUEST "POST"
#define POST_REQUEST_LEN strlen(POST_REQUEST)

#define HTTP_411_RESPONSE "HTTP/1.1 411 Length Required\r\nCache-Control: no-cache, private\r\n\r\n"

#define str3_cmp(m, c0, c1, c2)     (m[0] == c0 && m[1] == c1 && m[2] == c2)
#define str4_cmp(m, c0, c1, c2, c3) (m[0] == c0 && m[1] == c1 && m[2] == c2 && m[3] == c3)

httplite_request_list_t *httplite_init_list(ngx_connection_t *connection) {
    httplite_request_list_t *list = ngx_pcalloc(connection->pool, sizeof(httplite_request_list_t));
    if (!list) {
        ngx_log_error(NGX_ERROR_ALERT, connection->log, 0, "unable to allocate memory for list.");
        return NULL;
    }
    
    // httplite_request_slab_t *head = ngx_pcalloc(connection->pool, sizeof(httplite_request_slab_t));
    
    // if (!head) {
    //     ngx_log_error(NGX_ERROR_ALERT, connection->log, 0, "unable to create head slab on connection's memory pool.");
    //     return NULL;
    // }

    // head->buffer = ngx_pnalloc(connection->pool, SLAB_SIZE);

    // if (!head->buffer) {
    //     ngx_log_error(NGX_ERROR_ALERT, connection->log, 0, "unable to the string buffer on connection's memory pool.");
    //     return NULL;
    // }

    // head->size = 0;
    // head->next = NULL;
    // list->head = head;
    // list->tail = head;
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

void httplite_free_request_slab(httplite_request_slab_t *slab, ngx_pool_t *pool) {
    ngx_pfree(pool, slab->start);
    ngx_pfree(pool, slab);
}

void httplite_free_request_list(httplite_request_list_t *list) {
    httplite_request_slab_t *curr = list->head;
    while (curr) {
        httplite_request_slab_t *temp = curr;
        curr = curr->next;
        httplite_free_request_slab(temp, list->connection->pool);
    }
    ngx_pfree(list->connection->pool, list);
}

void httplite_remove_head(httplite_request_list_t *list) {
    httplite_free_request_list(list);
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
    httplite_request_list_t *read_list, *write_list;
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

        if (!httplite_add_slab(new_request_data->read_list)) {
            return;
        }

        new_request_data->write_list = NULL;
        c->data = new_request_data;
    }

    request_data = c->data;
    read_list = request_data->read_list;
    write_list = request_data->write_list;

    if (rev->ready) {
        read_list->head->size = c->recv(c, read_list->head->buffer, SLAB_SIZE);
    }

    write_list = split_request(read_list, write_list, c);
}

/* given a list of slabs, break it up into a list of lists, 
* each one containing a single request 
*/
httplite_request_list_t *split_request (httplite_request_list_t *read_list, httplite_request_list_t *write_list, 
                                        ngx_connection_t *c) {
    u_char *curr, *start_ptr, *temp;
    httplite_request_slab_t *read_slab;
    size_t l, iter_no, header_size;
    ssize_t body_size;
    httplite_request_list_t *head;
    enum HTTP_method method; 
    head = write_list; /* keeps track of the head of the list of lists to return at the end */

    read_slab = read_list->head;
    curr = read_slab->buffer;
    iter_no = 1;

    while(read_slab != NULL && curr <= read_slab->buffer + read_slab->size) { /* each iteration will find one request */

        if (curr == read_slab->buffer + read_slab->size) {
            if (read_slab->next == NULL) {
                break;
            }
            // if (read_slab->size < SLAB_SIZE) {
            //     ngx_log_error(NGX_LOG_ALERT, c->log, 0, "underfull slab followed by another slab");
            //     httplite_close_connection(c);
            //     return NULL;
            // }
        }

        /* make a new list to hold the next request, except on the first iteration */
        if (iter_no != 1) {
            httplite_request_list_t *temp = httplite_init_list(write_list->connection);
            httplite_add_slab(temp);
            write_list->next = temp;
            write_list = write_list->next;
        }
        ++iter_no;

        body_size = 0;
        header_size = 0;

        /* start_ptr will point to the first location we HAVE NOT copied yet 
        *  each time we call copy, we advance start_ptr by that much 
        *  curr will be the location in the read_slab we are currently looking at
        *  usually we copy in chunks, each chunk starting at start_ptr and ending at curr
        */

        start_ptr = curr;

        /* find header-body separator 
        *  will set curr to first char of the separator */ 
        curr = (u_char*) ngx_strstr(curr, HEADER_BODY_SEPARATOR);

        if (curr == NULL) {
            /* in this case (separator not found), headers continue onto next slab*/

            /* confirm that the current slab is full */
            /*if (read_slab->size != SLAB_SIZE) {
                ngx_log_error(NGX_LOG_ALERT, c->log, 0, "current slab underfull ");
                return NULL;
            }*/

            /* save these pointers before copying to use later when checking if separator is split */
            u_char *end = read_slab->buffer + read_slab->size;
            u_char *start = read_slab->next->buffer;

            /* update header_size */
            header_size = read_slab->buffer + read_slab->size - start_ptr;

            /* copy what we currently have before moving to the next slab */
            copy_to_list(write_list, header_size, &read_slab, &start_ptr);

            /* check if separator is split across slabs */
            if (read_slab->next == NULL) {
                ngx_log_error(NGX_LOG_ALERT, c->log, 0, "cannot find next slab");
                return NULL;
            }

            /* check if separator is split across slabs */
            if (*(end - 3) == HEADER_BODY_SEPARATOR[0] &&
                *(end - 2) == HEADER_BODY_SEPARATOR[1] &&
                *(end - 1) == HEADER_BODY_SEPARATOR[2] &&
                *(start)   == HEADER_BODY_SEPARATOR[3]) {
                printf("\n\n--------split 1--------\n\n");
                fflush(stdout);
                copy_to_list(write_list, 1, &read_slab, &start_ptr);
            } else if (*(end - 2) == HEADER_BODY_SEPARATOR[0] &&
                       *(end - 1) == HEADER_BODY_SEPARATOR[1] &&
                       *(start) == HEADER_BODY_SEPARATOR[2] &&
                       *(start + 1)   == HEADER_BODY_SEPARATOR[3]) {
                printf("\n\n--------split 2--------\n\n");
                fflush(stdout);
                copy_to_list(write_list, 2, &read_slab, &start_ptr);
            } else if (*(end - 1) == HEADER_BODY_SEPARATOR[0] &&
                       *(start) == HEADER_BODY_SEPARATOR[1] &&
                       *(start + 1) == HEADER_BODY_SEPARATOR[2] &&
                       *(start + 2)   == HEADER_BODY_SEPARATOR[3]) {
                printf("\n\n--------split 3--------\n\n");
                fflush(stdout);
                copy_to_list(write_list, 3, &read_slab, &start_ptr);
            }
            // TODO: if these happen, the following strstr search should not happen

            /* move to next slab */
            read_slab = read_slab->next;
            start_ptr = read_slab->buffer;

            /* for now assuming that the end of the headers is on the next slab */
            curr = (u_char*) ngx_strstr(start_ptr, HEADER_BODY_SEPARATOR);

            if (curr == NULL) {
                ngx_log_error(NGX_LOG_ALERT, c->log, 0, "header-body separator not found");
                // TODO: close connection?
                return NULL;
            } 

            curr += HEADER_BODY_SEPARATOR_SIZE;
            header_size += curr - start_ptr;
            
            /* copy up to separator */
            copy_to_list(write_list, curr - start_ptr, &read_slab, &start_ptr);
        } else {
            /* headers are all on the same slab */
            curr += HEADER_BODY_SEPARATOR_SIZE;
            header_size += curr - start_ptr;

            /* copy up to end of separator */
            copy_to_list(write_list, header_size, &read_slab, &start_ptr);
        }

        httplite_request_slab_t *header_slab = write_list->head;

        if (str3_cmp(header_slab->buffer, 'G', 'E', 'T')) {
            method = GET;
        } else if (str4_cmp(header_slab->buffer, 'P', 'O', 'S', 'T')) {
            method = POST;
        } else {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unknown method detected (only GET and POST accepted)");
            httplite_close_connection(c);
            return NULL;
        }
      
        /* find "Content Length" header */
        u_char* header_loc = ngx_strlcasestrn(header_slab->buffer, header_slab->buffer + header_slab->size, 
                                              (u_char*) LENGTH_HEADER, LENGTH_HEADER_SIZE - 1);

        /* get content length value */
        if (header_loc != NULL) { 
            if (method == GET) { /* GET request should not have a body */
                // TODO: verify that this 411 response is sent correctly (here and elsewhere)
                c->send(c, (u_char*) HTTP_411_RESPONSE, strlen(HTTP_411_RESPONSE));
                ngx_log_error(NGX_LOG_ALERT, c->log, 0, "411: GET request should not have body");
                httplite_close_connection(c);
                return NULL;
            }

            curr = header_loc;
            curr += LENGTH_HEADER_SIZE;
            temp = curr;

            /* find size of substring containing value after the header */
            while (*curr != '\r' && *curr != '\n') {
                ++curr;
            }

            body_size = ngx_atosz(temp, curr - temp);

            if (body_size < 0) {
                ngx_log_error(NGX_LOG_ALERT, c->log, 0, "content length value invalid");
                httplite_close_connection(c);
                return NULL;
            }
        } else {
            if (method == POST) { /* POST request must have body */
                c->send(c, (u_char*) HTTP_411_RESPONSE, strlen(HTTP_411_RESPONSE));
                ngx_log_error(NGX_LOG_ALERT, c->log, 0, "411: POST request detected but no body found");
                httplite_close_connection(c);
                return NULL;
            }
        }

        /* copy body */
        copy_to_list(write_list, body_size, &read_slab, &start_ptr);
        curr = start_ptr;
    }

    printRequests(head);
    return head;
}

/* this is a recursive function
*  see header file for documentation 
*/
void copy_to_list(httplite_request_list_t *write_list, size_t read_size, 
                    httplite_request_slab_t **read_slab, u_char **read_start_ptr) {

    /* base case */
    if (read_size == 0) { 
        return;
    }

    /* read_length = how much we need to read from current slab */
    size_t read_length = (*read_slab)->buffer + (*read_slab)->size - *read_start_ptr; 
   
    /* read_length should never be bigger than the size of the read_size,
    which most of the time should be request_size */
    if (read_length > read_size) { 
        read_length = read_size;
    }
    httplite_request_slab_t *write_slab = write_list->tail;

    /* how much we can write on the current slab */
    size_t write_space = SLAB_SIZE - write_slab->size;
   
    /* we will copy up to MIN(read_length, write_space) */
    size_t copy_bytes;
    if (read_length <= write_space) {
        copy_bytes = read_length;
    } else {
        copy_bytes = write_space;
    }

    memcpy(write_slab->buffer + write_slab->size, *read_start_ptr, copy_bytes);

    /*d ecrement read_size */
    read_size -= copy_bytes;
    /* update the buffer size */
    write_slab->size += copy_bytes;
    /* advance read pointer */
    *read_start_ptr += copy_bytes;
     
    /* if we have read the current slab and we still need to read more, move to next slab */
    if (read_length - copy_bytes == 0 && read_size != 0) {
        *read_slab = (*read_slab)->next; 
        *read_start_ptr = (*read_slab)->buffer;
    }

    /* if we have filled up the current write slab and still needs to write, make a new one */
    if (write_space-copy_bytes == 0 && read_length > 0) {
        httplite_add_slab(write_list);
        write_slab = write_list->tail;
    }

    /* recursive call */
    copy_to_list(write_list, read_size, read_slab, read_start_ptr);
}

/*Helper methods to print out requests in the queue*/

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
