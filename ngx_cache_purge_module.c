/*
 * Copyright (c) 2009-2014, FRiCKLE <info@frickle.com>
 * Copyright (c) 2009-2014, Piotr Sikora <piotr.sikora@frickle.com>
 * All rights reserved.
 *
 * This project was fully funded by yo.se.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


#ifndef nginx_version
#error This module cannot be build against an unknown nginx version.
#endif


#if (NGX_HTTP_CACHE)

typedef struct {
    ngx_flag_t                    enable;
    ngx_str_t                     method;
    ngx_array_t                  *access;   /* array of ngx_in_cidr_t */
    ngx_array_t                  *access6;  /* array of ngx_in6_cidr_t */
    ngx_shm_zone_t               *zone;
    ngx_http_complex_value_t     *cache_key;
} ngx_http_cache_purge_conf_t;

typedef struct {
    ngx_flag_t                    enable;
    ngx_shm_zone_t               *zone;
    ngx_http_complex_value_t     *prefix;
} ngx_http_cache_purge_prefix_conf_t;

typedef struct {
# if (NGX_HTTP_FASTCGI)
    ngx_http_cache_purge_conf_t        fastcgi;
    ngx_http_cache_purge_prefix_conf_t fastcgi_prefix;
# endif /* NGX_HTTP_FASTCGI */
# if (NGX_HTTP_PROXY)
    ngx_http_cache_purge_conf_t        proxy;
    ngx_http_cache_purge_prefix_conf_t proxy_prefix;
# endif /* NGX_HTTP_PROXY */
# if (NGX_HTTP_SCGI)
    ngx_http_cache_purge_conf_t        scgi;
    ngx_http_cache_purge_prefix_conf_t scgi_prefix;
# endif /* NGX_HTTP_SCGI */
# if (NGX_HTTP_UWSGI)
    ngx_http_cache_purge_conf_t        uwsgi;
    ngx_http_cache_purge_prefix_conf_t uwsgi_prefix;
# endif /* NGX_HTTP_UWSGI */

    ngx_http_cache_purge_conf_t  *conf;
    ngx_http_handler_pt           handler;
    ngx_http_handler_pt           original_handler;
} ngx_http_cache_purge_loc_conf_t;

typedef struct {
    ngx_http_request_t        *r;
    ngx_http_file_cache_t     *cache;
    ngx_str_t                  prefix;
    ngx_uint_t                 scanned;
    ngx_uint_t                 purged;
} ngx_http_cache_purge_walk_t;

# if (NGX_HTTP_FASTCGI)
char       *ngx_http_fastcgi_cache_purge_conf(ngx_conf_t *cf,
                ngx_command_t *cmd, void *conf);
ngx_int_t   ngx_http_fastcgi_cache_purge_handler(ngx_http_request_t *r);
char       *ngx_http_fastcgi_cache_purge_prefix_conf(ngx_conf_t *cf,
                ngx_command_t *cmd, void *conf);
ngx_int_t   ngx_http_fastcgi_cache_purge_prefix_handler(ngx_http_request_t *r);
# endif /* NGX_HTTP_FASTCGI */

# if (NGX_HTTP_PROXY)
char       *ngx_http_proxy_cache_purge_conf(ngx_conf_t *cf,
                ngx_command_t *cmd, void *conf);
ngx_int_t   ngx_http_proxy_cache_purge_handler(ngx_http_request_t *r);
char       *ngx_http_proxy_cache_purge_prefix_conf(ngx_conf_t *cf,
                ngx_command_t *cmd, void *conf);
ngx_int_t   ngx_http_proxy_cache_purge_prefix_handler(ngx_http_request_t *r);
# endif /* NGX_HTTP_PROXY */

# if (NGX_HTTP_SCGI)
char       *ngx_http_scgi_cache_purge_conf(ngx_conf_t *cf,
                ngx_command_t *cmd, void *conf);
ngx_int_t   ngx_http_scgi_cache_purge_handler(ngx_http_request_t *r);
char       *ngx_http_scgi_cache_purge_prefix_conf(ngx_conf_t *cf,
                ngx_command_t *cmd, void *conf);
ngx_int_t   ngx_http_scgi_cache_purge_prefix_handler(ngx_http_request_t *r);
# endif /* NGX_HTTP_SCGI */

# if (NGX_HTTP_UWSGI)
char       *ngx_http_uwsgi_cache_purge_conf(ngx_conf_t *cf,
                ngx_command_t *cmd, void *conf);
ngx_int_t   ngx_http_uwsgi_cache_purge_handler(ngx_http_request_t *r);
char       *ngx_http_uwsgi_cache_purge_prefix_conf(ngx_conf_t *cf,
                ngx_command_t *cmd, void *conf);
ngx_int_t   ngx_http_uwsgi_cache_purge_prefix_handler(ngx_http_request_t *r);
# endif /* NGX_HTTP_UWSGI */

ngx_int_t   ngx_http_cache_purge_access_handler(ngx_http_request_t *r);
ngx_int_t   ngx_http_cache_purge_access(ngx_array_t *a, ngx_array_t *a6,
    struct sockaddr *s);

ngx_int_t   ngx_http_cache_purge_send_response(ngx_http_request_t *r);
# if (nginx_version >= 1007009)
ngx_int_t   ngx_http_cache_purge_cache_get(ngx_http_request_t *r,
    ngx_http_upstream_t *u, ngx_http_file_cache_t **cache);
# endif /* nginx_version >= 1007009 */
ngx_int_t   ngx_http_cache_purge_init(ngx_http_request_t *r,
    ngx_http_file_cache_t *cache, ngx_http_complex_value_t *cache_key);
void        ngx_http_cache_purge_handler(ngx_http_request_t *r);

ngx_int_t   ngx_http_file_cache_purge(ngx_http_request_t *r);

char       *ngx_http_cache_purge_prefix_conf(ngx_conf_t *cf,
    ngx_http_cache_purge_prefix_conf_t *prefix_conf, ngx_module_t *tag);
void        ngx_http_cache_purge_prefix_exec(ngx_http_request_t *r,
    ngx_http_file_cache_t *cache, ngx_str_t *prefix);
ngx_int_t   ngx_http_cache_purge_prefix_send_response(ngx_http_request_t *r,
    ngx_uint_t scanned, ngx_uint_t purged);
ngx_str_t   ngx_http_cache_purge_key_uri(ngx_str_t *key);
ngx_int_t   ngx_http_cache_purge_prefix_purge(ngx_http_request_t *r,
    ngx_http_file_cache_t *cache, ngx_str_t *key);
ngx_int_t   ngx_http_cache_purge_file(ngx_tree_ctx_t *ctx, ngx_str_t *path);
ngx_int_t   ngx_http_cache_purge_noop(ngx_tree_ctx_t *ctx, ngx_str_t *path);
ngx_int_t   ngx_http_cache_purge_skip_temp(ngx_tree_ctx_t *ctx, ngx_str_t *path);

char       *ngx_http_cache_purge_conf(ngx_conf_t *cf,
    ngx_http_cache_purge_conf_t *cpcf);

void       *ngx_http_cache_purge_create_loc_conf(ngx_conf_t *cf);
char       *ngx_http_cache_purge_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);

static ngx_command_t  ngx_http_cache_purge_module_commands[] = {

# if (NGX_HTTP_FASTCGI)
    { ngx_string("fastcgi_cache_purge"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_fastcgi_cache_purge_conf,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("fastcgi_cache_purge_prefix"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_http_fastcgi_cache_purge_prefix_conf,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },
# endif /* NGX_HTTP_FASTCGI */

# if (NGX_HTTP_PROXY)
    { ngx_string("proxy_cache_purge"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_proxy_cache_purge_conf,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("proxy_cache_purge_prefix"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_http_proxy_cache_purge_prefix_conf,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },
# endif /* NGX_HTTP_PROXY */

# if (NGX_HTTP_SCGI)
    { ngx_string("scgi_cache_purge"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_scgi_cache_purge_conf,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("scgi_cache_purge_prefix"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_http_scgi_cache_purge_prefix_conf,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },
# endif /* NGX_HTTP_SCGI */

# if (NGX_HTTP_UWSGI)
    { ngx_string("uwsgi_cache_purge"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_uwsgi_cache_purge_conf,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("uwsgi_cache_purge_prefix"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_http_uwsgi_cache_purge_prefix_conf,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },
# endif /* NGX_HTTP_UWSGI */

      ngx_null_command
};

static ngx_http_module_t  ngx_http_cache_purge_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_cache_purge_create_loc_conf,  /* create location configuration */
    ngx_http_cache_purge_merge_loc_conf    /* merge location configuration */
};

ngx_module_t  ngx_http_cache_purge_module = {
    NGX_MODULE_V1,
    &ngx_http_cache_purge_module_ctx,      /* module context */
    ngx_http_cache_purge_module_commands,  /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

static char ngx_http_cache_purge_success_page_top[] =
"<html>" CRLF
"<head><title>Successful purge</title></head>" CRLF
"<body bgcolor=\"white\">" CRLF
"<center><h1>Successful purge</h1>" CRLF
;

static char ngx_http_cache_purge_success_page_tail[] =
CRLF "</center>" CRLF
"<hr><center>" NGINX_VER "</center>" CRLF
"</body>" CRLF
"</html>" CRLF
;

# if (NGX_HTTP_FASTCGI)
extern ngx_module_t  ngx_http_fastcgi_module;

#  if (nginx_version >= 1007009)

typedef struct {
    ngx_array_t                    caches;  /* ngx_http_file_cache_t * */
} ngx_http_fastcgi_main_conf_t;

#  endif /* nginx_version >= 1007009 */

#  if (nginx_version >= 1007008)

typedef struct {
    ngx_array_t                   *flushes;
    ngx_array_t                   *lengths;
    ngx_array_t                   *values;
    ngx_uint_t                     number;
    ngx_hash_t                     hash;
} ngx_http_fastcgi_params_t;

#  endif /* nginx_version >= 1007008 */

typedef struct {
    ngx_http_upstream_conf_t       upstream;

    ngx_str_t                      index;

#  if (nginx_version >= 1007008)
    ngx_http_fastcgi_params_t      params;
    ngx_http_fastcgi_params_t      params_cache;
#  else
    ngx_array_t                   *flushes;
    ngx_array_t                   *params_len;
    ngx_array_t                   *params;
#  endif /* nginx_version >= 1007008 */

    ngx_array_t                   *params_source;
    ngx_array_t                   *catch_stderr;

    ngx_array_t                   *fastcgi_lengths;
    ngx_array_t                   *fastcgi_values;

#  if (nginx_version >= 8040) && (nginx_version < 1007008)
    ngx_hash_t                     headers_hash;
    ngx_uint_t                     header_params;
#  endif /* nginx_version >= 8040 && nginx_version < 1007008 */

#  if (nginx_version >= 1001004)
    ngx_flag_t                     keep_conn;
#  endif /* nginx_version >= 1001004 */

    ngx_http_complex_value_t       cache_key;

#  if (NGX_PCRE)
    ngx_regex_t                   *split_regex;
    ngx_str_t                      split_name;
#  endif /* NGX_PCRE */
} ngx_http_fastcgi_loc_conf_t;

char *
ngx_http_fastcgi_cache_purge_conf(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    ngx_http_compile_complex_value_t   ccv;
    ngx_http_cache_purge_loc_conf_t   *cplcf;
    ngx_http_core_loc_conf_t          *clcf;
    ngx_str_t                         *value;

    cplcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_cache_purge_module);

    /* check for duplicates / collisions */
    if (cplcf->fastcgi.enable != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    if (cf->args->nelts != 3) {
        return ngx_http_cache_purge_conf(cf, &cplcf->fastcgi);
    }

    if (cf->cmd_type & (NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF)) {
        return "(separate location syntax) is not allowed here";
    }

    value = cf->args->elts;

    cplcf->fastcgi.zone = ngx_shared_memory_add(cf, &value[1], 0,
                                                &ngx_http_fastcgi_module);
    if (cplcf->fastcgi.zone == NULL) {
        return NGX_CONF_ERROR;
    }

    cplcf->fastcgi.cache_key = ngx_palloc(cf->pool,
                                          sizeof(ngx_http_complex_value_t));
    if (cplcf->fastcgi.cache_key == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[2];
    ccv.complex_value = cplcf->fastcgi.cache_key;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    /* set handler */
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    cplcf->fastcgi.enable = 0;
    clcf->handler = ngx_http_fastcgi_cache_purge_handler;

    return NGX_CONF_OK;
}

ngx_int_t
ngx_http_fastcgi_cache_purge_handler(ngx_http_request_t *r)
{
    ngx_http_cache_purge_loc_conf_t  *cplcf;
    ngx_http_file_cache_t            *cache;

    cplcf = ngx_http_get_module_loc_conf(r, ngx_http_cache_purge_module);

    if (ngx_http_discard_request_body(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (cplcf->fastcgi.zone == NULL || cplcf->fastcgi.cache_key == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cache = (ngx_http_file_cache_t *) cplcf->fastcgi.zone->data;
    if (cache == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_http_cache_purge_init(r, cache, cplcf->fastcgi.cache_key) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    r->main->count++;

    ngx_http_cache_purge_handler(r);

    return NGX_DONE;
}

char *
ngx_http_fastcgi_cache_purge_prefix_conf(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    ngx_http_cache_purge_loc_conf_t  *cplcf;
    ngx_http_core_loc_conf_t         *clcf;
    char                             *rv;

    cplcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_cache_purge_module);

    rv = ngx_http_cache_purge_prefix_conf(cf, &cplcf->fastcgi_prefix,
                                          &ngx_http_fastcgi_module);
    if (rv != NGX_CONF_OK) {
        return rv;
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_fastcgi_cache_purge_prefix_handler;

    return NGX_CONF_OK;
}

ngx_int_t
ngx_http_fastcgi_cache_purge_prefix_handler(ngx_http_request_t *r)
{
    ngx_http_cache_purge_loc_conf_t  *cplcf;
    ngx_http_file_cache_t            *cache;
    ngx_str_t                         prefix;
    ngx_int_t                         rc;

    if (ngx_http_discard_request_body(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cplcf = ngx_http_get_module_loc_conf(r, ngx_http_cache_purge_module);

    rc = ngx_http_complex_value(r, cplcf->fastcgi_prefix.prefix, &prefix);
    if (rc != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cache = (ngx_http_file_cache_t *) cplcf->fastcgi_prefix.zone->data;
    if (cache == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    r->main->count++;

    ngx_http_cache_purge_prefix_exec(r, cache, &prefix);

    return NGX_DONE;
}
# endif /* NGX_HTTP_FASTCGI */

# if (NGX_HTTP_PROXY)
extern ngx_module_t  ngx_http_proxy_module;

typedef struct {
    ngx_str_t                      key_start;
    ngx_str_t                      schema;
    ngx_str_t                      host_header;
    ngx_str_t                      port;
    ngx_str_t                      uri;
} ngx_http_proxy_vars_t;

#  if (nginx_version >= 1007009)

typedef struct {
    ngx_array_t                    caches;  /* ngx_http_file_cache_t * */
} ngx_http_proxy_main_conf_t;

#  endif /* nginx_version >= 1007009 */

#  if (nginx_version >= 1007008)

typedef struct {
    ngx_array_t                   *flushes;
    ngx_array_t                   *lengths;
    ngx_array_t                   *values;
    ngx_hash_t                     hash;
} ngx_http_proxy_headers_t;

#  endif /* nginx_version >= 1007008 */

typedef struct {
    ngx_http_upstream_conf_t       upstream;

#  if (nginx_version >= 1007008)
    ngx_array_t                   *body_flushes;
    ngx_array_t                   *body_lengths;
    ngx_array_t                   *body_values;
    ngx_str_t                      body_source;

    ngx_http_proxy_headers_t       headers;
    ngx_http_proxy_headers_t       headers_cache;
#  else
    ngx_array_t                   *flushes;
    ngx_array_t                   *body_set_len;
    ngx_array_t                   *body_set;
    ngx_array_t                   *headers_set_len;
    ngx_array_t                   *headers_set;
    ngx_hash_t                     headers_set_hash;
#  endif /* nginx_version >= 1007008 */

    ngx_array_t                   *headers_source;
#  if (nginx_version < 8040)
    ngx_array_t                   *headers_names;
#  endif /* nginx_version < 8040 */

    ngx_array_t                   *proxy_lengths;
    ngx_array_t                   *proxy_values;

    ngx_array_t                   *redirects;
#  if (nginx_version >= 1001015)
    ngx_array_t                   *cookie_domains;
    ngx_array_t                   *cookie_paths;
#  endif /* nginx_version >= 1001015 */

#  if (nginx_version < 1007008)
    ngx_str_t                      body_source;
#  endif /* nginx_version < 1007008 */

    ngx_str_t                      method;
    ngx_str_t                      location;
    ngx_str_t                      url;

    ngx_http_complex_value_t       cache_key;

    ngx_http_proxy_vars_t          vars;

    ngx_flag_t                     redirect;

#  if (nginx_version >= 1001004)
    ngx_uint_t                     http_version;
#  endif /* nginx_version >= 1001004 */

    ngx_uint_t                     headers_hash_max_size;
    ngx_uint_t                     headers_hash_bucket_size;

#  if (NGX_HTTP_SSL)
#    if (nginx_version >= 1005006)
    ngx_uint_t                     ssl;
    ngx_uint_t                     ssl_protocols;
    ngx_str_t                      ssl_ciphers;
#    endif /* nginx_version >= 1005006 */
#    if (nginx_version >= 1007000)
    ngx_uint_t                     ssl_verify_depth;
    ngx_str_t                      ssl_trusted_certificate;
    ngx_str_t                      ssl_crl;
#    endif /* nginx_version >= 1007000 */
#    if (nginx_version >= 1007008)
    ngx_str_t                      ssl_certificate;
    ngx_str_t                      ssl_certificate_key;
    ngx_array_t                   *ssl_passwords;
#    endif /* nginx_version >= 1007008 */
#  endif
} ngx_http_proxy_loc_conf_t;

char *
ngx_http_proxy_cache_purge_conf(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_compile_complex_value_t   ccv;
    ngx_http_cache_purge_loc_conf_t   *cplcf;
    ngx_http_core_loc_conf_t          *clcf;
    ngx_str_t                         *value;

    cplcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_cache_purge_module);

    /* check for duplicates / collisions */
    if (cplcf->proxy.enable != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    if (cf->args->nelts != 3) {
        return ngx_http_cache_purge_conf(cf, &cplcf->proxy);
    }

    if (cf->cmd_type & (NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF)) {
        return "(separate location syntax) is not allowed here";
    }

    value = cf->args->elts;

    cplcf->proxy.zone = ngx_shared_memory_add(cf, &value[1], 0,
                                              &ngx_http_proxy_module);
    if (cplcf->proxy.zone == NULL) {
        return NGX_CONF_ERROR;
    }

    cplcf->proxy.cache_key = ngx_palloc(cf->pool,
                                        sizeof(ngx_http_complex_value_t));
    if (cplcf->proxy.cache_key == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[2];
    ccv.complex_value = cplcf->proxy.cache_key;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    /* set handler */
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    cplcf->proxy.enable = 0;
    clcf->handler = ngx_http_proxy_cache_purge_handler;

    return NGX_CONF_OK;
}

ngx_int_t
ngx_http_proxy_cache_purge_handler(ngx_http_request_t *r)
{
    ngx_http_cache_purge_loc_conf_t  *cplcf;
    ngx_http_file_cache_t            *cache;

    cplcf = ngx_http_get_module_loc_conf(r, ngx_http_cache_purge_module);

    if (ngx_http_discard_request_body(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (cplcf->proxy.zone == NULL || cplcf->proxy.cache_key == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cache = (ngx_http_file_cache_t *) cplcf->proxy.zone->data;
    if (cache == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_http_cache_purge_init(r, cache, cplcf->proxy.cache_key) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    r->main->count++;

    ngx_http_cache_purge_handler(r);

    return NGX_DONE;
}

char *
ngx_http_proxy_cache_purge_prefix_conf(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    ngx_http_cache_purge_loc_conf_t  *cplcf;
    ngx_http_core_loc_conf_t         *clcf;
    char                             *rv;

    cplcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_cache_purge_module);

    rv = ngx_http_cache_purge_prefix_conf(cf, &cplcf->proxy_prefix,
                                          &ngx_http_proxy_module);
    if (rv != NGX_CONF_OK) {
        return rv;
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_proxy_cache_purge_prefix_handler;

    return NGX_CONF_OK;
}

ngx_int_t
ngx_http_proxy_cache_purge_prefix_handler(ngx_http_request_t *r)
{
    ngx_http_cache_purge_loc_conf_t  *cplcf;
    ngx_http_file_cache_t            *cache;
    ngx_str_t                         prefix;
    ngx_int_t                         rc;

    if (ngx_http_discard_request_body(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cplcf = ngx_http_get_module_loc_conf(r, ngx_http_cache_purge_module);

    rc = ngx_http_complex_value(r, cplcf->proxy_prefix.prefix, &prefix);
    if (rc != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cache = (ngx_http_file_cache_t *) cplcf->proxy_prefix.zone->data;
    if (cache == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    r->main->count++;

    ngx_http_cache_purge_prefix_exec(r, cache, &prefix);

    return NGX_DONE;
}
# endif /* NGX_HTTP_PROXY */

# if (NGX_HTTP_SCGI)
extern ngx_module_t  ngx_http_scgi_module;

#  if (nginx_version >= 1007009)

typedef struct {
    ngx_array_t                caches;  /* ngx_http_file_cache_t * */
} ngx_http_scgi_main_conf_t;

#  endif /* nginx_version >= 1007009 */

#  if (nginx_version >= 1007008)

typedef struct {
    ngx_array_t               *flushes;
    ngx_array_t               *lengths;
    ngx_array_t               *values;
    ngx_uint_t                 number;
    ngx_hash_t                 hash;
} ngx_http_scgi_params_t;

#  endif /* nginx_version >= 1007008 */

typedef struct {
    ngx_http_upstream_conf_t   upstream;

#  if (nginx_version >= 1007008)
    ngx_http_scgi_params_t     params;
    ngx_http_scgi_params_t     params_cache;
    ngx_array_t               *params_source;
#  else
    ngx_array_t               *flushes;
    ngx_array_t               *params_len;
    ngx_array_t               *params;
    ngx_array_t               *params_source;

    ngx_hash_t                 headers_hash;
    ngx_uint_t                 header_params;
#  endif /* nginx_version >= 1007008 */

    ngx_array_t               *scgi_lengths;
    ngx_array_t               *scgi_values;

    ngx_http_complex_value_t   cache_key;
} ngx_http_scgi_loc_conf_t;

char *
ngx_http_scgi_cache_purge_conf(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_compile_complex_value_t   ccv;
    ngx_http_cache_purge_loc_conf_t   *cplcf;
    ngx_http_core_loc_conf_t          *clcf;
    ngx_str_t                         *value;

    cplcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_cache_purge_module);

    /* check for duplicates / collisions */
    if (cplcf->scgi.enable != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    if (cf->args->nelts != 3) {
        return ngx_http_cache_purge_conf(cf, &cplcf->scgi);
    }

    if (cf->cmd_type & (NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF)) {
        return "(separate location syntax) is not allowed here";
    }

    value = cf->args->elts;

    cplcf->scgi.zone = ngx_shared_memory_add(cf, &value[1], 0,
                                             &ngx_http_scgi_module);
    if (cplcf->scgi.zone == NULL) {
        return NGX_CONF_ERROR;
    }

    cplcf->scgi.cache_key = ngx_palloc(cf->pool,
                                       sizeof(ngx_http_complex_value_t));
    if (cplcf->scgi.cache_key == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[2];
    ccv.complex_value = cplcf->scgi.cache_key;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    /* set handler */
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    cplcf->scgi.enable = 0;
    clcf->handler = ngx_http_scgi_cache_purge_handler;

    return NGX_CONF_OK;
}

ngx_int_t
ngx_http_scgi_cache_purge_handler(ngx_http_request_t *r)
{
    ngx_http_cache_purge_loc_conf_t  *cplcf;
    ngx_http_file_cache_t            *cache;

    cplcf = ngx_http_get_module_loc_conf(r, ngx_http_cache_purge_module);

    if (ngx_http_discard_request_body(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (cplcf->scgi.zone == NULL || cplcf->scgi.cache_key == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cache = (ngx_http_file_cache_t *) cplcf->scgi.zone->data;
    if (cache == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_http_cache_purge_init(r, cache, cplcf->scgi.cache_key) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    r->main->count++;

    ngx_http_cache_purge_handler(r);

    return NGX_DONE;
}

char *
ngx_http_scgi_cache_purge_prefix_conf(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    ngx_http_cache_purge_loc_conf_t  *cplcf;
    ngx_http_core_loc_conf_t         *clcf;
    char                             *rv;

    cplcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_cache_purge_module);

    rv = ngx_http_cache_purge_prefix_conf(cf, &cplcf->scgi_prefix,
                                          &ngx_http_scgi_module);
    if (rv != NGX_CONF_OK) {
        return rv;
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_scgi_cache_purge_prefix_handler;

    return NGX_CONF_OK;
}

ngx_int_t
ngx_http_scgi_cache_purge_prefix_handler(ngx_http_request_t *r)
{
    ngx_http_cache_purge_loc_conf_t  *cplcf;
    ngx_http_file_cache_t            *cache;
    ngx_str_t                         prefix;
    ngx_int_t                         rc;

    if (ngx_http_discard_request_body(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cplcf = ngx_http_get_module_loc_conf(r, ngx_http_cache_purge_module);

    rc = ngx_http_complex_value(r, cplcf->scgi_prefix.prefix, &prefix);
    if (rc != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cache = (ngx_http_file_cache_t *) cplcf->scgi_prefix.zone->data;
    if (cache == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    r->main->count++;

    ngx_http_cache_purge_prefix_exec(r, cache, &prefix);

    return NGX_DONE;
}
# endif /* NGX_HTTP_SCGI */

# if (NGX_HTTP_UWSGI)
extern ngx_module_t  ngx_http_uwsgi_module;

#  if (nginx_version >= 1007009)

typedef struct {
    ngx_array_t                caches;  /* ngx_http_file_cache_t * */
} ngx_http_uwsgi_main_conf_t;

#  endif /* nginx_version >= 1007009 */

#  if (nginx_version >= 1007008)

typedef struct {
    ngx_array_t               *flushes;
    ngx_array_t               *lengths;
    ngx_array_t               *values;
    ngx_uint_t                 number;
    ngx_hash_t                 hash;
} ngx_http_uwsgi_params_t;

#  endif /* nginx_version >= 1007008 */

typedef struct {
    ngx_http_upstream_conf_t   upstream;

#  if (nginx_version >= 1007008)
    ngx_http_uwsgi_params_t    params;
    ngx_http_uwsgi_params_t    params_cache;
    ngx_array_t               *params_source;
#  else
    ngx_array_t               *flushes;
    ngx_array_t               *params_len;
    ngx_array_t               *params;
    ngx_array_t               *params_source;

    ngx_hash_t                 headers_hash;
    ngx_uint_t                 header_params;
#  endif /* nginx_version >= 1007008 */

    ngx_array_t               *uwsgi_lengths;
    ngx_array_t               *uwsgi_values;

    ngx_http_complex_value_t   cache_key;

    ngx_str_t                  uwsgi_string;

    ngx_uint_t                 modifier1;
    ngx_uint_t                 modifier2;

#  if (NGX_HTTP_SSL)
#    if (nginx_version >= 1005008)
    ngx_uint_t                 ssl;
    ngx_uint_t                 ssl_protocols;
    ngx_str_t                  ssl_ciphers;
#    endif /* nginx_version >= 1005008 */
#    if (nginx_version >= 1007000)
    ngx_uint_t                 ssl_verify_depth;
    ngx_str_t                  ssl_trusted_certificate;
    ngx_str_t                  ssl_crl;
#    endif /* nginx_version >= 1007000 */
#    if (nginx_version >= 1007008)
    ngx_str_t                  ssl_certificate;
    ngx_str_t                  ssl_certificate_key;
    ngx_array_t               *ssl_passwords;
#    endif /* nginx_version >= 1007008 */
#  endif
} ngx_http_uwsgi_loc_conf_t;

char *
ngx_http_uwsgi_cache_purge_conf(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_compile_complex_value_t   ccv;
    ngx_http_cache_purge_loc_conf_t   *cplcf;
    ngx_http_core_loc_conf_t          *clcf;
    ngx_str_t                         *value;

    cplcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_cache_purge_module);

    /* check for duplicates / collisions */
    if (cplcf->uwsgi.enable != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    if (cf->args->nelts != 3) {
        return ngx_http_cache_purge_conf(cf, &cplcf->uwsgi);
    }

    if (cf->cmd_type & (NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF)) {
        return "(separate location syntax) is not allowed here";
    }

    value = cf->args->elts;

    cplcf->uwsgi.zone = ngx_shared_memory_add(cf, &value[1], 0,
                                              &ngx_http_uwsgi_module);
    if (cplcf->uwsgi.zone == NULL) {
        return NGX_CONF_ERROR;
    }

    cplcf->uwsgi.cache_key = ngx_palloc(cf->pool,
                                        sizeof(ngx_http_complex_value_t));
    if (cplcf->uwsgi.cache_key == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[2];
    ccv.complex_value = cplcf->uwsgi.cache_key;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    /* set handler */
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    cplcf->uwsgi.enable = 0;
    clcf->handler = ngx_http_uwsgi_cache_purge_handler;

    return NGX_CONF_OK;
}

ngx_int_t
ngx_http_uwsgi_cache_purge_handler(ngx_http_request_t *r)
{
    ngx_http_cache_purge_loc_conf_t  *cplcf;
    ngx_http_file_cache_t            *cache;

    cplcf = ngx_http_get_module_loc_conf(r, ngx_http_cache_purge_module);

    if (ngx_http_discard_request_body(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (cplcf->uwsgi.zone == NULL || cplcf->uwsgi.cache_key == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cache = (ngx_http_file_cache_t *) cplcf->uwsgi.zone->data;
    if (cache == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_http_cache_purge_init(r, cache, cplcf->uwsgi.cache_key) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    r->main->count++;

    ngx_http_cache_purge_handler(r);

    return NGX_DONE;
}

char *
ngx_http_uwsgi_cache_purge_prefix_conf(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    ngx_http_cache_purge_loc_conf_t  *cplcf;
    ngx_http_core_loc_conf_t         *clcf;
    char                             *rv;

    cplcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_cache_purge_module);

    rv = ngx_http_cache_purge_prefix_conf(cf, &cplcf->uwsgi_prefix,
                                          &ngx_http_uwsgi_module);
    if (rv != NGX_CONF_OK) {
        return rv;
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_uwsgi_cache_purge_prefix_handler;

    return NGX_CONF_OK;
}

ngx_int_t
ngx_http_uwsgi_cache_purge_prefix_handler(ngx_http_request_t *r)
{
    ngx_http_cache_purge_loc_conf_t  *cplcf;
    ngx_http_file_cache_t            *cache;
    ngx_str_t                         prefix;
    ngx_int_t                         rc;

    if (ngx_http_discard_request_body(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cplcf = ngx_http_get_module_loc_conf(r, ngx_http_cache_purge_module);

    rc = ngx_http_complex_value(r, cplcf->uwsgi_prefix.prefix, &prefix);
    if (rc != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cache = (ngx_http_file_cache_t *) cplcf->uwsgi_prefix.zone->data;
    if (cache == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    r->main->count++;

    ngx_http_cache_purge_prefix_exec(r, cache, &prefix);

    return NGX_DONE;
}
# endif /* NGX_HTTP_UWSGI */

ngx_int_t
ngx_http_cache_purge_access_handler(ngx_http_request_t *r)
{
    ngx_http_cache_purge_loc_conf_t   *cplcf;

    cplcf = ngx_http_get_module_loc_conf(r, ngx_http_cache_purge_module);

    if (r->method_name.len != cplcf->conf->method.len
        || (ngx_strncmp(r->method_name.data, cplcf->conf->method.data,
                        r->method_name.len)))
    {
        return cplcf->original_handler(r);
    }

    if ((cplcf->conf->access || cplcf->conf->access6)
         && ngx_http_cache_purge_access(cplcf->conf->access,
                                        cplcf->conf->access6,
                                        r->connection->sockaddr) != NGX_OK)
    {
        return NGX_HTTP_FORBIDDEN;
    }

    if (cplcf->handler == NULL) {
        return NGX_HTTP_NOT_FOUND;
    }

    return cplcf->handler(r);
}

ngx_int_t
ngx_http_cache_purge_access(ngx_array_t *access, ngx_array_t *access6,
    struct sockaddr *s)
{
    in_addr_t         inaddr;
    ngx_in_cidr_t    *a;
    ngx_uint_t        i;
# if (NGX_HAVE_INET6)
    struct in6_addr  *inaddr6;
    ngx_in6_cidr_t   *a6;
    u_char           *p;
    ngx_uint_t        n;
# endif /* NGX_HAVE_INET6 */

    switch (s->sa_family) {
    case AF_INET:
        if (access == NULL) {
            return NGX_DECLINED;
        }

        inaddr = ((struct sockaddr_in *) s)->sin_addr.s_addr;

# if (NGX_HAVE_INET6)
    ipv4:
# endif /* NGX_HAVE_INET6 */

        a = access->elts;
        for (i = 0; i < access->nelts; i++) {
            if ((inaddr & a[i].mask) == a[i].addr) {
                return NGX_OK;
            }
        }

        return NGX_DECLINED;

# if (NGX_HAVE_INET6)
    case AF_INET6:
        inaddr6 = &((struct sockaddr_in6 *) s)->sin6_addr;
        p = inaddr6->s6_addr;

        if (access && IN6_IS_ADDR_V4MAPPED(inaddr6)) {
            inaddr = p[12] << 24;
            inaddr += p[13] << 16;
            inaddr += p[14] << 8;
            inaddr += p[15];
            inaddr = htonl(inaddr);

            goto ipv4;
        }

        if (access6 == NULL) {
            return NGX_DECLINED;
        }

        a6 = access6->elts;
        for (i = 0; i < access6->nelts; i++) {
            for (n = 0; n < 16; n++) {
                if ((p[n] & a6[i].mask.s6_addr[n]) != a6[i].addr.s6_addr[n]) {
                    goto next;
                }
            }

            return NGX_OK;

        next:
            continue;
        }

        return NGX_DECLINED;
# endif /* NGX_HAVE_INET6 */
    }

    return NGX_DECLINED;
}

ngx_int_t
ngx_http_cache_purge_send_response(ngx_http_request_t *r)
{
    ngx_chain_t   out;
    ngx_buf_t    *b;
    ngx_str_t    *key;
    ngx_int_t     rc;
    size_t        len;

    key = r->cache->keys.elts;

    len = sizeof(ngx_http_cache_purge_success_page_top) - 1
          + sizeof(ngx_http_cache_purge_success_page_tail) - 1
          + sizeof("<br>Key : ") - 1 + sizeof(CRLF "<br>Path: ") - 1
          + key[0].len + r->cache->file.name.len;

    r->headers_out.content_type.len = sizeof("text/html") - 1;
    r->headers_out.content_type.data = (u_char *) "text/html";
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = len;

    if (r->method == NGX_HTTP_HEAD) {
        rc = ngx_http_send_header(r);
        if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
            return rc;
        }
    }

    b = ngx_create_temp_buf(r->pool, len);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    out.buf = b;
    out.next = NULL;

    b->last = ngx_cpymem(b->last, ngx_http_cache_purge_success_page_top,
                         sizeof(ngx_http_cache_purge_success_page_top) - 1);
    b->last = ngx_cpymem(b->last, "<br>Key : ", sizeof("<br>Key : ") - 1);
    b->last = ngx_cpymem(b->last, key[0].data, key[0].len);
    b->last = ngx_cpymem(b->last, CRLF "<br>Path: ",
                         sizeof(CRLF "<br>Path: ") - 1);
    b->last = ngx_cpymem(b->last, r->cache->file.name.data,
                         r->cache->file.name.len);
    b->last = ngx_cpymem(b->last, ngx_http_cache_purge_success_page_tail,
                         sizeof(ngx_http_cache_purge_success_page_tail) - 1);
    b->last_buf = 1;

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
       return rc;
    }

    return ngx_http_output_filter(r, &out);
}

# if (nginx_version >= 1007009)

/*
 * Based on: ngx_http_upstream.c/ngx_http_upstream_cache_get
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */
ngx_int_t
ngx_http_cache_purge_cache_get(ngx_http_request_t *r, ngx_http_upstream_t *u,
    ngx_http_file_cache_t **cache)
{
    ngx_str_t               *name, val;
    ngx_uint_t               i;
    ngx_http_file_cache_t  **caches;

    if (u->conf->cache_zone) {
        *cache = u->conf->cache_zone->data;
        return NGX_OK;
    }

    if (ngx_http_complex_value(r, u->conf->cache_value, &val) != NGX_OK) {
        return NGX_ERROR;
    }

    if (val.len == 0
        || (val.len == 3 && ngx_strncmp(val.data, "off", 3) == 0))
    {
        return NGX_DECLINED;
    }

    caches = u->caches->elts;

    for (i = 0; i < u->caches->nelts; i++) {
        name = &caches[i]->shm_zone->shm.name;

        if (name->len == val.len
            && ngx_strncmp(name->data, val.data, val.len) == 0)
        {
            *cache = caches[i];
            return NGX_OK;
        }
    }

    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "cache \"%V\" not found", &val);

    return NGX_ERROR;
}

# endif /* nginx_version >= 1007009 */

ngx_int_t
ngx_http_cache_purge_init(ngx_http_request_t *r, ngx_http_file_cache_t *cache,
    ngx_http_complex_value_t *cache_key)
{
    ngx_http_cache_t  *c;
    ngx_str_t         *key;
    ngx_int_t          rc;

    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    c = ngx_pcalloc(r->pool, sizeof(ngx_http_cache_t));
    if (c == NULL) {
        return NGX_ERROR;
    }

    rc = ngx_array_init(&c->keys, r->pool, 1, sizeof(ngx_str_t));
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    key = ngx_array_push(&c->keys);
    if (key == NULL) {
        return NGX_ERROR;
    }

    rc = ngx_http_complex_value(r, cache_key, key);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    r->cache = c;
    c->body_start = ngx_pagesize;
    c->file_cache = cache;
    c->file.log = r->connection->log;

    ngx_http_file_cache_create_key(r);

    return NGX_OK;
}

void
ngx_http_cache_purge_handler(ngx_http_request_t *r)
{
    ngx_int_t  rc;

#  if (NGX_HAVE_FILE_AIO)
    if (r->aio) {
        return;
    }
#  endif

    rc = ngx_http_file_cache_purge(r);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http file cache purge: %i, \"%s\"",
                   rc, r->cache->file.name.data);

    switch (rc) {
    case NGX_OK:
        r->write_event_handler = ngx_http_request_empty_handler;
        ngx_http_finalize_request(r, ngx_http_cache_purge_send_response(r));
        return;
    case NGX_DECLINED:
        ngx_http_finalize_request(r, NGX_HTTP_NOT_FOUND);
        return;
#  if (NGX_HAVE_FILE_AIO)
    case NGX_AGAIN:
        r->write_event_handler = ngx_http_cache_purge_handler;
        return;
#  endif
    default:
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    }
}

ngx_int_t
ngx_http_file_cache_purge(ngx_http_request_t *r)
{
    ngx_http_file_cache_t  *cache;
    ngx_http_cache_t       *c;

    switch (ngx_http_file_cache_open(r)) {
    case NGX_OK:
    case NGX_HTTP_CACHE_STALE:
#  if (nginx_version >= 8001) \
       || ((nginx_version < 8000) && (nginx_version >= 7060))
    case NGX_HTTP_CACHE_UPDATING:
#  endif
        break;
    case NGX_DECLINED:
        return NGX_DECLINED;
#  if (NGX_HAVE_FILE_AIO)
    case NGX_AGAIN:
        return NGX_AGAIN;
#  endif
    default:
        return NGX_ERROR;
    }

    c = r->cache;
    cache = c->file_cache;

    /*
     * delete file from disk but *keep* in-memory node,
     * because other requests might still point to it.
     */

    ngx_shmtx_lock(&cache->shpool->mutex);

    if (!c->node->exists) {
        /* race between concurrent purges, backoff */
        ngx_shmtx_unlock(&cache->shpool->mutex);
        return NGX_DECLINED;
    }

#  if (nginx_version >= 1000001)
    cache->sh->size -= c->node->fs_size;
    c->node->fs_size = 0;
#  else
    cache->sh->size -= (c->node->length + cache->bsize - 1) / cache->bsize;
    c->node->length = 0;
#  endif

    c->node->exists = 0;
#  if (nginx_version >= 8001) \
       || ((nginx_version < 8000) && (nginx_version >= 7060))
    c->node->updating = 0;
#  endif

    ngx_shmtx_unlock(&cache->shpool->mutex);

    if (ngx_delete_file(c->file.name.data) == NGX_FILE_ERROR) {
        /* entry in error log is enough, don't notice client */
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, ngx_errno,
                      ngx_delete_file_n " \"%s\" failed", c->file.name.data);
    }

    /* file deleted from cache */
    return NGX_OK;
}

char *
ngx_http_cache_purge_prefix_conf(ngx_conf_t *cf,
    ngx_http_cache_purge_prefix_conf_t *prefix_conf, ngx_module_t *tag)
{
    ngx_http_compile_complex_value_t   ccv;
    ngx_str_t                         *value;

    if (prefix_conf->enable != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    prefix_conf->zone = ngx_shared_memory_add(cf, &value[1], 0, tag);
    if (prefix_conf->zone == NULL) {
        return NGX_CONF_ERROR;
    }

    prefix_conf->prefix = ngx_palloc(cf->pool, sizeof(ngx_http_complex_value_t));
    if (prefix_conf->prefix == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[2];
    ccv.complex_value = prefix_conf->prefix;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    prefix_conf->enable = 1;

    return NGX_CONF_OK;
}

void
ngx_http_cache_purge_prefix_exec(ngx_http_request_t *r,
    ngx_http_file_cache_t *cache, ngx_str_t *prefix)
{
    ngx_http_cache_purge_walk_t  w;
    ngx_tree_ctx_t               tree;

    ngx_memzero(&w, sizeof(ngx_http_cache_purge_walk_t));

    w.r = r;
    w.cache = cache;
    w.prefix = *prefix;

    if (w.prefix.len && w.prefix.data[w.prefix.len - 1] == '*') {
        w.prefix.len--;
    }

    tree.init_handler = NULL;
    tree.file_handler = ngx_http_cache_purge_file;
    tree.pre_tree_handler = ngx_http_cache_purge_skip_temp;
    tree.post_tree_handler = ngx_http_cache_purge_noop;
    tree.spec_handler = ngx_http_cache_purge_noop;
    tree.data = &w;
    tree.alloc = 0;
    tree.log = r->connection->log;

    if (ngx_walk_tree(&tree, &cache->path->name) != NGX_OK) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    ngx_http_finalize_request(r,
        ngx_http_cache_purge_prefix_send_response(r, w.scanned, w.purged));
}

ngx_int_t
ngx_http_cache_purge_noop(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    return NGX_OK;
}

ngx_int_t
ngx_http_cache_purge_skip_temp(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    if (path->len >= 5
        && ngx_strncmp(path->data + path->len - 5, "/temp", 5) == 0)
    {
        return NGX_DECLINED;
    }

    return NGX_OK;
}

ngx_str_t
ngx_http_cache_purge_key_uri(ngx_str_t *key)
{
    ngx_str_t  uri;
    ngx_uint_t i;

    uri = *key;

    /*
     * cache keys are typically "$scheme$host$uri$is_args$args", so the
     * URI part starts at the first '/' (there is no "://" in the key).
     * Keys with an explicit "scheme://host" prefix are also supported.
     */

    for (i = 0; i + 2 < uri.len; i++) {
        if (uri.data[i] == ':' && uri.data[i + 1] == '/'
            && uri.data[i + 2] == '/')
        {
            uri.data += i + 3;
            uri.len -= i + 3;

            for (i = 0; i < uri.len; i++) {
                if (uri.data[i] == '/') {
                    break;
                }
            }

            if (i < uri.len) {
                uri.data += i;
                uri.len -= i;

            } else {
                uri.len = 0;
            }

            return uri;
        }
    }

    for (i = 0; i < uri.len; i++) {
        if (uri.data[i] == '/') {
            break;
        }
    }

    if (i < uri.len) {
        uri.data += i;
        uri.len -= i;
    }

    return uri;
}

ngx_int_t
ngx_http_cache_purge_prefix_purge(ngx_http_request_t *r,
    ngx_http_file_cache_t *cache, ngx_str_t *key)
{
    ngx_http_cache_t  *c;
    ngx_str_t         *k;

    c = ngx_pcalloc(r->pool, sizeof(ngx_http_cache_t));
    if (c == NULL) {
        return NGX_ERROR;
    }

    if (ngx_array_init(&c->keys, r->pool, 1, sizeof(ngx_str_t)) != NGX_OK) {
        return NGX_ERROR;
    }

    k = ngx_array_push(&c->keys);
    if (k == NULL) {
        return NGX_ERROR;
    }

    k->data = ngx_pnalloc(r->pool, key->len);
    if (k->data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(k->data, key->data, key->len);
    k->len = key->len;

    c->body_start = ngx_pagesize;
    c->file_cache = cache;
    c->file.log = r->connection->log;

    r->cache = c;

    ngx_http_file_cache_create_key(r);

    return ngx_http_file_cache_purge(r);
}

ngx_int_t
ngx_http_cache_purge_file(ngx_tree_ctx_t *ctx, ngx_str_t *path)
{
    u_char                        *buf;
    u_char                        *p;
    ssize_t                        n;
    size_t                         header_len;
    ngx_fd_t                       fd;
    ngx_file_t                     f;
    ngx_int_t                      rc;
    ngx_http_cache_purge_walk_t   *w;
    ngx_http_file_cache_header_t  *h;
    ngx_str_t                      key, uri;

    w = ctx->data;

    if (path->len < 2 * NGX_HTTP_CACHE_KEY_LEN) {
        return NGX_OK;
    }

    /*
     * Temporary files in cache have a suffix consisting of a dot
     * followed by 10 digits.
     */

    if (path->len >= 2 * NGX_HTTP_CACHE_KEY_LEN + 1 + 10
        && path->data[path->len - 10 - 1] == '.')
    {
        return NGX_OK;
    }

    if (ctx->size < (off_t) sizeof(ngx_http_file_cache_header_t)) {
        return NGX_OK;
    }

    fd = ngx_open_file(path->data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);
    if (fd == NGX_INVALID_FILE) {
        return NGX_OK;
    }

    f.fd = fd;
    f.name = *path;
    f.log = w->r->connection->log;

    buf = ngx_alloc(sizeof(ngx_http_file_cache_header_t), f.log);
    if (buf == NULL) {
        goto done;
    }

    n = ngx_read_file(&f, buf, sizeof(ngx_http_file_cache_header_t), 0);
    if (n < (ssize_t) sizeof(ngx_http_file_cache_header_t)) {
        goto cleanup;
    }

    h = (ngx_http_file_cache_header_t *) buf;

    if (h->version != NGX_HTTP_CACHE_VERSION
        || h->header_start < sizeof(ngx_http_file_cache_header_t)
        || h->header_start > ngx_pagesize)
    {
        goto cleanup;
    }

    header_len = h->header_start;

    ngx_free(buf);

    buf = ngx_alloc(header_len, f.log);
    if (buf == NULL) {
        goto done;
    }

    n = ngx_read_file(&f, buf, header_len, 0);
    if (n < (ssize_t) header_len) {
        goto cleanup;
    }

    p = buf + sizeof(ngx_http_file_cache_header_t);

    if ((size_t) (p - buf) + sizeof("\nKEY: ") - 1 > (size_t) n
        || ngx_memcmp(p, "\nKEY: ", sizeof("\nKEY: ") - 1) != 0)
    {
        goto cleanup;
    }

    key.data = p + sizeof("\nKEY: ") - 1;
    key.len = header_len - sizeof(ngx_http_file_cache_header_t)
              - (sizeof("\nKEY: ") - 1) - 1;

    if (key.len == 0) {
        goto cleanup;
    }

    uri = ngx_http_cache_purge_key_uri(&key);

    w->scanned++;

    if (w->prefix.len > uri.len
        || ngx_strncmp(uri.data, w->prefix.data, w->prefix.len) != 0)
    {
        goto cleanup;
    }

    rc = ngx_http_cache_purge_prefix_purge(w->r, w->cache, &key);
    if (rc == NGX_OK) {
        w->purged++;

    } else if (rc == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, w->r->connection->log, 0,
                      "cache purge prefix: failed to purge \"%V\"", path);
    }

cleanup:

    ngx_free(buf);

done:

    ngx_close_file(fd);

    return NGX_OK;
}

ngx_int_t
ngx_http_cache_purge_prefix_send_response(ngx_http_request_t *r,
    ngx_uint_t scanned, ngx_uint_t purged)
{
    ngx_chain_t   out;
    ngx_buf_t    *b;
    u_char       *p;
    ngx_int_t     rc;
    size_t        len;

    len = sizeof("<html>" CRLF
                 "<head><title>Successful purge</title></head>" CRLF
                 "<body bgcolor=\"white\">" CRLF
                 "<center><h1>Successful purge</h1>" CRLF
                 "<br>Scanned : ") - 1
          + NGX_INT_T_LEN
          + sizeof(CRLF "<br>Purged  : ") - 1
          + NGX_INT_T_LEN
          + sizeof(CRLF "</center>" CRLF
                 "<hr><center>" NGINX_VER "</center>" CRLF
                 "</body>" CRLF
                 "</html>" CRLF) - 1;

    b = ngx_create_temp_buf(r->pool, len);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    out.buf = b;
    out.next = NULL;

    p = ngx_cpymem(b->last,
                   "<html>" CRLF
                   "<head><title>Successful purge</title></head>" CRLF
                   "<body bgcolor=\"white\">" CRLF
                   "<center><h1>Successful purge</h1>" CRLF
                   "<br>Scanned : ",
                   sizeof("<html>" CRLF
                          "<head><title>Successful purge</title></head>" CRLF
                          "<body bgcolor=\"white\">" CRLF
                          "<center><h1>Successful purge</h1>" CRLF
                          "<br>Scanned : ") - 1);
    p = ngx_sprintf(p, "%ui", scanned);
    p = ngx_cpymem(p, CRLF "<br>Purged  : ",
                   sizeof(CRLF "<br>Purged  : ") - 1);
    p = ngx_sprintf(p, "%ui", purged);
    p = ngx_cpymem(p, CRLF "</center>" CRLF
                   "<hr><center>" NGINX_VER "</center>" CRLF
                   "</body>" CRLF
                   "</html>" CRLF,
                   sizeof(CRLF "</center>" CRLF
                          "<hr><center>" NGINX_VER "</center>" CRLF
                          "</body>" CRLF
                          "</html>" CRLF) - 1);

    b->last = p;
    b->last_buf = 1;

    r->headers_out.content_type.len = sizeof("text/html") - 1;
    r->headers_out.content_type.data = (u_char *) "text/html";
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = p - b->pos;

    if (r->method == NGX_HTTP_HEAD) {
        rc = ngx_http_send_header(r);
        if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
            return rc;
        }
    }

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_output_filter(r, &out);
}

char *
ngx_http_cache_purge_conf(ngx_conf_t *cf, ngx_http_cache_purge_conf_t *cpcf)
{
    ngx_cidr_t       cidr;
    ngx_in_cidr_t   *access;
# if (NGX_HAVE_INET6)
    ngx_in6_cidr_t  *access6;
# endif /* NGX_HAVE_INET6 */
    ngx_str_t       *value;
    ngx_int_t        rc;
    ngx_uint_t       i;

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "off") == 0) {
        cpcf->enable = 0;
        return NGX_CONF_OK;

    } else if (ngx_strcmp(value[1].data, "on") == 0) {
        ngx_str_set(&cpcf->method, "PURGE");

    } else {
        cpcf->method = value[1];
    }

    if (cf->args->nelts < 4) {
        cpcf->enable = 1;
        return NGX_CONF_OK;
    }

    /* sanity check */
    if (ngx_strcmp(value[2].data, "from") != 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid parameter \"%V\", expected"
                           " \"from\" keyword", &value[2]);
        return NGX_CONF_ERROR;
    }

    if (ngx_strcmp(value[3].data, "all") == 0) {
        cpcf->enable = 1;
        return NGX_CONF_OK;
    }

    for (i = 3; i < cf->args->nelts; i++) {
        rc = ngx_ptocidr(&value[i], &cidr);

        if (rc == NGX_ERROR) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid parameter \"%V\"", &value[i]);
            return NGX_CONF_ERROR;
        }

        if (rc == NGX_DONE) {
            ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                               "low address bits of %V are meaningless",
                               &value[i]);
        }

        switch (cidr.family) {
        case AF_INET:
            if (cpcf->access == NULL) {
                cpcf->access = ngx_array_create(cf->pool, cf->args->nelts - 3,
                                                sizeof(ngx_in_cidr_t));
                if (cpcf->access == NULL) {
                    return NGX_CONF_ERROR;
                }
            }

            access = ngx_array_push(cpcf->access);
            if (access == NULL) {
                return NGX_CONF_ERROR;
            }

            access->mask = cidr.u.in.mask;
            access->addr = cidr.u.in.addr;

            break;

# if (NGX_HAVE_INET6)
        case AF_INET6:
            if (cpcf->access6 == NULL) {
                cpcf->access6 = ngx_array_create(cf->pool, cf->args->nelts - 3,
                                                 sizeof(ngx_in6_cidr_t));
                if (cpcf->access6 == NULL) {
                    return NGX_CONF_ERROR;
                }
            }

            access6 = ngx_array_push(cpcf->access6);
            if (access6 == NULL) {
                return NGX_CONF_ERROR;
            }

            access6->mask = cidr.u.in6.mask;
            access6->addr = cidr.u.in6.addr;

            break;
# endif /* NGX_HAVE_INET6 */
        }
    }

    cpcf->enable = 1;

    return NGX_CONF_OK;
}

void
ngx_http_cache_purge_merge_conf(ngx_http_cache_purge_conf_t *conf,
    ngx_http_cache_purge_conf_t *prev)
{
    if (conf->enable == NGX_CONF_UNSET) {
        if (prev->enable == 1) {
            conf->enable = prev->enable;
            conf->method = prev->method;
            conf->access = prev->access;
            conf->access6 = prev->access6;

        } else {
            conf->enable = 0;
        }
    }
}

void *
ngx_http_cache_purge_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_cache_purge_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_cache_purge_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->*.method = { 0, NULL }
     *     conf->*.access = NULL
     *     conf->*.access6 = NULL
     *     conf->handler = NULL
     *     conf->original_handler = NULL
     */

# if (NGX_HTTP_FASTCGI)
    conf->fastcgi.enable = NGX_CONF_UNSET;
    conf->fastcgi_prefix.enable = NGX_CONF_UNSET;
# endif /* NGX_HTTP_FASTCGI */
# if (NGX_HTTP_PROXY)
    conf->proxy.enable = NGX_CONF_UNSET;
    conf->proxy_prefix.enable = NGX_CONF_UNSET;
# endif /* NGX_HTTP_PROXY */
# if (NGX_HTTP_SCGI)
    conf->scgi.enable = NGX_CONF_UNSET;
    conf->scgi_prefix.enable = NGX_CONF_UNSET;
# endif /* NGX_HTTP_SCGI */
# if (NGX_HTTP_UWSGI)
    conf->uwsgi.enable = NGX_CONF_UNSET;
    conf->uwsgi_prefix.enable = NGX_CONF_UNSET;
# endif /* NGX_HTTP_UWSGI */

    conf->conf = NGX_CONF_UNSET_PTR;

    return conf;
}

char *
ngx_http_cache_purge_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_cache_purge_loc_conf_t  *prev = parent;
    ngx_http_cache_purge_loc_conf_t  *conf = child;
    ngx_http_core_loc_conf_t         *clcf;
# if (NGX_HTTP_FASTCGI)
    ngx_http_fastcgi_loc_conf_t      *flcf;
# endif /* NGX_HTTP_FASTCGI */
# if (NGX_HTTP_PROXY)
    ngx_http_proxy_loc_conf_t        *plcf;
# endif /* NGX_HTTP_PROXY */
# if (NGX_HTTP_SCGI)
    ngx_http_scgi_loc_conf_t         *slcf;
# endif /* NGX_HTTP_SCGI */
# if (NGX_HTTP_UWSGI)
    ngx_http_uwsgi_loc_conf_t        *ulcf;
# endif /* NGX_HTTP_UWSGI */

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

# if (NGX_HTTP_FASTCGI)
    if (conf->fastcgi_prefix.enable != NGX_CONF_UNSET) {
        return NGX_CONF_OK;
    }
# endif /* NGX_HTTP_FASTCGI */
# if (NGX_HTTP_PROXY)
    if (conf->proxy_prefix.enable != NGX_CONF_UNSET) {
        return NGX_CONF_OK;
    }
# endif /* NGX_HTTP_PROXY */
# if (NGX_HTTP_SCGI)
    if (conf->scgi_prefix.enable != NGX_CONF_UNSET) {
        return NGX_CONF_OK;
    }
# endif /* NGX_HTTP_SCGI */
# if (NGX_HTTP_UWSGI)
    if (conf->uwsgi_prefix.enable != NGX_CONF_UNSET) {
        return NGX_CONF_OK;
    }
# endif /* NGX_HTTP_UWSGI */

# if (NGX_HTTP_FASTCGI)
    ngx_http_cache_purge_merge_conf(&conf->fastcgi, &prev->fastcgi);

    if (conf->fastcgi.enable && clcf->handler != NULL) {
        flcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_fastcgi_module);

        if (flcf->upstream.upstream || flcf->fastcgi_lengths) {
            conf->conf = &conf->fastcgi;
            conf->handler = flcf->upstream.cache
                          ? ngx_http_fastcgi_cache_purge_handler : NULL;
            conf->original_handler = clcf->handler;

            clcf->handler = ngx_http_cache_purge_access_handler;

            return NGX_CONF_OK;
        }
    }
# endif /* NGX_HTTP_FASTCGI */

# if (NGX_HTTP_PROXY)
    ngx_http_cache_purge_merge_conf(&conf->proxy, &prev->proxy);

    if (conf->proxy.enable && clcf->handler != NULL) {
        plcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_proxy_module);

        if (plcf->upstream.upstream || plcf->proxy_lengths) {
            conf->conf = &conf->proxy;
            conf->handler = plcf->upstream.cache
                          ? ngx_http_proxy_cache_purge_handler : NULL;
            conf->original_handler = clcf->handler;

            clcf->handler = ngx_http_cache_purge_access_handler;

            return NGX_CONF_OK;
        }
    }
# endif /* NGX_HTTP_PROXY */

# if (NGX_HTTP_SCGI)
    ngx_http_cache_purge_merge_conf(&conf->scgi, &prev->scgi);

    if (conf->scgi.enable && clcf->handler != NULL) {
        slcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_scgi_module);

        if (slcf->upstream.upstream || slcf->scgi_lengths) {
            conf->conf = &conf->scgi;
            conf->handler = slcf->upstream.cache
                          ? ngx_http_scgi_cache_purge_handler : NULL;
            conf->original_handler = clcf->handler;
            clcf->handler = ngx_http_cache_purge_access_handler;

            return NGX_CONF_OK;
        }
    }
# endif /* NGX_HTTP_SCGI */

# if (NGX_HTTP_UWSGI)
    ngx_http_cache_purge_merge_conf(&conf->uwsgi, &prev->uwsgi);

    if (conf->uwsgi.enable && clcf->handler != NULL) {
        ulcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_uwsgi_module);

        if (ulcf->upstream.upstream || ulcf->uwsgi_lengths) {
            conf->conf = &conf->uwsgi;
            conf->handler = ulcf->upstream.cache
                          ? ngx_http_uwsgi_cache_purge_handler : NULL;
            conf->original_handler = clcf->handler;

            clcf->handler = ngx_http_cache_purge_access_handler;

            return NGX_CONF_OK;
        }
    }
# endif /* NGX_HTTP_UWSGI */

    ngx_conf_merge_ptr_value(conf->conf, prev->conf, NULL);

    if (conf->handler == NULL) {
        conf->handler = prev->handler;
    }

    if (conf->original_handler == NULL) {
        conf->original_handler = prev->original_handler;
    }

    return NGX_CONF_OK;
}

#else /* !NGX_HTTP_CACHE */

static ngx_http_module_t  ngx_http_cache_purge_module_ctx = {
    NULL,  /* preconfiguration */
    NULL,  /* postconfiguration */

    NULL,  /* create main configuration */
    NULL,  /* init main configuration */

    NULL,  /* create server configuration */
    NULL,  /* merge server configuration */

    NULL,  /* create location configuration */
    NULL,  /* merge location configuration */
};

ngx_module_t  ngx_http_cache_purge_module = {
    NGX_MODULE_V1,
    &ngx_http_cache_purge_module_ctx,  /* module context */
    NULL,                              /* module directives */
    NGX_HTTP_MODULE,                   /* module type */
    NULL,                              /* init master */
    NULL,                              /* init module */
    NULL,                              /* init process */
    NULL,                              /* init thread */
    NULL,                              /* exit thread */
    NULL,                              /* exit process */
    NULL,                              /* exit master */
    NGX_MODULE_V1_PADDING
};

#endif /* NGX_HTTP_CACHE */
