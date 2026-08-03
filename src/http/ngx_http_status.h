
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HTTP_STATUS_H_INCLUDED_
#define _NGX_HTTP_STATUS_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * reason holds the fused "NNN Phrase" form, exactly the bytes that follow
 * "HTTP/1.1 " in a status line, so that a status line is still emitted with
 * a single copy.  rfc_section names the section that defines the code, or
 * marks the code as internal to nginx.
 */

typedef struct {
    ngx_uint_t   code;
    ngx_str_t    reason;
    ngx_uint_t   flags;
    const char  *rfc_section;
} ngx_http_status_def_t;


/*
 * CACHEABLE is heuristic cacheability as defined by RFC 9110, section 15.1,
 * while EXPIRES_OK is nginx's own set of codes that are eligible for expires
 * processing.  Neither set contains the other, so testing one in place of
 * the other changes which responses are eligible for an "Expires" header.
 */

#define NGX_HTTP_STATUS_CACHEABLE       0x0001
#define NGX_HTTP_STATUS_INFORMATIONAL   0x0002
#define NGX_HTTP_STATUS_CLIENT_ERROR    0x0004
#define NGX_HTTP_STATUS_SERVER_ERROR    0x0008
#define NGX_HTTP_STATUS_EXPIRES_OK      0x0010
#define NGX_HTTP_STATUS_INTERNAL        0x0020


/* the upper bound is exclusive; the range also sizes the lookup index */

#define NGX_HTTP_STATUS_MIN     100
#define NGX_HTTP_STATUS_MAX     600
#define NGX_HTTP_STATUS_RANGE   500


/*
 * A parameterized macro and not a function: the sources are ANSI C, which
 * has no keyword asking for a function to be expanded at its call site, and
 * a function with file scope defined in a header would be reported as
 * unused in every translation unit that does not call it, which is fatal
 * because warnings are treated as errors.  As in other nginx range macros,
 * the argument is expanded more than once and must not have side effects.
 */

#define ngx_http_status_in_range(s)                                          \
    ((s) >= NGX_HTTP_STATUS_MIN && (s) < NGX_HTTP_STATUS_MAX)


/*
 * The aggregator is included after the record above and before the prototypes
 * below, and not with the two headers at the top of this file, so that this
 * header may be included on its own: ngx_http.h includes this one in turn and
 * declares ngx_http_status_register() with the record type, which has to be
 * complete by then, and ngx_http_status_effective() below is declared with the
 * request type, which the aggregator brings.  Include ngx_http.h, as every HTTP
 * source already does, rather than this header on its own.
 */

#include <ngx_http.h>


/*
 * The functions of the published API are declared in ngx_http.h, beside the
 * rest of what a module may call.  These four are not part of it: they are the
 * registry's own, called by the HTTP core module to run the registry's
 * lifecycle and by two modules of the HTTP core to ask something the API does
 * not answer, so they are declared here, beside the definition of what they
 * answer from.
 *
 * ngx_http_status_init() builds the registry and is called for every
 * configuration, from the preconfiguration of the HTTP core module.  It leaves
 * the registry holding the codes nginx is built with and nothing besides,
 * whatever a configuration before it registered, so that a reload and a
 * configuration test are answered as a first start is.
 *
 * ngx_http_status_seal() closes the registry to registration and is called for
 * every configuration, from the postconfiguration of the HTTP core module:
 * every module has had its opportunity to register by then, and no worker
 * process has been forked yet, so nothing a worker reads is ever written.
 *
 * ngx_http_status_effective() answers which status a request is logged as, and
 * ngx_http_status_expires_ok() whether a status is eligible for expires
 * processing.  Each is the one place its answer is written down, and each is
 * asked by a module of the HTTP core rather than by the core itself.
 */

ngx_int_t ngx_http_status_init(ngx_conf_t *cf);
void ngx_http_status_seal(void);
ngx_uint_t ngx_http_status_effective(ngx_http_request_t *r);
ngx_uint_t ngx_http_status_expires_ok(ngx_uint_t status);


#endif /* _NGX_HTTP_STATUS_H_INCLUDED_ */
