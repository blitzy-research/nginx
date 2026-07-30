
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HTTP_STATUS_H_INCLUDED_
#define _NGX_HTTP_STATUS_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/*
 * The HTTP status code registry describes every status code that nginx
 * itself is able to produce.  A single table of definitions replaces the
 * reason phrase and the error page tables that the header filter and the
 * special response modules used to maintain separately, each with its own
 * set of offset macros for the ranges of codes involved.
 *
 * A definition carries the numeric code, the reason phrase as it appears
 * on the wire, a set of semantic flags, and the section of the RFC that
 * defines the code.  The reason member holds the fused "NNN Phrase" form,
 * that is, exactly the bytes that follow "HTTP/1.1 " in a status line, so
 * that a status line is still emitted with a single copy.
 *
 * The registry is filled in while the configuration is parsed and is
 * sealed before the worker processes are forked, so it is read only for
 * the whole life of a worker and nothing is allocated per request.
 */

typedef struct {
    ngx_uint_t   code;
    ngx_str_t    reason;
    ngx_uint_t   flags;
    const char  *rfc_section;
} ngx_http_status_def_t;


/*
 * Semantic flags.  Only the low six bits are assigned; the remaining bits
 * are reserved for future metadata and can be used without changing the
 * layout of ngx_http_status_def_t.
 *
 * NGX_HTTP_STATUS_CACHEABLE and NGX_HTTP_STATUS_EXPIRES_OK answer two
 * different questions and must never be collapsed into a single flag:
 *
 *   - CACHEABLE is heuristic cacheability as defined by RFC 9110, 15.1.
 *     It holds for exactly twelve codes: 200, 203, 204, 206, 300, 301,
 *     308, 404, 405, 410, 414, and 501.
 *
 *   - EXPIRES_OK is nginx's own, narrower set of codes for which the
 *     "Expires" and "Cache-Control" headers are emitted.  It holds for
 *     exactly ten codes: 200, 201, 204, 206, 301, 302, 303, 304, 307,
 *     and 308.
 *
 * Neither set contains the other.  EXPIRES_OK adds 201, 302, 303, 304,
 * and 307, which are not heuristically cacheable, and it omits 203, 300,
 * 404, 405, 410, 414, and 501, which are.  Testing the wrong flag would
 * change which responses receive an "Expires" header.
 *
 * The other flags describe which range a code belongs to, so that a
 * consumer can test a flag instead of open coding arithmetic on the code:
 *
 *   - INFORMATIONAL holds for 100, 101, 102, and 103.
 *   - CLIENT_ERROR holds for every 4xx code, including nginx's own codes
 *     444, 494, 495, 496, 497, and 499.
 *   - SERVER_ERROR holds for every 5xx code.
 *   - INTERNAL marks those six nginx codes, which have no RFC standing
 *     and must therefore be accepted without being reported as errors.
 */

#define NGX_HTTP_STATUS_CACHEABLE       0x0001
#define NGX_HTTP_STATUS_INFORMATIONAL   0x0002
#define NGX_HTTP_STATUS_CLIENT_ERROR    0x0004
#define NGX_HTTP_STATUS_SERVER_ERROR    0x0008
#define NGX_HTTP_STATUS_EXPIRES_OK      0x0010
#define NGX_HTTP_STATUS_INTERNAL        0x0020


/*
 * The range of status codes that the registry is able to describe.  The
 * lower bound is inclusive and the upper bound is exclusive, so the range
 * spans NGX_HTTP_STATUS_RANGE codes, which is also the number of entries
 * in the lookup index derived from the definitions.
 */

#define NGX_HTTP_STATUS_MIN     100
#define NGX_HTTP_STATUS_MAX     600
#define NGX_HTTP_STATUS_RANGE   500


/*
 * Tests whether a status code lies within the range that the registry
 * describes.  This is a parameterized macro rather than a function
 * because it is evaluated on the response path: the sources are ANSI C,
 * which has no keyword asking for a function to be expanded at its call
 * site, and a function with internal linkage defined in a header would be
 * reported as unused in every translation unit that does not call it,
 * which is fatal because warnings are treated as errors.
 *
 * Since a status code is unsigned, a compiler folds both comparisons into
 * a single unsigned comparison of the code less NGX_HTTP_STATUS_MIN
 * against NGX_HTTP_STATUS_RANGE.  That difference is also the value with
 * which the registry lookup indexes the code, so the same subtraction
 * serves both the bounds check and the lookup.
 *
 * As in other nginx range macros, the argument is expanded more than once
 * and therefore must not have side effects.
 */

#define ngx_http_status_in_range(s)                                          \
    ((s) >= NGX_HTTP_STATUS_MIN && (s) < NGX_HTTP_STATUS_MAX)


/*
 * ngx_http_status_init() populates the registry from the built-in
 * definitions and derives the lookup index.  It is called from the HTTP
 * core preconfiguration handler and is idempotent, because
 * preconfiguration runs again on every configuration reload and on every
 * configuration test.  It returns NGX_OK, or NGX_ERROR if a built-in
 * definition cannot be indexed.
 *
 * ngx_http_status_seal() marks the registry read only.  It is called from
 * the HTTP core postconfiguration handler, once every module has had the
 * opportunity to register additional codes.
 *
 * ngx_http_status_effective() returns the status value that the access
 * log and the "$status" variable report for a request, applying the
 * single fallback chain that the log module and the variable handler used
 * to implement separately.
 *
 * ngx_http_status_expires_ok() returns a non-zero value if nginx emits
 * the "Expires" and "Cache-Control" headers for the given status code and
 * zero otherwise; see the note on NGX_HTTP_STATUS_EXPIRES_OK above.
 */

ngx_int_t ngx_http_status_init(ngx_conf_t *cf);
void ngx_http_status_seal(void);
ngx_uint_t ngx_http_status_effective(ngx_http_request_t *r);
ngx_uint_t ngx_http_status_expires_ok(ngx_uint_t status);


#endif /* _NGX_HTTP_STATUS_H_INCLUDED_ */
