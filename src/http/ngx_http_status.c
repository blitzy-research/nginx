
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/*
 * The number of entries in the definition array.  Entry zero is a reserved
 * sentinel, so that a zero in the lookup index unambiguously means "not
 * registered" and no separate bitmap of the codes present is needed.  The
 * built-in definitions occupy the next NGX_HTTP_STATUS_NBUILTIN entries and
 * the remainder is headroom for ngx_http_status_register().
 *
 * The definition array and the lookup index together occupy
 *
 *     NGX_HTTP_STATUS_MAX_DEFS * sizeof(ngx_http_status_def_t)
 *     + NGX_HTTP_STATUS_RANGE * sizeof(u_short)
 *
 * bytes of static storage, that is 3560 bytes where a definition is 40
 * bytes wide.  Both are written only while the configuration is parsed,
 * which happens before the worker processes are forked, so a worker only
 * ever reads them and never dirties either page.
 */

#define NGX_HTTP_STATUS_MAX_DEFS   64
#define NGX_HTTP_STATUS_NBUILTIN   48


static ngx_http_status_def_t *ngx_http_status_lookup(ngx_uint_t status);


/*
 * The registry of the status codes that nginx itself is able to produce.
 * It is the union of the codes for which a NGX_HTTP_* constant exists, the
 * codes for which a reason phrase was kept by the header filter, the codes
 * for which an error page body is compiled in, and the codes defined by
 * RFC 9110, section 15.
 *
 * The reason member holds the fused "NNN Phrase" form, that is exactly the
 * bytes that follow "HTTP/1.1 " in a status line, so that a status line is
 * still emitted with a single copy.  A reason of ngx_null_string means
 * that nginx emits the code as three digits followed by a space, which is
 * what it did for these codes before the registry existed:
 *
 *   - the 1xx codes, whose status lines are emitted from their own
 *     constants and never from the phrase table;
 *   - 203 and 300, for which no phrase was ever compiled in;
 *   - nginx's own codes 444, 494, 495, 496, 497, and 499, which are
 *     internal signals rather than codes to be put on the wire.
 *
 * Eight of the phrases differ from the name that RFC 9110 recommends.
 * They are kept exactly as nginx has always emitted them, because a
 * reason phrase is a recommendation and not a requirement, and the name
 * from the RFC is recorded in rfc_section instead.
 *
 * Entries are ordered by code.  The order is a convenience for the reader
 * only: every lookup goes through the derived index.
 */

static ngx_http_status_def_t ngx_http_status_defs[NGX_HTTP_STATUS_MAX_DEFS] = {

    /* the reserved sentinel; index zero means "not registered" */

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


/*
 * The lookup index maps a status code to its entry in the definition
 * array.  It is indexed by the code less NGX_HTTP_STATUS_MIN and is
 * derived from the definitions by ngx_http_status_init(), so that the
 * definitions remain the only place where a code is spelled out.  A
 * u_short is wide enough because the number of entries can never approach
 * its range.
 */

static u_short     ngx_http_status_index[NGX_HTTP_STATUS_RANGE];
static ngx_uint_t  ngx_http_status_nelts;
static ngx_uint_t  ngx_http_status_sealed;


/*
 * Returns the definition of a status code, or NULL if the code is outside
 * the range that the registry describes or is not registered.  Subtracting
 * NGX_HTTP_STATUS_MIN before indexing lets a single unsigned comparison
 * cover both ends of the range, and the same difference is then used as the
 * index, so the bounds check costs nothing beyond the lookup itself.
 *
 * Before ngx_http_status_init() has run the index is all zeroes and every
 * code is reported as not registered, which is the safe answer while the
 * registry is not populated.
 */

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
 * Sets the response status of a request.  This is the single entry point
 * through which nginx assigns a status that it has chosen itself, so that
 * a policy such as validation has one place to apply.
 *
 * Only r->headers_out.status and r->status_final are written; in
 * particular the status line, the error status, and everything else are
 * left alone, because a caller that needs them changed does so itself and
 * the order in which those changes become visible must not move.
 *
 * Returns NGX_OK, or NGX_ERROR if the status was rejected, in which case
 * nothing has been written and the caller is expected to fail the request.
 * A status is only ever rejected in a build configured with
 * --with-http_status_validation; in the default build the whole check is
 * compiled out and what remains is the two stores.
 */

ngx_int_t
ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status)
{
#if (NGX_HTTP_STATUS_VALIDATION)

    /*
     * The exemption is scoped to the origin of the status rather than to
     * the call site.  An upstream server may return a status that nginx
     * has never heard of and nginx's contract is to relay it faithfully,
     * so a status that nginx did not choose is never validated.  The
     * upstream boundary is crossed in more than one place, including the
     * path that intercepts an upstream error and re-enters nginx's own
     * error page machinery while still carrying the upstream status, so an
     * exemption attached to individual call sites would miss it.
     */

    if (r->upstream == NULL && ngx_http_status_validate(status) != NGX_OK) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                      "unregistered HTTP status %ui", status);
        return NGX_ERROR;
    }

    /*
     * A status may legitimately be set again after the response has been
     * decided, for instance so that the access log records that a client
     * closed the connection, so this is reported and never refused.
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
 * Returns NGX_OK if the status code is described by the registry and
 * NGX_ERROR otherwise.  It takes no request, so it may also be called
 * while the configuration is parsed.
 *
 * nginx's own codes 444, 494, 495, 496, 497, and 499 are registry entries
 * like any other and are therefore accepted, which is what keeps them from
 * being reported as violations.
 */

ngx_int_t
ngx_http_status_validate(ngx_uint_t status)
{
    if (ngx_http_status_lookup(status) == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


/*
 * Returns the reason phrase of a status code in the fused "NNN Phrase"
 * form, that is exactly the bytes that follow "HTTP/1.1 " in a status
 * line, or NULL if the code is not registered.
 *
 * A registered code may have an empty phrase, in which case a pointer to
 * an ngx_str_t of zero length is returned rather than NULL.  A caller that
 * has no phrase to copy, for either reason, emits the code as three digits
 * followed by a space, exactly as nginx did before the registry existed.
 *
 * Only the address of the reason member is handed out, so that a caller
 * can neither reach the rest of the entry nor modify the registry.
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


/*
 * Returns a non-zero value if a cache may store a response with this
 * status code without being told for how long, as described by RFC 9110,
 * section 15.1, and zero otherwise.  An unregistered code is reported as
 * not cacheable, which is the conservative answer.
 *
 * This describes what the specification permits and decides nothing on its
 * own; nginx caches a response only when a configuration says so.  It is
 * also not the set of codes for which nginx emits an "Expires" header:
 * that is NGX_HTTP_STATUS_EXPIRES_OK, tested by
 * ngx_http_status_expires_ok(), and the two sets differ.
 */

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
 * Adds a status code to the registry, so that a module may describe a code
 * that nginx does not know about.  It returns NGX_OK, or NGX_ERROR if the
 * registry has been sealed, if the code is outside the range that the
 * registry describes, if the code is already described, or if the registry
 * is full.
 *
 * Registration is only permitted while the configuration is parsed, that
 * is between the preconfiguration and the postconfiguration of the HTTP
 * core module, both of which run before the worker processes are forked.
 * Once the registry has been sealed it stays read only for the life of the
 * process.
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
     * The definition is copied rather than referred to, so that a caller
     * may pass one that lives on its stack.  The copy duplicates the
     * ngx_str_t that describes the reason phrase and not the bytes it
     * points to, so reason.data and rfc_section must be static or must
     * live at least as long as the configuration pool.
     *
     * A registration always appends and never touches a built-in entry,
     * which is what lets ngx_http_status_init() discard registrations by
     * clearing everything past the built-in ones.
     */

    ngx_http_status_defs[ngx_http_status_nelts] = *def;

    ngx_http_status_index[def->code - NGX_HTTP_STATUS_MIN] =
                                            (u_short) ngx_http_status_nelts;

    ngx_http_status_nelts++;

    return NGX_OK;
}


/*
 * Prepares the registry and derives the lookup index from the built-in
 * definitions.  It is called from the preconfiguration of the HTTP core
 * module, which runs again on every configuration reload and on every
 * configuration test, so it is idempotent: the seal is released, any code
 * registered by a module for the previous configuration is discarded, and
 * the index is rebuilt from the built-in definitions alone.  The result is
 * therefore the same after every call.
 *
 * Nothing is allocated, here or anywhere else in the registry: both arrays
 * are static and cf is not used.
 *
 * Returns NGX_OK, or NGX_ERROR if a built-in definition cannot be indexed,
 * which would mean that the definitions themselves are inconsistent.
 */

ngx_int_t
ngx_http_status_init(ngx_conf_t *cf)
{
    ngx_uint_t  code, i;

    ngx_http_status_sealed = 0;

    ngx_memzero(ngx_http_status_index, sizeof(ngx_http_status_index));

    ngx_memzero(&ngx_http_status_defs[NGX_HTTP_STATUS_NBUILTIN + 1],
                (NGX_HTTP_STATUS_MAX_DEFS - NGX_HTTP_STATUS_NBUILTIN - 1)
                * sizeof(ngx_http_status_def_t));

    ngx_http_status_nelts = NGX_HTTP_STATUS_NBUILTIN + 1;

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

    return NGX_OK;
}


/*
 * Marks the registry read only.  It is called from the postconfiguration of
 * the HTTP core module, once every module has had the opportunity to
 * register a code of its own, and still before the worker processes are
 * forked, so that a worker only ever reads the registry.
 */

void
ngx_http_status_seal(void)
{
    ngx_http_status_sealed = 1;
}


/*
 * Returns the status that is reported for a request by the access log and
 * by the "$status" variable.  An error status takes precedence over the
 * response status, because it is set once the response has been replaced by
 * an error.  A request that was answered before a status was chosen at all
 * is reported as 9 when it used HTTP/0.9 and as 0 otherwise.
 *
 * A value is returned and never text: a caller formats it itself, with
 * three digits and leading zeroes, so 9 appears in a log as "009" and 0
 * appears as "000".
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
 * Returns a non-zero value if nginx emits the "Expires" and
 * "Cache-Control" headers of the "expires" directive for a response with
 * this status code, and zero otherwise.  An unregistered code is reported
 * as not eligible.
 *
 * This is nginx's own set of codes and is narrower than, and not contained
 * in, the set of codes that a cache may store heuristically: it holds for
 * 201, 302, 303, 304, and 307, which are not heuristically cacheable, and
 * does not hold for 203, 300, 404, 405, 410, 414, and 501, which are.
 * Testing NGX_HTTP_STATUS_CACHEABLE instead would change which responses
 * carry an "Expires" header.
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
