
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/*
 * A driver for the HTTP status code registry.  It exercises the five
 * functions the registry publishes through <ngx_http.h>, the seven it
 * publishes through its own header, the macros both of those headers define,
 * and the lifecycle that leaves the registry writable only while a
 * configuration is parsed.
 *
 * The driver is linked against the object tree of a build of nginx by the
 * makefile beside this file:
 *
 *     make -f misc/status_test/GNUmakefile test
 *
 * That makefile links every object of the build but the one which defines
 * main(), and this file defines in its place the four other symbols that
 * object defines and the rest of the tree refers to, so that no source of
 * nginx is compiled a second time and the registry examined here is the one
 * that ships rather than a copy of it.  Nothing in the build depends on this
 * file and auto/configure does not know of it: it is made on request.  It
 * reports on the standard error output and exits zero only when every check
 * passed, so that a script may run it.
 *
 * The registry's own header is not included here.  <ngx_http.h> includes it,
 * which is how every other file in the tree reaches the registry, so this one
 * reaches it the same way and would notice were that to stop being true.
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
 * Every flag the registry defines, as one word.  A row is expected to carry
 * some subset of these and nothing besides, so a flag added to the registry
 * must be added here and to the table of the sets that carry it, and until it
 * is the driver says so rather than passing on a row it knows nothing about.
 */

#define NGX_HTTP_STATUS_TEST_ALL_FLAGS                                       \
    (NGX_HTTP_STATUS_CACHEABLE|NGX_HTTP_STATUS_INFORMATIONAL                 \
     |NGX_HTTP_STATUS_CLIENT_ERROR|NGX_HTTP_STATUS_SERVER_ERROR              \
     |NGX_HTTP_STATUS_EXPIRES_OK|NGX_HTTP_STATUS_INTERNAL)


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
    ngx_uint_t   code;
    ngx_uint_t   row;
} ngx_http_status_test_page_t;


typedef struct {
    ngx_uint_t   flag;
    ngx_uint_t  *set;                    /* the codes that carry it */
    ngx_uint_t   n;
    const char  *what;                   /* said of a code answered for badly */
} ngx_http_status_test_flag_t;


typedef struct {
    ngx_uint_t   code;
    const char  *section;                /* where the code is recorded from */
} ngx_http_status_test_section_t;


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
static ngx_uint_t ngx_http_status_test_want_flags(ngx_uint_t code);
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
static ngx_int_t ngx_http_status_test_flag_sets(void);
static ngx_int_t ngx_http_status_test_flag_words(void);
static ngx_int_t ngx_http_status_test_sections(void);
static ngx_int_t ngx_http_status_test_provenance(void);
static ngx_int_t ngx_http_status_test_divergent_sections(void);
static ngx_int_t ngx_http_status_test_internal(void);
static ngx_int_t ngx_http_status_test_internal_flags(void);
static ngx_int_t ngx_http_status_test_no_row(ngx_uint_t *set, ngx_uint_t n);
static ngx_int_t ngx_http_status_test_register(void);
static ngx_int_t ngx_http_status_test_register_refuses(void);
static ngx_int_t ngx_http_status_test_sealed(void);
static ngx_int_t ngx_http_status_test_copied(void);
static ngx_int_t ngx_http_status_test_copied_metadata(void);
static ngx_int_t ngx_http_status_test_exhaustion(void);
static ngx_int_t ngx_http_status_test_builtin(ngx_uint_t code);
static ngx_int_t ngx_http_status_test_builtins(void);
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
static ngx_int_t ngx_http_status_test_set_relayed(void);
static ngx_int_t ngx_http_status_test_set_mixed(void);
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
 * The three classes of code the registry flags, which is what replaced the
 * arithmetic on a code's value that told them apart before it existed.  Every
 * 1xx code it describes is informational, every 4xx a client error, nginx's own
 * six included, and every 5xx a server error; a code of no class, 200 among
 * them, carries none of the three.
 */

static ngx_uint_t  ngx_http_status_test_informational_set[] = {
    NGX_HTTP_CONTINUE, NGX_HTTP_SWITCHING_PROTOCOLS,
    NGX_HTTP_PROCESSING, NGX_HTTP_EARLY_HINTS
};


static ngx_uint_t  ngx_http_status_test_client_error_set[] = {
    NGX_HTTP_BAD_REQUEST, NGX_HTTP_UNAUTHORIZED, 402,
    NGX_HTTP_FORBIDDEN, NGX_HTTP_NOT_FOUND, NGX_HTTP_NOT_ALLOWED, 406,
    NGX_HTTP_REQUEST_TIME_OUT, NGX_HTTP_CONFLICT, 410,
    NGX_HTTP_LENGTH_REQUIRED, NGX_HTTP_PRECONDITION_FAILED,
    NGX_HTTP_REQUEST_ENTITY_TOO_LARGE, NGX_HTTP_REQUEST_URI_TOO_LARGE,
    NGX_HTTP_UNSUPPORTED_MEDIA_TYPE, NGX_HTTP_RANGE_NOT_SATISFIABLE,
    NGX_HTTP_MISDIRECTED_REQUEST, NGX_HTTP_TOO_MANY_REQUESTS,
    NGX_HTTP_CLOSE, NGX_HTTP_NGINX_CODES, NGX_HTTPS_CERT_ERROR,
    NGX_HTTPS_NO_CERT, NGX_HTTP_TO_HTTPS, NGX_HTTP_CLIENT_CLOSED_REQUEST
};


static ngx_uint_t  ngx_http_status_test_server_error_set[] = {
    NGX_HTTP_INTERNAL_SERVER_ERROR, NGX_HTTP_NOT_IMPLEMENTED,
    NGX_HTTP_BAD_GATEWAY, NGX_HTTP_SERVICE_UNAVAILABLE,
    NGX_HTTP_GATEWAY_TIME_OUT, NGX_HTTP_VERSION_NOT_SUPPORTED,
    NGX_HTTP_INSUFFICIENT_STORAGE
};


/*
 * Each flag beside the codes that carry it, so that every flag of every code
 * of the range can be asked for and a failure can name both the flag and the
 * code.  What is said of a code is said in full here because the check that
 * says it knows only which flag it was asking about.
 */

static ngx_http_status_test_flag_t  ngx_http_status_test_flags[] = {

    { NGX_HTTP_STATUS_CACHEABLE, ngx_http_status_test_cacheable_set,
      ngx_http_status_test_nelts(ngx_http_status_test_cacheable_set),
      "is not flagged heuristically cacheable as RFC 9110 section 15.1 "
      "expects" },

    { NGX_HTTP_STATUS_INFORMATIONAL, ngx_http_status_test_informational_set,
      ngx_http_status_test_nelts(ngx_http_status_test_informational_set),
      "is not flagged informational as the 1xx codes described are" },

    { NGX_HTTP_STATUS_CLIENT_ERROR, ngx_http_status_test_client_error_set,
      ngx_http_status_test_nelts(ngx_http_status_test_client_error_set),
      "is not flagged a client error as the 4xx codes described are" },

    { NGX_HTTP_STATUS_SERVER_ERROR, ngx_http_status_test_server_error_set,
      ngx_http_status_test_nelts(ngx_http_status_test_server_error_set),
      "is not flagged a server error as the 5xx codes described are" },

    { NGX_HTTP_STATUS_EXPIRES_OK, ngx_http_status_test_expires_set,
      ngx_http_status_test_nelts(ngx_http_status_test_expires_set),
      "is not flagged eligible for expires as nginx's own set is" },

    { NGX_HTTP_STATUS_INTERNAL, ngx_http_status_test_internal_set,
      ngx_http_status_test_nelts(ngx_http_status_test_internal_set),
      "is not flagged one of nginx's own codes as those six are" }
};


/*
 * Where a code is recorded as coming from, for one code out of each
 * specification the registry cites and for one of nginx's own.  The eight
 * codes whose phrase differs from the name RFC 9110 recommends are all named
 * here as well, because the section reference is where that name is kept.
 */

static ngx_http_status_test_section_t  ngx_http_status_test_provenances[] = {

    { NGX_HTTP_CONTINUE, "RFC 9110 section 15.2.1" },
    { NGX_HTTP_PROCESSING, "RFC 2518 section 10.1" },
    { NGX_HTTP_EARLY_HINTS, "RFC 8297 section 2" },
    { NGX_HTTP_OK, "RFC 9110 section 15.3.1" },
    { NGX_HTTP_NOT_FOUND, "RFC 9110 section 15.5.5" },
    { NGX_HTTP_TOO_MANY_REQUESTS, "RFC 6585 section 4" },
    { NGX_HTTP_INSUFFICIENT_STORAGE, "RFC 4918 section 11.5" },
    { NGX_HTTP_CLOSE, "nginx internal" },

    { NGX_HTTP_MOVED_TEMPORARILY, "RFC 9110 section 15.4.3 (Found)" },
    { NGX_HTTP_NOT_ALLOWED,
      "RFC 9110 section 15.5.6 (Method Not Allowed)" },
    { NGX_HTTP_REQUEST_TIME_OUT,
      "RFC 9110 section 15.5.9 (Request Timeout)" },
    { NGX_HTTP_REQUEST_ENTITY_TOO_LARGE,
      "RFC 9110 section 15.5.14 (Content Too Large)" },
    { NGX_HTTP_REQUEST_URI_TOO_LARGE,
      "RFC 9110 section 15.5.15 (URI Too Long)" },
    { NGX_HTTP_RANGE_NOT_SATISFIABLE,
      "RFC 9110 section 15.5.17 (Range Not Satisfiable)" },
    { NGX_HTTP_SERVICE_UNAVAILABLE,
      "RFC 9110 section 15.6.4 (Service Unavailable)" },
    { NGX_HTTP_GATEWAY_TIME_OUT,
      "RFC 9110 section 15.6.5 (Gateway Timeout)" }
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


/*
 * The row of the error page table a code selects.  The rows follow the shape
 * of that table and not the membership of the registry: a code the registry
 * does not describe still selects a row when it falls within one of the spans
 * of consecutive codes the table holds, and a code no span covers selects the
 * row of zero length.
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
 * A zero-initialized instance of the request, of the connection it is answered
 * on, and of the log that connection reports through, each of them the type
 * nginx itself uses.  A request is allocated from a pool and zeroed when nginx
 * creates it, so a zeroed request is a request in the state nginx starts one
 * in, and the members read here are the three the effective status is chosen
 * from and the log an alert is reported through.
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


/* the flags a code is expected to carry, assembled from the sets above */

static ngx_uint_t
ngx_http_status_test_want_flags(ngx_uint_t code)
{
    ngx_uint_t                    i, n, want;
    ngx_http_status_test_flag_t  *f;

    want = 0;
    n = ngx_http_status_test_nelts(ngx_http_status_test_flags);

    for (i = 0; i < n; i++) {
        f = &ngx_http_status_test_flags[i];

        if (ngx_http_status_test_member(code, f->set, f->n)) {
            want |= f->flag;
        }
    }

    return want;
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


/* the same again, for the three of its own header that run the lifecycle */

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

    /* sealing having run above, the registry is writable again after init */

    ngx_http_status_test_assert(rc, ngx_http_status_validate(306) == NGX_ERROR,
                                "ngx_http_status_init() did not leave the "
                                "built-in codes alone through its pointer");

    return rc;
}


/* and for the four of its own header that answer about a code's metadata */

static ngx_int_t
ngx_http_status_test_metadata_signatures(void)
{
    ngx_int_t    rc;
    const char  *section;

    ngx_uint_t  (*expires)(ngx_uint_t status);
    ngx_uint_t  (*page)(ngx_uint_t status);
    ngx_uint_t  (*flags)(ngx_uint_t status, ngx_uint_t flags);
    const char *(*provenance)(ngx_uint_t status);

    rc = NGX_OK;

    expires = ngx_http_status_expires_ok;
    page = ngx_http_status_error_page_index;
    flags = ngx_http_status_has_flags;
    provenance = ngx_http_status_rfc_section;

    ngx_http_status_test_assert(rc, expires(NGX_HTTP_OK) != 0,
                                "ngx_http_status_expires_ok() did not answer "
                                "non-zero for 200 through its pointer");

    ngx_http_status_test_assert(rc,
                          page(NGX_HTTP_MOVED_PERMANENTLY) == 1,
                          "ngx_http_status_error_page_index() did not answer "
                          "the first row for 301 through its pointer");

    ngx_http_status_test_assert(rc,
                          flags(NGX_HTTP_CLOSE, NGX_HTTP_STATUS_INTERNAL)
                          == NGX_HTTP_STATUS_INTERNAL,
                          "ngx_http_status_has_flags() did not answer the flag "
                          "asked about for 444 through its pointer");

    section = provenance(NGX_HTTP_OK);

    ngx_http_status_test_assert(rc, section != NULL && *section != '\0',
                                "ngx_http_status_rfc_section() did not answer "
                                "where 200 comes from through its pointer");

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

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_has_flags(code,
                                  NGX_HTTP_STATUS_TEST_ALL_FLAGS) == 0,
                          "carries a flag although the registry does not "
                          "describe it", code);

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_rfc_section(code) == NULL,
                          "is recorded as coming from somewhere although the "
                          "registry does not describe it", code);
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
 * Every flag of every code of the range, one flag at a time, so that a failure
 * names the flag as well as the code.  The registry answers about a flag
 * through a query of its own, and that query is what makes this observable:
 * were the informational flag dropped from 100, or the internal flag from 444,
 * nothing else the registry answers would change and every check that follows
 * from a flag rather than from the flag itself would hold just the same.
 */

static ngx_int_t
ngx_http_status_test_flag_sets(void)
{
    ngx_int_t                     rc;
    ngx_uint_t                    code, i, n;
    ngx_http_status_test_flag_t  *f;

    rc = NGX_OK;
    n = ngx_http_status_test_nelts(ngx_http_status_test_flags);

    for (i = 0; i < n; i++) {
        f = &ngx_http_status_test_flags[i];

        for (code = NGX_HTTP_STATUS_MIN; code < NGX_HTTP_STATUS_MAX; code++) {

            ngx_http_status_test_assert_code(rc,
                          (ngx_http_status_has_flags(code, f->flag) ? 1 : 0)
                          == ngx_http_status_test_member(code, f->set, f->n),
                          f->what, code);
        }
    }

    return rc;
}


/*
 * And the whole flag word of every code at once, which is what catches a flag
 * carried by the wrong row as well as one the driver does not name at all.  A
 * flag the registry gains must be named here, and until it is this says so.
 */

static ngx_int_t
ngx_http_status_test_flag_words(void)
{
    ngx_int_t   rc;
    ngx_uint_t  code, i, n, unknown;

    rc = NGX_OK;
    unknown = ~((ngx_uint_t) NGX_HTTP_STATUS_TEST_ALL_FLAGS);

    for (code = NGX_HTTP_STATUS_MIN; code < NGX_HTTP_STATUS_MAX; code++) {

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_has_flags(code,
                                  NGX_HTTP_STATUS_TEST_ALL_FLAGS)
                          == ngx_http_status_test_want_flags(code),
                          "does not carry exactly the flags expected of it",
                          code);

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_has_flags(code, unknown) == 0,
                          "carries a flag the driver does not name, so one has "
                          "been added to the registry", code);
    }

    /* a code outside the range the registry covers carries none of them */

    n = ngx_http_status_test_nelts(ngx_http_status_test_outside);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_outside[i];

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_has_flags(code,
                                  NGX_HTTP_STATUS_TEST_ALL_FLAGS) == 0,
                          "carries a flag although it lies outside the range "
                          "the registry covers", code);
    }

    return rc;
}


/*
 * Where the registry records that a code comes from.  Every code it describes
 * is recorded and no code it does not is, which is the distinction a phrase
 * draws as well; nginx's own codes are recorded as internal to nginx rather
 * than against a specification that does not define them, and every other code
 * names the specification it comes from.
 */

static ngx_int_t
ngx_http_status_test_sections(void)
{
    ngx_int_t    rc;
    ngx_uint_t   code;
    const char  *got;

    rc = NGX_OK;

    for (code = NGX_HTTP_STATUS_MIN; code < NGX_HTTP_STATUS_MAX; code++) {

        got = ngx_http_status_rfc_section(code);

        if (ngx_http_status_validate(code) != NGX_OK) {

            ngx_http_status_test_assert_code(rc, got == NULL,
                          "is recorded as coming from somewhere although the "
                          "registry does not describe it", code);
            continue;
        }

        ngx_http_status_test_assert_code(rc, got != NULL && *got != '\0',
                          "is described and yet is recorded as coming from "
                          "nowhere", code);

        if (got == NULL) {
            continue;
        }

        if (ngx_http_status_has_flags(code, NGX_HTTP_STATUS_INTERNAL)) {

            ngx_http_status_test_assert_code(rc,
                          ngx_strcmp(got, "nginx internal") == 0,
                          "is one of nginx's own codes and so must be recorded "
                          "as internal to nginx", code);
            continue;
        }

        ngx_http_status_test_assert_code(rc,
                          ngx_strncmp(got, "RFC ", 4) == 0,
                          "does not name the specification it comes from",
                          code);
    }

    return rc;
}


/* the reference itself, for a code out of each specification cited */

static ngx_int_t
ngx_http_status_test_provenance(void)
{
    ngx_int_t                        rc;
    ngx_uint_t                       code, i, n;
    const char                      *got;
    ngx_http_status_test_section_t  *want;

    rc = NGX_OK;
    n = ngx_http_status_test_nelts(ngx_http_status_test_provenances);

    for (i = 0; i < n; i++) {
        want = &ngx_http_status_test_provenances[i];
        code = want->code;

        got = ngx_http_status_rfc_section(code);

        ngx_http_status_test_assert_code(rc,
                          got != NULL && ngx_strcmp(got, want->section) == 0,
                          "is not recorded as coming from where it does come "
                          "from", code);
    }

    return rc;
}


/*
 * The eight codes whose phrase differs from the name RFC 9110 recommends
 * record that name beside their section, in parentheses.  That is what lets
 * the phrase keep the bytes nginx has always sent while the registry is still
 * correct by that specification, so the name is read back here and compared
 * with the one the divergence table names.
 */

static ngx_int_t
ngx_http_status_test_divergent_sections(void)
{
    ngx_int_t    rc;
    ngx_uint_t   code, i, n;
    ngx_str_t   *name;
    const char  *got, *p;

    rc = NGX_OK;
    n = ngx_http_status_test_nelts(ngx_http_status_test_divergent);

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_divergent[i].code;
        name = &ngx_http_status_test_divergent[i].rfc;

        got = ngx_http_status_rfc_section(code);
        p = (got == NULL) ? NULL : ngx_strchr(got, '(');

        /* the name follows the code and a space in the divergence table */

        ngx_http_status_test_assert_code(rc,
                          p != NULL
                          && ngx_strlen(p + 1) == name->len - 3
                          && ngx_strncmp(p + 1, name->data + 4,
                                         name->len - 4) == 0
                          && p[name->len - 3] == ')',
                          "sends a phrase that differs from the name RFC 9110 "
                          "recommends without recording that name", code);
    }

    return rc;
}


/*
 * nginx's own codes are described by the registry as first class members of
 * it, so that a build which reports a status the registry does not describe
 * says nothing about them: they are load bearing signals and not violations of
 * any specification.  The flags each carries are asserted as well as what
 * follows from them, so that a build which stopped flagging one of them as
 * internal is caught here rather than passing because nothing else changed.
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

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_set(r, code) == NGX_OK
                          && ngx_http_status_test_alerts == 0,
                          "is one of nginx's own codes and so must be set "
                          "without complaint", code);
    }

    return rc;
}


/*
 * And the flags each of them carries, which is what says the registry holds
 * them as members of it and not merely as codes it happens to describe: each is
 * flagged as one of nginx's own and as a client error, both bits and not one of
 * them, so that the registry places them in the 4xx class they are numbered in
 * while marking them as having no standing outside nginx.  Neither bit follows
 * from anything else the registry answers, which is why each is read back.
 */

static ngx_int_t
ngx_http_status_test_internal_flags(void)
{
    ngx_int_t    rc;
    ngx_uint_t   code, i, n, want;
    const char  *section;

    rc = NGX_OK;
    n = ngx_http_status_test_nelts(ngx_http_status_test_internal_set);
    want = NGX_HTTP_STATUS_INTERNAL|NGX_HTTP_STATUS_CLIENT_ERROR;

    for (i = 0; i < n; i++) {
        code = ngx_http_status_test_internal_set[i];
        section = ngx_http_status_rfc_section(code);

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_has_flags(code, want) == want,
                          "is one of nginx's own codes and so must be flagged "
                          "both internal and a client error", code);

        ngx_http_status_test_assert_code(rc,
                          ngx_http_status_has_flags(code,
                                  NGX_HTTP_STATUS_TEST_ALL_FLAGS) == want,
                          "is one of nginx's own codes and so must carry no "
                          "flag besides those two", code);

        ngx_http_status_test_assert_code(rc,
                          section != NULL
                          && ngx_strcmp(section, "nginx internal") == 0,
                          "is one of nginx's own codes and so must be recorded "
                          "as internal to nginx", code);
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
 * Every member of a definition is copied, and not only the two another answer
 * follows from.  The flags are read back as a word, so that a member copied as
 * zero is caught, and where the code is recorded as coming from is read back as
 * the string it was registered as although the caller's pointer has since been
 * set to null, which is what proves that member was copied as well rather than
 * merely pointed at.
 */

static ngx_int_t
ngx_http_status_test_copied_metadata(void)
{
    ngx_int_t               rc;
    ngx_uint_t              want;
    const char             *section;
    ngx_http_status_def_t   def;

    rc = NGX_OK;
    want = NGX_HTTP_STATUS_CACHEABLE|NGX_HTTP_STATUS_EXPIRES_OK;

    def.code = 205;
    def.reason.len = sizeof("205 Reset Content") - 1;
    def.reason.data = (u_char *) "205 Reset Content";
    def.flags = want;
    def.rfc_section = "RFC 9110 section 15.3.6";

    ngx_http_status_test_assert(rc,
                          ngx_http_status_register(&def) == NGX_OK,
                          "a sound definition was refused before sealing");

    def.flags = 0;
    def.rfc_section = NULL;

    section = ngx_http_status_rfc_section(205);

    ngx_http_status_test_assert(rc,
                          ngx_http_status_has_flags(205,
                                  NGX_HTTP_STATUS_TEST_ALL_FLAGS) == want,
                          "the registry did not copy the flags of the "
                          "definition it was given");

    ngx_http_status_test_assert(rc,
                          ngx_http_status_is_cacheable(205) != 0
                          && ngx_http_status_expires_ok(205) != 0,
                          "the flags of a definition were copied and yet are "
                          "not what the registry answers from");

    ngx_http_status_test_assert(rc,
                          section != NULL
                          && ngx_strcmp(section,
                                        "RFC 9110 section 15.3.6") == 0,
                          "the registry did not copy where the definition it "
                          "was given records the code as coming from");

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
 * One built-in row in full: the code is described, it carries the phrase it
 * has always carried or none where nginx sends three digits and a space, it
 * carries exactly the flags expected of it, and it is recorded as coming from
 * somewhere.  This is what a count of the codes described cannot say, and it is
 * what every initialization is checked against.
 */

static ngx_int_t
ngx_http_status_test_builtin(ngx_uint_t code)
{
    ngx_int_t         rc;
    ngx_str_t        *want;
    const char       *section;
    const ngx_str_t  *got;

    rc = NGX_OK;

    got = ngx_http_status_reason(code);
    want = ngx_http_status_test_phrase(code);
    section = ngx_http_status_rfc_section(code);

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
                      ngx_http_status_has_flags(code,
                              NGX_HTTP_STATUS_TEST_ALL_FLAGS)
                      == ngx_http_status_test_want_flags(code),
                      "does not carry exactly the flags expected of it", code);

    ngx_http_status_test_assert_code(rc,
                      section != NULL && *section != '\0',
                      "is described and yet is recorded as coming from "
                      "nowhere", code);

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

    /* and what a module registered for an earlier configuration is gone */

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
 * registry every time: unsealed, describing every built-in code and no other,
 * with whatever a module registered for the configuration before discarded.
 * The cycle is run more than once because a second run of it is the case a
 * reload is, and the state after each is compared in full with the state
 * initialization is expected to leave rather than counted and let be.
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

    /* and one initialization more leaves the built-in codes alone again */

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

    return rc;
}


/*
 * The two range macros.  Each is a parameterized macro and not a function,
 * because the sources are ANSI C.  The range test expands its argument more
 * than once, so a variable is passed to it and never an expression with a side
 * effect; the width test expands its argument once.
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
 * The one entry point for a status nginx chose itself.  It writes the response
 * status and the bit recording that one was chosen, and writes nothing else:
 * not the status line, which a caller supplies verbatim or leaves empty, and
 * not the error status, which is a different status for a different purpose.
 *
 * A status too wide for the three bytes an HTTP/2 or an HTTP/3 response
 * reserves cannot be sent at all, so it is refused in every build and reported
 * as an alert.  A status the registry does not describe is by contrast
 * perfectly sendable, and only the build configured to look for one refuses it;
 * both of those bounds belong to the group that follows this one, which asserts
 * each of them against the build it is compiled into.
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
 * an alert.
 *
 * A status the registry merely does not describe is by contrast perfectly
 * sendable, and the build without the switch sends it: what a response carries
 * there is what it carried before the registry existed, which is the one thing
 * that build must not do differently.  The build with the switch is the build
 * that refuses it, that being what asking for the switch asks for, and the
 * difference between the two is asserted here against each of them rather than
 * left to be found out.
 */

static ngx_int_t
ngx_http_status_test_set_bounds(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();

#if (NGX_HTTP_STATUS_VALIDATION)

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, NGX_HTTP_STATUS_WIRE_MAX - 1)
                          == NGX_ERROR
                          && r->headers_out.status == 0
                          && r->status_final == 0
                          && ngx_http_status_test_alerts == 1,
                          "a status the registry does not describe was "
                          "accepted, stored, or not reported exactly once by "
                          "the build configured to refuse it");

#else

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, NGX_HTTP_STATUS_WIRE_MAX - 1)
                          == NGX_OK
                          && r->headers_out.status
                             == NGX_HTTP_STATUS_WIRE_MAX - 1
                          && r->status_final == 1
                          && ngx_http_status_test_alerts == 0,
                          "a sendable status the registry does not describe "
                          "was refused by the build that must send it");

#endif

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
 * Whether the status a request is presenting to nginx's own status machinery is
 * the one an upstream chose for the response being relayed.  Such a status is
 * exempt from being examined, because an upstream may answer with a code nginx
 * has never heard of, or with one below 100, and nginx's part is to relay what
 * it received.
 *
 * The answer is the authorship recorded on the request by the two places that
 * carry an upstream's status across into nginx's own structures, and never the
 * status having some particular value: two authors may choose the same number
 * for one request, so equal numbers would not prove equal authorship.  A
 * request that has an upstream is therefore not exempt as such, and a status
 * nginx chooses for such a request is not exempt even where it is the number
 * the upstream chose.
 */

static ngx_int_t
ngx_http_status_test_relayed(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();

    ngx_http_status_test_assert(rc,
                          !ngx_http_status_relayed(r),
                          "a request that has relayed nothing was taken to be "
                          "relaying a status");

    /* an upstream of its own says nothing of who chose a status */

    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 599;

    ngx_http_status_test_assert(rc,
                          !ngx_http_status_relayed(r),
                          "a request was taken to be relaying a status because "
                          "it has an upstream, which is not what an upstream "
                          "having answered means");

    /* the crossings of that boundary record it, whatever the code */

    r->status_upstream = 1;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_relayed(r),
                          "the status an upstream chose was not taken to be "
                          "the one being relayed");

    r->upstream->headers_in.status_n = 42;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_relayed(r)
                          && ngx_http_status_validate(42) == NGX_ERROR,
                          "a status an upstream chose below the range the "
                          "registry covers was not taken to be relayed");

    /*
     * And nginx choosing a status for that same request ends the exemption,
     * even where it chooses the number the upstream chose: the setter is nginx
     * choosing, so it clears what the crossing recorded.
     */

    r->upstream->headers_in.status_n = NGX_HTTP_BAD_GATEWAY;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, NGX_HTTP_BAD_GATEWAY) == NGX_OK
                          && !ngx_http_status_relayed(r),
                          "a status nginx chose for a request that had relayed "
                          "one was taken to be relayed as well, on being the "
                          "number the upstream chose");

    return rc;
}


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
 * alone.  A real connection and a real log are wired to the request and the
 * log is given a writer, which is the hook nginx's own logging offers, so that
 * the alerts a check produced are counted rather than inferred: each of these
 * checks emits one alert, which is the reporting working and not a failure.
 *
 * What is asserted is the policy: a status the registry describes is not
 * reported, one it does not describe is, the status an upstream chose is
 * exempt, and a request is reported for at most once however many statuses are
 * chosen for it.
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

    ngx_http_status_report(r, 305);

    ngx_http_status_test_assert(rc, ngx_http_status_test_alerts == 1,
                                "a request was reported for more than once, "
                                "so these lines count stores and not "
                                "requests");

    return rc;
}


/*
 * And the status an upstream chose, which is exempt.  The exemption belongs to
 * the gate that can be handed such a status rather than to the reporting
 * itself, and it is read from the authorship recorded on the request, so what
 * is asserted here is the sequence that gate performs: report unless the
 * request is relaying, then clear the record just read.
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
    r->status_upstream = 1;

    if (!ngx_http_status_relayed(r)) {
        ngx_http_status_report(r, 599);
    }

    ngx_http_status_test_assert(rc, r->status_reported == 0
                                    && ngx_http_status_test_alerts == 0,
                                "the status an upstream chose was reported, "
                                "which relaying a response must not be");

    /*
     * The record the gate read is cleared there, so that a status the
     * configuration chooses for the same request afterwards is reported even
     * where it is the number the upstream chose: an "error_page 599 =599 /uri"
     * against an upstream answering 599 leaves nginx relaying one 599 and the
     * configuration choosing another, and only the first of them is exempt.
     */

    r->status_upstream = 0;

    if (!ngx_http_status_relayed(r)) {
        ngx_http_status_report(r, 599);
    }

    ngx_http_status_test_assert(rc, r->status_reported == 1
                                    && ngx_http_status_test_alerts == 1,
                                "a status the configuration chose was taken to "
                                "be relayed because an upstream had chosen the "
                                "same number for the same request");

    /* a status below the range the registry covers is relayed as it stands */

    r = ngx_http_status_test_req();
    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 42;
    r->status_upstream = 1;

    if (!ngx_http_status_relayed(r)) {
        ngx_http_status_report(r, 42);
    }

    ngx_http_status_test_assert(rc, r->status_reported == 0
                                    && ngx_http_status_test_alerts == 0,
                                "a status an upstream chose below 100 was "
                                "reported");

    /* and a status nginx chose for a request with an upstream is not exempt */

    r = ngx_http_status_test_req();
    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 42;

    if (!ngx_http_status_relayed(r)) {
        ngx_http_status_report(r, 306);
    }

    ngx_http_status_test_assert(rc, r->status_reported == 1
                                    && ngx_http_status_test_alerts == 1,
                                "a status nginx chose for a request that has "
                                "an upstream was treated as relayed");

    return rc;
}


/*
 * The setter reports through that same policy and then refuses, it being the
 * one of the three points where a status is chosen that has a caller with a
 * result to act on: the caller's own error handling runs, answering the request
 * as it answers any other failure, and the status the request already carried
 * is left as it was rather than replaced by one this build was configured to
 * object to.
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
                          && r->status_reported == 1
                          && ngx_http_status_test_alerts == 1,
                          "setting a status the registry does not describe was "
                          "accepted, or was not reported exactly once");

    ngx_http_status_test_assert(rc,
                          r->headers_out.status == 0
                          && r->status_final == 0,
                          "a status that was refused was stored all the same, "
                          "so the response would carry one this build objects "
                          "to");

    /* refusing does not depend on the report having been made */

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 305) == NGX_ERROR
                          && ngx_http_status_test_alerts == 1,
                          "a second such status was accepted, or a request was "
                          "reported for more than once");

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


/*
 * The setter and the record of who chose a status composed, which is the pair a
 * request being relayed presents and so is the path the product takes.  Each is
 * asked of on its own above; what is asked here is that the setter does not
 * read that record.  Relaying is done by storing a status directly and never
 * through the setter, so every status the setter is handed was chosen by nginx
 * or by a configuration, and a request that is relaying one is not thereby
 * allowed to be given an undescribed status of its own.  Storing a status is
 * nginx choosing it, so the setter clears the record as it stores.
 */

static ngx_int_t
ngx_http_status_test_set_relayed(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    /* the request is relaying a status, recorded as the crossings record it */

    r = ngx_http_status_test_req();
    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 599;
    r->status_upstream = 1;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 599) == NGX_ERROR
                          && r->headers_out.status == 0
                          && r->status_final == 0
                          && r->status_reported == 1
                          && ngx_http_status_test_alerts == 1,
                          "the setter accepted a status the registry does not "
                          "describe because the request was relaying one");

    /* including one below the range the registry describes at all */

    r = ngx_http_status_test_req();
    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 42;
    r->status_upstream = 1;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 42) == NGX_ERROR
                          && r->headers_out.status == 0
                          && r->status_reported == 1
                          && ngx_http_status_test_alerts == 1,
                          "the setter accepted a status below 100 because the "
                          "request was relaying one");

    /* and a status it does store leaves nginx as the chooser of record */

    r = ngx_http_status_test_req();
    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 599;
    r->status_upstream = 1;

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, NGX_HTTP_NOT_FOUND) == NGX_OK
                          && r->headers_out.status == NGX_HTTP_NOT_FOUND
                          && r->status_final == 1
                          && !ngx_http_status_relayed(r)
                          && ngx_http_status_test_alerts == 0,
                          "the setter left the request recorded as relaying a "
                          "status after nginx had chosen the one it carries");

    return rc;
}


/*
 * The gate and the setter composed on one request, which is the sequence a
 * relayed response that nginx then answers for itself takes: the status the
 * upstream chose reaches a gate, which exempts it and clears the record it
 * read, and a status nginx chooses afterwards reaches the setter, which
 * examines it as it examines any other.  The report belongs to the request
 * rather than to either of them, so it is made once however many undescribed
 * statuses follow.
 */

static ngx_int_t
ngx_http_status_test_set_mixed(void)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    rc = NGX_OK;

    r = ngx_http_status_test_req();
    r->upstream = &ngx_http_status_test_u;
    r->upstream->headers_in.status_n = 42;
    r->status_upstream = 1;

    /* the gate the status the upstream chose reaches */

    if (!ngx_http_status_relayed(r)) {
        ngx_http_status_report(r, 42);
    }

    r->status_upstream = 0;

    ngx_http_status_test_assert(rc, r->status_reported == 0
                                    && ngx_http_status_test_alerts == 0,
                                "the status an upstream chose was reported at "
                                "the gate that is meant to exempt it");

    /* and the setter a status nginx chooses for the same request reaches */

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 306) == NGX_ERROR
                          && r->headers_out.status == 0
                          && r->status_final == 0
                          && r->status_reported == 1
                          && ngx_http_status_test_alerts == 1,
                          "a status nginx chose for a request that had been "
                          "relaying one was exempted by the setter");

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, 305) == NGX_ERROR
                          && ngx_http_status_test_alerts == 1,
                          "the setter reported a request for more than once, "
                          "so its lines count stores and not requests");

    /* while a status the registry does describe is stored on that request */

    ngx_http_status_test_assert(rc,
                          ngx_http_status_set(r, NGX_HTTP_BAD_GATEWAY) == NGX_OK
                          && r->headers_out.status == NGX_HTTP_BAD_GATEWAY
                          && r->status_final == 1
                          && ngx_http_status_test_alerts == 1,
                          "a status the registry describes was refused after "
                          "one it does not describe had been");

    return rc;
}

#endif


static ngx_http_status_test_case_t  ngx_http_status_test_cases[] = {

    /* this one runs before anything initializes the registry */

    { ngx_http_status_test_preinit, "the state before initialization", 0 },

    { ngx_http_status_test_signatures,
      "the published prototypes that answer about a status", 1 },
    { ngx_http_status_test_signatures_write,
      "the published prototypes that write", 1 },
    { ngx_http_status_test_helper_signatures,
      "the prototypes of its own header that run the lifecycle", 1 },
    { ngx_http_status_test_metadata_signatures,
      "the prototypes of its own header that answer about metadata", 1 },
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
    { ngx_http_status_test_flag_sets, "each flag over the whole range", 1 },
    { ngx_http_status_test_flag_words,
      "the whole flag word of every code", 1 },
    { ngx_http_status_test_sections,
      "where each code is recorded as coming from", 1 },
    { ngx_http_status_test_provenance,
      "the reference of a code out of each specification cited", 1 },
    { ngx_http_status_test_divergent_sections,
      "the name RFC 9110 recommends being recorded", 1 },
    { ngx_http_status_test_internal, "nginx's own codes", 1 },
    { ngx_http_status_test_internal_flags,
      "the flags nginx's own codes carry", 1 },
    { ngx_http_status_test_register, "registration", 1 },
    { ngx_http_status_test_register_refuses,
      "the registrations that are refused", 1 },
    { ngx_http_status_test_sealed, "sealing", 1 },
    { ngx_http_status_test_copied, "a definition being copied", 1 },
    { ngx_http_status_test_copied_metadata,
      "every member of a definition being copied", 1 },
    { ngx_http_status_test_exhaustion, "the headroom running out", 1 },
    { ngx_http_status_test_builtins,
      "the state initialization leaves behind", 1 },
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
    { ngx_http_status_test_set_reports,
      "the setter reporting and refusing", 1 },
    { ngx_http_status_test_set_relayed,
      "the setter not reading the record of who chose a status", 1 },
    { ngx_http_status_test_set_mixed,
      "the gate and the setter composed on one request", 1 },
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
