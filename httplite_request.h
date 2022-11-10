#ifndef HTTPLITE_HTTP_REQUEST_H
#define HTTPLITE_HTTP_REQUEST_H

#include <nginx.h>
#include <ngx_core.h>
#include <ngx_event.h>

#define NODE_SIZE 1500      /* MTU size */

// typedef struct httplite_request_s {
//     ngx_pool_t  *pool;      /* The pool in which this memory lies */
//     u_char      *start;     /* A pointer to the start of the request string */
//     u_char      *end;       /* A pointer to the end of allocated memory for the string */
//     u_char      *last;      /* A pointer to the last character of the request string */
//     size_t       size;      /* The amount of memory allocated for the request string */
// } httplite_request_t;

typedef struct httplite_request_s {
    size_t size;                    /* The total size of the request (headers + body)*/
    ngx_connection_t *connection;   /* A pointer to the parent connection */
    httplite_list_t *list;          /* A pointer to the linked list holding the request string */
} httplite_request_t;

typedef struct httplite_node_s {
    uchar[NODE_SIZE] data;          /* An array holding the data for this node*/
    size_t filled;                  /* The total amount that has been filled so far (0 <= filled <= NODE_SIZE) */
    httplite_node_t  *next;         /* A pointer to the next node in the list */
} httplite_node_t;

typedef struct httplite_list_s {
    httplite_node_t *head;          /* The head node of the linked list */
    httplite_node_t *tail;          /* The tail node of the linked list */
    ngx_pool_t *pool;               /* The pool where the linked list holds its nodes */
} httplite_list_t;

/* Initializes an linked list containing one node in the given pool */
httplite_list_t *init_httplite_list(ngx_pool_t *pool);

/* Adds a node to the given list. Returns 0 on success. */
int *httplite_add_node(httplite_list_t *list);


/**
 * Allocates memory for the httplite request string on the given pool of an
 * nginx connection. The request string starts with the given size. Allocates this
 * structure in the pool as well, and hence must be freed after.
 *
 * NOTE: If the connection is closed, the pool is no longer valid.
*/
httplite_request_t *init_httplite_request(ngx_connection_t *connection);

/**
 * Allocates more space for the string in the given request object. Increases the
 * size of the request by a factor of 2. Frees the old allocated memory.
 */
int httplite_request_realloc(httplite_request_t *request);

/**
 * Frees all memory associated with this request.
*/
int httplite_request_free(httplite_request_t *request);

void httplite_request_handler(ngx_event_t *rev);
void httplite_request_close_connection(ngx_connection_t *c);

#endif
