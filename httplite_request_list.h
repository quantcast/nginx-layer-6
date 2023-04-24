#ifndef HTTPLITE_REQUEST_LIST_H
#define HTTPLITE_REQUEST_LIST_H

#include <nginx.h>
#include <ngx_core.h>

#define SLAB_SIZE 1500                              /* MTU size */

typedef struct httplite_request_slab_s {
    u_char *buffer_start;                     /* A pointer to the memory holding the start of the request string */
    u_char *buffer_pos;                       /* A pointer to the memory holding the current position in the request string */
    struct httplite_request_slab_s *next;     /* A pointer to the next slab in the linked list */
    size_t size;                              /* The number of bytes that have been filled in the slab */
} httplite_request_slab_t;

typedef struct httplite_request_list_s {
    httplite_request_slab_t *head;
    httplite_request_slab_t *tail;
    httplite_request_slab_t *curr;
    ngx_connection_t *connection;               /* A pointer to the parent connection */
    struct httplite_request_list_s *next;       /* A pointer to next request in pipeline queue */
} httplite_request_list_t;

httplite_request_list_t *httplite_init_list(ngx_connection_t *connection);
httplite_request_list_t *httplite_add_list_to_chain(httplite_request_list_t *list, ngx_connection_t *c);
httplite_request_slab_t *httplite_add_slab(httplite_request_list_t *list);

httplite_request_list_t *httplite_advance_list(httplite_request_list_t *list);

void httplite_free_slab(ngx_connection_t *c, httplite_request_slab_t *slab);
void httplite_free_list(httplite_request_list_t *list);

void httplite_copy_to_list(
    httplite_request_list_t *write_list,
    size_t read_size,
    httplite_request_slab_t *read_slab,
    u_char **read_start_ptr
);

#endif
