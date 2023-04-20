#include "httplite_request_list.h"

#include <nginx.h>
#include <ngx_core.h>

httplite_request_list_t *httplite_init_list(ngx_connection_t *connection) {
    /* does not add a slab to the list, i.e. list->head is NULL. This must be done manually */
    httplite_request_list_t *list = ngx_pcalloc(connection->pool, sizeof(httplite_request_list_t));
    if (!list) {
        ngx_log_error(NGX_ERROR_ALERT, connection->log, 0, "unable to allocate memory for list.");
        return NULL;
    }
    
    list->connection = connection;
    list->head = NULL;
    list->tail = NULL;
    list->curr = NULL;
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

httplite_request_slab_t *httplite_add_slab(httplite_request_list_t *list) {
    httplite_request_slab_t *new_slab = ngx_pcalloc(list->connection->pool, sizeof(httplite_request_slab_t));

    if (!new_slab) {
        ngx_log_error(NGX_ERROR_ALERT, list->connection->log, 0, "unable to create new slab on connection's memory pool.");
        return NULL;
    }

    new_slab->size = 0;
    new_slab->buffer_start = ngx_pnalloc(list->connection->pool, SLAB_SIZE);
    if (!new_slab->buffer_start) {
        ngx_log_error(NGX_ERROR_ALERT,list->connection->log, 0, "unable to the string buffer on connection's memory pool.");
        return NULL;
    }

    new_slab->buffer_pos = new_slab->buffer_start;
    new_slab->next = NULL;

    if (!list->head) {
        list->head = new_slab;
        list->tail = new_slab;
        list->curr = new_slab;
        list->next = NULL;
        return new_slab;
    }

    // add the slab to the tail of the list
    list->tail->next= new_slab;
    list->tail = new_slab;

    return new_slab;
}

httplite_request_list_t *httplite_advance_list(httplite_request_list_t *list) {
    if (!list) {
        return NULL;
    }

    // we are at the tail
    if (!list->head) {
        return list;
    }

    return list->next;
}

void httplite_free_list(httplite_request_list_t *list) {
    httplite_request_slab_t *slab;
    slab = list->head;
    while (slab) {
        void *free = slab;
        slab = slab->next;
        ngx_pfree(list->connection->pool, free);
    }

    ngx_pfree(list->connection->pool, list);
}

void httplite_copy_to_list (
  httplite_request_list_t *write_list,
  size_t read_size, 
  httplite_request_slab_t *read_slab,
  u_char **read_start_ptr
) {

    size_t available_read_space = read_slab->buffer_start + read_slab->size - *read_start_ptr;
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

        memcpy(write_slab->buffer_pos + write_slab->size, *read_start_ptr, copy_bytes);

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
