
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
 * The registry also owns which row of the error page table a status code
 * selects, so that no table outside it is keyed by status code, and this is
 * the number of rows that mapping covers; it sizes that table.
 */

#define NGX_HTTP_STATUS_ERROR_PAGE_ROWS   53


/*
 * A parameterized macro and not a function: the sources are ANSI C, which
 * has no keyword asking for a function to be expanded at its call site, and
 * a function with internal linkage defined in a header would be reported as
 * unused in every translation unit that does not call it, which is fatal
 * because warnings are treated as errors.  As in other nginx range macros,
 * the argument is expanded more than once and must not have side effects.
 */

#define ngx_http_status_in_range(s)                                          \
    ((s) >= NGX_HTTP_STATUS_MIN && (s) < NGX_HTTP_STATUS_MAX)


/*
 * Whether a status can be written as the three digits that the HTTP/2 and
 * HTTP/3 field encoders reserve room for.  The ":status" field of such a
 * response is written into a field of exactly three bytes, and the width in a
 * %03ui conversion is a minimum and not a limit: it pads a shorter value but
 * never truncates a longer one, so a status of four digits or more would be
 * written past the end of that field.  A status must therefore be below this
 * bound before it reaches a response, whoever chose it, which is a wider range
 * than ngx_http_status_in_range() admits and a stricter requirement than being
 * described by the registry: a status relayed from an upstream is not
 * validated, and may fall outside the range the registry covers, but is still
 * emitted through those fields.  The argument is expanded once.
 */

#define NGX_HTTP_STATUS_WIRE_MAX  1000

#define ngx_http_status_wire_width_ok(s)                                     \
    ((s) < NGX_HTTP_STATUS_WIRE_MAX)


#if (NGX_HTTP_STATUS_VALIDATION)

/*
 * The one place a status that nginx or a configuration chose and that the
 * registry does not describe is reported, so that such a status is reported
 * once for a request and reported the same way whatever protocol version the
 * response uses.  It is called where a status is chosen and not where one is
 * emitted or promoted.  A status an upstream chose is exempt.
 */

ngx_int_t ngx_http_status_report(ngx_http_request_t *r, ngx_uint_t status);

#endif


/* init() runs for every configuration parsed, seal() before workers fork */

ngx_int_t ngx_http_status_init(ngx_conf_t *cf);
void ngx_http_status_seal(void);
ngx_uint_t ngx_http_status_effective(ngx_http_request_t *r);
ngx_uint_t ngx_http_status_expires_ok(ngx_uint_t status);
ngx_uint_t ngx_http_status_error_page_index(ngx_uint_t status);


#endif /* _NGX_HTTP_STATUS_H_INCLUDED_ */
