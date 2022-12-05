#include <nginx.h>
#include <ngx_core.h>
#include "httplite_module.h"
#include "httplite_http_module.h"
#include "httplite_module_configuration.h"

ngx_int_t httplite_http_block_initialization(ngx_conf_t *configuration) {
    // associating configuration with module
    return NGX_OK;
}

void* httplite_http_block_create_main_configuration(ngx_conf_t* configuration) {
    httplite_main_configuration_t* httplite_main_configuration;
    httplite_main_configuration = ngx_pcalloc(
        configuration->pool, sizeof(httplite_main_configuration_t)
    );
    
    if (httplite_main_configuration == NULL) {
        return NULL;
    }

    return httplite_main_configuration;
}

char *
httplite_core_server(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy)
{
    char                        *rv;
    void                        *mconf;
    // size_t                       len;
    // u_char                      *p;
    ngx_uint_t                   i;
    ngx_conf_t                   pcf;
    httplite_module_t           *module;
    // struct sockaddr_in          *sin;
    httplite_configuration_context_t         *ctx, *http_ctx;
    // ngx_http_listen_opt_t        lsopt;
    httplite_server_conf_t    *cscf;
    httplite_main_configuration_t   *cmcf;

    ctx = ngx_pcalloc(cf->pool, sizeof(httplite_configuration_context_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    http_ctx = cf->ctx;
    ctx->main_configuration = http_ctx->main_configuration;

    /* the server{}'s srv_conf */

    ctx->server_configuration = ngx_pcalloc(cf->pool, sizeof(void *));
    if (ctx->server_configuration == NULL) {
        return NGX_CONF_ERROR;
    }

    for (i = 0; cf->cycle->modules[i]; i++) {
        if (cf->cycle->modules[i]->type != HTTPLITE_MODULE) {
            continue;
        }

        module = cf->cycle->modules[i]->ctx;

        if (module->create_server_configuration) {
            mconf = module->create_server_configuration(cf);
            if (mconf == NULL) {
                return NGX_CONF_ERROR;
            }

            ctx->server_configuration[cf->cycle->modules[i]->ctx_index] = mconf;
        }
    }

    /* the server configuration context */

    cscf = ctx->server_configuration[httplite_http_module.ctx_index];
    cscf->ctx = ctx;

    cmcf = ctx->main_configuration[httplite_http_module.ctx_index];
    (void) cmcf;

    // cscfp = ngx_array_push(&cmcf->servers);
    // if (cscfp == NULL) {
    //     return NGX_CONF_ERROR;
    // }

    // *cscfp = cscf;


    /* parse inside server{} */

    pcf = *cf;
    cf->ctx = ctx;
    cf->cmd_type = HTTPLITE_SERVER_CONFIGURATION;

    rv = ngx_conf_parse(cf, NULL);

    *cf = pcf;

    printf("port: %lu\n", cscf->port);
    printf("server name: %s\n", cscf->server_name.data);
    fflush(stdout);

//     if (rv == NGX_CONF_OK && !cscf->listen) {
//         ngx_memzero(&lsopt, sizeof(ngx_http_listen_opt_t));

//         p = ngx_pcalloc(cf->pool, sizeof(struct sockaddr_in));
//         if (p == NULL) {
//             return NGX_CONF_ERROR;
//         }

//         lsopt.sockaddr = (struct sockaddr *) p;

//         sin = (struct sockaddr_in *) p;

//         sin->sin_family = AF_INET;
// #if (NGX_WIN32)
//         sin->sin_port = htons(80);
// #else
//         sin->sin_port = htons((getuid() == 0) ? 80 : 8000);
// #endif
//         sin->sin_addr.s_addr = INADDR_ANY;

//         lsopt.socklen = sizeof(struct sockaddr_in);

//         lsopt.backlog = NGX_LISTEN_BACKLOG;
//         lsopt.rcvbuf = -1;
//         lsopt.sndbuf = -1;
// #if (NGX_HAVE_SETFIB)
//         lsopt.setfib = -1;
// #endif
// #if (NGX_HAVE_TCP_FASTOPEN)
//         lsopt.fastopen = -1;
// #endif
//         lsopt.wildcard = 1;

//         len = NGX_INET_ADDRSTRLEN + sizeof(":65535") - 1;

//         p = ngx_pnalloc(cf->pool, len);
//         if (p == NULL) {
//             return NGX_CONF_ERROR;
//         }

//         lsopt.addr_text.data = p;
//         lsopt.addr_text.len = ngx_sock_ntop(lsopt.sockaddr, lsopt.socklen, p,
//                                             len, 1);

//         if (ngx_http_add_listen(cf, cscf, &lsopt) != NGX_OK) {
//             return NGX_CONF_ERROR;
//         }
//     }

    return rv;
}

void *
httplite_core_create_server_configuration(ngx_conf_t *cf)
{
    httplite_server_conf_t  *cscf;

    cscf = ngx_pcalloc(cf->pool, sizeof(httplite_server_conf_t));
    if (cscf == NULL) {
        return NULL;
    }

    // /*
    //  * set by ngx_pcalloc():
    //  *
    //  *     conf->client_large_buffers.num = 0;
    //  */

    // if (ngx_array_init(&cscf->server_names, cf->temp_pool, 4,
    //                    sizeof(ngx_http_server_name_t))
    //     != NGX_OK)
    // {
    //     return NULL;
    // }

    // cscf->connection_pool_size = NGX_CONF_UNSET_SIZE;
    // cscf->request_pool_size = NGX_CONF_UNSET_SIZE;
    // cscf->client_header_timeout = NGX_CONF_UNSET_MSEC;
    // cscf->client_header_buffer_size = NGX_CONF_UNSET_SIZE;
    // cscf->ignore_invalid_headers = NGX_CONF_UNSET;
    // cscf->merge_slashes = NGX_CONF_UNSET;
    // cscf->underscores_in_headers = NGX_CONF_UNSET;

    // cscf->file_name = cf->conf_file->file.name.data;
    // cscf->line = cf->conf_file->line;

    cscf->port = NGX_CONF_UNSET;
    cscf->server_name = (ngx_str_t)ngx_null_string;

    return cscf;
}
