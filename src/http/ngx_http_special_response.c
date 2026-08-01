
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>


static ngx_int_t ngx_http_send_error_page(ngx_http_request_t *r,
    ngx_http_err_page_t *err_page);
static ngx_int_t ngx_http_send_special_response(ngx_http_request_t *r,
    ngx_http_core_loc_conf_t *clcf, ngx_uint_t err);
static ngx_int_t ngx_http_send_refresh(ngx_http_request_t *r);
static ngx_uint_t ngx_http_error_page_index(ngx_uint_t status);


static u_char ngx_http_error_full_tail[] =
"<hr><center>" NGINX_VER "</center>" CRLF
"</body>" CRLF
"</html>" CRLF
;


static u_char ngx_http_error_build_tail[] =
"<hr><center>" NGINX_VER_BUILD "</center>" CRLF
"</body>" CRLF
"</html>" CRLF
;


static u_char ngx_http_error_tail[] =
"<hr><center>nginx</center>" CRLF
"</body>" CRLF
"</html>" CRLF
;


static u_char ngx_http_msie_padding[] =
"<!-- a padding to disable MSIE and Chrome friendly error page -->" CRLF
"<!-- a padding to disable MSIE and Chrome friendly error page -->" CRLF
"<!-- a padding to disable MSIE and Chrome friendly error page -->" CRLF
"<!-- a padding to disable MSIE and Chrome friendly error page -->" CRLF
"<!-- a padding to disable MSIE and Chrome friendly error page -->" CRLF
"<!-- a padding to disable MSIE and Chrome friendly error page -->" CRLF
;


static u_char ngx_http_msie_refresh_head[] =
"<html><head><meta http-equiv=\"Refresh\" content=\"0; URL=";


static u_char ngx_http_msie_refresh_tail[] =
"\"></head><body></body></html>" CRLF;


static char ngx_http_error_301_page[] =
"<html>" CRLF
"<head><title>301 Moved Permanently</title></head>" CRLF
"<body>" CRLF
"<center><h1>301 Moved Permanently</h1></center>" CRLF
;


static char ngx_http_error_302_page[] =
"<html>" CRLF
"<head><title>302 Found</title></head>" CRLF
"<body>" CRLF
"<center><h1>302 Found</h1></center>" CRLF
;


static char ngx_http_error_303_page[] =
"<html>" CRLF
"<head><title>303 See Other</title></head>" CRLF
"<body>" CRLF
"<center><h1>303 See Other</h1></center>" CRLF
;


static char ngx_http_error_307_page[] =
"<html>" CRLF
"<head><title>307 Temporary Redirect</title></head>" CRLF
"<body>" CRLF
"<center><h1>307 Temporary Redirect</h1></center>" CRLF
;


static char ngx_http_error_308_page[] =
"<html>" CRLF
"<head><title>308 Permanent Redirect</title></head>" CRLF
"<body>" CRLF
"<center><h1>308 Permanent Redirect</h1></center>" CRLF
;


static char ngx_http_error_400_page[] =
"<html>" CRLF
"<head><title>400 Bad Request</title></head>" CRLF
"<body>" CRLF
"<center><h1>400 Bad Request</h1></center>" CRLF
;


static char ngx_http_error_401_page[] =
"<html>" CRLF
"<head><title>401 Authorization Required</title></head>" CRLF
"<body>" CRLF
"<center><h1>401 Authorization Required</h1></center>" CRLF
;


static char ngx_http_error_402_page[] =
"<html>" CRLF
"<head><title>402 Payment Required</title></head>" CRLF
"<body>" CRLF
"<center><h1>402 Payment Required</h1></center>" CRLF
;


static char ngx_http_error_403_page[] =
"<html>" CRLF
"<head><title>403 Forbidden</title></head>" CRLF
"<body>" CRLF
"<center><h1>403 Forbidden</h1></center>" CRLF
;


static char ngx_http_error_404_page[] =
"<html>" CRLF
"<head><title>404 Not Found</title></head>" CRLF
"<body>" CRLF
"<center><h1>404 Not Found</h1></center>" CRLF
;


static char ngx_http_error_405_page[] =
"<html>" CRLF
"<head><title>405 Not Allowed</title></head>" CRLF
"<body>" CRLF
"<center><h1>405 Not Allowed</h1></center>" CRLF
;


static char ngx_http_error_406_page[] =
"<html>" CRLF
"<head><title>406 Not Acceptable</title></head>" CRLF
"<body>" CRLF
"<center><h1>406 Not Acceptable</h1></center>" CRLF
;


static char ngx_http_error_408_page[] =
"<html>" CRLF
"<head><title>408 Request Time-out</title></head>" CRLF
"<body>" CRLF
"<center><h1>408 Request Time-out</h1></center>" CRLF
;


static char ngx_http_error_409_page[] =
"<html>" CRLF
"<head><title>409 Conflict</title></head>" CRLF
"<body>" CRLF
"<center><h1>409 Conflict</h1></center>" CRLF
;


static char ngx_http_error_410_page[] =
"<html>" CRLF
"<head><title>410 Gone</title></head>" CRLF
"<body>" CRLF
"<center><h1>410 Gone</h1></center>" CRLF
;


static char ngx_http_error_411_page[] =
"<html>" CRLF
"<head><title>411 Length Required</title></head>" CRLF
"<body>" CRLF
"<center><h1>411 Length Required</h1></center>" CRLF
;


static char ngx_http_error_412_page[] =
"<html>" CRLF
"<head><title>412 Precondition Failed</title></head>" CRLF
"<body>" CRLF
"<center><h1>412 Precondition Failed</h1></center>" CRLF
;


static char ngx_http_error_413_page[] =
"<html>" CRLF
"<head><title>413 Request Entity Too Large</title></head>" CRLF
"<body>" CRLF
"<center><h1>413 Request Entity Too Large</h1></center>" CRLF
;


static char ngx_http_error_414_page[] =
"<html>" CRLF
"<head><title>414 Request-URI Too Large</title></head>" CRLF
"<body>" CRLF
"<center><h1>414 Request-URI Too Large</h1></center>" CRLF
;


static char ngx_http_error_415_page[] =
"<html>" CRLF
"<head><title>415 Unsupported Media Type</title></head>" CRLF
"<body>" CRLF
"<center><h1>415 Unsupported Media Type</h1></center>" CRLF
;


static char ngx_http_error_416_page[] =
"<html>" CRLF
"<head><title>416 Requested Range Not Satisfiable</title></head>" CRLF
"<body>" CRLF
"<center><h1>416 Requested Range Not Satisfiable</h1></center>" CRLF
;


static char ngx_http_error_421_page[] =
"<html>" CRLF
"<head><title>421 Misdirected Request</title></head>" CRLF
"<body>" CRLF
"<center><h1>421 Misdirected Request</h1></center>" CRLF
;


static char ngx_http_error_429_page[] =
"<html>" CRLF
"<head><title>429 Too Many Requests</title></head>" CRLF
"<body>" CRLF
"<center><h1>429 Too Many Requests</h1></center>" CRLF
;


static char ngx_http_error_494_page[] =
"<html>" CRLF
"<head><title>400 Request Header Or Cookie Too Large</title></head>"
CRLF
"<body>" CRLF
"<center><h1>400 Bad Request</h1></center>" CRLF
"<center>Request Header Or Cookie Too Large</center>" CRLF
;


static char ngx_http_error_495_page[] =
"<html>" CRLF
"<head><title>400 The SSL certificate error</title></head>"
CRLF
"<body>" CRLF
"<center><h1>400 Bad Request</h1></center>" CRLF
"<center>The SSL certificate error</center>" CRLF
;


static char ngx_http_error_496_page[] =
"<html>" CRLF
"<head><title>400 No required SSL certificate was sent</title></head>"
CRLF
"<body>" CRLF
"<center><h1>400 Bad Request</h1></center>" CRLF
"<center>No required SSL certificate was sent</center>" CRLF
;


static char ngx_http_error_497_page[] =
"<html>" CRLF
"<head><title>400 The plain HTTP request was sent to HTTPS port</title></head>"
CRLF
"<body>" CRLF
"<center><h1>400 Bad Request</h1></center>" CRLF
"<center>The plain HTTP request was sent to HTTPS port</center>" CRLF
;


static char ngx_http_error_500_page[] =
"<html>" CRLF
"<head><title>500 Internal Server Error</title></head>" CRLF
"<body>" CRLF
"<center><h1>500 Internal Server Error</h1></center>" CRLF
;


static char ngx_http_error_501_page[] =
"<html>" CRLF
"<head><title>501 Not Implemented</title></head>" CRLF
"<body>" CRLF
"<center><h1>501 Not Implemented</h1></center>" CRLF
;


static char ngx_http_error_502_page[] =
"<html>" CRLF
"<head><title>502 Bad Gateway</title></head>" CRLF
"<body>" CRLF
"<center><h1>502 Bad Gateway</h1></center>" CRLF
;


static char ngx_http_error_503_page[] =
"<html>" CRLF
"<head><title>503 Service Temporarily Unavailable</title></head>" CRLF
"<body>" CRLF
"<center><h1>503 Service Temporarily Unavailable</h1></center>" CRLF
;


static char ngx_http_error_504_page[] =
"<html>" CRLF
"<head><title>504 Gateway Time-out</title></head>" CRLF
"<body>" CRLF
"<center><h1>504 Gateway Time-out</h1></center>" CRLF
;


static char ngx_http_error_505_page[] =
"<html>" CRLF
"<head><title>505 HTTP Version Not Supported</title></head>" CRLF
"<body>" CRLF
"<center><h1>505 HTTP Version Not Supported</h1></center>" CRLF
;


static char ngx_http_error_507_page[] =
"<html>" CRLF
"<head><title>507 Insufficient Storage</title></head>" CRLF
"<body>" CRLF
"<center><h1>507 Insufficient Storage</h1></center>" CRLF
;


/*
 * The shape of the table below: the three spans of consecutive status codes it
 * holds a row for, each written as the half open range [FIRST, LIMIT) of the
 * codes one span covers, and the row each span starts at, which follows from
 * the spans before it.  The spans cover 301 through 308, that is
 * NGX_HTTP_MOVED_PERMANENTLY through NGX_HTTP_PERMANENT_REDIRECT, then 400
 * through 429, NGX_HTTP_BAD_REQUEST through NGX_HTTP_TOO_MANY_REQUESTS, and
 * then 494 through 507, NGX_HTTP_NGINX_CODES through
 * NGX_HTTP_INSUFFICIENT_STORAGE.
 *
 * This is the one place that shape is written down, and it is written here
 * because the table is here.  Only the six bounds are written out: the row each
 * span starts at is derived from them, and so is the number of rows the table
 * holds, which the assertion after the rows below holds the table itself to.
 */

#define NGX_HTTP_ERROR_PAGE_3XX_FIRST   301
#define NGX_HTTP_ERROR_PAGE_3XX_LIMIT   309
#define NGX_HTTP_ERROR_PAGE_4XX_FIRST   400
#define NGX_HTTP_ERROR_PAGE_4XX_LIMIT   430
#define NGX_HTTP_ERROR_PAGE_49X_FIRST   494
#define NGX_HTTP_ERROR_PAGE_49X_LIMIT   508

#define NGX_HTTP_ERROR_PAGE_3XX_ROW     1

#define NGX_HTTP_ERROR_PAGE_4XX_ROW                                           \
    (NGX_HTTP_ERROR_PAGE_3XX_ROW                                              \
     + (NGX_HTTP_ERROR_PAGE_3XX_LIMIT - NGX_HTTP_ERROR_PAGE_3XX_FIRST))

#define NGX_HTTP_ERROR_PAGE_49X_ROW                                           \
    (NGX_HTTP_ERROR_PAGE_4XX_ROW                                              \
     + (NGX_HTTP_ERROR_PAGE_4XX_LIMIT - NGX_HTTP_ERROR_PAGE_4XX_FIRST))

#define NGX_HTTP_ERROR_PAGE_ROWS                                              \
    (NGX_HTTP_ERROR_PAGE_49X_ROW                                              \
     + (NGX_HTTP_ERROR_PAGE_49X_LIMIT - NGX_HTTP_ERROR_PAGE_49X_FIRST))


/*
 * The rows are the zero length row followed by the three spans written above,
 * and ngx_http_error_page_index() selects one from those spans: the rows follow
 * the shape of this table rather than the membership of the status code
 * registry, so which row a code selects is answered from the bounds of the
 * table and not by looking a definition up.  The table is sized by the rows
 * written out below, which the assertion after them holds to the rows those
 * spans account for.
 *
 * A row exists for every code those spans cover, including a code the registry
 * does not describe: the row for 498 holds the 404 page, which is how nginx
 * answers a request whose host name is invalid, and the rows for the other
 * codes it does not describe hold no page at all.
 */

static ngx_str_t ngx_http_error_pages[] = {

    ngx_null_string,                     /* 201, 204 */

    /* 300 has no row */
    ngx_string(ngx_http_error_301_page),
    ngx_string(ngx_http_error_302_page),
    ngx_string(ngx_http_error_303_page),
    ngx_null_string,                     /* 304 */
    ngx_null_string,                     /* 305 */
    ngx_null_string,                     /* 306 */
    ngx_string(ngx_http_error_307_page),
    ngx_string(ngx_http_error_308_page),

    ngx_string(ngx_http_error_400_page),
    ngx_string(ngx_http_error_401_page),
    ngx_string(ngx_http_error_402_page),
    ngx_string(ngx_http_error_403_page),
    ngx_string(ngx_http_error_404_page),
    ngx_string(ngx_http_error_405_page),
    ngx_string(ngx_http_error_406_page),
    ngx_null_string,                     /* 407 */
    ngx_string(ngx_http_error_408_page),
    ngx_string(ngx_http_error_409_page),
    ngx_string(ngx_http_error_410_page),
    ngx_string(ngx_http_error_411_page),
    ngx_string(ngx_http_error_412_page),
    ngx_string(ngx_http_error_413_page),
    ngx_string(ngx_http_error_414_page),
    ngx_string(ngx_http_error_415_page),
    ngx_string(ngx_http_error_416_page),
    ngx_null_string,                     /* 417 */
    ngx_null_string,                     /* 418 */
    ngx_null_string,                     /* 419 */
    ngx_null_string,                     /* 420 */
    ngx_string(ngx_http_error_421_page),
    ngx_null_string,                     /* 422 */
    ngx_null_string,                     /* 423 */
    ngx_null_string,                     /* 424 */
    ngx_null_string,                     /* 425 */
    ngx_null_string,                     /* 426 */
    ngx_null_string,                     /* 427 */
    ngx_null_string,                     /* 428 */
    ngx_string(ngx_http_error_429_page),

    ngx_string(ngx_http_error_494_page), /* 494, request header too large */
    ngx_string(ngx_http_error_495_page), /* 495, https certificate error */
    ngx_string(ngx_http_error_496_page), /* 496, https no certificate */
    ngx_string(ngx_http_error_497_page), /* 497, http to https */
    ngx_string(ngx_http_error_404_page), /* 498, canceled */
    ngx_null_string,                     /* 499, client has closed connection */

    ngx_string(ngx_http_error_500_page),
    ngx_string(ngx_http_error_501_page),
    ngx_string(ngx_http_error_502_page),
    ngx_string(ngx_http_error_503_page),
    ngx_string(ngx_http_error_504_page),
    ngx_string(ngx_http_error_505_page),
    ngx_null_string,                     /* 506 */
    ngx_string(ngx_http_error_507_page)

};


/*
 * That the table holds a row for every code those spans cover and no row
 * besides, checked where the rows are written and not while a configuration is
 * parsed: an array of a negative length is not a type, so a build fails should
 * a row be added or dropped here without the span that accounts for it, or a
 * span be moved without the rows it accounts for.  It is the size of the table
 * itself that is checked, so nothing of the mapping is written out twice.
 */

typedef char ngx_http_error_pages_check_t
    [sizeof(ngx_http_error_pages) / sizeof(ngx_str_t)
     == NGX_HTTP_ERROR_PAGE_ROWS ? 1 : -1];


/*
 * The row of the table above that a status code selects: the offset of the code
 * within the span that covers it, added to the row that span starts at.  A code
 * that no span covers, whether it falls between the spans as 444 does or lies
 * outside them altogether, selects the zero length row.
 *
 * A span covers every code in its range, including a code the status code
 * registry does not describe, so the spans and not the registry are what this
 * answers from.
 */

static ngx_uint_t
ngx_http_error_page_index(ngx_uint_t status)
{
    if (status >= NGX_HTTP_ERROR_PAGE_3XX_FIRST
        && status < NGX_HTTP_ERROR_PAGE_3XX_LIMIT)
    {
        return NGX_HTTP_ERROR_PAGE_3XX_ROW
               + (status - NGX_HTTP_ERROR_PAGE_3XX_FIRST);
    }

    if (status >= NGX_HTTP_ERROR_PAGE_4XX_FIRST
        && status < NGX_HTTP_ERROR_PAGE_4XX_LIMIT)
    {
        return NGX_HTTP_ERROR_PAGE_4XX_ROW
               + (status - NGX_HTTP_ERROR_PAGE_4XX_FIRST);
    }

    if (status >= NGX_HTTP_ERROR_PAGE_49X_FIRST
        && status < NGX_HTTP_ERROR_PAGE_49X_LIMIT)
    {
        return NGX_HTTP_ERROR_PAGE_49X_ROW
               + (status - NGX_HTTP_ERROR_PAGE_49X_FIRST);
    }

    /* a code with no row of its own, so a zero length body */

    return 0;
}


/*
 * When HTTP status validation is enabled, the status this function is given is
 * checked here rather than at each of the many places that produce one:
 * ngx_http_finalize_request() routes here the statuses a handler returned and
 * ngx_http_filter_finalize_request() those a filter chose part way through a
 * response, so the two paths converge on this one point.  This gate reports a
 * status and refuses none, having no caller with a result to act on; the status
 * reaches ngx_http_status_set() later, where ngx_http_send_header() moves the
 * error status of the request into the response, and is reported and refused
 * there in its turn.
 *
 * The exemption is scoped to the origin of the response and not to the call
 * site: the status an upstream chose is relayed faithfully, and it reaches this
 * function whenever an upstream error is intercepted and re-enters nginx's own
 * error page machinery, which a site scoped exemption would miss.  A status
 * that an error_page directive supplies in place of one intercepted here was
 * chosen by the configuration whatever the response's origin, so it is examined
 * where that directive is applied, in ngx_http_send_error_page(), even where it
 * repeats the number it replaces.  The codes that are internal to nginx are
 * registered, so they are accepted in silence.
 */

ngx_int_t
ngx_http_special_response_handler(ngx_http_request_t *r, ngx_int_t error)
{
    ngx_uint_t                 i, err;
    ngx_http_err_page_t       *err_page;
    ngx_http_core_loc_conf_t  *clcf;

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http special response: %i, \"%V?%V\"",
                   error, &r->uri, &r->args);

#if (NGX_HTTP_STATUS_VALIDATION)

    /*
     * Reported and never replaced: the response carries what was asked for.
     * There is no caller here with a result to act on, this function being
     * what answers a request that has already gone wrong, so a status is
     * reported and not refused; ngx_http_status_set() is where a status is
     * refused, its caller having an error path of its own, and such a status
     * reaches the setter once more when ngx_http_send_header() moves the error
     * status of the request into the response.  The report is written at alert
     * level, so that it survives an error_log level that hides anything less.
     *
     * This is the one gate that is handed a status whose author it does not
     * know, an upstream's status arriving here whenever an error of its is
     * intercepted, so the exemption for such a status is applied here, from the
     * origin of the response and never from the value of the status.
     */

    if (r->upstream == NULL
        && ngx_http_status_validate((ngx_uint_t) error) != NGX_OK)
    {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                      "unregistered HTTP status %ui", (ngx_uint_t) error);
    }

#endif

    r->err_status = error;

    if (r->keepalive) {
        switch (error) {
            case NGX_HTTP_BAD_REQUEST:
            case NGX_HTTP_REQUEST_ENTITY_TOO_LARGE:
            case NGX_HTTP_REQUEST_URI_TOO_LARGE:
            case NGX_HTTP_TO_HTTPS:
            case NGX_HTTPS_CERT_ERROR:
            case NGX_HTTPS_NO_CERT:
            case NGX_HTTP_INTERNAL_SERVER_ERROR:
            case NGX_HTTP_NOT_IMPLEMENTED:
                r->keepalive = 0;
        }
    }

    if (r->lingering_close) {
        switch (error) {
            case NGX_HTTP_BAD_REQUEST:
            case NGX_HTTP_TO_HTTPS:
            case NGX_HTTPS_CERT_ERROR:
            case NGX_HTTPS_NO_CERT:
                r->lingering_close = 0;
        }
    }

    r->headers_out.content_type.len = 0;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (!r->error_page && clcf->error_pages && r->uri_changes != 0) {

        if (clcf->recursive_error_pages == 0) {
            r->error_page = 1;
        }

        err_page = clcf->error_pages->elts;

        for (i = 0; i < clcf->error_pages->nelts; i++) {
            if (err_page[i].status == error) {
                return ngx_http_send_error_page(r, &err_page[i]);
            }
        }
    }

    r->expect_tested = 1;

    if (ngx_http_discard_request_body(r) != NGX_OK) {
        r->keepalive = 0;
    }

    if (clcf->msie_refresh
        && r->headers_in.msie
        && (error == NGX_HTTP_MOVED_PERMANENTLY
            || error == NGX_HTTP_MOVED_TEMPORARILY))
    {
        return ngx_http_send_refresh(r);
    }

    if (error == NGX_HTTP_CREATED) {
        /* 201 */
        err = 0;

    } else if (error == NGX_HTTP_NO_CONTENT) {
        /* 204 */
        err = 0;

    } else {
        /* 3XX, 4XX, 49X, 5XX, and zero body for an unknown code */
        err = ngx_http_error_page_index((ngx_uint_t) error);

        if (error >= NGX_HTTP_NGINX_CODES) {
            /* 49X */
            switch (error) {
                case NGX_HTTP_TO_HTTPS:
                case NGX_HTTPS_CERT_ERROR:
                case NGX_HTTPS_NO_CERT:
                case NGX_HTTP_REQUEST_HEADER_TOO_LARGE:
                    r->err_status = NGX_HTTP_BAD_REQUEST;
            }
        }
    }

    return ngx_http_send_special_response(r, clcf, err);
}


ngx_int_t
ngx_http_filter_finalize_request(ngx_http_request_t *r, ngx_module_t *m,
    ngx_int_t error)
{
    void       *ctx;
    ngx_int_t   rc;

    ngx_http_clean_header(r);

    ctx = NULL;

    if (m) {
        ctx = r->ctx[m->ctx_index];
    }

    /* clear the modules contexts */
    ngx_memzero(r->ctx, sizeof(void *) * ngx_http_max_module);

    if (m) {
        r->ctx[m->ctx_index] = ctx;
    }

    r->filter_finalize = 1;

    rc = ngx_http_special_response_handler(r, error);

    /* NGX_ERROR resets any pending data */

    switch (rc) {

    case NGX_OK:
    case NGX_DONE:
        return NGX_ERROR;

    default:
        return rc;
    }
}


void
ngx_http_clean_header(ngx_http_request_t *r)
{
    ngx_memzero(&r->headers_out.status,
                sizeof(ngx_http_headers_out_t)
                    - offsetof(ngx_http_headers_out_t, status));

    r->headers_out.headers.part.nelts = 0;
    r->headers_out.headers.part.next = NULL;
    r->headers_out.headers.last = &r->headers_out.headers.part;

    r->headers_out.trailers.part.nelts = 0;
    r->headers_out.trailers.part.next = NULL;
    r->headers_out.trailers.last = &r->headers_out.trailers.part;

    r->headers_out.content_length_n = -1;
    r->headers_out.last_modified_time = -1;
}


static ngx_int_t
ngx_http_send_error_page(ngx_http_request_t *r, ngx_http_err_page_t *err_page)
{
    ngx_int_t                  overwrite;
    ngx_str_t                  uri, args;
    ngx_uint_t                 err;
    ngx_table_elt_t           *location;
    ngx_http_core_loc_conf_t  *clcf;

    overwrite = err_page->overwrite;

    if (overwrite && overwrite != NGX_HTTP_OK) {
        r->expect_tested = 1;
    }

    if (overwrite >= 0) {

#if (NGX_HTTP_STATUS_VALIDATION)

        /*
         * A status an error_page directive supplies is chosen here, and it is
         * the second author of the error status of a request, beside the
         * handler whose returned status ngx_http_special_response_handler()
         * gates above; it writes after that gate has run, so it is gated in
         * its own right rather than left to whatever reads the status later.
         *
         * A zero overwrite carries no status: it is the "=" and "=0" form of
         * the directive, which keeps the status of what the request is
         * redirected to.
         *
         * The configuration and not the upstream chose this status, whatever
         * the origin of the response it replaces, so this gate is not scoped to
         * that origin as the gate above is and this status is never exempt: not
         * where it replaces the status of an intercepted upstream response, and
         * not where it names the very same number.  Were the number consulted
         * instead, "error_page 599 =599 /uri" for an upstream that answered 599
         * would pass as a relayed response, which it is not.
         *
         * Reported and never replaced, as at the gate above: the response
         * carries what the configuration asked for, so nothing that is sent
         * depends on this gate in any build.
         */

        if (overwrite
            && ngx_http_status_validate((ngx_uint_t) overwrite) != NGX_OK)
        {
            ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                          "unregistered HTTP status %ui",
                          (ngx_uint_t) overwrite);
        }

#endif

        r->err_status = overwrite;
    }

    if (ngx_http_complex_value(r, &err_page->value, &uri) != NGX_OK) {
        return NGX_ERROR;
    }

    if (uri.len && uri.data[0] == '/') {

        if (err_page->value.lengths) {
            ngx_http_split_args(r, &uri, &args);

        } else {
            args = err_page->args;
        }

        if (r->method != NGX_HTTP_HEAD) {
            r->method = NGX_HTTP_GET;
            r->method_name = ngx_http_core_get_method;
        }

        return ngx_http_internal_redirect(r, &uri, &args);
    }

    if (uri.len && uri.data[0] == '@') {
        return ngx_http_named_location(r, &uri);
    }

    r->expect_tested = 1;

    if (ngx_http_discard_request_body(r) != NGX_OK) {
        r->keepalive = 0;
    }

    location = ngx_list_push(&r->headers_out.headers);

    if (location == NULL) {
        return NGX_ERROR;
    }

    if (overwrite != NGX_HTTP_MOVED_PERMANENTLY
        && overwrite != NGX_HTTP_MOVED_TEMPORARILY
        && overwrite != NGX_HTTP_SEE_OTHER
        && overwrite != NGX_HTTP_TEMPORARY_REDIRECT
        && overwrite != NGX_HTTP_PERMANENT_REDIRECT)
    {
        r->err_status = NGX_HTTP_MOVED_TEMPORARILY;
    }

    location->hash = 1;
    location->next = NULL;
    ngx_str_set(&location->key, "Location");
    location->value = uri;

    ngx_http_clear_location(r);

    r->headers_out.location = location;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (clcf->msie_refresh && r->headers_in.msie) {
        return ngx_http_send_refresh(r);
    }

    /* the status here is one of the redirects, so it selects a 3XX row */

    err = ngx_http_error_page_index(r->err_status);

    return ngx_http_send_special_response(r, clcf, err);
}


static ngx_int_t
ngx_http_send_special_response(ngx_http_request_t *r,
    ngx_http_core_loc_conf_t *clcf, ngx_uint_t err)
{
    u_char       *tail;
    size_t        len;
    ngx_int_t     rc;
    ngx_buf_t    *b;
    ngx_uint_t    msie_padding;
    ngx_chain_t   out[3];

    if (clcf->server_tokens == NGX_HTTP_SERVER_TOKENS_ON) {
        len = sizeof(ngx_http_error_full_tail) - 1;
        tail = ngx_http_error_full_tail;

    } else if (clcf->server_tokens == NGX_HTTP_SERVER_TOKENS_BUILD) {
        len = sizeof(ngx_http_error_build_tail) - 1;
        tail = ngx_http_error_build_tail;

    } else {
        len = sizeof(ngx_http_error_tail) - 1;
        tail = ngx_http_error_tail;
    }

    msie_padding = 0;

    if (ngx_http_error_pages[err].len) {
        r->headers_out.content_length_n = ngx_http_error_pages[err].len + len;

        /*
         * The last test admits the rows that hold a 4XX or 5XX page, which are
         * the rows at or after the one that 400 selects, and so excludes both
         * the zero length row and the 3XX redirect rows.
         */

        if (clcf->msie_padding
            && (r->headers_in.msie || r->headers_in.chrome)
            && r->http_version >= NGX_HTTP_VERSION_10
            && err >= NGX_HTTP_ERROR_PAGE_4XX_ROW)
        {
            r->headers_out.content_length_n +=
                                         sizeof(ngx_http_msie_padding) - 1;
            msie_padding = 1;
        }

        r->headers_out.content_type_len = sizeof("text/html") - 1;
        ngx_str_set(&r->headers_out.content_type, "text/html");
        r->headers_out.content_type_lowcase = NULL;

    } else {
        r->headers_out.content_length_n = 0;
    }

    if (r->headers_out.content_length) {
        r->headers_out.content_length->hash = 0;
        r->headers_out.content_length = NULL;
    }

    ngx_http_clear_accept_ranges(r);
    ngx_http_clear_last_modified(r);
    ngx_http_clear_etag(r);

    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || r->header_only) {
        return rc;
    }

    if (ngx_http_error_pages[err].len == 0) {
        return ngx_http_send_special(r, NGX_HTTP_LAST);
    }

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_ERROR;
    }

    b->memory = 1;
    b->pos = ngx_http_error_pages[err].data;
    b->last = ngx_http_error_pages[err].data + ngx_http_error_pages[err].len;

    out[0].buf = b;
    out[0].next = &out[1];

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_ERROR;
    }

    b->memory = 1;

    b->pos = tail;
    b->last = tail + len;

    out[1].buf = b;
    out[1].next = NULL;

    if (msie_padding) {
        b = ngx_calloc_buf(r->pool);
        if (b == NULL) {
            return NGX_ERROR;
        }

        b->memory = 1;
        b->pos = ngx_http_msie_padding;
        b->last = ngx_http_msie_padding + sizeof(ngx_http_msie_padding) - 1;

        out[1].next = &out[2];
        out[2].buf = b;
        out[2].next = NULL;
    }

    if (r == r->main) {
        b->last_buf = 1;
    }

    b->last_in_chain = 1;

    return ngx_http_output_filter(r, &out[0]);
}


static ngx_int_t
ngx_http_send_refresh(ngx_http_request_t *r)
{
    u_char       *p, *location;
    size_t        len, size;
    uintptr_t     escape;
    ngx_int_t     rc;
    ngx_buf_t    *b;
    ngx_chain_t   out;

    len = r->headers_out.location->value.len;
    location = r->headers_out.location->value.data;

    escape = 2 * ngx_escape_uri(NULL, location, len, NGX_ESCAPE_REFRESH);

    size = sizeof(ngx_http_msie_refresh_head) - 1
           + escape + len
           + sizeof(ngx_http_msie_refresh_tail) - 1;

    r->err_status = NGX_HTTP_OK;

    r->headers_out.content_type_len = sizeof("text/html") - 1;
    ngx_str_set(&r->headers_out.content_type, "text/html");
    r->headers_out.content_type_lowcase = NULL;

    r->headers_out.location->hash = 0;
    r->headers_out.location = NULL;

    r->headers_out.content_length_n = size;

    if (r->headers_out.content_length) {
        r->headers_out.content_length->hash = 0;
        r->headers_out.content_length = NULL;
    }

    ngx_http_clear_accept_ranges(r);
    ngx_http_clear_last_modified(r);
    ngx_http_clear_etag(r);

    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || r->header_only) {
        return rc;
    }

    b = ngx_create_temp_buf(r->pool, size);
    if (b == NULL) {
        return NGX_ERROR;
    }

    p = ngx_cpymem(b->pos, ngx_http_msie_refresh_head,
                   sizeof(ngx_http_msie_refresh_head) - 1);

    if (escape == 0) {
        p = ngx_cpymem(p, location, len);

    } else {
        p = (u_char *) ngx_escape_uri(p, location, len, NGX_ESCAPE_REFRESH);
    }

    b->last = ngx_cpymem(p, ngx_http_msie_refresh_tail,
                         sizeof(ngx_http_msie_refresh_tail) - 1);

    b->last_buf = (r == r->main) ? 1 : 0;
    b->last_in_chain = 1;

    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}
