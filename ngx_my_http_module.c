#include <nginx.h>
#include <ngx_core.h>
#include <ngx_config.h>
#include <ngx_event.h>
#include <ngx_http.h>
#include <string.h>
#include <ngx_socket.h>

#include "ngx_my_http_module.h"

#include <stdio.h>

ngx_uint_t ngx_my_http_max_module;

static char* ngx_my_http_block(ngx_conf_t *configuration, ngx_command_t *command, void *base_configuration);
static ngx_int_t ngx_my_http_init_listening(ngx_conf_t *cf, ngx_int_t port);
void ngx_my_http_init_connection(ngx_connection_t *c);
u_char* ngx_my_http_accept_log_error(ngx_log_t *log, u_char *buf, size_t len);

static ngx_command_t ngx_my_http_commands[] = {
  {
    ngx_string("myhttp"),
    NGX_MAIN_CONF | NGX_CONF_BLOCK | NGX_CONF_NOARGS,
    ngx_my_http_block,
    0,
    0,
    NULL
  },
  ngx_null_command
};

static ngx_core_module_t  ngx_my_http_module_context = {
    ngx_string("myhttp"),
    NULL,
    NULL
};

ngx_module_t  ngx_my_http_module = {
    NGX_MODULE_V1,
    &ngx_my_http_module_context,           /* module context */
    ngx_my_http_commands,                  /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

static char* ngx_my_http_block(ngx_conf_t *configuration, ngx_command_t *command, void *base_configuration) {

    char                                *rv;
    ngx_uint_t                           m, module_index;
    ngx_conf_t                           pcf;
    ngx_my_http_module_t                *module;
    ngx_my_http_configuration_context_t *context;
    // ngx_my_http_main_configuration_t    *core_main_configuration;

    /* the main http context */
    context = ngx_pcalloc(configuration->pool, sizeof(ngx_my_http_configuration_context_t));
    if (context == NULL) {
        return NGX_CONF_ERROR;
    }

    *(ngx_my_http_configuration_context_t **) base_configuration = context;

    /* count all modules and set up their indices */
    ngx_my_http_max_module = 0;
    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_MY_HTTP_MODULE) {
            continue;
        }

        ngx_modules[m]->ctx_index = ngx_my_http_max_module++;
    }

    /* the my_http main context */
    context->main_configuration = ngx_pcalloc(configuration->pool, ngx_my_http_max_module * sizeof(void*));
    if (context->main_configuration == NULL) {
        return NGX_CONF_ERROR;
    }

    /* create the main configuration */
    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_MY_HTTP_MODULE) {
            continue;
        }

        module = ngx_modules[m]->ctx;
        module_index = ngx_modules[m]->ctx_index;

        if (module->create_main_configuration) {
            context->main_configuration[module_index] = module->create_main_configuration(configuration);
            if (context->main_configuration[module_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }

    }

    /* parse inside the myhttp{} block */
    pcf = *configuration;
    configuration->ctx = context;

    configuration->module_type = NGX_MY_HTTP_MODULE;
    configuration->cmd_type = NGX_MY_HTTP_MAIN_CONFIGURATION;
    rv = ngx_conf_parse(configuration, NULL);

    if (rv != NGX_CONF_OK) {
        *configuration = pcf;
        return rv;
    }

    // core_main_configuration = context->main_configuration[ngx_my_http_module.ctx_index];

    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_MY_HTTP_MODULE) {
            continue;
        }

        module = ngx_modules[m]->ctx;
        module_index = ngx_modules[m]->ctx_index;

        /* init tcp{} main_configurationss */
        configuration->ctx = context;

        if (module->init_main_configuration) {
            rv = module->init_main_configuration(configuration, context->main_configuration[module_index]);
            if (rv != NGX_CONF_OK) {
                *configuration = pcf;
                return rv;
            }
        }
    }

    *configuration = pcf;

    if (ngx_my_http_init_listening(configuration, 0xB822) != NGX_OK) {
        printf("Failed to init connection\n");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

void
ngx_my_http_close_connection(ngx_connection_t *c)
{
    ngx_pool_t  *pool;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "close http connection: %d", c->fd);

    c->destroyed = 1;

    pool = c->pool;

    ngx_close_connection(c);

    ngx_destroy_pool(pool);
}

static void ngx_my_http_wait_request_handler(ngx_event_t *rev)
{
    printf("entering the wait request handler: \n");
    
    // u_char                    *p;
    size_t                     size;
    ssize_t                    n;
    ngx_buf_t                 *b;
    ngx_connection_t          *c;
    // ngx_http_connection_t     *hc;

    c = rev->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http wait request handler");

    if (rev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        ngx_my_http_close_connection(c);
        return;
    }

    if (c->close) {
        ngx_my_http_close_connection(c);
        return;
    }

    // hc = c->data;

    size = 1024;

    b = c->buffer;

    if (b == NULL) {
        printf("case 1\n");
        b = ngx_create_temp_buf(c->pool, size);
        if (b == NULL) {
            ngx_my_http_close_connection(c);
            return;
        }

        c->buffer = b;

    } else if (b->start == NULL) {
        printf("case 2\n");
        b->start = ngx_palloc(c->pool, size);
        if (b->start == NULL) {
            ngx_my_http_close_connection(c);
            return;
        }

        b->pos = b->start;
        b->last = b->start;
        b->end = b->last + size;
    }

    n = c->recv(c, b->last, size);

    if (n == NGX_AGAIN) {

        if (!rev->timer_set) {
            ngx_add_timer(rev, 60 * 1000);
            ngx_reusable_connection(c, 1);
        }

        if (ngx_handle_read_event(rev, 0) != NGX_OK) {
            ngx_my_http_close_connection(c);
            return;
        }

        /*
         * We are trying to not hold c->buffer's memory for an idle connection.
         */

        if (ngx_pfree(c->pool, b->start) == NGX_OK) {
            b->start = NULL;
        }

        return;
    }

    if (n == NGX_ERROR) {
        ngx_my_http_close_connection(c);
        return;
    }

    if (n == 0) {
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                      "client closed connection");
        ngx_my_http_close_connection(c);
        return;
    }

    b->last += n;

    c->log->action = "reading client request line";

    ngx_reusable_connection(c, 0);

    printf("request string: %s\n", (char *) rev->data);
}

void ngx_my_http_empty_handler(ngx_event_t *wev)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, wev->log, 0, "http empty handler");

    return;
}

static u_char *
ngx_http_log_error(ngx_log_t *log, u_char *buf, size_t len)
{
   return NGX_OK;
}

void ngx_my_http_init_connection(ngx_connection_t *c)
{
    printf("initializing connection\n");
    // ngx_uint_t                 i;
    ngx_event_t               *rev;
    // struct sockaddr_in        *sin;
    // ngx_http_port_t           *port;

    c->log->connection = c->number;
    c->log->handler = ngx_http_log_error;
    c->log->data = NULL;
    c->log->action = "waiting for request";

    c->log_error = NGX_ERROR_INFO;

    rev = c->read;
    rev->handler = ngx_my_http_wait_request_handler;
    c->write->handler = ngx_my_http_empty_handler;

    if (rev->ready) {
        /* the deferred accept(), iocp */

        if (ngx_use_accept_mutex) {
            ngx_post_event(rev, &ngx_posted_events);
            return;
        }

        rev->handler(rev);

        return;
    }

    ngx_add_timer(rev, 60 * 1000);
    ngx_reusable_connection(c, 1);

    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        ngx_my_http_close_connection(c);
        return;
    }
}

static ngx_int_t
ngx_my_http_init_listening(ngx_conf_t *cf, ngx_int_t port)
{
    ngx_listening_t *ls;
    struct sockaddr_in *socket_address;
    size_t socket_length = sizeof(struct sockaddr_in);

    ngx_str_t raw_address = ngx_string("127.0.0.1");

    socket_address = ngx_pcalloc(cf->pool, socket_length);
    if (socket_address == NULL) {
        printf("Failed to allocate socket address\n");
        return NGX_ERROR;
    }
    ngx_memzero(socket_address, socket_length);
    socket_address->sin_family = AF_INET;
    socket_address->sin_port = port;
    socket_address->sin_len = socket_length;
    socket_address->sin_addr.s_addr = ngx_inet_addr(raw_address.data, raw_address.len);
    
    ls = ngx_create_listening(cf, (struct sockaddr*)socket_address, socket_length);
    if (ls == NULL) {
        printf("Failed to create listening socket\n");
        return NGX_ERROR;
    }

    ls->addr_ntop = 1;

    ls->handler = ngx_my_http_init_connection;
    ls->pool_size = 512;

    ls->logp = cf->log;
    ls->log.data = &ls->addr_text;
    ls->log.handler = ngx_my_http_accept_log_error;

    ls->backlog = -1;
    ls->rcvbuf = SO_RCVBUF;
    ls->sndbuf = SO_SNDBUF;

    ls->keepalive = 1;

    return NGX_OK;
}

u_char* ngx_my_http_accept_log_error(ngx_log_t *log, u_char *buf, size_t len)
{
    printf("accepting connection\n");
    return ngx_snprintf(buf, len, " while accepting new connection on %V",
                        log->data);
}

// static ssize_t
// ngx_my_http_read_request_header(ngx_http_request_t *r)
// {
//     ssize_t                    n;
//     ngx_event_t               *rev;
//     ngx_connection_t          *c;
//     ngx_http_core_srv_conf_t  *cscf;

//     c = r->connection;
//     rev = c->read;

//     n = r->header_in->last - r->header_in->pos;

//     if (n > 0) {
//         return n;
//     }

//     if (rev->ready) {
//         n = c->recv(c, r->header_in->last,
//                     r->header_in->end - r->header_in->last);
//     } else {
//         n = NGX_AGAIN;
//     }

//     if (n == NGX_AGAIN) {
//         if (!rev->timer_set) {
//             cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);
//             ngx_add_timer(rev, cscf->client_header_timeout);
//         }

//         if (ngx_handle_read_event(rev, 0) != NGX_OK) {
//             // ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
//             return NGX_ERROR;
//         }

//         return NGX_AGAIN;
//     }

//     if (n == 0) {
//         ngx_log_error(NGX_LOG_INFO, c->log, 0,
//                       "client prematurely closed connection");
//     }

//     if (n == 0 || n == NGX_ERROR) {
//         c->error = 1;
//         c->log->action = "reading client request headers";

//         // ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
//         return NGX_ERROR;
//     }

//     r->header_in->last += n;

//     return n;
// }
