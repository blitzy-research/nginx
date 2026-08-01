
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
 * Reports a status that nginx or a configuration chose and that the registry
 * does not describe.  It is used where a status is chosen, and not where one is
 * emitted or promoted, so that such a status is reported once for a request and
 * reported the same way whatever protocol version the response uses.
 *
 * Reporting is one half of the policy and refusing is the other, and each is
 * applied where it can be.  ngx_http_status_set() reports such a status and
 * then refuses it, so that the caller's own error handling runs and answers the
 * request as it would answer any other failure.  The two gates that stand
 * where a status arrives with no caller to answer to, in
 * ngx_http_special_response_handler() and in ngx_http_send_error_page(), report
 * and let the response carry the status chosen for it: those functions are what
 * answers a request that has already gone wrong, so refusing there would leave
 * it with no response at all rather than with a worse one.  No status is
 * rewritten to a different one at any of the three.
 *
 * This reports whatever status it is given.  Whether a status an upstream
 * chose is exempt is decided by its caller, from r->upstream, before the status
 * reaches here.
 *
 * It is a macro rather than a function so that a build configured with
 * --with-http_status_validation exports no symbol that a build without it does
 * not: what may be linked against must not depend on a build option.  As in
 * other nginx macros the arguments are expanded more than once and must not
 * have side effects, and as in ngx_log_error() the expansion is a statement.
 * A report is made at alert level, so that it survives an error_log level that
 * hides anything less.
 */

#define ngx_http_status_report(r, s)                                          \
    if (!(r)->status_reported                                                 \
        && ngx_http_status_validate(s) != NGX_OK)                             \
    {                                                                         \
        (r)->status_reported = 1;                                             \
                                                                              \
        ngx_log_error(NGX_LOG_ALERT, (r)->connection->log, 0,                 \
                      "unregistered HTTP status %ui", (ngx_uint_t) (s));      \
    }

#endif


/*
 * init() runs for every configuration parsed, seal() before workers fork.
 *
 * error_page_index() is the registry's own answer to which row of the error
 * page table a status code selects: that mapping is status code knowledge, so
 * the registry owns it rather than the table's file, and it is therefore
 * reached by a call.  Like every function here it is defined in either build.
 *
 * The surface stops here.  The flags a row carries and the section reference
 * beside them are the registry's own record of where a code comes from and
 * what may be said of it, and no function yields either: what is asked of the
 * registry at run time is whether a code is cacheable and whether nginx's
 * "expires" processing applies to it, and each of those has a function of its
 * own above.  Nothing yields the address of a row, so no caller reaches a
 * member it was not asking about and none can be written through.
 */

ngx_int_t ngx_http_status_init(ngx_conf_t *cf);
void ngx_http_status_seal(void);
ngx_uint_t ngx_http_status_effective(ngx_http_request_t *r);
ngx_uint_t ngx_http_status_expires_ok(ngx_uint_t status);
ngx_uint_t ngx_http_status_error_page_index(ngx_uint_t status);


#endif /* _NGX_HTTP_STATUS_H_INCLUDED_ */
