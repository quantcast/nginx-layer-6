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
    list->head->next= new_slab;
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
    httplite_request_slab_t   *curr;
    ngx_connection_t          *c;

    /* read_list = all of the connection buffer content in a single list 
    *           (read_list potentially includes multiple pipelined requests)
    *  write_list = the head of a list of lists where each list is a single request
    *           (write_list will be constructed by the split_request function after looking at read_list)
    */
    httplite_request_list_t    read_list, write_list; 

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

    curr = read_list.head;
    
    if (read_list.head == NULL) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to allocate space for the request slab.");
        ngx_httplite_close_connection(c);
        return;
    }

    if (read_list.head->buffer == NULL) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to allocate space for the request string.");
        ngx_httplite_close_connection(c);
        return;
    }

    n = recv_wrapper(c, curr, rev);

    /* read entire connection into read_list*/
    while (rev->ready) {
        m = 0;
        if (!httplite_add_slab(&read_list)) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0, "unable to add slab.");
            return;
        }
        m = recv_wrapper(c, read_list.tail, rev);

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
    
    printRequests(&read_list);
    write_list = *split_request(&read_list, &write_list);
    printRequests(&write_list);   
}

/* given a list of slabs, break it up into a list of lists, 
* each one containing a single request 
*/
httplite_request_list_t *split_request (httplite_request_list_t *read_list, httplite_request_list_t *write_list) {
    printf("%s", "Splitting request\n\n");
    fflush(stdout);
    u_char *curr, *start_ptr;
    httplite_request_slab_t *read_slab;
    size_t l, first_iter;
    ssize_t body_size;
    httplite_request_list_t *head, temp;
    head = write_list; /* keeps track of the head of the list of lists to return at the end */

    read_slab = read_list->head;
    curr = read_slab->buffer;
    first_iter = 1;

    while (true) { /* each iteration will find one request */
        printf("\nrunning iteration\n");
        printf("read slab size = %zu\n", read_slab->size);
        printf("curr = %zu\n", (size_t) curr);
        printf("end of slab = %zu\n", (size_t) read_slab->buffer + read_slab->size);
        fflush(stdout);
        printRequests(head);

        if(curr >= read_slab->buffer + read_slab->size || read_slab == NULL) {
            printf("break cond 1\n");
            fflush(stdout);
            if (read_slab->next == NULL) {
                printf("break cond 2\n");
                fflush(stdout);
                break;
            }
            read_slab = read_slab->next;
        }

        /* make a new list to hold the next request, except on the first iteration */
        if (!first_iter) {
            /* we think this init_list call is causing a bug
            * it seems like the bug occurs when init_list(c) is called after c->recv has been called 
            * when init_list is called before the recv call, there is no bug */
            temp = httplite_init_list(write_list->connection);
            write_list->next = &temp;
            write_list = &temp;
        }
        first_iter = 0;

        body_size = 0;

        /* start_ptr will point to the first location we HAVE NOT copied yet 
        *  each time we call copy, we advance start_ptr by that much 
        *  curr will be the location in the read_slab we are currently looking at
        *  usually we copy in chunks, each chunk starting at start_ptr and ending at curr
        */

        start_ptr = curr;

        /* find "Content Length" header */
        curr = ngx_strlcasestrn(start_ptr, start_ptr + read_slab->size, (u_char*) LENGTH_HEADER, LENGTH_HEADER_SIZE - 1);
        // TODO: check next slab for content length
        // TODO: check if content length header is cut off by slab, e.g. "Content le"

        /* get content length value */
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
        printf("content length = %zu\n", body_size);
        fflush(stdout);

        /* find header-body separator */ 
        curr = (u_char*) ngx_strstr(curr, HEADER_BODY_SEPARATOR);

        if (curr == NULL) { 
            printf("separator was null\n");
            fflush(stdout);
            /* in this case, headers continue onto next slab.
            * we need to copy what we currently have before moving to the next slab */
            copy_to_list(write_list, read_slab->buffer + SLAB_SIZE - start_ptr, &read_slab, &start_ptr);

            /* move to next slab */
            read_slab = read_slab->next;
            start_ptr = read_slab->buffer;

            /* for now assuming that the end of the headers is on the next slab */
            curr = (u_char*) ngx_strstr(start_ptr, HEADER_BODY_SEPARATOR); 
            curr += HEADER_BODY_SEPARATOR_SIZE;
        }
        
        /* curr - start_ptr = size of headers + size of header_body_separator
        *  curr - start_ptr + body_size = size of entire request
        */
        copy_to_list(write_list, curr - start_ptr + body_size, &read_slab, &start_ptr);
        printf("copied successfully\n");
        fflush(stdout);
        curr = start_ptr + 1;
    }
    printf("\ndone splitting");
    fflush(stdout);
    return head;
}

/* this is a recursive function
*  see header file for documentation 
*/
void copy_to_list(httplite_request_list_t *write_list, size_t size, 
                    httplite_request_slab_t **read_slab, u_char **read_start_ptr) {

    /* base case */
    if (size == 0) { 
        return;
    }

    printf("size = %zu\n", size);
    fflush(stdout);

    /* read_length = how much we need to read from current slab */
    size_t read_length = (*read_slab)->buffer + SLAB_SIZE - *read_start_ptr; 
    printf("read_space = %zu\n", read_length);
    fflush(stdout);

    /* read_length should never be bigger than the size of the request */
    if (read_length > size) { 
        read_length = size;
    }

    httplite_request_slab_t *write_slab = write_list->tail;

    /* write_space = how much we can write on the current slab */
    size_t write_space = SLAB_SIZE - write_slab->size;

    /* if we have read the current slab, move to next slab */
    if (read_length == 0) {
        *read_slab = (*read_slab)->next; 
        *read_start_ptr = (*read_slab)->buffer;
    }

    /* if we have filled up the current write slab, make a new one */
    if (write_space == 0) {
        httplite_add_slab(write_list);
        write_slab = write_list->tail;
    }

    /* we will memcpy an amount equal to MIN(read_length, write_space) */
    size_t min_space;
    if (read_length <= write_space) {
        min_space = read_length;
    } else {
        min_space = write_space;
    }
    printf("min_space = %zu\n", min_space);
    fflush(stdout);

    memcpy(write_slab->buffer + write_slab->size, *read_start_ptr, min_space);

    /* advance read pointer */
    *read_start_ptr += min_space;

    /* recursive call */
    copy_to_list(write_list, size-min_space, read_slab, read_start_ptr);
}

/*Helper methods to print out requests in the queue*/

void printRequests (httplite_request_list_t *requests) {
    printf("%s", "Printing request list\n\n");
    httplite_request_list_t *curr = requests;
    size_t i = 1;
    while(curr != NULL){
        printf("%s","Starting to print request ");
        printf("%zu\n\n", i);
        printRequest(curr);
        printf("%s", "Done with the request ");
        printf("%zu\n\n", i);
        i++;
        curr = curr->next;
    }
    printf("%s", "Done printing request list\n\n");
    fflush(stdout);
}

void printRequest(httplite_request_list_t *request) {
    httplite_request_slab_t *curr = request->head;
     
    while(curr != NULL) {
        printf("%s\n\n", curr->buffer);
        fflush(stdout);
        curr = curr->next;
    }
}