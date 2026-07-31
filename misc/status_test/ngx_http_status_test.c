
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/*
 * A driver for the HTTP status code registry.  It exercises the five
 * functions the registry publishes through <ngx_http.h>, the five it
 * publishes through its own header, the macros both of those headers define,
 * and the lifecycle that leaves the registry writable only while a
 * configuration is parsed.
 *
 * The driver is linked against the object tree of a build of nginx, with the
 * one object that defines main() left out, by the makefile beside this file.
 * Nothing in the build depends on it and auto/configure does not know of it:
 * it is made on request.  It reports on the standard error output and exits
 * zero only when every check passed, so that a script may run it.
 *
 * The registry's own header is not included here.  <ngx_http.h> includes it,
 * which is how every other file in the tree reaches the registry, so this one
 * reaches it the same way and would notice were that to stop being true.
 *
 * Where an expectation here and the registry disagree, the registry is what
 * ships and this file is what is wrong about it, or the registry has a defect;
 * either way the disagreement is worth reporting rather than papering over, so
 * no check is weakened to make a run pass.
 */


/* the number of elements of an array whose size is known here */

#define ngx_http_status_test_nelts(a)   (sizeof(a) / sizeof((a)[0]))


/*
 * The rows the registry keeps for codes a module may register.  It is the
 * headroom the registry documents rather than the size of its table, which is
 * private to it: exhaustion is found by registering until a registration is
 * refused, and this is only how many must have been accepted by then.
 */

#define NGX_HTTP_STATUS_TEST_HEADROOM   15


/*
 * How many codes of the range the registry describes without a module having
 * registered any, and how many of those carry a phrase for a status line.  A
 * code with no phrase is emitted as three digits and a space, which is what
 * nginx sent for it before the registry existed.
 */

#define NGX_HTTP_STATUS_TEST_NBUILTIN   48
#define NGX_HTTP_STATUS_TEST_NPHRASE    36


/*
 * Records a check and, when it did not hold, reports it.  Each argument is
 * expanded once, so an argument with a side effect is admissible, unlike in
 * the registry's own macros.
 */

#define ngx_http_status_test_assert(rc, ok, what)                            \
    if (ngx_http_status_test_check(ok, what) != NGX_OK) {                    \
        rc = NGX_ERROR;                                                      \
    }

#define ngx_http_status_test_assert_code(rc, ok, what, code)                 \
    if (ngx_http_status_test_check_code(ok, what, code) != NGX_OK) {         \
        rc = NGX_ERROR;                                                      \
    }


typedef struct {
    ngx_uint_t   code;
    ngx_str_t    reason;
} ngx_http_status_test_reason_t;


typedef struct {
    ngx_uint_t   code;
    ngx_str_t    kept;                   /* the phrase nginx has always sent */
    ngx_str_t    rfc;                    /* the name RFC 9110 recommends */
} ngx_http_status_test_diverge_t;


typedef struct {
    ngx_uint_t   code;
    ngx_uint_t   row;                    /* row of the error page table */
} ngx_http_status_test_page_t;


typedef struct {
    ngx_int_t  (*run)(void);
    const char  *name;
    ngx_uint_t   init;                   /* initialize the registry first */
} ngx_http_status_test_case_t;


static ngx_int_t ngx_http_status_test_check(ngx_uint_t ok, const char *what);
static ngx_int_t ngx_http_status_test_check_code(ngx_uint_t ok,
    const char *what, ngx_uint_t code);
static ngx_uint_t ngx_http_status_test_member(ngx_uint_t code, ngx_uint_t *set,
    ngx_uint_t n);
static ngx_uint_t ngx_http_status_test_described(void);
static ngx_http_request_t *ngx_http_status_test_req(void);
static void ngx_http_status_test_writer(ngx_log_t *log, ngx_uint_t level,
    u_char *buf, size_t len);
static void ngx_http_status_test_fill(ngx_http_status_def_t *def,
    ngx_uint_t code);

static ngx_int_t ngx_http_status_test_preinit(void);
static ngx_int_t ngx_http_status_test_signatures(void);
static ngx_int_t ngx_http_status_test_helper_signatures(void);
static ngx_int_t ngx_http_status_test_complete(void);
static ngx_int_t ngx_http_status_test_undescribed(void);
static ngx_int_t ngx_http_status_test_reasons(void);
static ngx_int_t ngx_http_status_test_empty_reasons(void);
static ngx_int_t ngx_http_status_test_divergence(void);
static ngx_int_t ngx_http_status_test_validate(void);
static ngx_int_t ngx_http_status_test_cacheable(void);
static ngx_int_t ngx_http_status_test_expires(void);
static ngx_int_t ngx_http_status_test_separation(void);
static ngx_int_t ngx_http_status_test_difference(void);
static ngx_int_t ngx_http_status_test_internal(void);
static ngx_int_t ngx_http_status_test_no_row(ngx_uint_t *set, ngx_uint_t n);
static ngx_int_t ngx_http_status_test_register(void);
static ngx_int_t ngx_http_status_test_register_refuses(void);
static ngx_int_t ngx_http_status_test_sealed(void);
static ngx_int_t ngx_http_status_test_copied(void);
static ngx_int_t ngx_http_status_test_exhaustion(void);
static ngx_int_t ngx_http_status_test_cycle(void);
static ngx_int_t ngx_http_status_test_idempotent(void);
static ngx_int_t ngx_http_status_test_ranges(void);
static ngx_int_t ngx_http_status_test_effective(void);
static ngx_int_t ngx_http_status_test_precedence(void);
static ngx_int_t ngx_http_status_test_set(void);
static ngx_int_t ngx_http_status_test_set_bounds(void);
static ngx_int_t ngx_http_status_test_promote(void);
static ngx_int_t ngx_http_status_test_relayed(void);
static ngx_int_t ngx_http_status_test_error_pages(void);
#if (NGX_HTTP_STATUS_VALIDATION)
static ngx_int_t ngx_http_status_test_report(void);
static ngx_int_t ngx_http_status_test_report_exempts(void);
static ngx_int_t ngx_http_status_test_set_reports(void);
#endif
static ngx_int_t ngx_http_status_test_run(void);


/*
 * Every code the registry describes: the union of the codes nginx defines a
 * constant for, the codes its status line table held a phrase for, the codes
 * its error page table held a body for, and the codes RFC 9110 defines.  The
 * four that nginx defines no constant for at all are written as numbers, and
 * so are read from the phrase each is expected to carry as well.
 */

static ngx_uint_t  ngx_http_status_test_codes[] = {

    /* informational */

    NGX_HTTP_CONTINUE, NGX_HTTP_SWITCHING_PROTOCOLS,
    NGX_HTTP_PROCESSING, NGX_HTTP_EARLY_HINTS,

    /* successful; 203 has no constant */

    NGX_HTTP_OK, NGX_HTTP_CREATED, NGX_HTTP_ACCEPTED, 203,
    NGX_HTTP_NO_CONTENT, NGX_HTTP_PARTIAL_CONTENT,

    /* redirection */

    NGX_HTTP_SPECIAL_RESPONSE, NGX_HTTP_MOVED_PERMANENTLY,
    NGX_HTTP_MOVED_TEMPORARILY, NGX_HTTP_SEE_OTHER,
    NGX_HTTP_NOT_MODIFIED, NGX_HTTP_TEMPORARY_REDIRECT,
    NGX_HTTP_PERMANENT_REDIRECT,

    /* client error; 402, 406 and 410 have no constant */

    NGX_HTTP_BAD_REQUEST, NGX_HTTP_UNAUTHORIZED, 402,
    NGX_HTTP_FORBIDDEN, NGX_HTTP_NOT_FOUND, NGX_HTTP_NOT_ALLOWED, 406,
    NGX_HTTP_REQUEST_TIME_OUT, NGX_HTTP_CONFLICT, 410,
    NGX_HTTP_LENGTH_REQUIRED, NGX_HTTP_PRECONDITION_FAILED,
    NGX_HTTP_REQUEST_ENTITY_TOO_LARGE, NGX_HTTP_REQUEST_URI_TOO_LARGE,
    NGX_HTTP_UNSUPPORTED_MEDIA_TYPE, NGX_HTTP_RANGE_NOT_SATISFIABLE,
    NGX_HTTP_MISDIRECTED_REQUEST, NGX_HTTP_TOO_MANY_REQUESTS,

    /* nginx's own codes, which have no standing outside nginx */

    NGX_HTTP_CLOSE, NGX_HTTP_NGINX_CODES, NGX_HTTPS_CERT_ERROR,
    NGX_HTTPS_NO_CERT, NGX_HTTP_TO_HTTPS, NGX_HTTP_CLIENT_CLOSED_REQUEST,

    /* server error */

    NGX_HTTP_INTERNAL_SERVER_ERROR, NGX_HTTP_NOT_IMPLEMENTED,
    NGX_HTTP_BAD_GATEWAY, NGX_HTTP_SERVICE_UNAVAILABLE,
    NGX_HTTP_GATEWAY_TIME_OUT, NGX_HTTP_VERSION_NOT_SUPPORTED,
    NGX_HTTP_INSUFFICIENT_STORAGE
};


/*
 * The phrase each code carries, in the fused form the registry holds: exactly
 * the bytes that follow "HTTP/1.1 " in a status line, so that a status line is
 * still emitted with a single copy.  These are the bytes nginx sent before the
 * registry existed and they are asserted byte for byte, the eight of them that
 * differ from the name RFC 9110 recommends included.
 */

static ngx_http_status_test_reason_t  ngx_http_status_test_phrases[] = {

    { NGX_HTTP_OK, ngx_string("200 OK") },
    { NGX_HTTP_CREATED, ngx_string("201 Created") },
    { NGX_HTTP_ACCEPTED, ngx_string("202 Accepted") },
    { NGX_HTTP_NO_CONTENT, ngx_string("204 No Content") },
    { NGX_HTTP_PARTIAL_CONTENT, ngx_string("206 Partial Content") },

    { NGX_HTTP_MOVED_PERMANENTLY, ngx_string("301 Moved Permanently") },
    { NGX_HTTP_MOVED_TEMPORARILY, ngx_string("302 Moved Temporarily") },
    { NGX_HTTP_SEE_OTHER, ngx_string("303 See Other") },
    { NGX_HTTP_NOT_MODIFIED, ngx_string("304 Not Modified") },
    { NGX_HTTP_TEMPORARY_REDIRECT, ngx_string("307 Temporary Redirect") },
    { NGX_HTTP_PERMANENT_REDIRECT, ngx_string("308 Permanent Redirect") },

    { NGX_HTTP_BAD_REQUEST, ngx_string("400 Bad Request") },
    { NGX_HTTP_UNAUTHORIZED, ngx_string("401 Unauthorized") },
    { 402, ngx_string("402 Payment Required") },
    { NGX_HTTP_FORBIDDEN, ngx_string("403 Forbidden") },
    { NGX_HTTP_NOT_FOUND, ngx_string("404 Not Found") },
    { NGX_HTTP_NOT_ALLOWED, ngx_string("405 Not Allowed") },
    { 406, ngx_string("406 Not Acceptable") },
    { NGX_HTTP_REQUEST_TIME_OUT, ngx_string("408 Request Time-out") },
    { NGX_HTTP_CONFLICT, ngx_string("409 Conflict") },
    { 410, ngx_string("410 Gone") },
    { NGX_HTTP_LENGTH_REQUIRED, ngx_string("411 Length Required") },
    { NGX_HTTP_PRECONDITION_FAILED,
      ngx_string("412 Precondition Failed") },
    { NGX_HTTP_REQUEST_ENTITY_TOO_LARGE,
      ngx_string("413 Request Entity Too Large") },
    { NGX_HTTP_REQUEST_URI_TOO_LARGE,
      ngx_string("414 Request-URI Too Large") },
    { NGX_HTTP_UNSUPPORTED_MEDIA_TYPE,
      ngx_string("415 Unsupported Media Type") },
    { NGX_HTTP_RANGE_NOT_SATISFIABLE,
      ngx_string("416 Requested Range Not Satisfiable") },
    { NGX_HTTP_MISDIRECTED_REQUEST,
      ngx_string("421 Misdirected Request") },
    { NGX_HTTP_TOO_MANY_REQUESTS, ngx_string("429 Too Many Requests") },

    { NGX_HTTP_INTERNAL_SERVER_ERROR,
      ngx_string("500 Internal Server Error") },
    { NGX_HTTP_NOT_IMPLEMENTED, ngx_string("501 Not Implemented") },
    { NGX_HTTP_BAD_GATEWAY, ngx_string("502 Bad Gateway") },
    { NGX_HTTP_SERVICE_UNAVAILABLE,
      ngx_string("503 Service Temporarily Unavailable") },
    { NGX_HTTP_GATEWAY_TIME_OUT, ngx_string("504 Gateway Time-out") },
    { NGX_HTTP_VERSION_NOT_SUPPORTED,
      ngx_string("505 HTTP Version Not Supported") },
    { NGX_HTTP_INSUFFICIENT_STORAGE,
      ngx_string("507 Insufficient Storage") }
};


/*
 * The codes the registry describes that carry no phrase, so that a response
 * with one of them is emitted as three digits and a space.  This is asserted
 * as deliberately as the phrases are: giving 100 a phrase would send
 * "HTTP/1.1 100 Continue" where nginx sends "HTTP/1.1 100 ", which is a
 * change to what is sent and so is not admissible.
 *
 * The separate "100 Continue" and "103 Early Hints" lines that nginx does send
 * come from constants of their own and are not driven by the registry.
 */

static ngx_uint_t  ngx_http_status_test_empty[] = {
    NGX_HTTP_CONTINUE, NGX_HTTP_SWITCHING_PROTOCOLS,
    NGX_HTTP_PROCESSING, NGX_HTTP_EARLY_HINTS,
    203,
    NGX_HTTP_SPECIAL_RESPONSE,
    NGX_HTTP_CLOSE, NGX_HTTP_NGINX_CODES, NGX_HTTPS_CERT_ERROR,
    NGX_HTTPS_NO_CERT, NGX_HTTP_TO_HTTPS, NGX_HTTP_CLIENT_CLOSED_REQUEST
};


/* what RFC 9110, section 15.1, permits a cache to store heuristically */

static ngx_uint_t  ngx_http_status_test_cacheable_set[] = {
    NGX_HTTP_OK, 203, NGX_HTTP_NO_CONTENT, NGX_HTTP_PARTIAL_CONTENT,
    NGX_HTTP_SPECIAL_RESPONSE, NGX_HTTP_MOVED_PERMANENTLY,
    NGX_HTTP_PERMANENT_REDIRECT, NGX_HTTP_NOT_FOUND, NGX_HTTP_NOT_ALLOWED,
    410, NGX_HTTP_REQUEST_URI_TOO_LARGE, NGX_HTTP_NOT_IMPLEMENTED
};


/*
 * The codes nginx's own "expires" processing is eligible for, which is a
 * different question from the one above and a different set of codes.  It was
 * read from the two switch statements of the headers filter, which listed
 * exactly these ten.
 */

static ngx_uint_t  ngx_http_status_test_expires_set[] = {
    NGX_HTTP_OK, NGX_HTTP_CREATED, NGX_HTTP_NO_CONTENT,
    NGX_HTTP_PARTIAL_CONTENT, NGX_HTTP_MOVED_PERMANENTLY,
    NGX_HTTP_MOVED_TEMPORARILY, NGX_HTTP_SEE_OTHER, NGX_HTTP_NOT_MODIFIED,
    NGX_HTTP_TEMPORARY_REDIRECT, NGX_HTTP_PERMANENT_REDIRECT
};


/* eligible for "expires" but not heuristically cacheable */

static ngx_uint_t  ngx_http_status_test_expires_only[] = {
    NGX_HTTP_CREATED, NGX_HTTP_MOVED_TEMPORARILY, NGX_HTTP_SEE_OTHER,
    NGX_HTTP_NOT_MODIFIED, NGX_HTTP_TEMPORARY_REDIRECT
};


/* heuristically cacheable but not eligible for "expires" */

static ngx_uint_t  ngx_http_status_test_cacheable_only[] = {
    203, NGX_HTTP_SPECIAL_RESPONSE, NGX_HTTP_NOT_FOUND,
    NGX_HTTP_NOT_ALLOWED, 410, NGX_HTTP_REQUEST_URI_TOO_LARGE,
    NGX_HTTP_NOT_IMPLEMENTED
};


/* nginx's own codes, which strict reporting must accept without complaint */

static ngx_uint_t  ngx_http_status_test_internal_set[] = {
    NGX_HTTP_CLOSE, NGX_HTTP_NGINX_CODES, NGX_HTTPS_CERT_ERROR,
    NGX_HTTPS_NO_CERT, NGX_HTTP_TO_HTTPS, NGX_HTTP_CLIENT_CLOSED_REQUEST
};


/*
 * Codes of the range that the registry does not describe.  Most held an empty
 * slot in the status line table the registry replaced, and 498 never had a
 * constant at all: it is recorded in a comment beside nginx's own codes and is
 * answered with the body of a 404, which is a row of the error page table and
 * not a row of the registry.
 */

static ngx_uint_t  ngx_http_status_test_absent[] = {
    205, 305, 306, 407, 417, 418, 419, 420, 422, 423, 424, 425, 426, 427,
    428, 498, 506, 509, 510, 550, 599
};


/* codes outside the range the registry covers at all */

static ngx_uint_t  ngx_http_status_test_outside[] = {
    0, 1, 99, 600, 601, 999, 1000, 65535
};


/*
 * The eight phrases that differ from the name RFC 9110 recommends.  A reason
 * phrase is a recommendation only, which is what lets the registry be both
 * correct by that specification and byte compatible with what nginx sends: the
 * phrase carries nginx's bytes and the name from the specification is recorded
 * in the section reference beside it.  Both forms are named here so that a
 * change of one of them into the other fails loudly rather than quietly.
 */

static ngx_http_status_test_diverge_t  ngx_http_status_test_divergent[] = {

    { NGX_HTTP_MOVED_TEMPORARILY, ngx_string("302 Moved Temporarily"),
      ngx_string("302 Found") },
    { NGX_HTTP_NOT_ALLOWED, ngx_string("405 Not Allowed"),
      ngx_string("405 Method Not Allowed") },
    { NGX_HTTP_REQUEST_TIME_OUT, ngx_string("408 Request Time-out"),
      ngx_string("408 Request Timeout") },
    { NGX_HTTP_REQUEST_ENTITY_TOO_LARGE,
      ngx_string("413 Request Entity Too Large"),
      ngx_string("413 Content Too Large") },
    { NGX_HTTP_REQUEST_URI_TOO_LARGE,
      ngx_string("414 Request-URI Too Large"),
      ngx_string("414 URI Too Long") },
    { NGX_HTTP_RANGE_NOT_SATISFIABLE,
      ngx_string("416 Requested Range Not Satisfiable"),
      ngx_string("416 Range Not Satisfiable") },
    { NGX_HTTP_SERVICE_UNAVAILABLE,
      ngx_string("503 Service Temporarily Unavailable"),
      ngx_string("503 Service Unavailable") },
    { NGX_HTTP_GATEWAY_TIME_OUT, ngx_string("504 Gateway Time-out"),
      ngx_string("504 Gateway Timeout") }
};


/*
 * The row of the error page table a code selects.  The rows follow the shape
 * of that table and not the membership of the registry: a code the registry
 * does not describe still selects a row when it falls within one of the spans
 * of consecutive codes the table holds, which is how 498 is answered with the
 * body of a 404, and a code no span covers selects the row of zero length.
 */

static ngx_http_status_test_page_t  ngx_http_status_test_pages[] = {

    { 100, 0 }, { 200, 0 }, { 201, 0 }, { 204, 0 }, { 206, 0 }, { 300, 0 },

    { 301, 1 }, { 302, 2 }, { 303, 3 }, { 304, 4 }, { 305, 5 }, { 306, 6 },
    { 307, 7 }, { 308, 8 },

    { 309, 0 }, { 399, 0 },

    { 400, 9 }, { 404, 13 }, { 407, 16 }, { 416, 25 }, { 429, 38 },

    { 430, 0 }, { 444, 0 }, { 493, 0 },

    { 494, 39 }, { 495, 40 }, { 496, 41 }, { 497, 42 }, { 498, 43 },
    { 499, 44 }, { 500, 45 }, { 501, 46 }, { 502, 47 }, { 503, 48 },
    { 504, 49 }, { 505, 50 }, { 506, 51 }, { 507, 52 },

    { 508, 0 }, { 599, 0 }, { 0, 0 }, { 1000, 0 }
};


/* the bounds of the range the registry covers, and one code either side */

static ngx_uint_t  ngx_http_status_test_range_probe[] = {
    0, 1, 99, 100, 101, 300, 598, 599, 600, 601, 1000
};

static ngx_uint_t  ngx_http_status_test_range_want[] = {
    0, 0,  0,   1,   1,   1,   1,   1,   0,   0,    0
};


/*
 * The bound on what can be written into the three bytes an HTTP/2 or an
 * HTTP/3 response reserves for its status, which is a wider range than the one
 * the registry describes.
 */

static ngx_uint_t  ngx_http_status_test_wire_probe[] = {
    0, 100, 200, 599, 600, 998, 999, 1000, 1001, 10000
};

static ngx_uint_t  ngx_http_status_test_wire_want[] = {
    1,   1,   1,   1,   1,   1,   1,    0,    0,     0
};


/*
 * Codes of the range that the registry does not describe, used to fill its
 * headroom until a registration is refused.  There are more of them than the
 * headroom holds, so that the refusal is observed rather than assumed.
 */

static ngx_uint_t  ngx_http_status_test_spare[] = {
    205, 305, 306, 407, 417, 418, 419, 420, 422, 423, 424, 425, 426, 427,
    428, 498, 506, 509, 510, 550, 551, 552, 553, 554, 555
};


static ngx_uint_t  ngx_http_status_test_checks;
static ngx_uint_t  ngx_http_status_test_errors;
static ngx_uint_t  ngx_http_status_test_alerts;


/*
 * A zero initialized instance of the request, of the connection it is answered
 * on, and of the log that connection reports through, each of them the type
 * nginx itself uses rather than anything standing in for one.  A request is
 * allocated from a pool and zeroed when nginx creates it, so a zeroed request
 * is a request in the state nginx starts one in, and the members read here are
 * the three the effective status is chosen from and the log an alert is
 * reported through.
 *
 * The log is wired to the standard error output and given a writer, which is
 * the hook nginx's own logging offers and which is used here to count the
 * alerts a check produced, so that reporting is observed and not inferred.
 */

static ngx_http_request_t   ngx_http_status_test_r;
static ngx_connection_t     ngx_http_status_test_c;
static ngx_log_t            ngx_http_status_test_log;
static ngx_open_file_t      ngx_http_status_test_log_file;
static ngx_http_upstream_t  ngx_http_status_test_u;


static ngx_int_t
ngx_http_status_test_check(ngx_uint_t ok, const char *what)
{
    ngx_http_status_test_checks++;

    if (ok) {
        return NGX_OK;
    }

    ngx_http_status_test_errors++;

    ngx_log_stderr(0, "    failed: %s", what);

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_status_test_check_code(ngx_uint_t ok, const char *what,
    ngx_uint_t code)
{
    ngx_http_status_test_checks++;

    if (ok) {
        return NGX_OK;
    }

    ngx_http_status_test_errors++;

    ngx_log_stderr(0, "    failed: status %ui, %s", code, what);

    return NGX_ERROR;
}


static ngx_uint_t
ngx_http_status_test_member(ngx_uint_t code, ngx_uint_t *set, ngx_uint_t n)
{
    ngx_uint_t  i;

    for (i = 0; i < n; i++) {
        if (set[i] == code) {
            return 1;
        }
    }

    return 0;
}


/* how many codes of the whole range the registry describes */

static ngx_uint_t
ngx_http_status_test_described(void)
{
    ngx_uint_t  code, n;

    n = 0;

    for (code = NGX_HTTP_STATUS_MIN; code < NGX_HTTP_STATUS_MAX; code++) {

        if (ngx_http_status_validate(code) == NGX_OK) {
            n++;
        }
    }

    return n;
}


/* a request in the state nginx creates one in, on a connection that logs */

static ngx_http_request_t *
ngx_http_status_test_req(void)
{
    ngx_memzero(&ngx_http_status_test_r, sizeof(ngx_http_request_t));
    ngx_memzero(&ngx_http_status_test_c, sizeof(ngx_connection_t));
    ngx_memzero(&ngx_http_status_test_u, sizeof(ngx_http_upstream_t));
    ngx_memzero(&ngx_http_status_test_log, sizeof(ngx_log_t));
    ngx_memzero(&ngx_http_status_test_log_file, sizeof(ngx_open_file_t));

    ngx_http_status_test_log_file.fd = ngx_stderr;

    ngx_http_status_test_log.file = &ngx_http_status_test_log_file;
    ngx_http_status_test_log.log_level = NGX_LOG_ALERT;
    ngx_http_status_test_log.writer = ngx_http_status_test_writer;

    ngx_http_status_test_c.log = &ngx_http_status_test_log;
    ngx_http_status_test_r.connection = &ngx_http_status_test_c;

    ngx_http_status_test_alerts = 0;

    return &ngx_http_status_test_r;
}


static void
ngx_http_status_test_writer(ngx_log_t *log, ngx_uint_t level, u_char *buf,
    size_t len)
{
    if (level <= NGX_LOG_ALERT) {
        ngx_http_status_test_alerts++;
    }
}


/*
 * A definition of the shape a module would register, filled in completely.
 * The registry copies the definition and not the bytes its members point to,
 * so both of those point at storage that outlives any registration.
 */

static void
ngx_http_status_test_fill(ngx_http_status_def_t *def, ngx_uint_t code)
{
    def->code = code;
    def->reason.len = 0;
    def->reason.data = NULL;
    def->flags = 0;
    def->rfc_section = "registered by the registry test";
}


/*
 * Before the registry is initialized its index is empty, so it describes
 * nothing.  That is the state a process is in before it has parsed a
 * configuration, and it is asserted once, here, because every group that
 * follows runs with an initialized registry.
 */

static ngx_int_t
ngx_http_status_test_preinit(void)
{
    ngx_int_t   rc;
    ngx_uint_t  code, i, n;

    rc = NGX_OK;
    n = ngx_http_status_test_nelts(ngx_http_status_test_codes);

    ngx_http_status_test_assert(rc, ngx_http_status_test_described() == 0,
                                "the registry described a code before it was "
                                "initialized");

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_codes[i];

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_validate(code) == NGX_ERROR,
                          "validated before initialization", code);

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_reason(code) == NULL,
                          "carried a phrase before initialization", code);

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_is_cacheable(code) == 0,
                          "was cacheable before initialization", code);

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_expires_ok(code) == 0,
                          "was expires eligible before initialization", code);
    }

    /*
     * The one argument is the configuration being parsed, which the registry
     * does not read and does not allocate from, so a driver with no
     * configuration passes a null pointer rather than inventing one.
     */

    ngx_http_status_test_assert(rc, ngx_http_status_init(NULL) == NGX_OK,
                                "the registry failed to initialize");

    ngx_http_status_test_assert(rc,
                          ngx_http_status_test_described()
                          == NGX_HTTP_STATUS_TEST_NBUILTIN,
                          "initialization did not describe every code");

    return rc;
}


/*
 * The published prototypes, taken through pointers of the exact declared type.
 * A change to any of them, the constness of the phrase a code carries
 * included, stops this from compiling, which is the point: what other files
 * link against must not drift.
 */

static ngx_int_t
ngx_http_status_test_signatures(void)
{
    ngx_int_t              rc;
    const ngx_str_t       *reason;
    ngx_http_request_t    *r;
    ngx_http_status_def_t  def;

    ngx_int_t        (*set)(ngx_http_request_t *r, ngx_uint_t status);
    ngx_int_t        (*validate)(ngx_uint_t status);
    ngx_int_t        (*reg)(ngx_http_status_def_t *def);
    ngx_uint_t       (*cacheable)(ngx_uint_t status);
    const ngx_str_t *(*phrase)(ngx_uint_t status);

    rc = NGX_OK;

    set = ngx_http_status_set;
    validate = ngx_http_status_validate;
    phrase = ngx_http_status_reason;
    cacheable = ngx_http_status_is_cacheable;
    reg = ngx_http_status_register;

    r = ngx_http_status_test_req();
    reason = phrase(NGX_HTTP_OK);

    ngx_http_status_test_fill(&def, 205);

    ngx_http_status_test_assert(rc, set(r, NGX_HTTP_OK) == NGX_OK
                                    && r->headers_out.status == NGX_HTTP_OK
                                    && validate(NGX_HTTP_OK) == NGX_OK
                                    && cacheable(NGX_HTTP_OK) != 0
                                    && reason != NULL && reason->len != 0
                                    && reg(&def) == NGX_OK,
                                "a function published through <ngx_http.h> "
                                "answered wrongly through its pointer");

    return rc;
}


/* the same, for the five the registry's own header publishes */

static ngx_int_t
ngx_http_status_test_helper_signatures(void)
{
    ngx_int_t   rc;

    ngx_int_t  (*init)(ngx_conf_t *cf);
    void       (*seal)(void);
    ngx_uint_t (*effective)(ngx_http_request_t *r);
    ngx_uint_t (*expires)(ngx_uint_t status);
    ngx_uint_t (*page)(ngx_uint_t status);

    rc = NGX_OK;

    init = ngx_http_status_init;
    seal = ngx_http_status_seal;
    effective = ngx_http_status_effective;
    expires = ngx_http_status_expires_ok;
    page = ngx_http_status_error_page_index;

    seal();

    ngx_http_status_test_assert(rc, init(NULL) == NGX_OK
                                    && effective(ngx_http_status_test_req())
                                       == 0
                                    && expires(NGX_HTTP_OK) != 0
                                    && page(NGX_HTTP_MOVED_PERMANENTLY) == 1,
                                "a function the registry's own header "
                                "publishes answered wrongly through its "
                                "pointer");

    return rc;
}


/*
 * Every code the registry is expected to describe is described.  Seven of them
 * are absent from the enumeration of RFC 9110 that this work started from and
 * yet were already sent by nginx: 402, 406, 410, 411, 412, 421 and 507.  Were
 * any of those dropped, responses carrying them would fall back to three bare
 * digits and nothing would say so, which is why the whole set is asserted and
 * not a sample of it.
 */

static ngx_int_t
ngx_http_status_test_complete(void)
{
    ngx_int_t   rc;
    ngx_uint_t  code, i, n;

    rc = NGX_OK;
    n = ngx_http_status_test_nelts(ngx_http_status_test_codes);

    ngx_http_status_test_assert(rc, n == NGX_HTTP_STATUS_TEST_NBUILTIN,
                                "the driver does not name every code the "
                                "registry is expected to describe");

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_codes[i];

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_validate(code) == NGX_OK,
                          "is not described by the registry", code);

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_reason(code) != NULL,
                          "carries no phrase of any length", code);
    }

    ngx_http_status_test_assert(rc,
                          ngx_http_status_test_described() == n,
                          "the registry describes a code the driver does not "
                          "name, or names one it does not describe");

    return rc;
}


/* every answer the registry gives about a code it does not describe */

static ngx_int_t
ngx_http_status_test_no_row(ngx_uint_t *set, ngx_uint_t n)
{
    ngx_int_t   rc;
    ngx_uint_t  code, i;

    rc = NGX_OK;

    for (i = 0; i < n; i++) {
        code = set[i];

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_validate(code) == NGX_ERROR,
                          "is described although it must not be", code);

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_reason(code) == NULL,
                          "carries a phrase although it must not", code);

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_is_cacheable(code) == 0,
                          "is cacheable although it must not be", code);

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_expires_ok(code) == 0,
                          "is expires eligible although it must not be", code);
    }

    return rc;
}


/* codes the registry must not describe, inside its range and outside it */

static ngx_int_t
ngx_http_status_test_undescribed(void)
{
    ngx_int_t  rc;

    rc = NGX_OK;

    if (ngx_http_status_test_no_row(ngx_http_status_test_absent,
            ngx_http_status_test_nelts(ngx_http_status_test_absent))
        != NGX_OK)
    {
        rc = NGX_ERROR;
    }

    if (ngx_http_status_test_no_row(ngx_http_status_test_outside,
            ngx_http_status_test_nelts(ngx_http_status_test_outside))
        != NGX_OK)
    {
        rc = NGX_ERROR;
    }

    return rc;
}


/*
 * The phrase each code carries, byte for byte.  The length alone is not
 * enough, so the bytes are compared as well, and the three digits the fused
 * form starts with are read back and compared with the code the registry was
 * asked about, which is what would catch a phrase attached to the wrong row.
 */

static ngx_int_t
ngx_http_status_test_reasons(void)
{
    ngx_int_t                       rc;
    ngx_uint_t                      code, i, n;
    const ngx_str_t                *got;
    ngx_http_status_test_reason_t  *want;

    rc = NGX_OK;
    n = ngx_http_status_test_nelts(ngx_http_status_test_phrases);

    for (i = 0; i < n; i++) {
        want = &ngx_http_status_test_phrases[i];
        code = want->code;

        got = ngx_http_status_reason(code);

        ngx_http_status_test_assert_code(rc,
                          got != NULL && got->len == want->reason.len
                          && ngx_memcmp(got->data, want->reason.data,
                                        want->reason.len) == 0,
                          "does not carry the phrase nginx has always sent "
                          "for it", code);

        if (got == NULL) {
            continue;
        }

        /* the fused form starts with the code it belongs to */

        ngx_http_status_test_assert_code(rc,
                          got->len > 3 && got->data[3] == ' '
                          && (ngx_uint_t) ngx_atoi(got->data, 3) == code,
                          "carries a phrase fused to another code, or one "
                          "not in the fused form at all", code);

        /* the phrase is the registry's own and not the driver's copy */

        ngx_http_status_test_assert_code(rc,
                          got != &want->reason
                          && got == ngx_http_status_reason(code),
                          "answers with a phrase that is not the registry's "
                          "own, or with a different one each time", code);
    }

    return rc;
}


/*
 * The codes that carry no phrase carry an empty one and not none at all: the
 * registry answers with a length of zero for a code it describes and with
 * nothing for a code it does not, and the two are what a caller tells apart to
 * choose between copying a phrase and writing three digits.
 */

static ngx_int_t
ngx_http_status_test_empty_reasons(void)
{
    ngx_int_t         rc;
    ngx_uint_t        code, i, n;
    const ngx_str_t  *got;

    rc = NGX_OK;
    n = ngx_http_status_test_nelts(ngx_http_status_test_empty);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_empty[i];

        got = ngx_http_status_reason(code);

        ngx_http_status_test_assert_code(rc, got != NULL,
                          "is described and yet carries no phrase at all, so "
                          "a caller cannot tell it from an unknown code",
                          code);

        if (got == NULL) {
            continue;
        }

        ngx_http_status_test_assert_code(rc, got->len == 0,
                          "carries a phrase where nginx sends three digits "
                          "and a space", code);
    }

    ngx_http_status_test_assert(rc,
                          n == NGX_HTTP_STATUS_TEST_NBUILTIN
                               - NGX_HTTP_STATUS_TEST_NPHRASE
                          && ngx_http_status_test_nelts(
                                     ngx_http_status_test_phrases)
                             == NGX_HTTP_STATUS_TEST_NPHRASE,
                          "the codes with a phrase and those without do not "
                          "account for every code described");

    return rc;
}


/*
 * The eight phrases that differ from the name RFC 9110 recommends are the ones
 * nginx has always sent, and correcting one of them would change what is sent.
 * Both forms are compared so that such a correction fails here and says why.
 */

static ngx_int_t
ngx_http_status_test_divergence(void)
{
    ngx_int_t                        rc;
    ngx_uint_t                       code, i, n;
    const ngx_str_t                 *got;
    ngx_http_status_test_diverge_t  *want;

    rc = NGX_OK;
    n = ngx_http_status_test_nelts(ngx_http_status_test_divergent);

    for (i = 0; i < n; i++) {
        want = &ngx_http_status_test_divergent[i];
        code = want->code;

        got = ngx_http_status_reason(code);

        if (got == NULL) {
            ngx_http_status_test_assert_code(rc, 0,
                              "carries no phrase, so the phrase nginx has "
                              "always sent for it has been lost", code);
            continue;
        }

        ngx_http_status_test_assert_code(rc,
                          got->len == want->kept.len
                          && ngx_memcmp(got->data, want->kept.data,
                                        want->kept.len) == 0,
                          "no longer carries the phrase nginx has always "
                          "sent for it", code);

        ngx_http_status_test_assert_code(rc,
                          got->len != want->rfc.len
                          || ngx_memcmp(got->data, want->rfc.data,
                                        want->rfc.len) != 0,
                          "carries the name RFC 9110 recommends, which is a "
                          "change to what nginx sends", code);
    }

    return rc;
}


/* the presence test on its own, which needs no request and keeps no state */

static ngx_int_t
ngx_http_status_test_validate(void)
{
    ngx_int_t   rc;
    ngx_uint_t  code, i, n;

    rc = NGX_OK;

    n = ngx_http_status_test_nelts(ngx_http_status_test_codes);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_codes[i];

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_validate(code) == NGX_OK
                          && ngx_http_status_validate(code) == NGX_OK,
                          "did not validate, or did not validate twice "
                          "alike, which a stateless test must", code);
    }

    n = ngx_http_status_test_nelts(ngx_http_status_test_absent);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_absent[i];

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_validate(code) == NGX_ERROR,
                          "validated although the registry does not describe "
                          "it", code);
    }

    n = ngx_http_status_test_nelts(ngx_http_status_test_outside);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_outside[i];

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_validate(code) == NGX_ERROR,
                          "validated although it lies outside the range the "
                          "registry covers", code);
    }

    return rc;
}


/*
 * Heuristic cacheability, over the whole range rather than at a sample of it,
 * so that the answer is the set of codes and not merely a member of it: a code
 * that gained the flag would be found as surely as one that lost it.
 */

static ngx_int_t
ngx_http_status_test_cacheable(void)
{
    ngx_int_t   rc;
    ngx_uint_t  code, i, n, want;

    rc = NGX_OK;
    n = ngx_http_status_test_nelts(ngx_http_status_test_cacheable_set);

    ngx_http_status_test_assert(rc, n == 12,
                                "the driver does not name the twelve codes "
                                "RFC 9110 makes heuristically cacheable");

    for (code = NGX_HTTP_STATUS_MIN; code < NGX_HTTP_STATUS_MAX; code++) {

        want = ngx_http_status_test_member(code,
                                       ngx_http_status_test_cacheable_set, n);

        ngx_http_status_test_assert_code(rc,
                          (ngx_http_status_is_cacheable(code) != 0) == want,
                          want ? "is not cacheable although RFC 9110 makes "
                                 "it so"
                               : "is cacheable although RFC 9110 does not "
                                 "make it so", code);
    }

    n = ngx_http_status_test_nelts(ngx_http_status_test_outside);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_outside[i];

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_is_cacheable(code) == 0,
                          "is cacheable although it lies outside the range "
                          "the registry covers", code);
    }

    return rc;
}


/* the same, for the codes nginx's own "expires" processing applies to */

static ngx_int_t
ngx_http_status_test_expires(void)
{
    ngx_int_t   rc;
    ngx_uint_t  code, i, n, want;

    rc = NGX_OK;
    n = ngx_http_status_test_nelts(ngx_http_status_test_expires_set);

    ngx_http_status_test_assert(rc, n == 10,
                                "the driver does not name the ten codes the "
                                "headers filter held a case for");

    for (code = NGX_HTTP_STATUS_MIN; code < NGX_HTTP_STATUS_MAX; code++) {

        want = ngx_http_status_test_member(code,
                                         ngx_http_status_test_expires_set, n);

        ngx_http_status_test_assert_code(rc,
                          (ngx_http_status_expires_ok(code) != 0) == want,
                          want ? "is not expires eligible although the "
                                 "headers filter held a case for it"
                               : "is expires eligible although the headers "
                                 "filter held no case for it", code);
    }

    n = ngx_http_status_test_nelts(ngx_http_status_test_outside);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_outside[i];

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_expires_ok(code) == 0,
                          "is expires eligible although it lies outside the "
                          "range the registry covers", code);
    }

    return rc;
}


/*
 * The two sets above are different sets and neither contains the other, which
 * is why the registry carries two flags and not one.  Testing one where the
 * other is meant would change which responses receive an "Expires" header, so
 * the difference is asserted here rather than left to be noticed: five codes
 * are eligible for expires without being cacheable, being 201, 302, 303, 304
 * and 307; seven are cacheable without being eligible, being 203, 300, 404,
 * 405, 410, 414 and 501; and twelve codes therefore answer the two questions
 * differently, while only 200, 204, 206, 301 and 308 answer both alike.
 */

static ngx_int_t
ngx_http_status_test_separation(void)
{
    ngx_int_t   rc;
    ngx_uint_t  code, i, n;

    rc = NGX_OK;
    n = ngx_http_status_test_nelts(ngx_http_status_test_expires_only);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_expires_only[i];

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_expires_ok(code) != 0
                          && ngx_http_status_is_cacheable(code) == 0,
                          "must be expires eligible and not cacheable, so "
                          "the two sets have been conflated", code);
    }

    n = ngx_http_status_test_nelts(ngx_http_status_test_cacheable_only);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_cacheable_only[i];

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_is_cacheable(code) != 0
                          && ngx_http_status_expires_ok(code) == 0,
                          "must be cacheable and not expires eligible, so "
                          "the two sets have been conflated", code);
    }

    ngx_http_status_test_assert(rc,
                          n == 7
                          && ngx_http_status_test_nelts(
                                     ngx_http_status_test_expires_only) == 5,
                          "the driver does not name every code on which the "
                          "two sets differ");

    return rc;
}


/* and how many codes of the range answer the two questions differently */

static ngx_int_t
ngx_http_status_test_difference(void)
{
    ngx_int_t   rc;
    ngx_uint_t  agree, code, differ;

    rc = NGX_OK;

    agree = 0;
    differ = 0;

    for (code = NGX_HTTP_STATUS_MIN; code < NGX_HTTP_STATUS_MAX; code++) {

        if ((ngx_http_status_is_cacheable(code) != 0)
            != (ngx_http_status_expires_ok(code) != 0))
        {
            differ++;
            continue;
        }

        if (ngx_http_status_is_cacheable(code) != 0) {
            agree++;
        }
    }

    ngx_http_status_test_assert(rc, differ == 12,
                                "the cacheable and the expires eligible sets "
                                "no longer differ on twelve codes");

    ngx_http_status_test_assert(rc, agree == 5,
                                "the cacheable and the expires eligible sets "
                                "no longer share exactly five codes");

    return rc;
}


/*
 * nginx's own codes are described by the registry as first class members of
 * it, so that a build which reports a status the registry does not describe
 * says nothing about them: they are load bearing signals and not violations of
 * any specification.  The flags a row carries are not published, by design, so
 * what is asserted here is what follows from them.
 */

static ngx_int_t
ngx_http_status_test_internal(void)
{
    ngx_int_t                rc;
    ngx_uint_t               code, i, n;
    const ngx_str_t         *got;
    ngx_http_request_t      *r;

    rc = NGX_OK;
    n = ngx_http_status_test_nelts(ngx_http_status_test_internal_set);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_internal_set[i];

        got = ngx_http_status_reason(code);
        r = ngx_http_status_test_req();

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_validate(code) == NGX_OK,
                          "is one of nginx's own codes and yet is not "
                          "described by the registry", code);

        ngx_http_status_test_assert_code(rc,
                          got != NULL && got->len == 0,
                          "is one of nginx's own codes and so must carry an "
                          "empty phrase", code);

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_is_cacheable(code) == 0
                          && ngx_http_status_expires_ok(code) == 0,
                          "is one of nginx's own codes and so must be "
                          "neither cacheable nor expires eligible", code);

        /* setting one is accepted, and reported by no build */

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_set(r, code) == NGX_OK
                          && ngx_http_status_test_alerts == 0,
                          "is one of nginx's own codes and so must be set "
                          "without complaint", code);
    }

    return rc;
}


/*
 * Registration, and the order in which it refuses.  A null definition is
 * refused before anything else is examined, then a sealed registry, then a
 * code outside the range, then a code already described; exhaustion is left to
 * a group of its own because it takes the headroom to reach.  Each refusal is
 * asked for with a definition that is otherwise sound, so that the reason
 * asserted is the reason it was refused.
 */

static ngx_int_t
ngx_http_status_test_register(void)
{
    ngx_int_t              rc;
    ngx_http_status_def_t  def;

    rc = NGX_OK;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(NULL) == NGX_ERROR,
                          "a null definition was registered");

    /* a code the registry does not describe is accepted before sealing */

    ngx_http_status_test_fill(&def, 205);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(&def) == NGX_OK
                          && ngx_http_status_validate(205) == NGX_OK
                          && ngx_http_status_reason(205) != NULL,
                          "a sound definition was refused before sealing");

    /* a code already described is refused, whoever described it */

    ngx_http_status_test_fill(&def, 205);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(&def) == NGX_ERROR,
                          "a code a module had registered was registered "
                          "again");

    return rc;
}


/* the two reasons a definition is refused that every code can be asked of */

static ngx_int_t
ngx_http_status_test_register_refuses(void)
{
    ngx_int_t              rc;
    ngx_uint_t             code, i, n;
    ngx_http_status_def_t  def;

    rc = NGX_OK;

    /* a code the registry already describes is refused */

    n = ngx_http_status_test_nelts(ngx_http_status_test_codes);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_codes[i];

        ngx_http_status_test_fill(&def, code);

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_register(&def) == NGX_ERROR,
                          "was registered again although the registry "
                          "already describes it", code);
    }

    /* a code outside the range the registry covers is refused */

    n = ngx_http_status_test_nelts(ngx_http_status_test_outside);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_outside[i];

        ngx_http_status_test_fill(&def, code);

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_register(&def) == NGX_ERROR,
                          "was registered although it lies outside the range "
                          "the registry covers", code);

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_validate(code) == NGX_ERROR,
                          "is described although its registration was "
                          "refused", code);
    }

    return rc;
}


/*
 * Sealing, which happens once every module has had its chance to register and
 * before any worker forks, refuses every later registration unconditionally: a
 * worker inherits a registry it only reads.
 */

static ngx_int_t
ngx_http_status_test_sealed(void)
{
    ngx_int_t              rc;
    ngx_http_status_def_t  def;

    rc = NGX_OK;

    ngx_http_status_test_fill(&def, 305);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(&def) == NGX_OK,
                          "a sound definition was refused before sealing");

    ngx_http_status_seal();

    ngx_http_status_test_fill(&def, 306);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(&def) == NGX_ERROR
                          && ngx_http_status_validate(306) == NGX_ERROR,
                          "a definition was registered after sealing");

    /* sealing twice is not a way through it */

    ngx_http_status_seal();

    ngx_http_status_test_fill(&def, 407);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(&def) == NGX_ERROR
                          && ngx_http_status_register(NULL) == NGX_ERROR,
                          "a definition was registered after sealing twice");

    /* what was registered before sealing is still described */

    ngx_http_status_test_assert(rc,
                          ngx_http_status_validate(305) == NGX_OK,
                          "sealing discarded what had been registered");

    return rc;
}


/*
 * The registry copies a definition rather than keeping the caller's, so that a
 * caller may pass one from its stack.  The copy duplicates the string
 * descriptor and not the bytes it points at, so the bytes are a literal here
 * and what is altered afterwards to prove the copy is the descriptor.
 */

static ngx_int_t
ngx_http_status_test_copied(void)
{
    ngx_int_t               rc;
    const ngx_str_t        *got;
    ngx_http_status_def_t   def;

    rc = NGX_OK;

    def.code = 205;
    def.reason.len = sizeof("205 Reset Content") - 1;
    def.reason.data = (u_char *) "205 Reset Content";
    def.flags = NGX_HTTP_STATUS_CACHEABLE;
    def.rfc_section = "RFC 9110 section 15.3.6";

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(&def) == NGX_OK,
                          "a sound definition was refused before sealing");

    /* the caller's definition is now worthless, and the registry's is not */

    def.code = 599;
    def.reason.len = 0;
    def.flags = 0;
    def.rfc_section = NULL;

    got = ngx_http_status_reason(205);

    ngx_http_status_test_assert(rc,
                          got != NULL
                          && got->len == sizeof("205 Reset Content") - 1
                          && ngx_memcmp(got->data, "205 Reset Content",
                                        got->len) == 0,
                          "the registry kept the caller's definition rather "
                          "than a copy of it");

    ngx_http_status_test_assert(rc,
                          ngx_http_status_is_cacheable(205) != 0
                          && ngx_http_status_validate(205) == NGX_OK
                          && ngx_http_status_validate(599) == NGX_ERROR,
                          "altering the caller's definition altered the "
                          "registry");

    return rc;
}


/*
 * Exhaustion.  How many rows the registry keeps for registrations is private
 * to it, so the limit is found by registering until a registration is refused
 * rather than by naming it, and what is asserted is that the refusal came
 * after at least the headroom the registry documents was used.
 */

static ngx_int_t
ngx_http_status_test_exhaustion(void)
{
    ngx_int_t               rc;
    ngx_uint_t              accepted, code, i, n, refused;
    ngx_http_status_def_t   def;

    rc = NGX_OK;

    accepted = 0;
    code = 0;
    refused = 0;
    n = ngx_http_status_test_nelts(ngx_http_status_test_spare);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_spare[i];

        ngx_http_status_test_fill(&def, code);

        if (ngx_http_status_register(&def) == NGX_OK) {
            accepted++;
            continue;
        }

        refused = 1;
        break;
    }

    ngx_http_status_test_assert(rc, refused,
                                "the registry accepted every registration "
                                "the driver could offer, so exhaustion was "
                                "never reached");

    ngx_http_status_test_assert(rc,
                          accepted >= NGX_HTTP_STATUS_TEST_HEADROOM,
                          "the registry refused a registration before the "
                          "headroom it documents had been used");

    if (refused) {
        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_validate(code) == NGX_ERROR,
                          "was described although its registration had been "
                          "refused", code);
    }

    ngx_log_stderr(0, "    note: %ui registrations were accepted before one "
                      "was refused", accepted);

    return rc;
}


/*
 * Initialization runs again for every configuration that is parsed, which is
 * on every reload and on every configuration test, so it must leave the same
 * registry every time: unsealed, describing every built-in code and no other,
 * with whatever a module registered for the configuration before discarded.
 * The cycle is run more than once because a second run of it is the case a
 * reload is, and the state after each is compared with the state after the
 * first rather than merely inspected.
 */

static ngx_int_t
ngx_http_status_test_cycle(void)
{
    ngx_int_t              rc;
    ngx_http_status_def_t  def;

    rc = NGX_OK;

    ngx_http_status_test_assert(rc, ngx_http_status_init(NULL) == NGX_OK,
                                "the registry failed to initialize again");

    ngx_http_status_test_assert(rc,
                          ngx_http_status_test_described()
                          == NGX_HTTP_STATUS_TEST_NBUILTIN
                          && ngx_http_status_validate(205) == NGX_ERROR
                          && ngx_http_status_validate(305) == NGX_ERROR,
                          "initialization did not describe exactly the "
                          "built-in codes, or kept an earlier registration");

    /* the registry is unsealed again, so a module may register */

    ngx_http_status_test_fill(&def, 205);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(&def) == NGX_OK
                          && ngx_http_status_test_described()
                             == NGX_HTTP_STATUS_TEST_NBUILTIN + 1,
                          "the registry was not writable again after "
                          "initialization");

    ngx_http_status_seal();

    ngx_http_status_test_fill(&def, 305);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(&def) == NGX_ERROR,
                          "the registry was writable after sealing");

    return rc;
}


static ngx_int_t
ngx_http_status_test_idempotent(void)
{
    ngx_int_t   rc;
    ngx_uint_t  i;

    rc = NGX_OK;

    for (i = 0; i < 3; i++) {

        if (ngx_http_status_test_cycle() != NGX_OK) {
            rc = NGX_ERROR;
        }
    }

    /* and one initialization more leaves the built-in codes alone again */

    ngx_http_status_test_assert(rc,
                          ngx_http_status_init(NULL) == NGX_OK
                          && ngx_http_status_test_described()
                             == NGX_HTTP_STATUS_TEST_NBUILTIN
                          && ngx_http_status_validate(205) == NGX_ERROR
                          && ngx_http_status_validate(305) == NGX_ERROR,
                          "the registry did not return to its built-in state");

    return rc;
}


/*
 * The two range macros.  Each is a parameterized macro and not a function,
 * because the sources are ANSI C, and each expands its argument more than
 * once, so a variable is passed and never an expression with a side effect.
 */

static ngx_int_t
ngx_http_status_test_ranges(void)
{
    ngx_int_t   rc;
    ngx_uint_t  code, i, n;

    rc = NGX_OK;
    n = ngx_http_status_test_nelts(ngx_http_status_test_range_probe);

    ngx_http_status_test_assert(rc,
                          n == ngx_http_status_test_nelts(
                                        ngx_http_status_test_range_want),
                          "the range probes and the answers expected of them "
                          "are not of the same number");

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_range_probe[i];

        ngx_http_status_test_assert_code(rc,
                          (ngx_http_status_in_range(code) ? 1 : 0)
                          == ngx_http_status_test_range_want[i],
                          "is not answered for as expected by the range "
                          "macro, whose upper bound is exclusive", code);
    }

    n = ngx_http_status_test_nelts(ngx_http_status_test_wire_probe);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_wire_probe[i];

        ngx_http_status_test_assert_code(rc,
                          (ngx_http_status_wire_width_ok(code) ? 1 : 0)
                          == ngx_http_status_test_wire_want[i],
                          "is not answered for as expected by the macro that "
                          "bounds what a response can carry", code);
    }

    ngx_http_status_test_assert(rc,
                          NGX_HTTP_STATUS_MAX - NGX_HTTP_STATUS_MIN
                          == NGX_HTTP_STATUS_RANGE,
                          "the range the registry covers and the constant "
                          "that sizes its index disagree");

    return rc;
}


/*
 * The status a request is reported as having, which the access log and the
 * "status" variable both read.  An error status wins over a response status,
 * because it is chosen once the response has been replaced by an error, and a
 * request answered before any status was chosen is reported as 9 when it spoke
 * HTTP/0.9 and as 0 otherwise.
 *
 * That 9 is worth a word.  The constant naming HTTP/0.9 is itself 9, so the
 * version and the value reported for it coincide by accident, and a version
 * returned in place of a status would pass unnoticed for that one case.  So
 * HTTP/1.0, whose constant is 1000, is asked about as well: a request that
 * spoke it and was answered before a status was chosen is reported as 0, which
 * a version returned in place of a status could not manage.  The three digit
 * formatting that turns 9 into "009" belongs to the callers, not here.
 */

static ngx_int_t
ngx_http_status_test_effective(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();

    ngx_http_status_test_assert(rc, ngx_http_status_effective(r) == 0,
                                "a request with no status of any kind is not "
                                "reported as 0");

    r = ngx_http_status_test_req();
    r->http_version = NGX_HTTP_VERSION_9;

    ngx_http_status_test_assert(rc, ngx_http_status_effective(r) == 9,
                                "a request that spoke HTTP/0.9 and has no "
                                "status is not reported as 9");

    r = ngx_http_status_test_req();
    r->http_version = NGX_HTTP_VERSION_10;

    ngx_http_status_test_assert(rc, ngx_http_status_effective(r) == 0,
                                "a request that spoke HTTP/1.0 and has no "
                                "status is reported as its version");

    return rc;
}


/* and the order in which the three members it reads win over one another */

static ngx_int_t
ngx_http_status_test_precedence(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();
    r->headers_out.status = NGX_HTTP_NOT_FOUND;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_effective(r) == NGX_HTTP_NOT_FOUND,
                          "a response status is not what a request is "
                          "reported as having");

    r = ngx_http_status_test_req();
    r->headers_out.status = NGX_HTTP_NOT_FOUND;
    r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_effective(r)
                          == NGX_HTTP_INTERNAL_SERVER_ERROR,
                          "an error status does not win over a response "
                          "status");

    r = ngx_http_status_test_req();
    r->http_version = NGX_HTTP_VERSION_9;
    r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_effective(r)
                          == NGX_HTTP_INTERNAL_SERVER_ERROR,
                          "an error status does not win over the version a "
                          "request spoke");

    r = ngx_http_status_test_req();
    r->http_version = NGX_HTTP_VERSION_9;
    r->headers_out.status = NGX_HTTP_NOT_FOUND;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_effective(r) == NGX_HTTP_NOT_FOUND,
                          "a response status does not win over the version a "
                          "request spoke");

    return rc;
}


/*
 * The one entry point for a status nginx chose itself.  It writes the response
 * status and the bit recording that one was chosen, and writes nothing else:
 * not the status line, which a caller supplies verbatim or leaves empty, and
 * not the error status, which is a different status for a different purpose.
 *
 * A status too wide for the three bytes an HTTP/2 or an HTTP/3 response
 * reserves cannot be sent at all, so it is refused in every build and reported
 * as an alert.  A status the registry does not describe is by contrast
 * perfectly sendable and is refused by no build, which is what keeps a
 * response the same whether or not the build was configured to look for one.
 */

static ngx_int_t
ngx_http_status_test_set(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, NGX_HTTP_OK) == NGX_OK
                          && r->headers_out.status == NGX_HTTP_OK
                          && r->status_final == 1,
                          "setting a described status did not record it");

    ngx_http_status_test_assert(rc,
                          r->headers_out.status_line.len == 0
                          && r->headers_out.status_line.data == NULL
                          && r->err_status == 0 && r->upstream == NULL
                          && ngx_http_status_test_alerts == 0,
                          "setting a status wrote more than the response "
                          "status and the bit recording it");

    /* a status is set again once the response has been decided */

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r,
                              NGX_HTTP_CLIENT_CLOSED_REQUEST) == NGX_OK
                          && r->headers_out.status
                             == NGX_HTTP_CLIENT_CLOSED_REQUEST
                          && r->status_final == 1,
                          "setting a status a second time was refused, which "
                          "the access log depends on being allowed");

    return rc;
}


/*
 * The bound the setter holds every status to, whoever chose it.  A status too
 * wide for the three bytes an HTTP/2 or an HTTP/3 response reserves for it
 * cannot be sent at all, because the width in a three digit conversion is a
 * minimum and never a limit, so it is refused in every build and reported as
 * an alert.  A status the registry merely does not describe is by contrast
 * perfectly sendable, and is refused by no build: were it refused, a response
 * would depend on a build option, which is the one thing this must not do.
 */

static ngx_int_t
ngx_http_status_test_set_bounds(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, NGX_HTTP_STATUS_WIRE_MAX - 1)
                          == NGX_OK
                          && r->headers_out.status
                             == NGX_HTTP_STATUS_WIRE_MAX - 1
                          && r->status_final == 1,
                          "a sendable status the registry does not describe "
                          "was refused, so a response depends on a build");

    /* a status too wide to be sent is refused, and stored all the same */

    r = ngx_http_status_test_req();

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, NGX_HTTP_STATUS_WIRE_MAX)
                          == NGX_ERROR
                          && r->headers_out.status == NGX_HTTP_STATUS_WIRE_MAX
                          && r->status_final == 1
                          && ngx_http_status_test_alerts == 1,
                          "a status too wide to be sent was accepted, not "
                          "stored, or not reported exactly once");

    r = ngx_http_status_test_req();

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 10 * NGX_HTTP_STATUS_WIRE_MAX)
                          == NGX_ERROR
                          && ngx_http_status_test_alerts == 1,
                          "a status of five digits was accepted, or was not "
                          "reported exactly once");

    return rc;
}


/*
 * Promotion, which moves a status that was already examined where it was
 * chosen into the response, and makes the same two stores the setter does
 * without examining or refusing anything.
 */

static ngx_int_t
ngx_http_status_test_promote(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();
    r->err_status = NGX_HTTP_BAD_GATEWAY;

    ngx_http_status_promote(r, r->err_status);

    ngx_http_status_test_assert(rc,
                          r->headers_out.status == NGX_HTTP_BAD_GATEWAY
                          && r->status_final == 1
                          && r->err_status == NGX_HTTP_BAD_GATEWAY
                          && ngx_http_status_test_alerts == 0,
                          "promoting an error status over a response status "
                          "did not make the stores the setter makes");

    /* promotion examines nothing, so it reports nothing either */

    r = ngx_http_status_test_req();

    ngx_http_status_promote(r, NGX_HTTP_STATUS_WIRE_MAX);

    ngx_http_status_test_assert(rc,
                          r->headers_out.status == NGX_HTTP_STATUS_WIRE_MAX
                          && ngx_http_status_test_alerts == 0,
                          "promoting a status examined it, which is the "
                          "setter's part and not promotion's");

    return rc;
}


/*
 * Whether a status is the one an upstream chose for the response being
 * relayed.  Such a status is exempt from being examined, because an upstream
 * may answer with a code nginx has never heard of, or with one below 100, and
 * nginx's part is to relay what it received.  The test is on the status and not
 * on the request alone: a request that has an upstream also carries statuses
 * that nginx or a configuration chose for it, and those are not exempt.
 */

static ngx_int_t
ngx_http_status_test_relayed(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();

    ngx_http_status_test_assert(rc,
                          !ngx_http_status_relayed(r, NGX_HTTP_OK),
                          "a request with no upstream at all was taken to be "
                          "relaying a status");

    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 599;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_relayed(r, 599),
                          "the status an upstream chose was not taken to be "
                          "the one being relayed");

    ngx_http_status_test_assert(rc,
                          !ngx_http_status_relayed(r, NGX_HTTP_BAD_GATEWAY)
                          && !ngx_http_status_relayed(r, 598),
                          "a status nginx chose for a request that has an "
                          "upstream was taken to be relayed");

    /* an upstream may answer below the range the registry covers */

    r->upstream->headers_in.status_n = 42;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_relayed(r, 42)
                          && ngx_http_status_validate(42) == NGX_ERROR,
                          "a status an upstream chose below 100 was not taken "
                          "to be relayed");

    return rc;
}


/*
 * The row of the error page table a status selects, which is status code
 * knowledge and so belongs to the registry rather than to the table's file.
 * Every row is accounted for once, and no status of any value selects a row
 * outside the table, which is what the row count is asserted against.
 */

static ngx_int_t
ngx_http_status_test_error_pages(void)
{
    ngx_int_t                     rc;
    ngx_uint_t                    bad, code, found, i, n;
    ngx_http_status_test_page_t  *want;

    rc = NGX_OK;
    n = ngx_http_status_test_nelts(ngx_http_status_test_pages);

    for (i = 0; i < n; i++) {
        want = &ngx_http_status_test_pages[i];

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_error_page_index(want->code)
                          == want->row,
                          "does not select the row of the error page table "
                          "that it did before the registry owned that "
                          "mapping", want->code);
    }

    /* no status of any value reaches past the end of that table */

    bad = 0;
    found = 0;

    for (code = 0; code < 2 * NGX_HTTP_STATUS_MAX; code++) {

        if (ngx_http_status_error_page_index(code)
            >= NGX_HTTP_STATUS_ERROR_PAGE_ROWS)
        {
            bad = code;
            found = 1;
            break;
        }
    }

    ngx_http_status_test_assert_code(rc, !found,
                          "selects a row past the end of the error page "
                          "table", bad);

    return rc;
}


#if (NGX_HTTP_STATUS_VALIDATION)

/*
 * Reporting exists only in a build configured with
 * --with-http_status_validation, so these checks are compiled into that build
 * alone.  A bare #if is what selects them, and not #ifdef: the macro is either
 * defined as 1 by auto/have or not defined at all, and an undefined macro is
 * zero in an #if.
 *
 * Of the two ways the reporting path could be covered, this is the first: a
 * real connection and a real log are wired to the request, and the log is given
 * a writer, which is the hook nginx's own logging offers, so that the alerts a
 * check produced are counted rather than inferred.  The path therefore runs in
 * full here rather than being reasoned about, and each of these checks emits
 * one alert as well, which is the reporting working and not a failure.
 *
 * What is asserted is the policy: a status the registry describes is not
 * reported, one it does not describe is, the status an upstream chose is
 * exempt, and a request is reported for at most once however many statuses are
 * chosen for it, which is what makes the number of these lines the number of
 * requests rather than the number of stores.
 */

static ngx_int_t
ngx_http_status_test_report(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();

    ngx_http_status_report(r, NGX_HTTP_OK);

    ngx_http_status_test_assert(rc, r->status_reported == 0
                                    && ngx_http_status_test_alerts == 0,
                                "a status the registry describes was "
                                "reported");

    r = ngx_http_status_test_req();

    ngx_http_status_report(r, 306);

    ngx_http_status_test_assert(rc, r->status_reported == 1
                                    && ngx_http_status_test_alerts == 1,
                                "a status the registry does not describe was "
                                "not reported exactly once");

    /* and the same request is not reported for again */

    ngx_http_status_report(r, 305);

    ngx_http_status_test_assert(rc, ngx_http_status_test_alerts == 1,
                                "a request was reported for more than once, "
                                "so these lines count stores and not "
                                "requests");

    return rc;
}


/* and the status an upstream chose, which is exempt from being reported */

static ngx_int_t
ngx_http_status_test_report_exempts(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();
    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 599;

    ngx_http_status_report(r, 599);

    ngx_http_status_test_assert(rc, r->status_reported == 0
                                    && ngx_http_status_test_alerts == 0,
                                "the status an upstream chose was reported, "
                                "which relaying a response must not be");

    /* including one below the range the registry covers */

    r = ngx_http_status_test_req();
    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 42;

    ngx_http_status_report(r, 42);

    ngx_http_status_test_assert(rc, r->status_reported == 0
                                    && ngx_http_status_test_alerts == 0,
                                "a status an upstream chose below 100 was "
                                "reported");

    /* a status nginx chose for that same request is not exempt */

    ngx_http_status_report(r, 306);

    ngx_http_status_test_assert(rc, r->status_reported == 1
                                    && ngx_http_status_test_alerts == 1,
                                "a status nginx chose for a request that has "
                                "an upstream was treated as relayed");

    return rc;
}


/*
 * The setter reports through that same policy, and reports rather than
 * refusing: the response carries the status that was chosen for it, so that it
 * is the response a build without the switch would send.
 */

static ngx_int_t
ngx_http_status_test_set_reports(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 306) == NGX_OK
                          && r->headers_out.status == 306
                          && r->status_reported == 1
                          && ngx_http_status_test_alerts == 1,
                          "setting a status the registry does not describe "
                          "was refused, or was not reported exactly once");

    r = ngx_http_status_test_req();

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, NGX_HTTP_NOT_FOUND) == NGX_OK
                          && r->status_reported == 0
                          && ngx_http_status_test_alerts == 0,
                          "setting a status the registry describes was "
                          "reported");

    /*
     * A status too wide to be sent is reported as being too wide and is not
     * reported a second time as being undescribed.
     */

    r = ngx_http_status_test_req();

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, NGX_HTTP_STATUS_WIRE_MAX)
                          == NGX_ERROR
                          && r->status_reported == 0
                          && ngx_http_status_test_alerts == 1,
                          "a status too wide to be sent was reported twice, "
                          "or was reported as merely undescribed");

    return rc;
}

#endif


static ngx_http_status_test_case_t  ngx_http_status_test_cases[] = {

    /* this one runs before anything initializes the registry */

    { ngx_http_status_test_preinit, "the state before initialization", 0 },

    { ngx_http_status_test_signatures, "the published prototypes", 1 },
    { ngx_http_status_test_helper_signatures,
      "the prototypes the registry's own header publishes", 1 },
    { ngx_http_status_test_complete, "every code that must be described", 1 },
    { ngx_http_status_test_undescribed,
      "every code that must not be described", 1 },
    { ngx_http_status_test_reasons, "the phrase each code carries", 1 },
    { ngx_http_status_test_empty_reasons, "the codes carrying no phrase", 1 },
    { ngx_http_status_test_divergence,
      "the phrases that differ from RFC 9110", 1 },
    { ngx_http_status_test_validate, "the presence test", 1 },
    { ngx_http_status_test_cacheable, "heuristic cacheability", 1 },
    { ngx_http_status_test_expires, "eligibility for expires", 1 },
    { ngx_http_status_test_separation, "those two sets being different", 1 },
    { ngx_http_status_test_difference,
      "how far those two sets differ", 1 },
    { ngx_http_status_test_internal, "nginx's own codes", 1 },
    { ngx_http_status_test_register, "registration", 1 },
    { ngx_http_status_test_register_refuses,
      "the registrations that are refused", 1 },
    { ngx_http_status_test_sealed, "sealing", 1 },
    { ngx_http_status_test_copied, "a definition being copied", 1 },
    { ngx_http_status_test_exhaustion, "the headroom running out", 1 },
    { ngx_http_status_test_idempotent, "initializing more than once", 1 },
    { ngx_http_status_test_ranges, "the range macros", 1 },
    { ngx_http_status_test_effective,
      "the status a request is reported as", 1 },
    { ngx_http_status_test_precedence,
      "which status is reported when there is more than one", 1 },
    { ngx_http_status_test_set, "setting a status", 1 },
    { ngx_http_status_test_set_bounds,
      "the bound on what a response can carry", 1 },
    { ngx_http_status_test_promote, "promoting a status", 1 },
    { ngx_http_status_test_relayed, "the status an upstream chose", 1 },
    { ngx_http_status_test_error_pages, "the error page a status selects", 1 },

#if (NGX_HTTP_STATUS_VALIDATION)
    { ngx_http_status_test_report, "reporting an undescribed status", 1 },
    { ngx_http_status_test_report_exempts,
      "the status an upstream chose being exempt from reporting", 1 },
    { ngx_http_status_test_set_reports, "the setter reporting", 1 },
#endif

    { NULL, NULL, 0 }
};


static ngx_int_t
ngx_http_status_test_run(void)
{
    ngx_int_t                     rc;
    ngx_uint_t                    groups, bad;
    ngx_http_status_test_case_t  *tc;

    bad = 0;
    groups = 0;
    rc = NGX_OK;

    for (tc = ngx_http_status_test_cases; tc->run; tc++) {
        groups++;

        if (tc->init && ngx_http_status_init(NULL) != NGX_OK) {
            ngx_log_stderr(0, "  FAILED  %s: the registry would not "
                              "initialize", tc->name);
            bad++;
            rc = NGX_ERROR;
            continue;
        }

        ngx_http_status_test_alerts = 0;

        if (tc->run() == NGX_OK) {
            ngx_log_stderr(0, "  passed  %s", tc->name);
            continue;
        }

        ngx_log_stderr(0, "  FAILED  %s", tc->name);
        bad++;
        rc = NGX_ERROR;
    }

    ngx_log_stderr(0, "%ui groups, %ui failed; %ui checks, %ui failed",
                   groups, bad, ngx_http_status_test_checks,
                   ngx_http_status_test_errors);

    return rc;
}


/*
 * What src/core/nginx.c defines besides the main() this file replaces.
 *
 * The makefile beside this file leaves out exactly one object, the object of
 * src/core/nginx.c, because that is the one object of the tree that defines
 * main().  It defines four other things as well: the core module, which is
 * the first entry of the generated ngx_modules[], and the three functions of
 * src/core/ngx_cycle.h that live with it, which the cycle, the process and
 * the upstream code call.  Leaving that object out therefore leaves those
 * four undefined, so they are defined here: replacing a main() means taking
 * over the rest of its file as well.
 *
 * The core module is one of no directives and no context callbacks, which is
 * all that is wanted of it: nothing here runs a cycle, so nothing reads
 * either, and only the address of the module is ever needed.  The three
 * functions belong to starting, reloading and binding worker processes, none
 * of which a run of these tests reaches, so each reports and stops rather
 * than answer with something a caller would go on to trust.
 */

static ngx_core_module_t  ngx_http_status_test_core_ctx = {
    ngx_string("core"),
    NULL,
    NULL
};


ngx_module_t  ngx_core_module = {
    NGX_MODULE_V1,
    &ngx_http_status_test_core_ctx,        /* module context */
    NULL,                                  /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


char **
ngx_set_environment(ngx_cycle_t *cycle, ngx_uint_t *last)
{
    ngx_log_stderr(0, "ngx_set_environment() was reached, which a run of "
                      "these tests never does");
    ngx_abort();

    return NULL;
}


ngx_pid_t
ngx_exec_new_binary(ngx_cycle_t *cycle, char *const *argv)
{
    ngx_log_stderr(0, "ngx_exec_new_binary() was reached, which a run of "
                      "these tests never does");
    ngx_abort();

    return NGX_INVALID_PID;
}


ngx_cpuset_t *
ngx_get_cpu_affinity(ngx_uint_t n)
{
    ngx_log_stderr(0, "ngx_get_cpu_affinity() was reached, which a run of "
                      "these tests never does");
    ngx_abort();

    return NULL;
}


int
main(void)
{
    /*
     * The cached times the logging reads are set up here, as nginx's own
     * main() does before anything can report: without them an alert would be
     * assembled from a timestamp of no length, which the logging then writes
     * a second copy of behind the start of its own buffer.
     */

    ngx_time_init();

    ngx_log_stderr(0, "the HTTP status code registry");

#if (NGX_HTTP_STATUS_VALIDATION)
    ngx_log_stderr(0, "built with --with-http_status_validation, so the "
                      "groups that report are included; the alerts those "
                      "groups emit are expected");
#else
    ngx_log_stderr(0, "built without --with-http_status_validation, so the "
                      "groups that report are compiled out");
#endif

    if (ngx_http_status_test_run() != NGX_OK) {
        ngx_log_stderr(0, "FAILED");
        return 1;
    }

    ngx_log_stderr(0, "passed");

    return 0;
}
