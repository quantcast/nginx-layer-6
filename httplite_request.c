#include <nginx.h>
#include <ngx_core.h>
#include <ngx_string.h>
#include <ngx_event.h>

#include "httplite_request.h"

#define LENGTH_HEADER "\nContent-Length: "
#define LENGTH_HEADER_SIZE strlen(LENGTH_HEADER)
#define HEADER_BODY_SEPARATOR "\r\n\r\n"
#define HEADER_BODY_SEPARATOR_SIZE strlen(HEADER_BODY_SEPARATOR)
#define HTTP_411_RESPONSE "HTTP/1.1 411 Length Required\r\nCache-Control: no-cache, private\r\n\r\n"

#define str3_cmp(m, c0, c1, c2)     (m[0] == c0 && m[1] == c1 && m[2] == c2)
#define str4_cmp(m, c0, c1, c2, c3) (m[0] == c0 && m[1] == c1 && m[2] == c2 && m[3] == c3)


httplite_request_list_t *httplite_init_list(ngx_connection_t *connection) {
    httplite_request_list_t *list = ngx_pcalloc(connection->pool, sizeof(httplite_request_list_t));
    
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
    head->next = NULL;
    list->head = head;
    list->tail = head;
    list->connection = connection;
    list->next = NULL;
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

    // add the slab to the tail of the list
    list->tail->next= new_slab;
    list->tail = new_slab;

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
    httplite_request_slab_t   *curr;
    ngx_connection_t          *c;

    /* read_list = all of the connection buffer content in a single list 
    *           (read_list potentially includes multiple pipelined requests)
    *  write_list = the head of a list of lists where each list is a single request
    *           (write_list will be constructed by the split_request function after looking at read_list)
    */
    httplite_request_list_t    *read_list, *write_list; 

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
 
    read_list = httplite_init_list(c);
    write_list = httplite_init_list(c);
   
    curr = read_list->head;
    
    if (read_list->head == NULL) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to allocate space for the request slab.");
        ngx_httplite_close_connection(c);
        return;
    }

    if (read_list->head->buffer == NULL) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to allocate space for the request string.");
        ngx_httplite_close_connection(c);
        return;
    }

    n = recv_wrapper(c, curr, rev);

    /* read entire connection into read_list*/
    while (rev->ready) {
        m = 0;
        if (!httplite_add_slab(read_list)) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to add slab.");
            return;
        }
        m = recv_wrapper(c, read_list->tail, rev);

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
    //printRequests(read_list);
    write_list = split_request(read_list, write_list, c);

    // if (write_list == NULL) {
    //     ngx_httplite_close_connection(c);
    // }

    printRequests(write_list);
    printf("Total bytes read = %zu\n\n",n);
    fflush(stdout);
}

/* given a list of slabs, break it up into a list of lists, 
* each one containing a single request 
*/
httplite_request_list_t *split_request (httplite_request_list_t *read_list, httplite_request_list_t *write_list, 
                                        ngx_connection_t *c) {
    u_char *curr, *start_ptr, *temp;
    httplite_request_slab_t *read_slab;
    size_t l, first_iter, header_size;
    ssize_t body_size;
    httplite_request_list_t *head;
    enum HTTP_method method; 
    head = write_list; /* keeps track of the head of the list of lists to return at the end */

    read_slab = read_list->head;
    curr = read_slab->buffer;
    first_iter = 1;

    for(;;) { /* each iteration will find one request */
        if(read_slab == NULL || curr >= read_slab->buffer + read_slab->size) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0, "error reading requests from connection buffer");
            return NULL;
        }

        /* make a new list to hold the next request, except on the first iteration */
        if (first_iter != 1) {
            httplite_request_list_t *temp = httplite_init_list(write_list->connection);
            write_list->next = temp;
            write_list = write_list->next;
        }
        ++first_iter;

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

        // TODO: handle when separator is split by packet boundary
        if (curr == NULL) {
            /* in this case (separator not found), headers continue onto next slab*/

            /* confirm that the current slab is full */
            if (read_slab->size != SLAB_SIZE) {
                ngx_log_error(NGX_LOG_ALERT, c->log, 0, "header-body separator not found");
                return NULL;
            }

            /* update header_size */
            header_size += read_slab->buffer + read_slab->size - start_ptr;

            /* copy what we currently have before moving to the next slab */
            copy_to_list(write_list, header_size, &read_slab, &start_ptr);

            /* move to next slab */
            read_slab = read_slab->next;
            start_ptr = read_slab->buffer;

            /* for now assuming that the end of the headers is on the next slab */
            curr = (u_char*) ngx_strstr(start_ptr, HEADER_BODY_SEPARATOR);

            if (curr == NULL) {
                ngx_log_error(NGX_LOG_ALERT, c->log, 0, "header-body separator not found");
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
            ngx_httplite_close_connection(c);
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
                ngx_httplite_close_connection(c);
                return NULL;
            }

            curr = header_loc;
            curr += LENGTH_HEADER_SIZE;
            temp = curr;
            l = 0;

            /* find size of substring containing value after the header */
            while (*curr != '\r' && *curr != '\n') {
                ++curr;
            }

            body_size = ngx_atosz(temp, l);
        } else {
            if (method == POST) { /* POST request must have body */
                c->send(c, (u_char*) HTTP_411_RESPONSE, strlen(HTTP_411_RESPONSE));
                ngx_log_error(NGX_LOG_ALERT, c->log, 0, "411: POST request detected but no body found");
                ngx_httplite_close_connection(c);
                return NULL;
            }
        }

        /* copy body */
        copy_to_list(write_list, body_size, &read_slab, &start_ptr);
        curr = start_ptr;
        printRequest(write_list);
        fflush(stdout);
    }

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
