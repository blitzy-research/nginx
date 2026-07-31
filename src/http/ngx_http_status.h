
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


/*
 * Whether the status the request is presenting to nginx's own status machinery
 * is the one an upstream chose for the response being relayed.  Such a status
 * is never validated: an upstream may answer with a code nginx has never heard
 * of, or with one below 100, and nginx's contract is to relay what it received.
 *
 * Authorship is recorded on the request where it is known, by the two places
 * that carry an upstream's status across into nginx's own structures, and is
 * never deduced afterwards from the status having some particular value.  Two
 * authors may choose the same number for one request, so equal numbers would
 * not prove equal authorship: an upstream answering 599 for a request whose
 * configuration says "error_page 599 =599 /uri" leaves nginx relaying one 599
 * and the configuration choosing another, and only the first is exempt.
 *
 * A request that has an upstream is therefore not exempt as such.  The path
 * that intercepts an upstream error re-enters nginx's own error page
 * machinery, where an error_page directive may replace the status of the
 * intercepted response and a location the request is redirected to may choose
 * one of its own; those statuses are the configuration's and nginx's, and the
 * record is cleared as each of them is chosen.
 *
 * This answers for the status a request is presenting and not for a status a
 * caller is about to choose, so it belongs at a gate that is handed a status
 * whose author it does not know, which is the one in
 * ngx_http_special_response_handler().  A status handed to
 * ngx_http_status_set() was chosen by nginx or by a module, relaying being done
 * by storing a status directly and not through the setter, so the setter does
 * not consult this: doing so would exempt a status a filter chose for a
 * response whose headers came from an upstream, a 304 or a 206 say, on the
 * strength of the response it is amending.  The argument is expanded once.
 *
 * The record is set where a response status is relayed as well as where an
 * error of an upstream's is intercepted, so that both crossings of the upstream
 * boundary say who chose the status rather than leaving it to be worked out
 * later.  It is cleared by ngx_http_status_set(), which is nginx choosing a
 * status, and by the gate itself once it has read it.  One case is therefore
 * exempt more broadly than it need be, and is worth naming: a status nginx
 * chooses by finalizing a request whose relayed response status is still the
 * one recorded, a 502 after the header of a relayed response has been sent say,
 * is exempt at that gate.  Every status nginx itself chooses is described by
 * the registry, so no report is lost by it; and no response in any build
 * depends on it, reporting being all that the record decides.
 */

#define ngx_http_status_relayed(r)   ((r)->status_upstream)


#if (NGX_HTTP_STATUS_VALIDATION)

/*
 * Reports a status that nginx or a configuration chose and that the registry
 * does not describe.  It is used where a status is chosen, and not where one is
 * emitted or promoted, so that such a status is reported once for a request and
 * reported the same way whatever protocol version the response uses; the report
 * is recorded on the request itself and never inferred from the status having
 * some particular value.
 *
 * Reporting is one half of the policy and refusing is the other, and they are
 * applied where each of them can be: a status is reported wherever it is
 * chosen, and refused where the caller that chose it has a result to act on.
 * ngx_http_status_set() therefore reports such a status and then refuses it, so
 * that the caller's own error handling runs and answers the request as it would
 * answer any other failure, while the two gates that stand where a status
 * arrives with no caller to answer to, in ngx_http_special_response_handler()
 * and in ngx_http_send_error_page(), report and let the response carry the
 * status that was chosen for it: those functions are what answers a request
 * that has already gone wrong, so refusing there would leave the request with
 * no response at all rather than with a worse one.
 *
 * This reports whatever status it is given: a status an upstream chose is not
 * examined at all, and the one gate that can be handed such a status is where
 * that is decided, by ngx_http_status_relayed() above.  Every other place a
 * status is examined has been handed one that nginx or a configuration chose.
 *
 * This is the one definition of that policy, and it is a macro rather than a
 * function so that a build configured with --with-http_status_validation
 * exports no symbol that a build without it does not: what may be linked
 * against must not depend on a build option.  As in other nginx macros the
 * arguments are expanded more than once and must not have side effects, and as
 * in ngx_log_error() the expansion is a statement, so it is used as one.
 *
 * A build configured with --with-http_status_validation is a build for
 * development and for conformance work rather than the build to run: the switch
 * defaults to off, so a build that does not ask for it reports nothing and
 * sends what it always sent, and refusing a status is confined to that build in
 * the same way the reporting is.  A report is made at alert level, so that it
 * survives an error_log level that hides anything less.
 *
 * One consequence is worth stating, because it reads as a defect and is not: on
 * this build a status the registry does not describe is answered differently
 * depending on how it was chosen.  One that reaches the setter is refused, so
 * the request is answered with the 500 that the caller's error handling
 * produces, while one that reaches a gate is reported and sent.  A "return"
 * directive falls on either side of that, since nginx answers it with a
 * response of its own, through the setter, when it names a body or a code below
 * 400, and returns the code to be answered as an error otherwise.  A build
 * without the switch sends the status that was asked for in every one of those
 * cases.
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
 * has_flags() and rfc_section() answer for the two members of a row that no
 * other function reads out: the flags a code carries and the section that
 * defines it.  They are what lets a caller test class membership, or nginx's
 * own codes, against the registry rather than against arithmetic on the status
 * value written out where it is needed; the cacheable and the expires
 * questions keep functions of their own because they are asked so often, and
 * because which of the two is meant at a call site is worth saying in its
 * name.  Neither yields the address of a row: has_flags() yields a value and
 * rfc_section() the pointer a row holds, so neither reaches a member a caller
 * was not asking about and neither can be written through.
 */

ngx_int_t ngx_http_status_init(ngx_conf_t *cf);
void ngx_http_status_seal(void);
ngx_uint_t ngx_http_status_effective(ngx_http_request_t *r);
ngx_uint_t ngx_http_status_expires_ok(ngx_uint_t status);
ngx_uint_t ngx_http_status_error_page_index(ngx_uint_t status);
ngx_uint_t ngx_http_status_has_flags(ngx_uint_t status, ngx_uint_t flags);
const char *ngx_http_status_rfc_section(ngx_uint_t status);


#endif /* _NGX_HTTP_STATUS_H_INCLUDED_ */
