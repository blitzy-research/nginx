
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/*
 * A driver for the HTTP status code registry.  Of the five functions the
 * registry publishes through <ngx_http.h>, two write - one sets the status of
 * a response, the other adds a definition to the registry - and three answer
 * about a status code without changing anything.  Four more are declared in
 * ngx_http_status.h: two run the lifecycle, and two answer for the HTTP core
 * and the modules of it.  That header also holds the record a definition is
 * written as and the macros over a status code.  All nine, the macros of both
 * headers, and the lifecycle that leaves the registry writable only while a
 * configuration is parsed are exercised here.
 *
 *     make -f misc/status_test/GNUmakefile test
 *
 * That makefile links every object of the build but the one which defines
 * main(), and this file defines in its place the four other symbols that
 * object defines, so the registry examined here is the one that ships rather
 * than a copy of it.  The driver reports on the standard error output and
 * exits zero only when every check passed.
 *
 * The registry's own header is not included: <ngx_http.h> includes it, which
 * is how every other file in the tree reaches the registry, so this one
 * reaches it the same way and would notice were that to stop being true.
 *
 * Which row of the table of error pages a status selects is not asked of the
 * registry and so is not checked here: that mapping is held in the file which
 * sends those pages, beside the table it indexes.
 */


#define ngx_http_status_test_nelts(a)   (sizeof(a) / sizeof((a)[0]))


/*
 * How many definitions a module may register.  Exhaustion is found by
 * registering until a registration is refused, and this is how many must have
 * been accepted by then.
 */

#define NGX_HTTP_STATUS_TEST_HEADROOM   15


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
    ngx_str_t    kept;                   /* nginx wire phrase */
    ngx_str_t    rfc;                    /* RFC 9110 canonical name */
} ngx_http_status_test_diverge_t;


typedef struct {
    ngx_int_t  (*run)(void);
    const char  *name;
    ngx_uint_t   init;
} ngx_http_status_test_case_t;


static ngx_int_t ngx_http_status_test_check(ngx_uint_t ok, const char *what);
static ngx_int_t ngx_http_status_test_check_code(ngx_uint_t ok,
    const char *what, ngx_uint_t code);
static ngx_uint_t ngx_http_status_test_member(ngx_uint_t code, ngx_uint_t *set,
    ngx_uint_t n);
static ngx_uint_t ngx_http_status_test_described(void);
static ngx_str_t *ngx_http_status_test_phrase(ngx_uint_t code);
static ngx_http_request_t *ngx_http_status_test_req(void);
static void ngx_http_status_test_writer(ngx_log_t *log, ngx_uint_t level,
    u_char *buf, size_t len);
static void ngx_http_status_test_fill(ngx_http_status_def_t *def,
    ngx_uint_t code);

static ngx_int_t ngx_http_status_test_preinit(void);
static ngx_int_t ngx_http_status_test_signatures(void);
static ngx_int_t ngx_http_status_test_signatures_write(void);
static ngx_int_t ngx_http_status_test_helper_signatures(void);
static ngx_int_t ngx_http_status_test_metadata_signatures(void);
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
static ngx_uint_t ngx_http_status_test_fill_headroom(void);
static ngx_int_t ngx_http_status_test_refuses_order(void);
static ngx_int_t ngx_http_status_test_borrowed(void);
static ngx_int_t ngx_http_status_test_sealed(void);
static ngx_int_t ngx_http_status_test_copied(void);
static ngx_int_t ngx_http_status_test_copied_metadata(void);
static ngx_int_t ngx_http_status_test_exhaustion(void);
static ngx_int_t ngx_http_status_test_builtin(ngx_uint_t code);
static ngx_int_t ngx_http_status_test_builtins(void);
static ngx_int_t ngx_http_status_test_cycle(void);
static ngx_int_t ngx_http_status_test_idempotent(void);
static ngx_int_t ngx_http_status_test_headroom_reset(void);
static ngx_int_t ngx_http_status_test_ranges(void);
static ngx_int_t ngx_http_status_test_effective(void);
static ngx_int_t ngx_http_status_test_precedence(void);
static ngx_int_t ngx_http_status_test_set(void);
static ngx_int_t ngx_http_status_test_set_width(ngx_uint_t status,
    const char *what);
static ngx_int_t ngx_http_status_test_set_bounds(void);
static ngx_int_t ngx_http_status_test_set_undescribed(void);
static ngx_int_t ngx_http_status_test_promote(void);
static ngx_int_t ngx_http_status_test_promote_undescribed(void);
static ngx_int_t ngx_http_status_test_relayed(void);
#if (NGX_HTTP_STATUS_VALIDATION)
static void ngx_http_status_test_gate(ngx_http_request_t *r,
    ngx_uint_t status);
static void ngx_http_status_test_overwrite(ngx_http_request_t *r,
    ngx_uint_t status);
static ngx_int_t ngx_http_status_test_reported(ngx_uint_t status,
    const char *what);
static ngx_int_t ngx_http_status_test_report(void);
static ngx_int_t ngx_http_status_test_report_bytes(void);
static ngx_int_t ngx_http_status_test_report_exempts(void);
static ngx_int_t ngx_http_status_test_report_overwrites(void);
static ngx_int_t ngx_http_status_test_set_reports(void);
static ngx_int_t ngx_http_status_test_set_wide_reports(void);
static ngx_int_t ngx_http_status_test_set_relayed(void);
static ngx_int_t ngx_http_status_test_set_mixed(void);
static ngx_int_t ngx_http_status_test_gated_promotion(void);
#endif
static ngx_int_t ngx_http_status_test_run(void);


/*
 * Every code the registry describes.  Four of them have no constant of their
 * own, so they are written as numbers here, and are read from the phrase each
 * is expected to carry as well.
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
 * still emitted with a single copy.  They are asserted byte for byte, those
 * that differ from the RFC 9110 canonical name included.
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
 * with one of them is emitted as three digits and a space.  Giving 100 a
 * phrase would send "HTTP/1.1 100 Continue" where nginx sends "HTTP/1.1 100 ",
 * which is a change to what is sent.  The separate "100 Continue" and
 * "103 Early Hints" lines nginx does send come from constants of their own and
 * are not driven by the registry.
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
 * different question from the one above and a different set of codes.
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
 * Codes of the range that the registry does not describe.  498 has no constant
 * of its own: it is recorded in a comment beside nginx's own codes and is
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
 * The phrases that differ from the RFC 9110 canonical name.  A reason phrase
 * is a recommendation only, which is what lets the registry be both correct by
 * that specification and byte compatible with what nginx sends: the phrase
 * carries nginx's bytes and the canonical name is recorded in the section
 * reference beside it.  Both forms are named here so that a change of one of
 * them into the other fails loudly.
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


static ngx_uint_t  ngx_http_status_test_range_probe[] = {
    0, 1, 42, 99, 100, 101, 300, 598, 599, 600, 601, 1000, 10000
};

static ngx_uint_t  ngx_http_status_test_range_want[] = {
    0, 0,  0,  0,   1,   1,   1,   1,   1,   0,   0,    0,     0
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
 * What the last line written to the log said: the severity it was written at,
 * and the bytes of it.  Counting lines alone would pass a report raised at the
 * wrong severity or carrying the wrong status, so both are kept.  The severity
 * is seeded to NGX_LOG_DEBUG for every request, which no report is written at,
 * so a report that was never raised cannot pass for one that was.
 */

static ngx_uint_t  ngx_http_status_test_level = NGX_LOG_DEBUG;
static size_t      ngx_http_status_test_line_len;
static u_char      ngx_http_status_test_line[NGX_MAX_ERROR_STR];


/*
 * The fixture: a request, the connection it is answered on, and the log that
 * connection reports through, each of them the type nginx itself uses.  nginx
 * allocates a request from a pool and zeroes it, so a zeroed request is a
 * request in the state nginx starts one in.
 *
 * The log carries a writer, which is the hook nginx's own logging offers and
 * which nginx calls in place of writing to the log's own file, so that the
 * alerts a check produced are counted rather than inferred.
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


/*
 * The phrase a code is expected to carry, or nothing where the code is one of
 * those the registry describes without a phrase.  The two are told apart by the
 * caller, which is the distinction the registry itself draws.
 */

static ngx_str_t *
ngx_http_status_test_phrase(ngx_uint_t code)
{
    ngx_uint_t  i, n;

    n = ngx_http_status_test_nelts(ngx_http_status_test_phrases);

    for (i = 0; i < n; i++) {

        if (ngx_http_status_test_phrases[i].code == code) {
            return &ngx_http_status_test_phrases[i].reason;
        }
    }

    return NULL;
}


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
    ngx_http_status_test_level = NGX_LOG_DEBUG;
    ngx_http_status_test_line_len = 0;

    return &ngx_http_status_test_r;
}


static void
ngx_http_status_test_writer(ngx_log_t *log, ngx_uint_t level, u_char *buf,
    size_t len)
{
    if (level <= NGX_LOG_ALERT) {
        ngx_http_status_test_alerts++;
    }

    ngx_http_status_test_level = level;

    if (len > sizeof(ngx_http_status_test_line)) {
        len = sizeof(ngx_http_status_test_line);
    }

    ngx_memcpy(ngx_http_status_test_line, buf, len);
    ngx_http_status_test_line_len = len;
}


/*
 * A definition of the shape a module would register.  The registry copies the
 * definition and not the bytes its members point to, so a member that does
 * point at bytes must point at bytes that outlive the registration.
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
 * configuration.
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
 * link against must not drift.  Each function is then asked one question of its
 * own, so that a failure names the function that answered wrongly rather than
 * saying only that one of them did.
 *
 * These three answer about a status and change nothing.
 */

static ngx_int_t
ngx_http_status_test_signatures(void)
{
    ngx_int_t         rc;
    const ngx_str_t  *reason;

    ngx_int_t        (*validate)(ngx_uint_t status);
    ngx_uint_t       (*cacheable)(ngx_uint_t status);
    const ngx_str_t *(*phrase)(ngx_uint_t status);

    rc = NGX_OK;

    validate = ngx_http_status_validate;
    phrase = ngx_http_status_reason;
    cacheable = ngx_http_status_is_cacheable;

    ngx_http_status_test_assert(rc, validate(NGX_HTTP_OK) == NGX_OK,
                                "ngx_http_status_validate() did not answer "
                                "NGX_OK for 200 through its pointer");

    reason = phrase(NGX_HTTP_OK);

    ngx_http_status_test_assert(rc, reason != NULL && reason->len != 0,
                                "ngx_http_status_reason() did not answer the "
                                "phrase 200 carries through its pointer");

    ngx_http_status_test_assert(rc, cacheable(NGX_HTTP_OK) != 0,
                                "ngx_http_status_is_cacheable() did not answer "
                                "non-zero for 200 through its pointer");

    return rc;
}


/* and these two write: one into a request, one into the registry */

static ngx_int_t
ngx_http_status_test_signatures_write(void)
{
    ngx_int_t              rc;
    ngx_http_request_t    *r;
    ngx_http_status_def_t  def;

    ngx_int_t  (*set)(ngx_http_request_t *r, ngx_uint_t status);
    ngx_int_t  (*reg)(ngx_http_status_def_t *def);

    rc = NGX_OK;

    set = ngx_http_status_set;
    reg = ngx_http_status_register;

    r = ngx_http_status_test_req();

    ngx_http_status_test_assert(rc, set(r, NGX_HTTP_OK) == NGX_OK,
                                "ngx_http_status_set() did not answer NGX_OK "
                                "through its pointer");

    ngx_http_status_test_assert(rc, r->headers_out.status == NGX_HTTP_OK,
                                "ngx_http_status_set() did not store 200 in "
                                "the response through its pointer");

    ngx_http_status_test_fill(&def, 205);

    ngx_http_status_test_assert(rc, reg(&def) == NGX_OK,
                                "ngx_http_status_register() did not answer "
                                "NGX_OK for a sound definition through its "
                                "pointer");

    return rc;
}


/*
 * The same for three of the four helpers ngx_http_status.h declares beside the
 * published API: the two that run the lifecycle, and the one the log module and
 * the variable evaluator read an effective status through.
 */

static ngx_int_t
ngx_http_status_test_helper_signatures(void)
{
    ngx_int_t              rc;
    ngx_http_status_def_t  def;

    ngx_int_t  (*init)(ngx_conf_t *cf);
    void       (*seal)(void);
    ngx_uint_t (*effective)(ngx_http_request_t *r);

    rc = NGX_OK;

    init = ngx_http_status_init;
    seal = ngx_http_status_seal;
    effective = ngx_http_status_effective;

    seal();

    ngx_http_status_test_fill(&def, 205);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(&def) == NGX_ERROR,
                          "ngx_http_status_seal() did not refuse a later "
                          "registration through its pointer");

    ngx_http_status_test_assert(rc, init(NULL) == NGX_OK,
                                "ngx_http_status_init() did not answer NGX_OK "
                                "through its pointer");

    ngx_http_status_test_assert(rc, effective(ngx_http_status_test_req()) == 0,
                                "ngx_http_status_effective() did not answer 0 "
                                "for a request with no status through its "
                                "pointer");

    ngx_http_status_test_assert(rc, ngx_http_status_validate(306) == NGX_ERROR,
                                "ngx_http_status_init() did not leave the "
                                "built-in codes alone through its pointer");

    return rc;
}


static ngx_int_t
ngx_http_status_test_metadata_signatures(void)
{
    ngx_int_t  rc;

    ngx_uint_t  (*expires)(ngx_uint_t status);

    rc = NGX_OK;

    expires = ngx_http_status_expires_ok;

    ngx_http_status_test_assert(rc, expires(NGX_HTTP_OK) != 0,
                                "ngx_http_status_expires_ok() did not answer "
                                "non-zero for 200 through its pointer");

    ngx_http_status_test_assert(rc, expires(NGX_HTTP_NOT_FOUND) == 0,
                                "ngx_http_status_expires_ok() did not answer "
                                "zero for 404 through its pointer");

    return rc;
}


/*
 * Every code the registry is expected to describe is described.  Were one
 * dropped, responses carrying it would fall back to three bare digits and
 * nothing would say so, which is why the whole set is asserted and not a
 * sample of it.
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

        ngx_http_status_test_assert_code(rc,
                          got->len > 3 && got->data[3] == ' '
                          && (ngx_uint_t) ngx_atoi(got->data, 3) == code,
                          "carries a phrase fused to another code, or one "
                          "not in the fused form at all", code);

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
 * Where a phrase differs from the name RFC 9110 recommends, the bytes nginx
 * sends are the phrase and not the name, so replacing one would change what is
 * sent.  Both forms are compared, so that such a replacement fails here and
 * says why.
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
 * the difference is asserted here rather than left to be noticed.
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
 * nginx's own codes are described by the registry as first-class members of
 * it, so that a build which reports a status the registry does not describe
 * says nothing about them: they are load-bearing signals and not violations of
 * any specification.  Which rows are flagged as nginx's own is checked against
 * the table the registry is built from, by the "metadata" target of the
 * makefile beside this file, the registry publishing no function that yields a
 * row's flags.
 */

static ngx_int_t
ngx_http_status_test_internal(void)
{
    ngx_int_t            rc;
    ngx_uint_t           code, i, n;
    const ngx_str_t     *got;
    ngx_http_request_t  *r;

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

    ngx_http_status_test_fill(&def, 205);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(&def) == NGX_OK
                          && ngx_http_status_validate(205) == NGX_OK
                          && ngx_http_status_reason(205) != NULL,
                          "a sound definition was refused before sealing");

    ngx_http_status_test_fill(&def, 205);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(&def) == NGX_ERROR,
                          "a code a module had registered was registered "
                          "again");

    return rc;
}


static ngx_int_t
ngx_http_status_test_register_refuses(void)
{
    ngx_int_t              rc;
    ngx_uint_t             code, i, n;
    ngx_http_status_def_t  def;

    rc = NGX_OK;

    n = ngx_http_status_test_nelts(ngx_http_status_test_codes);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_codes[i];

        ngx_http_status_test_fill(&def, code);

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_register(&def) == NGX_ERROR,
                          "was registered again although the registry "
                          "already describes it", code);
    }

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
 * Registers spare codes until the registry has no row left, and answers how
 * many were accepted, so that a group may ask for a full registry without
 * writing the loop again.  A code the registry already describes is passed
 * over rather than offered: it would be refused for being described, which is
 * not the refusal being asked for here.
 */

static ngx_uint_t
ngx_http_status_test_fill_headroom(void)
{
    ngx_uint_t             accepted, code, i, n;
    ngx_http_status_def_t  def;

    accepted = 0;
    n = ngx_http_status_test_nelts(ngx_http_status_test_spare);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_spare[i];

        if (ngx_http_status_validate(code) == NGX_OK) {
            continue;
        }

        ngx_http_status_test_fill(&def, code);

        if (ngx_http_status_register(&def) != NGX_OK) {
            break;
        }

        accepted++;
    }

    return accepted;
}


/*
 * The order the refusals are decided in.  A registration is refused for more
 * than one reason, and asking only whether each reason refuses would pass an
 * implementation that decided them in another order, or one that had already
 * altered the registry by the time it refused.  So each refusal is asked for in
 * a state that only the right order answers, and each is followed by a positive
 * control, so that a refusal which refuses everything cannot pass.
 */

static ngx_int_t
ngx_http_status_test_refuses_order(void)
{
    ngx_int_t              rc;
    ngx_http_status_def_t  def;

    rc = NGX_OK;

    ngx_http_status_test_fill(&def, 306);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(NULL) == NGX_ERROR
                          && ngx_http_status_register(&def) == NGX_OK
                          && ngx_http_status_validate(306) == NGX_OK,
                          "a definition that is not there was accepted, or "
                          "left the registry unable to accept one that is");

    ngx_http_status_test_fill(&def, 601);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(&def) == NGX_ERROR
                          && ngx_http_status_validate(NGX_HTTP_OK) == NGX_OK
                          && ngx_http_status_validate(306) == NGX_OK,
                          "a code outside the range was refused only after "
                          "the registry had been altered");

    ngx_http_status_test_assert(rc,
                          ngx_http_status_test_fill_headroom()
                          >= NGX_HTTP_STATUS_TEST_HEADROOM - 1,
                          "the registry refused a registration before the "
                          "headroom it documents had been used");

    ngx_http_status_test_fill(&def, 555);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(NULL) == NGX_ERROR
                          && ngx_http_status_register(&def) == NGX_ERROR
                          && ngx_http_status_validate(NGX_HTTP_OK) == NGX_OK,
                          "a registry with no row left refused a definition "
                          "that is not there for the wrong reason, or lost "
                          "what it already described");

    return rc;
}


/*
 * The bytes a phrase points at are borrowed and not copied: the contract is
 * that a caller passes bytes which outlive the registration, and a literal or a
 * static is what that means in practice.  They are altered after the
 * registration and the registry asked again, so a registry that had duplicated
 * them would answer with the bytes as they were, and they are put back
 * afterwards, the array being static.
 */

static u_char  ngx_http_status_test_lent[] = "205 Reset Content";


static ngx_int_t
ngx_http_status_test_borrowed(void)
{
    ngx_int_t               rc;
    const ngx_str_t        *got;
    ngx_http_status_def_t   def;

    rc = NGX_OK;

    def.code = 205;
    def.reason.len = sizeof(ngx_http_status_test_lent) - 1;
    def.reason.data = ngx_http_status_test_lent;
    def.flags = 0;
    def.rfc_section = "registered by the registry test";

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(&def) == NGX_OK,
                          "a definition whose phrase is borrowed was refused");

    ngx_http_status_test_lent[4] = 'B';

    got = ngx_http_status_reason(205);

    ngx_http_status_test_assert(rc,
                          got != NULL
                          && got->data == ngx_http_status_test_lent
                          && got->len == sizeof(ngx_http_status_test_lent) - 1
                          && ngx_memcmp(got->data, "205 Beset Content",
                                        got->len) == 0,
                          "the registry answered with a copy of the phrase "
                          "rather than the bytes the caller lent it");

    ngx_http_status_test_lent[4] = 'R';

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

    ngx_http_status_seal();

    ngx_http_status_test_fill(&def, 407);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(&def) == NGX_ERROR
                          && ngx_http_status_register(NULL) == NGX_ERROR,
                          "a definition was registered after sealing twice");

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
 * Every member of a definition is copied, so the whole descriptor is altered at
 * once after the registration and the registry asked again: a member it pointed
 * at rather than copied is caught however the alteration reached it.  Where a
 * code is recorded as coming from is the one member no function answers from,
 * and it is checked by the "metadata" target of the makefile beside this file,
 * which holds the table the registry is built from and the reference the
 * registry publishes to it row for row.
 */

static ngx_int_t
ngx_http_status_test_copied_metadata(void)
{
    ngx_int_t               rc;
    const ngx_str_t        *got;
    ngx_http_status_def_t   def;

    rc = NGX_OK;

    def.code = 205;
    def.reason.len = sizeof("205 Reset Content") - 1;
    def.reason.data = (u_char *) "205 Reset Content";
    def.flags = NGX_HTTP_STATUS_CACHEABLE|NGX_HTTP_STATUS_EXPIRES_OK;
    def.rfc_section = "RFC 9110 section 15.3.6";

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(&def) == NGX_OK,
                          "a sound definition was refused before sealing");

    def.code = 599;
    def.reason.len = 0;
    def.reason.data = NULL;
    def.flags = 0;
    def.rfc_section = NULL;

    got = ngx_http_status_reason(205);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_validate(205) == NGX_OK
                          && ngx_http_status_validate(599) == NGX_ERROR,
                          "the registry kept the code of the caller's "
                          "definition rather than a copy of it");

    ngx_http_status_test_assert(rc,
                          got != NULL
                          && got->len == sizeof("205 Reset Content") - 1
                          && ngx_memcmp(got->data, "205 Reset Content",
                                        got->len) == 0,
                          "the registry kept the phrase descriptor of the "
                          "caller's definition rather than a copy of it");

    ngx_http_status_test_assert(rc,
                          ngx_http_status_is_cacheable(205) != 0
                          && ngx_http_status_expires_ok(205) != 0,
                          "the registry kept the flags of the caller's "
                          "definition rather than a copy of them");

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
 * One built-in row in full: the code is described, it carries the phrase it is
 * expected to carry or none where nginx sends three digits and a space, and
 * each of the two questions the registry answers from a row's flags is
 * answered as the set that names those codes expects.  This is what a count of
 * the codes described cannot say, and it is what every initialization is
 * checked against.
 */

static ngx_int_t
ngx_http_status_test_builtin(ngx_uint_t code)
{
    ngx_int_t         rc;
    ngx_str_t        *want;
    const ngx_str_t  *got;

    rc = NGX_OK;

    got = ngx_http_status_reason(code);
    want = ngx_http_status_test_phrase(code);

    ngx_http_status_test_assert_code(rc,
                      ngx_http_status_validate(code) == NGX_OK,
                      "is a built-in code and yet is not described", code);

    if (want == NULL) {
        ngx_http_status_test_assert_code(rc, got != NULL && got->len == 0,
                          "carries a phrase where nginx sends three digits "
                          "and a space", code);

    } else {
        ngx_http_status_test_assert_code(rc,
                          got != NULL && got->len == want->len
                          && ngx_memcmp(got->data, want->data,
                                        want->len) == 0,
                          "does not carry the phrase nginx has always sent "
                          "for it", code);
    }

    ngx_http_status_test_assert_code(rc,
                      (ngx_http_status_is_cacheable(code) != 0)
                      == ngx_http_status_test_member(code,
                              ngx_http_status_test_cacheable_set,
                              ngx_http_status_test_nelts(
                                      ngx_http_status_test_cacheable_set)),
                      "is not heuristically cacheable exactly as RFC 9110 "
                      "section 15.1 makes it", code);

    ngx_http_status_test_assert_code(rc,
                      (ngx_http_status_expires_ok(code) != 0)
                      == ngx_http_status_test_member(code,
                              ngx_http_status_test_expires_set,
                              ngx_http_status_test_nelts(
                                      ngx_http_status_test_expires_set)),
                      "is not eligible for expires exactly as nginx's own set "
                      "makes it", code);

    return rc;
}


/*
 * And every built-in row together, with nothing else described beside them.
 * The count is asserted over the whole range, so a count of exactly the
 * built-in number taken together with every built-in code being described
 * leaves no room for a code the driver does not name to be described as well.
 */

static ngx_int_t
ngx_http_status_test_builtins(void)
{
    ngx_int_t   rc;
    ngx_uint_t  i, n;

    rc = NGX_OK;
    n = ngx_http_status_test_nelts(ngx_http_status_test_codes);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_test_described()
                          == NGX_HTTP_STATUS_TEST_NBUILTIN,
                          "the registry does not describe exactly as many "
                          "codes as it has built-in rows");

    for (i = 0; i < n; i++) {

        if (ngx_http_status_test_builtin(ngx_http_status_test_codes[i])
            != NGX_OK)
        {
            rc = NGX_ERROR;
        }
    }

    if (ngx_http_status_test_no_row(ngx_http_status_test_absent,
            ngx_http_status_test_nelts(ngx_http_status_test_absent))
        != NGX_OK)
    {
        rc = NGX_ERROR;
    }

    return rc;
}


/*
 * Initialization runs again for every configuration that is parsed, which is
 * on every reload and on every configuration test, so it must leave the same
 * registry every time however many configurations came before: unsealed,
 * describing every built-in code and no other, with whatever a module
 * registered for the configuration before discarded and the room to register
 * whole again.
 */

static ngx_int_t
ngx_http_status_test_cycle(void)
{
    ngx_int_t              rc;
    ngx_http_status_def_t  def;

    rc = NGX_OK;

    ngx_http_status_test_assert(rc, ngx_http_status_init(NULL) == NGX_OK,
                                "the registry failed to initialize again");

    if (ngx_http_status_test_builtins() != NGX_OK) {
        rc = NGX_ERROR;
    }

    ngx_http_status_test_assert(rc,
                          ngx_http_status_validate(205) == NGX_ERROR
                          && ngx_http_status_validate(305) == NGX_ERROR,
                          "initialization kept a registration made for the "
                          "configuration before it");

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

    ngx_http_status_test_assert(rc, ngx_http_status_init(NULL) == NGX_OK,
                                "the registry failed to initialize a last "
                                "time");

    if (ngx_http_status_test_builtins() != NGX_OK) {
        rc = NGX_ERROR;
    }

    ngx_http_status_test_assert(rc,
                          ngx_http_status_validate(205) == NGX_ERROR
                          && ngx_http_status_validate(305) == NGX_ERROR,
                          "the registry did not return to its built-in state");

    if (ngx_http_status_test_headroom_reset() != NGX_OK) {
        rc = NGX_ERROR;
    }

    return rc;
}


/*
 * The room a configuration has to register in is whole again.  How many
 * registrations are accepted is the one thing a carrying over of the rows an
 * earlier configuration registered would show as, so the count is asserted and
 * not the state.
 */

static ngx_int_t
ngx_http_status_test_headroom_reset(void)
{
    ngx_int_t  rc;

    rc = NGX_OK;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_test_fill_headroom()
                          == NGX_HTTP_STATUS_TEST_HEADROOM,
                          "the room a configuration has to register in was not "
                          "whole again after the configurations before it");

    ngx_http_status_test_assert(rc, ngx_http_status_init(NULL) == NGX_OK,
                                "the registry failed to initialize once its "
                                "headroom had been filled");

    if (ngx_http_status_test_builtins() != NGX_OK) {
        rc = NGX_ERROR;
    }

    return rc;
}


/*
 * The range macro, which is a parameterized macro and not a function because
 * the sources are ANSI C.  It expands its argument more than once, so a
 * variable is passed to it here and never an expression with a side effect,
 * which is how every caller of it in the tree passes one.
 *
 * Its bound is what the embedded Perl setter, the one caller that guards on it,
 * refuses a status a script chose outside of: a status below 100 and a status
 * of 600 or more, which as a consequence of the same bound is also every status
 * too wide for the three bytes an HTTP/2 or an HTTP/3 response reserves.
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
 * and a request answered before any status was chosen is reported as 9 when it
 * spoke HTTP/0.9 and as 0 otherwise.
 *
 * The constant naming HTTP/0.9 is itself 9, so a version returned in place of
 * a status would pass unnoticed for that one case.  HTTP/1.0, whose constant
 * is 1000, is asked about as well: a request that spoke it and was answered
 * before a status was chosen is reported as 0, which a version returned in
 * place of a status could not manage.  The three-digit formatting that turns 9
 * into "009" belongs to the callers.
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
 * The setter writes the response status and the bit recording that one was
 * chosen, and writes nothing else: not the status line, which a caller supplies
 * verbatim or leaves empty, and not the error status, which is a different
 * status for a different purpose.
 *
 * It holds a status to no width, and a status the registry does not describe is
 * refused only by the build configured to look for one.
 *
 * A status may be set again after the response has been decided, which is what
 * the two places answering for a request being torn down do: they write the
 * status the access log is to record and nothing of a response being sent
 * afterwards.  Both codes that reach them are nginx's own, so the registry
 * describes both and no build refuses either, and either may be written over a
 * response status that was already set.
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

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r,
                              NGX_HTTP_CLIENT_CLOSED_REQUEST) == NGX_OK
                          && r->headers_out.status
                             == NGX_HTTP_CLIENT_CLOSED_REQUEST
                          && r->status_final == 1
                          && ngx_http_status_test_alerts == 0,
                          "setting a status a second time was refused or "
                          "reported, which the access log depends on being "
                          "allowed");

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, NGX_HTTP_CLOSE) == NGX_OK
                          && r->headers_out.status == NGX_HTTP_CLOSE
                          && r->status_final == 1
                          && ngx_http_status_test_alerts == 0,
                          "setting the status a request closed without a "
                          "response is logged with was refused or reported");

    return rc;
}


/*
 * A status of the given width, on a request of its own.  The outcome required
 * of the setter is the outcome of a status the registry does not describe:
 * stored and unreported where the build carries no switch, refused and reported
 * where it does.
 */

static ngx_int_t
ngx_http_status_test_set_width(ngx_uint_t status, const char *what)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();

#if (NGX_HTTP_STATUS_VALIDATION)

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, status) == NGX_ERROR
                          && r->headers_out.status == 0
                          && r->status_final == 0
                          && ngx_http_status_test_alerts == 1, what);

#else

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, status) == NGX_OK
                          && r->headers_out.status == status
                          && r->status_final == 1
                          && ngx_http_status_test_alerts == 0, what);

#endif

    return rc;
}


/*
 * How wide a status may be is not a question the setter asks.  The width in a
 * three-digit conversion is a minimum and never a limit, so a status of four
 * digits or more cannot be written into the three bytes that the HTTP/2 and
 * HTTP/3 field encoders reserve for it; but an HTTP/1.x status line reserves
 * NGX_INT_T_LEN bytes for the same number and carries any width, which a
 * configuration may ask for:
 *
 *     error_page 404 =1234 /wide;
 *
 * is accepted and answers "HTTP/1.1 1234 ".  A bound applied where a status is
 * chosen would refuse that in the build without the switch as well, whose
 * behaviour must not differ.  The bound belongs to each encoding that has one:
 * each of the two filters that reserve those three bytes holds its own and
 * refuses a status wider than the bytes it reserved, and the boundary that
 * admits a status a script chose holds the range of the registry, which is
 * narrower than any such bound.
 *
 * The setter itself holds a status to no width in either build: a wide status
 * is treated exactly as any other status the registry does not describe.
 */

static ngx_int_t
ngx_http_status_test_set_bounds(void)
{
    ngx_int_t  rc;

    rc = NGX_OK;

    if (ngx_http_status_test_set_width(1000,
            "a status of four digits was not treated the way any other status "
            "the registry does not describe is treated")
        != NGX_OK)
    {
        rc = NGX_ERROR;
    }

    if (ngx_http_status_test_set_width(10000,
            "a status of five digits was not treated the way any other status "
            "the registry does not describe is treated")
        != NGX_OK)
    {
        rc = NGX_ERROR;
    }

    return rc;
}


/*
 * A status the registry merely does not describe is by contrast perfectly
 * sendable: the build without the switch stores it and sends it, and the build
 * with the switch refuses it, that being what asking for the switch asks for.
 * The difference is asserted against each build rather than left to be found
 * out.
 */

static ngx_int_t
ngx_http_status_test_set_undescribed(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();

#if (NGX_HTTP_STATUS_VALIDATION)

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 999) == NGX_ERROR
                          && r->headers_out.status == 0
                          && r->status_final == 0
                          && ngx_http_status_test_alerts == 1,
                          "a status the registry does not describe was "
                          "accepted, stored, or not reported exactly once by "
                          "the build configured to refuse it");

#else

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 999) == NGX_OK
                          && r->headers_out.status == 999
                          && r->status_final == 1
                          && ngx_http_status_test_alerts == 0,
                          "a sendable status the registry does not describe "
                          "was refused by the build that must send it");

#endif

    return rc;
}


/*
 * Moving the error status of a request over its response status, which is what
 * ngx_http_send_header() does last of all before the filters run and which is
 * therefore the last status any response carries.  The core hands the setter
 * the error status it has just found on the request, and clears the status line
 * itself where the setter answered NGX_OK, so that the moved status is not sent
 * with the phrase of the status it replaced.
 *
 * The setter writes the response status and the record on the request and
 * writes nothing else: the error status it was taken from is left where it was,
 * the access log reading that member rather than the response status, and the
 * status line is left for the caller to clear.
 */

static ngx_int_t
ngx_http_status_test_promote(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();
    r->headers_out.status = NGX_HTTP_OK;
    r->err_status = NGX_HTTP_BAD_GATEWAY;
    ngx_str_set(&r->headers_out.status_line, "200 OK");

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, r->err_status) == NGX_OK
                          && r->headers_out.status == NGX_HTTP_BAD_GATEWAY
                          && r->status_final == 1
                          && r->err_status == NGX_HTTP_BAD_GATEWAY
                          && r->headers_out.status_line.len
                             == sizeof("200 OK") - 1
                          && ngx_http_status_test_alerts == 0,
                          "moving an error status over a response status did "
                          "not make exactly the two stores the setter makes");

    if (ngx_http_status_test_promote_undescribed() != NGX_OK) {
        rc = NGX_ERROR;
    }

    return rc;
}


/*
 * And an error status the registry does not describe, which a configuration may
 * name.  The build without the switch moves it; the build with the switch
 * refuses it, leaves the response status as it found it, and reports it, so
 * that the core answers NGX_ERROR to whoever asked for the header rather than
 * sending a status that build was configured to refuse.
 */

static ngx_int_t
ngx_http_status_test_promote_undescribed(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();
    r->headers_out.status = NGX_HTTP_OK;
    r->err_status = 599;

#if (NGX_HTTP_STATUS_VALIDATION)

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, r->err_status) == NGX_ERROR
                          && r->headers_out.status == NGX_HTTP_OK
                          && r->status_final == 0
                          && r->err_status == 599
                          && ngx_http_status_test_alerts == 1,
                          "moving an error status the registry does not "
                          "describe was not refused, left alone, and reported "
                          "by the build configured to refuse it");

#else

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, r->err_status) == NGX_OK
                          && r->headers_out.status == 599
                          && r->status_final == 1
                          && r->err_status == 599
                          && ngx_http_status_test_alerts == 0,
                          "moving an error status the registry does not "
                          "describe was refused by the build that must send "
                          "it");

#endif

    return rc;
}


/*
 * The setter on a request that is relaying a response an upstream answered.
 * The two stores it makes are the same stores whether or not a request has an
 * upstream, and it writes nothing besides them.  The status an upstream chose
 * reaches the response by being stored directly and never through the setter,
 * which is why relaying one the registry does not describe is silent in every
 * build.
 */

static ngx_int_t
ngx_http_status_test_relayed(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();
    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 599;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, NGX_HTTP_BAD_GATEWAY) == NGX_OK
                          && r->headers_out.status == NGX_HTTP_BAD_GATEWAY
                          && r->status_final == 1
                          && ngx_http_status_test_alerts == 0,
                          "setting a described status on a request that has an "
                          "upstream did not record it, or was not silent");

    ngx_http_status_test_assert(rc,
                          r->headers_out.status_line.len == 0
                          && r->headers_out.status_line.data == NULL
                          && r->err_status == 0
                          && r->upstream->headers_in.status_n == 599,
                          "setting a status on a request that has an upstream "
                          "wrote more than the response status and the bit "
                          "recording it");

    r->headers_out.status = r->upstream->headers_in.status_n;

    ngx_http_status_test_assert(rc,
                          r->headers_out.status == 599
                          && ngx_http_status_validate(599) == NGX_ERROR
                          && ngx_http_status_test_alerts == 0,
                          "relaying a status the registry does not describe "
                          "was not silent, which relaying must be");

    return rc;
}


#if (NGX_HTTP_STATUS_VALIDATION)

/*
 * Reporting exists only in a build configured with
 * --with-http_status_validation, so these checks are compiled into that build
 * alone.  The log is given a writer, which is the hook nginx's own logging
 * offers, so that the alerts a check produced are counted rather than inferred;
 * the alerts these checks write out are the reporting working and not a failure
 * of it.
 */

static ngx_int_t
ngx_http_status_test_report(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();

    ngx_http_status_test_gate(r, NGX_HTTP_OK);

    ngx_http_status_test_assert(rc, ngx_http_status_test_alerts == 0,
                                "a status the registry describes was reported "
                                "by a gate that examines one");

    r = ngx_http_status_test_req();

    ngx_http_status_test_gate(r, 306);

    ngx_http_status_test_assert(rc, ngx_http_status_test_alerts == 1,
                                "a status the registry does not describe was "
                                "not reported exactly once");

    ngx_http_status_test_gate(r, 305);

    ngx_http_status_test_assert(rc, ngx_http_status_test_alerts == 2,
                                "a second status the registry does not "
                                "describe went unreported, this request having "
                                "been reported for once already");

    return rc;
}


/*
 * What the last report said, for one status: the count of lines, the severity
 * exactly, and the message exactly, a check that counts lines alone passing a
 * line written at the wrong severity or naming the wrong status.  The message
 * is compared as the tail of the formatted line before the linefeed, what
 * precedes it being the time, the severity, the process and the connection,
 * none of which belongs to the registry.
 */

static ngx_int_t
ngx_http_status_test_reported(ngx_uint_t status, const char *what)
{
    size_t      len;
    u_char     *last;
    ngx_int_t   rc;
    u_char      want[NGX_MAX_ERROR_STR];

    rc = NGX_OK;

    last = ngx_snprintf(want, sizeof(want), "unregistered HTTP status %ui",
                        status);
    len = last - want;

    ngx_http_status_test_assert_code(rc,
                          ngx_http_status_test_alerts == 1,
                          "was not reported exactly once", status);

    ngx_http_status_test_assert_code(rc,
                          ngx_http_status_test_level == NGX_LOG_ALERT,
                          "was not reported as an alert, which is the severity "
                          "a status that cannot be described is reported at",
                          status);

    ngx_http_status_test_assert_code(rc,
                          ngx_http_status_test_line_len
                          >= len + NGX_LINEFEED_SIZE
                          && ngx_memcmp(ngx_http_status_test_line
                                        + ngx_http_status_test_line_len
                                        - NGX_LINEFEED_SIZE - len,
                                        want, len) == 0,
                          what, status);

    return rc;
}


/*
 * The bytes of a report, for a status of every width one can have.  The message
 * names the status it was raised for, so a report of the wrong status, or of
 * the right status at the wrong severity, is caught rather than counted as a
 * report of the right one.
 */

static ngx_int_t
ngx_http_status_test_report_bytes(void)
{
    ngx_uint_t           i, n;
    ngx_int_t            rc;
    ngx_http_request_t  *r;
    static ngx_uint_t    set[] = { 42, 306, 599, 999, 1000, 10000 };

    rc = NGX_OK;
    n = sizeof(set) / sizeof(set[0]);

    for (i = 0; i < n; i++) {
        r = ngx_http_status_test_req();

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_set(r, set[i]) == NGX_ERROR,
                          "was accepted by the setter, so no report of it was "
                          "written to assert", set[i]);

        if (ngx_http_status_test_reported(set[i],
                          "was not reported with the message the registry "
                          "writes for a status it cannot describe")
            != NGX_OK)
        {
            rc = NGX_ERROR;
        }
    }

    r = ngx_http_status_test_req();

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, NGX_HTTP_OK) == NGX_OK
                          && ngx_http_status_test_alerts == 0
                          && ngx_http_status_test_level == NGX_LOG_DEBUG
                          && ngx_http_status_test_line_len == 0,
                          "a status the registry describes had a line written "
                          "for it");

    return rc;
}


/*
 * The sequence the gate of ngx_http_special_response_handler() performs, so
 * that the groups below assert the gate and not a rewriting of it: it is
 * scoped to the origin of the response, which is r->upstream, it asks the
 * registry about the status it was handed, and it writes the line itself, there
 * being nothing between it and the log.
 */

static void
ngx_http_status_test_gate(ngx_http_request_t *r, ngx_uint_t status)
{
    if (r->upstream == NULL
        && ngx_http_status_validate(status) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                      "unregistered HTTP status %ui", status);
    }
}


/*
 * And the sequence the gate of ngx_http_send_error_page() performs for the
 * status an "error_page" directive supplies.  That status was chosen by a
 * configuration whatever the origin of the response it replaces, so this gate
 * is not scoped to that origin; a zero carries no status at all, being the "="
 * and "=0" form of the directive, and is not examined.
 */

static void
ngx_http_status_test_overwrite(ngx_http_request_t *r, ngx_uint_t status)
{
    if (status && ngx_http_status_validate(status) != NGX_OK) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                      "unregistered HTTP status %ui", status);
    }
}


/*
 * And the status an upstream chose, which is exempt: an upstream may answer
 * with a code nginx has never heard of, or with one below 100, and nginx's part
 * is to relay what it received.  What answers for the origin is r->upstream and
 * never the status having some particular value, two authors being free to
 * choose the same number, so a request answered from an upstream is exempt
 * whatever status it carries and a request nginx answered itself is examined
 * whatever status that is.
 */

static ngx_int_t
ngx_http_status_test_report_exempts(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();
    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 599;

    ngx_http_status_test_gate(r, 599);

    ngx_http_status_test_assert(rc, ngx_http_status_test_alerts == 0,
                                "the status an upstream chose was reported, "
                                "which relaying a response must not be");

    r = ngx_http_status_test_req();
    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 42;

    ngx_http_status_test_gate(r, 42);

    ngx_http_status_test_assert(rc, ngx_http_status_test_alerts == 0,
                                "a status an upstream chose below 100 was "
                                "reported");

    r = ngx_http_status_test_req();

    ngx_http_status_test_gate(r, 306);

    ngx_http_status_test_assert(rc, ngx_http_status_test_alerts == 1,
                                "a status nginx chose for a request it "
                                "answered itself was exempted");

    if (ngx_http_status_test_report_overwrites() != NGX_OK) {
        rc = NGX_ERROR;
    }

    return rc;
}


/*
 * And the gate an "error_page" directive reaches, which exempts nothing: a
 * configuration chose that status whatever the origin of the response it
 * replaces, so it is examined even where an upstream answered the request and
 * even where it names the very number the upstream chose.
 */

static ngx_int_t
ngx_http_status_test_report_overwrites(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();
    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 599;

    ngx_http_status_test_overwrite(r, 599);

    ngx_http_status_test_assert(rc, ngx_http_status_test_alerts == 1,
                                "a status an error_page directive chose was "
                                "exempted because an upstream had chosen the "
                                "same number for the same request");

    ngx_http_status_test_overwrite(r, 0);

    ngx_http_status_test_assert(rc, ngx_http_status_test_alerts == 1,
                                "the form of the directive that keeps the "
                                "status of what a request is redirected to had "
                                "a status examined for it");

    return rc;
}


/*
 * The setter reports and then refuses, it being the one of the three points
 * where a status is chosen that has a caller with a result to act on: the
 * caller's own error handling runs, and the status the request already carried
 * is left as it was rather than replaced by one this build was configured to
 * object to.
 *
 * Every call is examined and every refusal is reported, whatever has happened
 * to the request before it: nothing the request accumulated grants a status or
 * silences a report.
 */

static ngx_int_t
ngx_http_status_test_set_reports(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 306) == NGX_ERROR
                          && ngx_http_status_test_alerts == 1,
                          "setting a status the registry does not describe was "
                          "accepted, or was not reported exactly once");

    ngx_http_status_test_assert(rc,
                          r->headers_out.status == 0
                          && r->status_final == 0,
                          "a status that was refused was stored all the same, "
                          "so the response would carry one this build objects "
                          "to");

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 305) == NGX_ERROR
                          && r->headers_out.status == 0
                          && r->status_final == 0
                          && ngx_http_status_test_alerts == 2,
                          "a second such status was accepted, or went "
                          "unreported because the first had been reported");

    r = ngx_http_status_test_req();

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, NGX_HTTP_NOT_FOUND) == NGX_OK
                          && ngx_http_status_test_alerts == 0,
                          "setting a status the registry describes was "
                          "reported");

    return rc;
}


/*
 * There is no report of a status being too wide, because the setter holds a
 * status to no width: a wide status is reported as a status the registry does
 * not describe, exactly as a narrow one the registry does not describe is, and
 * each of them is reported where it is refused.
 */

static ngx_int_t
ngx_http_status_test_set_wide_reports(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 1000) == NGX_ERROR
                          && ngx_http_status_test_alerts == 1,
                          "a status of four digits was not reported exactly "
                          "once as a status the registry does not describe");

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 10000) == NGX_ERROR
                          && r->headers_out.status == 0
                          && r->status_final == 0
                          && ngx_http_status_test_alerts == 2,
                          "a second status was accepted, or went unreported "
                          "because the first had been reported");

    return rc;
}


/*
 * The setter applies that same exemption, and applies it on the same origin:
 * r->upstream and never the value of the status.  A request being answered from
 * an upstream is therefore exempt for as long as it is, whatever status is
 * chosen for it, and a request nginx answered itself is examined however its
 * status compares with one an upstream chose for some other request.  The
 * exemption is read inside the setter, so no caller of it decides this.
 */

static ngx_int_t
ngx_http_status_test_set_relayed(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();
    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 599;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 599) == NGX_OK
                          && r->headers_out.status == 599
                          && r->status_final == 1
                          && ngx_http_status_test_alerts == 0,
                          "the setter refused or reported a status on a "
                          "request being answered from an upstream");

    r = ngx_http_status_test_req();
    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 42;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 42) == NGX_OK
                          && r->headers_out.status == 42
                          && ngx_http_status_test_alerts == 0,
                          "the setter refused or reported a status below 100 "
                          "on a request being answered from an upstream");

    r = ngx_http_status_test_req();

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 599) == NGX_ERROR
                          && r->headers_out.status == 0
                          && ngx_http_status_test_alerts == 1,
                          "the setter exempted a status on a request nginx "
                          "answered itself");

    return rc;
}


/*
 * The two gates and the setter composed on one request, which is the sequence a
 * relayed response that a configuration then answers for itself takes.  The
 * gate of ngx_http_special_response_handler() and the setter are scoped to the
 * origin of the response and exempt such a request; the gate of
 * ngx_http_send_error_page() is not, a status an "error_page" directive chose
 * having been chosen by a configuration whatever the origin of the response it
 * replaces.  An "error_page 599 =599 /uri" for an upstream that answered 599
 * leaves the upstream having chosen one 599 and the configuration another, and
 * only the first of them is exempt.
 */

static ngx_int_t
ngx_http_status_test_set_mixed(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();
    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 599;

    ngx_http_status_test_gate(r, 599);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 599) == NGX_OK
                          && r->headers_out.status == 599
                          && ngx_http_status_test_alerts == 0,
                          "the status an upstream chose was reported at the "
                          "gate that exempts it or by the setter that exempts "
                          "it");

    ngx_http_status_test_overwrite(r, 599);

    ngx_http_status_test_assert(rc, ngx_http_status_test_alerts == 1,
                                "a status a configuration chose was exempted "
                                "because an upstream had chosen the same "
                                "number for the same request");

    ngx_http_status_test_overwrite(r, 306);

    ngx_http_status_test_assert(rc, ngx_http_status_test_alerts == 2,
                                "a second status a configuration chose went "
                                "unreported, this request having been reported "
                                "for once already");

    return rc;
}


/*
 * The gate and the move composed on one request, which is the sequence a
 * response that has already gone wrong takes.  The gate of
 * ngx_http_special_response_handler() reports a status the registry does not
 * describe and lets it stand, having no caller of its own to answer a refusal
 * to.  ngx_http_send_header() then hands that same status to the setter, which
 * does have such a caller: this build refuses it, reports it in its turn, and
 * leaves the response status as it found it, so the request is answered as any
 * other failure of send_header is answered.  The build without the switch sends
 * it.
 *
 * A second such status on that same request is refused still and reported
 * still: nothing the request accumulated grants a status or silences a line.
 */

static ngx_int_t
ngx_http_status_test_gated_promotion(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();
    r->headers_out.status = NGX_HTTP_OK;

    ngx_http_status_test_gate(r, 599);

    ngx_http_status_test_assert(rc,
                          r->headers_out.status == NGX_HTTP_OK
                          && r->status_final == 0
                          && ngx_http_status_test_alerts == 1,
                          "the gate did not report the status once and leave "
                          "the response as it found it");

    r->err_status = 599;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, r->err_status) == NGX_ERROR
                          && r->headers_out.status == NGX_HTTP_OK
                          && r->status_final == 0
                          && r->err_status == 599
                          && ngx_http_status_test_alerts == 2,
                          "moving the status the gate let stand was accepted, "
                          "or was not reported where it was refused");

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 598) == NGX_ERROR
                          && r->headers_out.status == NGX_HTTP_OK
                          && ngx_http_status_test_alerts == 3,
                          "the setter accepted a status the registry does not "
                          "describe, or left it unreported, because this "
                          "request had been reported for already");

    return rc;
}

#endif


static ngx_http_status_test_case_t  ngx_http_status_test_cases[] = {

    { ngx_http_status_test_preinit, "the state before initialization", 0 },

    { ngx_http_status_test_signatures,
      "the published prototypes that answer about a status", 1 },
    { ngx_http_status_test_signatures_write,
      "the published prototypes that write", 1 },
    { ngx_http_status_test_helper_signatures,
      "the prototypes beside the API that run the lifecycle", 1 },
    { ngx_http_status_test_metadata_signatures,
      "the prototype beside the API that answers about a code", 1 },
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
    { ngx_http_status_test_refuses_order,
      "the order the refusals are decided in", 1 },
    { ngx_http_status_test_borrowed,
      "the bytes a registration borrows", 1 },
    { ngx_http_status_test_sealed, "sealing", 1 },
    { ngx_http_status_test_copied, "a definition being copied", 1 },
    { ngx_http_status_test_copied_metadata,
      "every member of a definition being copied", 1 },
    { ngx_http_status_test_exhaustion, "the headroom running out", 1 },
    { ngx_http_status_test_builtins,
      "the state initialization leaves behind", 1 },
    { ngx_http_status_test_idempotent, "initializing more than once", 1 },
    { ngx_http_status_test_ranges, "the range macro", 1 },
    { ngx_http_status_test_effective,
      "the status a request is reported as", 1 },
    { ngx_http_status_test_precedence,
      "which status is reported when there is more than one", 1 },
    { ngx_http_status_test_set, "setting a status", 1 },
    { ngx_http_status_test_set_bounds,
      "the setter holding a status to no width", 1 },
    { ngx_http_status_test_set_undescribed,
      "setting a status the registry does not describe", 1 },
    { ngx_http_status_test_promote,
      "moving an error status over a response status", 1 },
    { ngx_http_status_test_relayed,
      "setting a status on a request answered from an upstream", 1 },
#if (NGX_HTTP_STATUS_VALIDATION)
    { ngx_http_status_test_report,
      "a gate reporting an undescribed status", 1 },
    { ngx_http_status_test_report_bytes,
      "the severity and the bytes of a report", 1 },
    { ngx_http_status_test_report_exempts,
      "which statuses a gate exempts from reporting", 1 },
    { ngx_http_status_test_set_reports,
      "the setter reporting and refusing", 1 },
    { ngx_http_status_test_set_wide_reports,
      "a status of four digits being reported as undescribed", 1 },
    { ngx_http_status_test_set_relayed,
      "the setter exempting a response an upstream answered", 1 },
    { ngx_http_status_test_set_mixed,
      "the two gates and the setter composed on one request", 1 },
    { ngx_http_status_test_gated_promotion,
      "the gate and the move composed on one request", 1 },
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
 * The makefile beside this file leaves out exactly one object, that of
 * src/core/nginx.c, because it is the one object of the tree that defines
 * main().  It defines four other things as well: the core module, which is the
 * first entry of the generated ngx_modules[], and the three functions of
 * src/core/ngx_cycle.h that live with it, which the cycle, the process and the
 * upstream code call.  Leaving that object out leaves those four undefined, so
 * they are defined here.
 *
 * The core module needs no directives and no context callbacks, nothing here
 * running a cycle.  The three functions belong to starting, reloading and
 * binding worker processes, none of which a run of these tests reaches, so each
 * reports and stops rather than answer with something a caller would go on to
 * trust.
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
