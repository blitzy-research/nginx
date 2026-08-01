
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/*
 * Entry zero is a reserved sentinel, so that a zero in the lookup index means
 * "not registered" and no separate bitmap is needed.  The entries past the
 * built-in ones are headroom for ngx_http_status_register().
 */

#define NGX_HTTP_STATUS_MAX_DEFS   64
#define NGX_HTTP_STATUS_NBUILTIN   48


static ngx_http_status_def_t *ngx_http_status_lookup(ngx_uint_t status);
static void ngx_http_status_discard(void);


/*
 * The reason member is the fused "NNN Phrase" form, exactly the bytes that
 * follow "HTTP/1.1 " in a status line, so that a status line is still emitted
 * with a single copy.  An ngx_null_string reason keeps the three digits and a
 * space that nginx emitted for that code before the registry existed, as it
 * did for every 1xx code; the separate "100 Continue" and "103 Early Hints"
 * lines are emitted from their own constants and are not registry driven.
 *
 * The eight phrases that differ from the name RFC 9110 recommends are kept
 * exactly as nginx has always emitted them, because a reason phrase is a
 * recommendation only; the name from the RFC is recorded in rfc_section.
 */

static ngx_http_status_def_t ngx_http_status_defs[NGX_HTTP_STATUS_MAX_DEFS] = {

    /* the reserved sentinel */

    { 0, ngx_null_string,
      0,
      NULL },

    /* 1xx informational */

    { 100, ngx_null_string,
      NGX_HTTP_STATUS_INFORMATIONAL,
      "RFC 9110 section 15.2.1" },

    { 101, ngx_null_string,
      NGX_HTTP_STATUS_INFORMATIONAL,
      "RFC 9110 section 15.2.2" },

    { 102, ngx_null_string,
      NGX_HTTP_STATUS_INFORMATIONAL,
      "RFC 2518 section 10.1" },

    { 103, ngx_null_string,
      NGX_HTTP_STATUS_INFORMATIONAL,
      "RFC 8297 section 2" },

    /* 2xx successful */

    { 200, ngx_string("200 OK"),
      NGX_HTTP_STATUS_CACHEABLE|NGX_HTTP_STATUS_EXPIRES_OK,
      "RFC 9110 section 15.3.1" },

    { 201, ngx_string("201 Created"),
      NGX_HTTP_STATUS_EXPIRES_OK,
      "RFC 9110 section 15.3.2" },

    { 202, ngx_string("202 Accepted"),
      0,
      "RFC 9110 section 15.3.3" },

    { 203, ngx_null_string,
      NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 section 15.3.4" },

    { 204, ngx_string("204 No Content"),
      NGX_HTTP_STATUS_CACHEABLE|NGX_HTTP_STATUS_EXPIRES_OK,
      "RFC 9110 section 15.3.5" },

    { 206, ngx_string("206 Partial Content"),
      NGX_HTTP_STATUS_CACHEABLE|NGX_HTTP_STATUS_EXPIRES_OK,
      "RFC 9110 section 15.3.7" },

    /* 3xx redirection */

    { 300, ngx_null_string,
      NGX_HTTP_STATUS_CACHEABLE,
      "RFC 9110 section 15.4.1" },

    { 301, ngx_string("301 Moved Permanently"),
      NGX_HTTP_STATUS_CACHEABLE|NGX_HTTP_STATUS_EXPIRES_OK,
      "RFC 9110 section 15.4.2" },

    { 302, ngx_string("302 Moved Temporarily"),
      NGX_HTTP_STATUS_EXPIRES_OK,
      "RFC 9110 section 15.4.3 (Found)" },

    { 303, ngx_string("303 See Other"),
      NGX_HTTP_STATUS_EXPIRES_OK,
      "RFC 9110 section 15.4.4" },

    { 304, ngx_string("304 Not Modified"),
      NGX_HTTP_STATUS_EXPIRES_OK,
      "RFC 9110 section 15.4.5" },

    { 307, ngx_string("307 Temporary Redirect"),
      NGX_HTTP_STATUS_EXPIRES_OK,
      "RFC 9110 section 15.4.8" },

    { 308, ngx_string("308 Permanent Redirect"),
      NGX_HTTP_STATUS_CACHEABLE|NGX_HTTP_STATUS_EXPIRES_OK,
      "RFC 9110 section 15.4.9" },

    /* 4xx client error */

    { 400, ngx_string("400 Bad Request"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.1" },

    { 401, ngx_string("401 Unauthorized"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.2" },

    { 402, ngx_string("402 Payment Required"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.3" },

    { 403, ngx_string("403 Forbidden"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.4" },

    { 404, ngx_string("404 Not Found"),
      NGX_HTTP_STATUS_CACHEABLE|NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.5" },

    { 405, ngx_string("405 Not Allowed"),
      NGX_HTTP_STATUS_CACHEABLE|NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.6 (Method Not Allowed)" },

    { 406, ngx_string("406 Not Acceptable"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.7" },

    { 408, ngx_string("408 Request Time-out"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.9 (Request Timeout)" },

    { 409, ngx_string("409 Conflict"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.10" },

    { 410, ngx_string("410 Gone"),
      NGX_HTTP_STATUS_CACHEABLE|NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.11" },

    { 411, ngx_string("411 Length Required"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.12" },

    { 412, ngx_string("412 Precondition Failed"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.13" },

    { 413, ngx_string("413 Request Entity Too Large"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.14 (Content Too Large)" },

    { 414, ngx_string("414 Request-URI Too Large"),
      NGX_HTTP_STATUS_CACHEABLE|NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.15 (URI Too Long)" },

    { 415, ngx_string("415 Unsupported Media Type"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.16" },

    { 416, ngx_string("416 Requested Range Not Satisfiable"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.17 (Range Not Satisfiable)" },

    { 421, ngx_string("421 Misdirected Request"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 9110 section 15.5.20" },

    { 429, ngx_string("429 Too Many Requests"),
      NGX_HTTP_STATUS_CLIENT_ERROR,
      "RFC 6585 section 4" },

    /* nginx's own codes; they have no standing outside nginx */

    { 444, ngx_null_string,
      NGX_HTTP_STATUS_CLIENT_ERROR|NGX_HTTP_STATUS_INTERNAL,
      "nginx internal" },

    { 494, ngx_null_string,
      NGX_HTTP_STATUS_CLIENT_ERROR|NGX_HTTP_STATUS_INTERNAL,
      "nginx internal" },

    { 495, ngx_null_string,
      NGX_HTTP_STATUS_CLIENT_ERROR|NGX_HTTP_STATUS_INTERNAL,
      "nginx internal" },

    { 496, ngx_null_string,
      NGX_HTTP_STATUS_CLIENT_ERROR|NGX_HTTP_STATUS_INTERNAL,
      "nginx internal" },

    { 497, ngx_null_string,
      NGX_HTTP_STATUS_CLIENT_ERROR|NGX_HTTP_STATUS_INTERNAL,
      "nginx internal" },

    { 499, ngx_null_string,
      NGX_HTTP_STATUS_CLIENT_ERROR|NGX_HTTP_STATUS_INTERNAL,
      "nginx internal" },

    /* 5xx server error */

    { 500, ngx_string("500 Internal Server Error"),
      NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 9110 section 15.6.1" },

    { 501, ngx_string("501 Not Implemented"),
      NGX_HTTP_STATUS_CACHEABLE|NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 9110 section 15.6.2" },

    { 502, ngx_string("502 Bad Gateway"),
      NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 9110 section 15.6.3" },

    { 503, ngx_string("503 Service Temporarily Unavailable"),
      NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 9110 section 15.6.4 (Service Unavailable)" },

    { 504, ngx_string("504 Gateway Time-out"),
      NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 9110 section 15.6.5 (Gateway Timeout)" },

    { 505, ngx_string("505 HTTP Version Not Supported"),
      NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 9110 section 15.6.6" },

    { 507, ngx_string("507 Insufficient Storage"),
      NGX_HTTP_STATUS_SERVER_ERROR,
      "RFC 4918 section 11.5" }
};


static u_short     ngx_http_status_index[NGX_HTTP_STATUS_RANGE];
static ngx_uint_t  ngx_http_status_nelts;
static ngx_uint_t  ngx_http_status_sealed;
static ngx_uint_t  ngx_http_status_built;


/*
 * The presence test that ngx_http_status_validate() answers with, applied
 * directly by ngx_http_status_set() so that setting a status tests the index
 * rather than calling a function to do it.  As in ngx_http_status_in_range(),
 * the argument is expanded more than once and must not have side effects.
 */

#define ngx_http_status_present(s)                                            \
    (ngx_http_status_in_range(s)                                              \
     && ngx_http_status_index[(s) - NGX_HTTP_STATUS_MIN] != 0)


/* before ngx_http_status_init() the index is empty and nothing is found */

static ngx_http_status_def_t *
ngx_http_status_lookup(ngx_uint_t status)
{
    ngx_uint_t  i;

    if (!ngx_http_status_in_range(status)) {
        return NULL;
    }

    i = ngx_http_status_index[status - NGX_HTTP_STATUS_MIN];

    if (i == 0) {
        return NULL;
    }

    return &ngx_http_status_defs[i];
}


/*
 * The single entry point for a status that nginx chose itself, so that a
 * policy such as validation has one place to apply.  headers_out.status and
 * status_final are the only members written in every build; a build configured
 * with --with-http_status_validation also maintains the status_reported bit.
 * The status line, which a caller supplies verbatim or leaves empty, and the
 * error status, which is a different status for a different purpose, are not
 * written here.
 *
 * A status that reaches this function was chosen by nginx or by a module, and
 * never by an upstream: the two places that carry an upstream's status across
 * into nginx's own structures, the copy in ngx_http_upstream_process_headers()
 * and the interception in ngx_http_upstream_intercept_errors(), both store it
 * directly and neither calls this function.
 *
 * A build without the switch stores the status and answers NGX_OK, whatever the
 * status is: what nginx sends is then exactly what it sent before the registry
 * existed, which is where behaviour matters, and the whole of this function is
 * two stores.  How wide a status may be is not asked here either, because it is
 * a property of the encoding a response uses rather than of choosing a status:
 * an HTTP/1.x status line encodes any width, and the two field encoders that
 * reserve exactly three bytes for it test it themselves, as does the one
 * boundary that admits a status from outside nginx altogether, the status()
 * method of the embedded Perl module.
 *
 * A build configured with --with-http_status_validation looks for a status the
 * registry does not describe.  Such a status is perfectly sendable, so it is
 * reported and then refused before it is stored, so that the caller's own error
 * handling runs and answers the request as it answers any other failure, and so
 * that the status the request already carried is not replaced by one that build
 * was configured to object to.  A call looks at the status once, and that one
 * answer decides both the report and the refusal.
 *
 * Every call looks, and a call is refused whenever the status it carries is one
 * the registry does not describe.  What has already happened to the request
 * decides nothing here: a status refused once and then set again, from the same
 * caller or from another, is examined and refused again, so that no sequence of
 * calls arrives at a status this build was configured to object to.  How often
 * such a status is written to the log is a separate question, and the only one
 * the record kept on the request answers: ngx_http_status_report() writes one
 * line for a request however many statuses that request is refused.
 *
 * That search is scoped to the origin of the response, by r->upstream, and not
 * to the value of the status: a request answered from an upstream is relayed
 * faithfully, whatever it answered.  A status nginx chooses for such a request
 * is exempt with it, which is wider than it need be and costs nothing, every
 * status nginx itself chooses being described by the registry.  Reporting is
 * all that the exemption decides, so no response in any build depends on it.
 *
 * Reporting is the policy ngx_http_status_report() defines, applied at each of
 * the points at which a status is chosen: here for a status a caller chose, the
 * single gate in ngx_http_special_response_handler() for one that a handler
 * returned, and ngx_http_send_error_page() for one an error_page supplied.  Of
 * those three only this one refuses, because only this one has a caller with a
 * result to act on; the other two are what answers a request that has already
 * gone wrong.
 *
 * Moving the error status of a request into the response afterwards is not a
 * status being chosen and does not come here at all: ngx_http_status_promote()
 * is what ngx_http_send_header() moves it with, for the reason given there.
 *
 * The setter is a function that is called in either build, so that a module
 * compiled against one build of nginx behaves in another exactly as a module
 * compiled against that one does, and so that its argument is evaluated once.
 */

ngx_int_t
ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status)
{
#if (NGX_HTTP_STATUS_VALIDATION)

    if (r->upstream == NULL && !ngx_http_status_present(status)) {
        ngx_http_status_report(r, status);

        return NGX_ERROR;
    }

    /*
     * A status may legitimately be set again after the response has been
     * decided, for instance so that the access log records that a client
     * closed the connection; this is therefore only noted and never refused.
     */

    if (r->status_final) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http status already set, now %ui", status);
    }

#endif

    r->headers_out.status = status;
    r->status_final = 1;

    return NGX_OK;
}


/*
 * Moves the error status of a request into the response, over the response
 * status, which is what ngx_http_send_header() does last of all before the
 * filters that emit a response run.  It writes the same two members
 * ngx_http_status_set() writes and nothing else; the status line, which the
 * promoted status must not be sent under, is cleared by its caller and not by
 * this, the members this writes being the members the setter writes.
 *
 * It is not a second way to choose a status and is not part of the API a module
 * uses: the HTTP core calls it from one place, for one status, and a module
 * with a status to set calls ngx_http_status_set() as everything else does.
 *
 * The status it moves has been examined already, at whichever point chose it:
 * every write of r->err_status is in ngx_http_special_response.c, and the two
 * that carry a status from outside that file -- the status a handler returned
 * and the status an error_page directive supplied -- each pass a gate that
 * examines it first.  A status the registry does not describe reaches here only
 * because one of those gates reported it and deliberately let it stand, having
 * no caller of its own to answer a refusal to, so this cannot refuse it and
 * does not examine it: refusing it here would take away the very response the
 * gate that let it stand exists to produce, leaving a request that had already
 * gone wrong with no response at all rather than with a worse one.
 *
 * That is why this stands beside the setter rather than as a condition inside
 * it.  The setter refuses every status the registry does not describe, whatever
 * has happened to the request before it, and this one place has a status that
 * was examined elsewhere to move and no error path to move it by.
 */

void
ngx_http_status_promote(ngx_http_request_t *r)
{
    r->headers_out.status = (ngx_uint_t) r->err_status;
    r->status_final = 1;
}


ngx_int_t
ngx_http_status_validate(ngx_uint_t status)
{
    if (!ngx_http_status_present(status)) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


/*
 * A registered code with an empty phrase yields an ngx_str_t of zero length
 * and an unregistered one yields NULL; a caller with no phrase to copy emits
 * the code as three digits followed by a space in either case.
 */

const ngx_str_t *
ngx_http_status_reason(ngx_uint_t status)
{
    ngx_http_status_def_t  *def;

    def = ngx_http_status_lookup(status);

    if (def == NULL) {
        return NULL;
    }

    return &def->reason;
}


/* what RFC 9110, section 15.1, permits a cache to store heuristically */

ngx_uint_t
ngx_http_status_is_cacheable(ngx_uint_t status)
{
    ngx_http_status_def_t  *def;

    def = ngx_http_status_lookup(status);

    if (def == NULL) {
        return 0;
    }

    return def->flags & NGX_HTTP_STATUS_CACHEABLE;
}


/*
 * Registration is only permitted while a configuration is parsed and is
 * refused once that configuration has been sealed.  The next configuration to
 * be parsed reinitializes the registry and unseals it again in the master
 * process; a worker inherits a sealed registry and only reads it.
 */

ngx_int_t
ngx_http_status_register(ngx_http_status_def_t *def)
{
    if (def == NULL) {
        return NGX_ERROR;
    }

    if (ngx_http_status_sealed) {
        return NGX_ERROR;
    }

    if (!ngx_http_status_in_range(def->code)) {
        return NGX_ERROR;
    }

    if (ngx_http_status_index[def->code - NGX_HTTP_STATUS_MIN] != 0) {
        return NGX_ERROR;
    }

    if (ngx_http_status_nelts >= NGX_HTTP_STATUS_MAX_DEFS) {
        return NGX_ERROR;
    }

    /*
     * The definition is copied so that a caller may pass one from its stack,
     * but the copy duplicates the ngx_str_t and not the bytes it points to, so
     * reason.data and rfc_section must live at least as long as the
     * configuration.  A registration always appends, which is what lets
     * ngx_http_status_init() discard registrations.
     */

    ngx_http_status_defs[ngx_http_status_nelts] = *def;

    ngx_http_status_index[def->code - NGX_HTTP_STATUS_MIN] =
                                            (u_short) ngx_http_status_nelts;

    ngx_http_status_nelts++;

    return NGX_OK;
}


/*
 * Preconfiguration runs again for every configuration that is parsed, that is
 * on every reload and on every configuration test, so this is idempotent.
 *
 * It is not, however, a rebuild.  The built-in definitions and the index over
 * them do not depend on the configuration, so they are derived once in the life
 * of the process and every later configuration finds them exactly as the first
 * one left them; a reload therefore writes the seal and nothing else, and no
 * page a worker shares with the master is written after that worker forked.
 * What a configuration can change is what its own modules registered, and only
 * a configuration that registered something has anything to discard.
 */

ngx_int_t
ngx_http_status_init(ngx_conf_t *cf)
{
    ngx_uint_t  code, i;

    ngx_http_status_sealed = 0;

    if (ngx_http_status_built) {
        ngx_http_status_discard();
        return NGX_OK;
    }

    for (i = 1; i <= NGX_HTTP_STATUS_NBUILTIN; i++) {
        code = ngx_http_status_defs[i].code;

        if (!ngx_http_status_in_range(code)) {
            return NGX_ERROR;
        }

        if (ngx_http_status_index[code - NGX_HTTP_STATUS_MIN] != 0) {
            return NGX_ERROR;
        }

        ngx_http_status_index[code - NGX_HTTP_STATUS_MIN] = (u_short) i;
    }

    ngx_http_status_nelts = NGX_HTTP_STATUS_NBUILTIN + 1;
    ngx_http_status_built = 1;

    return NGX_OK;
}


/*
 * What the configuration before this one registered, and nothing else: a
 * registration always appends, so the rows past the built-in ones are exactly
 * those, and a configuration that registered nothing leaves the registry
 * untouched.
 */

static void
ngx_http_status_discard(void)
{
    ngx_uint_t  code, i;

    for (i = NGX_HTTP_STATUS_NBUILTIN + 1; i < ngx_http_status_nelts; i++) {
        code = ngx_http_status_defs[i].code;

        ngx_http_status_index[code - NGX_HTTP_STATUS_MIN] = 0;
    }

    if (ngx_http_status_nelts > NGX_HTTP_STATUS_NBUILTIN + 1) {
        ngx_memzero(&ngx_http_status_defs[NGX_HTTP_STATUS_NBUILTIN + 1],
                    (ngx_http_status_nelts - NGX_HTTP_STATUS_NBUILTIN - 1)
                    * sizeof(ngx_http_status_def_t));

        ngx_http_status_nelts = NGX_HTTP_STATUS_NBUILTIN + 1;
    }
}


void
ngx_http_status_seal(void)
{
    ngx_http_status_sealed = 1;
}


/*
 * An error status wins over the response status because it is set once the
 * response has been replaced by an error; a request answered before any status
 * was chosen is reported as 9 when it used HTTP/0.9 and as 0 otherwise, which
 * a caller's three digit formatting turns into "009" and "000".
 */

ngx_uint_t
ngx_http_status_effective(ngx_http_request_t *r)
{
    if (r->err_status) {
        return r->err_status;
    }

    if (r->headers_out.status) {
        return r->headers_out.status;
    }

    if (r->http_version == NGX_HTTP_VERSION_9) {
        return 9;
    }

    return 0;
}


/*
 * Whether a status is eligible for the expires processing of the "expires"
 * directive; whether the "Expires" and "Cache-Control" headers are then
 * emitted is a matter of configuration.  Neither this set nor the set of
 * heuristically cacheable codes contains the other, so testing
 * NGX_HTTP_STATUS_CACHEABLE instead would change which responses are eligible.
 */

ngx_uint_t
ngx_http_status_expires_ok(ngx_uint_t status)
{
    ngx_http_status_def_t  *def;

    def = ngx_http_status_lookup(status);

    if (def == NULL) {
        return 0;
    }

    return def->flags & NGX_HTTP_STATUS_EXPIRES_OK;
}
