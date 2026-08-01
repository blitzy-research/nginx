
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
 * a function with internal linkage defined in a header would be reported as
 * unused in every translation unit that does not call it, which is fatal
 * because warnings are treated as errors.  As in other nginx range macros,
 * the argument is expanded more than once and must not have side effects.
 */

#define ngx_http_status_in_range(s)                                          \
    ((s) >= NGX_HTTP_STATUS_MIN && (s) < NGX_HTTP_STATUS_MAX)


/*
 * Whether a status can be written as the three digits that the HTTP/2 and
 * HTTP/3 field encoders reserve room for.  This is a bound belonging to a fixed
 * width encoding and not a rule about which statuses nginx may use: the
 * ":status" field of such a response is written into a field of exactly three
 * bytes, and the width in a %03ui conversion is a minimum and not a limit, so
 * it pads a shorter value but never truncates a longer one and a status of four
 * digits or more would be written past the end of that field.  An HTTP/1.x
 * status line reserves NGX_INT_T_LEN bytes for the same number and encodes any
 * width, so nothing outside such an encoding consults this.
 *
 * It is therefore tested by the two filters that reserve those three bytes, and
 * once more where the one status that neither a parser nor the registry bounds
 * is chosen, in the status() method of the embedded Perl module: an integer
 * arriving from a Perl script is the only status nginx neither parses from a
 * response nor writes itself.  The bound is a wider range than
 * ngx_http_status_in_range() admits and answers a different question from being
 * described by the registry: a status relayed from an upstream is not
 * validated, and may fall outside the range the registry covers, but is still
 * written into those fields.  The argument is expanded once.
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
 * This reports whatever status it is given and examines nothing: whether the
 * status is one the registry describes, and whether a status an upstream chose
 * is exempt, are both decided by its caller before the status reaches here, so
 * that a status is examined once wherever it is both reported and acted upon.
 *
 * It is a macro rather than a function so that a build configured with
 * --with-http_status_validation exports no symbol that a build without it does
 * not: what may be linked against must not depend on a build option.  As in
 * other nginx macros the arguments are expanded more than once and must not
 * have side effects.  The expansion is a single statement, so that it may stand
 * as the body of an unbraced "if" without capturing an "else" of its caller's.
 * A report is made at alert level, so that it survives an error_log level that
 * hides anything less.
 */

#define ngx_http_status_report(r, s)                                          \
    do {                                                                      \
        if (!(r)->status_reported) {                                          \
            (r)->status_reported = 1;                                         \
                                                                              \
            ngx_log_error(NGX_LOG_ALERT, (r)->connection->log, 0,             \
                          "unregistered HTTP status %ui", (ngx_uint_t) (s));  \
        }                                                                     \
    } while (0)

#endif


#endif /* _NGX_HTTP_STATUS_H_INCLUDED_ */
