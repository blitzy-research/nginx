# Status Code Registry

Reference for the HTTP status codes that the status code registry in
`src/http/ngx_http_status.c` describes, with the reason phrase each one is
emitted with, the semantic flags it carries, and the specification section that
defines it.

The registry is the single place that holds this knowledge. It replaced four
mechanisms that had to be kept consistent by hand: the status constants in
`src/http/ngx_http_request.h`, the reason phrase table that was private to the
header filter, the error page table in `src/http/ngx_http_special_response.c`
with its own set of identically named offset macros holding different values,
and status class tests written out at each place that needed one.

Nothing a client can observe changed. Every status line, every error page body,
every `$status` value, and every HTTP/2 and HTTP/3 `:status` field are the bytes
nginx sent before. The registry describes a status; it never chooses one, and no
status code was added, renamed, or removed. All 45 `NGX_HTTP_*` status
constants remain in `src/http/ngx_http_request.h` unchanged, so a module written
against them needs no edit.

## Registry membership

The registry describes **48** status codes. Membership is the union of four
existing sources, so that no response that had a reason phrase or an error page
body before lost one: the 45 status constants in `src/http/ngx_http_request.h`
(44 distinct values, because 494 is defined twice under two names), the 36
reason phrases the header filter held, the error page bodies in
`src/http/ngx_http_special_response.c`, and the codes RFC 9110, section 15,
defines.

| Class | Codes | Count |
| ----- | ----- | ----- |
| 1xx informational | 100, 101, 102, 103 | 4 |
| 2xx successful | 200, 201, 202, 203, 204, 206 | 6 |
| 3xx redirection | 300, 301, 302, 303, 304, 307, 308 | 7 |
| 4xx client error | 400, 401, 402, 403, 404, 405, 406, 408, 409, 410, 411, 412, 413, 414, 415, 416, 421, 429 | 18 |
| 4xx nginx internal | 444, 494, 495, 496, 497, 499 | 6 |
| 5xx server error | 500, 501, 502, 503, 504, 505, 507 | 7 |
| **Total** | | **48** |

Codes the registry does not describe are still sent. A status nginx relays from
an upstream is never validated, because an upstream may answer with a code
nginx has never heard of, or with one below 100, and nginx's contract is to
relay what it received. A status a configuration names is not validated either:
the 306 that a `return` directive may ask for is sendable, and is sent, without
being a registry member.

Three codes are worth naming for what they are not, because each looks like an
omission and is not one.

**205** and **407** are not members. The reason phrase table held a slot for
each, but an empty one, and the error page table holds no body for either, so
nginx emits neither a reason phrase nor a body for them. Nothing in the registry
needs to know them. **203** is the contrast that explains the rule: it
has no reason phrase either, but RFC 9110, section 15.1, lists it among the
codes a cache may store heuristically, so the registry has to describe it for
`ngx_http_status_is_cacheable()` to answer for it. Membership follows from
something needing to be recorded about a code, not from the code being well
known.

**498** is not a member. It appears in `src/http/ngx_http_request.h` only as a
comment, `498 is the canceled code for the requests with invalid host name`, and
has no constant of its own. The error page table nevertheless holds a row for
it, and that row deliberately holds the 404 page, which is how nginx answers a
request whose host name is invalid. The error page rows follow the shape of that
table and not the membership of the registry.

## Registered status codes

The `reason` column is the fused `"NNN Phrase"` form, exactly the bytes that
follow `HTTP/1.1 ` in a status line; see
[the fused form](#the-fused-nnn-phrase-form) for why the digits appear twice.
`_none_` means the row carries a zero length reason and the code is emitted as
three digits; see
[wire reason phrases](#wire-reason-phrases). Flag names are shown without their
`NGX_HTTP_STATUS_` prefix. The `rfc_section` strings are quoted exactly as the
code spells them, which is why they read `section` rather than `§`, and why
eight of them carry the name RFC 9110 recommends in parentheses.

| Code | `reason` | Flags | `rfc_section` |
| ---- | -------- | ----- | ------------- |
| 100 | _none_ | `INFORMATIONAL` | `RFC 9110 section 15.2.1` |
| 101 | _none_ | `INFORMATIONAL` | `RFC 9110 section 15.2.2` |
| 102 | _none_ | `INFORMATIONAL` | `RFC 2518 section 10.1` |
| 103 | _none_ | `INFORMATIONAL` | `RFC 8297 section 2` |
| 200 | `200 OK` | `CACHEABLE`, `EXPIRES_OK` | `RFC 9110 section 15.3.1` |
| 201 | `201 Created` | `EXPIRES_OK` | `RFC 9110 section 15.3.2` |
| 202 | `202 Accepted` | _none_ | `RFC 9110 section 15.3.3` |
| 203 | _none_ | `CACHEABLE` | `RFC 9110 section 15.3.4` |
| 204 | `204 No Content` | `CACHEABLE`, `EXPIRES_OK` | `RFC 9110 section 15.3.5` |
| 206 | `206 Partial Content` | `CACHEABLE`, `EXPIRES_OK` | `RFC 9110 section 15.3.7` |
| 300 | _none_ | `CACHEABLE` | `RFC 9110 section 15.4.1` |
| 301 | `301 Moved Permanently` | `CACHEABLE`, `EXPIRES_OK` | `RFC 9110 section 15.4.2` |
| 302 | `302 Moved Temporarily` | `EXPIRES_OK` | `RFC 9110 section 15.4.3 (Found)` |
| 303 | `303 See Other` | `EXPIRES_OK` | `RFC 9110 section 15.4.4` |
| 304 | `304 Not Modified` | `EXPIRES_OK` | `RFC 9110 section 15.4.5` |
| 307 | `307 Temporary Redirect` | `EXPIRES_OK` | `RFC 9110 section 15.4.8` |
| 308 | `308 Permanent Redirect` | `CACHEABLE`, `EXPIRES_OK` | `RFC 9110 section 15.4.9` |
| 400 | `400 Bad Request` | `CLIENT_ERROR` | `RFC 9110 section 15.5.1` |
| 401 | `401 Unauthorized` | `CLIENT_ERROR` | `RFC 9110 section 15.5.2` |
| 402 | `402 Payment Required` | `CLIENT_ERROR` | `RFC 9110 section 15.5.3` |
| 403 | `403 Forbidden` | `CLIENT_ERROR` | `RFC 9110 section 15.5.4` |
| 404 | `404 Not Found` | `CLIENT_ERROR`, `CACHEABLE` | `RFC 9110 section 15.5.5` |
| 405 | `405 Not Allowed` | `CLIENT_ERROR`, `CACHEABLE` | `RFC 9110 section 15.5.6 (Method Not Allowed)` |
| 406 | `406 Not Acceptable` | `CLIENT_ERROR` | `RFC 9110 section 15.5.7` |
| 408 | `408 Request Time-out` | `CLIENT_ERROR` | `RFC 9110 section 15.5.9 (Request Timeout)` |
| 409 | `409 Conflict` | `CLIENT_ERROR` | `RFC 9110 section 15.5.10` |
| 410 | `410 Gone` | `CLIENT_ERROR`, `CACHEABLE` | `RFC 9110 section 15.5.11` |
| 411 | `411 Length Required` | `CLIENT_ERROR` | `RFC 9110 section 15.5.12` |
| 412 | `412 Precondition Failed` | `CLIENT_ERROR` | `RFC 9110 section 15.5.13` |
| 413 | `413 Request Entity Too Large` | `CLIENT_ERROR` | `RFC 9110 section 15.5.14 (Content Too Large)` |
| 414 | `414 Request-URI Too Large` | `CLIENT_ERROR`, `CACHEABLE` | `RFC 9110 section 15.5.15 (URI Too Long)` |
| 415 | `415 Unsupported Media Type` | `CLIENT_ERROR` | `RFC 9110 section 15.5.16` |
| 416 | `416 Requested Range Not Satisfiable` | `CLIENT_ERROR` | `RFC 9110 section 15.5.17 (Range Not Satisfiable)` |
| 421 | `421 Misdirected Request` | `CLIENT_ERROR` | `RFC 9110 section 15.5.20` |
| 429 | `429 Too Many Requests` | `CLIENT_ERROR` | `RFC 6585 section 4` |
| 444 | _none_ | `CLIENT_ERROR`, `INTERNAL` | `nginx internal` |
| 494 | _none_ | `CLIENT_ERROR`, `INTERNAL` | `nginx internal` |
| 495 | _none_ | `CLIENT_ERROR`, `INTERNAL` | `nginx internal` |
| 496 | _none_ | `CLIENT_ERROR`, `INTERNAL` | `nginx internal` |
| 497 | _none_ | `CLIENT_ERROR`, `INTERNAL` | `nginx internal` |
| 499 | _none_ | `CLIENT_ERROR`, `INTERNAL` | `nginx internal` |
| 500 | `500 Internal Server Error` | `SERVER_ERROR` | `RFC 9110 section 15.6.1` |
| 501 | `501 Not Implemented` | `SERVER_ERROR`, `CACHEABLE` | `RFC 9110 section 15.6.2` |
| 502 | `502 Bad Gateway` | `SERVER_ERROR` | `RFC 9110 section 15.6.3` |
| 503 | `503 Service Temporarily Unavailable` | `SERVER_ERROR` | `RFC 9110 section 15.6.4 (Service Unavailable)` |
| 504 | `504 Gateway Time-out` | `SERVER_ERROR` | `RFC 9110 section 15.6.5 (Gateway Timeout)` |
| 505 | `505 HTTP Version Not Supported` | `SERVER_ERROR` | `RFC 9110 section 15.6.6` |
| 507 | `507 Insufficient Storage` | `SERVER_ERROR` | `RFC 4918 section 11.5` |

## Wire reason phrases

Only **36** of the 48 registered codes carry a reason phrase. The other **12**
carry a zero length one, and that is the design rather than a gap:

**100, 101, 102, 103, 203, 300, 444, 494, 495, 496, 497, 499.**

A status with no phrase to copy is emitted as three digits followed by a space,
which is what nginx emitted for these codes before the registry existed. The
header filter reserves `NGX_INT_T_LEN + 1 /* SP */` for it and writes it with
`ngx_sprintf(b->last, "%03ui ", status)`. The trailing space is part of those
bytes and is kept.

Which codes those are follows from the table the registry replaced, whose four
per class branches covered `200 <= status < 207`, `301 <= status < 309`,
`400 <= status < 430`, and `500 <= status < 508`:

- the 1xx codes fell below every branch;
- **300** had no slot at all, being commented out, and the 3xx branch began at
  301;
- **203** had a slot, but an empty one;
- **444, 494, 495, 496, 497** and **499** fell in the gap between the 4xx and
  5xx branches.

Two 1xx status lines nginx does send come from their own constants and are not
registry driven, so their absence from the table above is not a contradiction:
`HTTP/1.1 100 Continue` is written by `src/http/ngx_http_request_body.c`, which
sends it with a double CRLF because it is a complete interim response, and
`HTTP/1.1 103 Early Hints` is `ngx_http_early_hints_status_line` in
`src/http/ngx_http_header_filter_module.c`, used only by
`ngx_http_send_early_hints()`. Both carry the `HTTP/1.1 ` prefix and a trailing
CRLF, so neither has the shape of a `reason` member, and both were left where
they were.

### Unregistered is not the same as phraseless

`ngx_http_status_reason()` distinguishes the two cases, and a caller should not
collapse them:

- a **registered** code returns a pointer to the row's `reason`, which may be an
  `ngx_str_t` of zero length;
- an **unregistered** code returns `NULL`.

Both end in the same bytes on the wire, through two fallback branches that the
header filter keeps separately, but they are different answers to different
questions. The first says the registry describes this code and has no phrase for
it; the second says the registry does not describe it at all.

## The fused `"NNN Phrase"` form

The `reason` member holds the number and the phrase together, as `200 OK` rather
than `OK`. The digits therefore appear twice for a registered code, once in
`code` and once at the front of `reason`. That is deliberate.

The bytes in `reason` are exactly the bytes that follow `HTTP/1.1 ` in a status
line, so the header filter emits a status line with a single copy:

```c
b->last = ngx_cpymem(b->last, "HTTP/1.1 ", sizeof("HTTP/1.x ") - 1);

/* status line */
if (status_line) {
    b->last = ngx_copy(b->last, status_line->data, status_line->len);

} else {
    b->last = ngx_sprintf(b->last, "%03ui ", status);
}
```

Holding the phrase alone would mean either a second copy or a conversion for
every response, in place of that one `ngx_copy`. The duplicated digits are what
keep it. They are not a modeling mistake, and removing them would change the
emission path.

## Reason phrases that differ from RFC 9110

Eight of nginx's reason phrases differ from the name RFC 9110 recommends. They
are kept exactly as nginx has always sent them:

| Code | `reason` sent on the wire | Name RFC 9110 recommends |
| ---- | ------------------------- | ------------------------ |
| 302 | `302 Moved Temporarily` | Found |
| 405 | `405 Not Allowed` | Method Not Allowed |
| 408 | `408 Request Time-out` | Request Timeout |
| 413 | `413 Request Entity Too Large` | Content Too Large |
| 414 | `414 Request-URI Too Large` | URI Too Long |
| 416 | `416 Requested Range Not Satisfiable` | Range Not Satisfiable |
| 503 | `503 Service Temporarily Unavailable` | Service Unavailable |
| 504 | `504 Gateway Time-out` | Gateway Timeout |

RFC 9110, section 15.1, permits this: a reason phrase is a recommendation, and a
client is not expected to examine it. That is what lets the registry hold to the
specification and to nginx's existing output at the same time. The `reason`
member carries the bytes nginx sends, and the name from the specification is
recorded in `rfc_section`, in parentheses after the section number, for the
eight codes where the two differ.

**Changing any of these to the name in the specification would be a regression,
not a correction.** These bytes are what clients, proxies, and test suites have
matched on for as long as nginx has sent them, and keeping them byte for byte is
the point of the exercise.

A related pair is easy to misread. The compiled in error page for 302 in
`src/http/ngx_http_special_response.c` reads `<title>302 Found</title>` and
`<h1>302 Found</h1>`, using the name from the specification, while the reason
phrase on the same response reads `302 Moved Temporarily`. These are two
independent byte streams that have differed for a long time. Neither is the
correct one that the other should be made to match; both are frozen.

## Semantic flags

Six flags occupy the low six bits of the `flags` member, which leaves the rest
free for anything a later change needs to record without altering the layout.
They are tested in place of the class arithmetic and the open coded status
comparisons that used to be written out at each site that needed one.

| Flag | Value | Membership | Count |
| ---- | ----- | ---------- | ----- |
| `NGX_HTTP_STATUS_CACHEABLE` | `0x0001` | 200, 203, 204, 206, 300, 301, 308, 404, 405, 410, 414, 501 | 12 |
| `NGX_HTTP_STATUS_INFORMATIONAL` | `0x0002` | 100, 101, 102, 103 | 4 |
| `NGX_HTTP_STATUS_CLIENT_ERROR` | `0x0004` | every 4xx code, the six nginx internal ones included | 24 |
| `NGX_HTTP_STATUS_SERVER_ERROR` | `0x0008` | every 5xx code | 7 |
| `NGX_HTTP_STATUS_EXPIRES_OK` | `0x0010` | 200, 201, 204, 206, 301, 302, 303, 304, 307, 308 | 10 |
| `NGX_HTTP_STATUS_INTERNAL` | `0x0020` | 444, 494, 495, 496, 497, 499 | 6 |

`CLIENT_ERROR` covers 24 rows because the six nginx internal codes are 4xx codes
and are flagged as such; see
[nginx's own status codes](#nginxs-own-status-codes).

## `CACHEABLE` is not `EXPIRES_OK`

These two flags answer different questions and have different membership.
Treating either as the other changes which responses receive an `Expires`
header. This is the one thing on this page most worth reading before making a
change.

- `NGX_HTTP_STATUS_CACHEABLE` answers **would RFC 9110, section 15.1, permit a
  cache to store this response heuristically?** Its membership is exactly the 12
  codes that section lists: **200, 203, 204, 206, 300, 301, 308, 404, 405, 410,
  414, 501**.
- `NGX_HTTP_STATUS_EXPIRES_OK` answers **is this response eligible for the
  expires processing of the `expires` directive?** Its membership is nginx's
  own, narrower set of 10 codes: **200, 201, 204, 206, 301, 302, 303, 304, 307,
  308**. It was lifted from a ten case switch that appeared twice in
  `src/http/modules/ngx_http_headers_filter_module.c`, with the same ten labels
  in both copies, and is now reached through `ngx_http_status_expires_ok()`.

Neither set contains the other. They differ on **12** codes, in both directions:

| | Codes | Count |
| --- | ----- | ----- |
| `EXPIRES_OK` but **not** `CACHEABLE` | 201, 302, 303, 304, 307 | 5 |
| `CACHEABLE` but **not** `EXPIRES_OK` | 203, 300, 404, 405, 410, 414, 501 | 7 |

Only five codes carry both: 200, 204, 206, 301 and 308.

**Testing `ngx_http_status_is_cacheable()` where `EXPIRES_OK` is meant would
change which responses receive `Expires` and `Cache-Control` headers.** A 302
would stop being eligible and a 404 would start being eligible, which is a
change a client can see. The two flags exist so that the two questions stay
apart, and both `src/http/ngx_http_status.h` and `src/http/ngx_http_status.c`
carry a comment saying so where each is defined.

`CACHEABLE` is metadata and nothing more. **It drives no caching decision.**
What nginx caches is decided by configuration: `ngx_http_file_cache_valid()`
reads the list a `proxy_cache_valid` directive built, and where that directive
is given only a time, the set it applies is `{ 200, 301, 302 }` — which is
neither of the two flag sets above. That parser also accepts any status from 100
through 599, so it is not bounded by registry membership either. RFC 9111,
section 4.2.2, likewise makes heuristic freshness a permission and not an
instruction, so a flag recording it cannot be an instruction to cache.

One nuance follows from the membership: **203** carries `CACHEABLE`, because
section 15.1 lists it, while having no reason phrase at all. A registered row
with a zero length `reason` is normal, and 203 is the reason the code is
registered rather than left out.

## nginx's own status codes

Six codes have no standing outside nginx. They are internal signals rather than
statuses chosen for a response, and they are load bearing, so the registry
describes them with `NGX_HTTP_STATUS_INTERNAL` and an `rfc_section` of
`nginx internal`. Strict validation, which a build enables with
`--with-http_status_validation` and which is off unless it is asked for, accepts
them and does not report them.

| Code | Constant | What it signals |
| ---- | -------- | --------------- |
| 444 | `NGX_HTTP_CLOSE` | close the connection without any response |
| 494 | `NGX_HTTP_NGINX_CODES`, `NGX_HTTP_REQUEST_HEADER_TOO_LARGE` | a request header too large; the value is defined twice, under two names |
| 495 | `NGX_HTTPS_CERT_ERROR` | an error in the client certificate |
| 496 | `NGX_HTTPS_NO_CERT` | the client sent no certificate |
| 497 | `NGX_HTTP_TO_HTTPS` | a plain HTTP request sent to an HTTPS port, kept distinct from 4xx so that an error page redirection can tell them apart |
| 499 | `NGX_HTTP_CLIENT_CLOSED_REQUEST` | the client closed the connection before nginx tried to send the response header |

494 is one registry row even though two constants name it, because the registry
is keyed by the value and not by the name. Both names continue to work.

These codes are largely consumed rather than sent. When an error page body is
selected, 494, 495, 496 and 497 are rewritten to 400, and 444 and 499 select a
zero length body, so what reaches a client is either a 400 response or no
response at all. `NGX_HTTP_STATUS_INTERNAL` is what tells strict validation
that such a code is deliberate, so that a code with no RFC section is not
reported as though it were an oversight.

## Codes that are easy to leave out

Seven codes nginx emits are easy to leave out of a registry assembled from any
single source: **402, 406, 410, 411, 412, 421** and **507**. Each has a reason
phrase that nginx has always sent, and each would have fallen back to three
digits and a space had it been left out.

Two different sources would each have missed some of them, which is why
membership is the union of four rather than a list from one place:

- **402, 406** and **410** have **no constant** in
  `src/http/ngx_http_request.h` at all. They existed only as rows of the reason
  phrase table, so a registry built from the constant block would not have known
  them, even though nginx emits a phrase for each.
- **507** is not an RFC 9110 code, so a registry built from that specification's
  section 15 would not have known it either. It comes from RFC 4918.

The remaining three — **411, 412** and **421** — have both a constant and an
RFC 9110 section, and are named here only because they are among the codes most
easily forgotten when this set is written out by hand.

Four codes are not defined by RFC 9110, and their `rfc_section` records where
they do come from:

| Code | `rfc_section` | Note |
| ---- | ------------- | ---- |
| 102 | `RFC 2518 section 10.1` | RFC 4918 removed 102, and RFC 9110 does not define it, so RFC 2518 is the only place that does |
| 103 | `RFC 8297 section 2` | |
| 429 | `RFC 6585 section 4` | outside RFC 9110's own numbering |
| 507 | `RFC 4918 section 11.5` | |

Every other code cites RFC 9110, section 15.x, and the six nginx internal codes
read `nginx internal`. No section number is recorded that its specification does
not actually carry.

## HTTP/2 and HTTP/3

`:status` is a numeric pseudo header in both HPACK and QPACK, so **neither
protocol ever emits a reason phrase** and the `reason` member is unused on those
paths. No phrase can regress over HTTP/2 or HTTP/3, whatever a row holds.

HTTP/2 encodes seven statuses as a static table index — **200, 204, 206, 304,
400, 404** and **500** — and writes any other status as a three byte literal
with `%03ui`, in `src/http/v2/ngx_http_v2_filter_module.c`. HTTP/3 writes a
three byte QPACK literal the same way, in
`src/http/v3/ngx_http_v3_filter_module.c`. Both reserve exactly three bytes for
the digits, which is why a status must be below 1000 before it reaches a
response; `NGX_HTTP_STATUS_WIRE_MAX` and `ngx_http_status_wire_width_ok()` are
that bound, and it is enforced in every build rather than only under strict
validation.

## The record type, the range, and the footprint

Each row is one `ngx_http_status_def_t`, declared in
`src/http/ngx_http_status.h`:

```c
typedef struct {
    ngx_uint_t   code;
    ngx_str_t    reason;
    ngx_uint_t   flags;
    const char  *rfc_section;
} ngx_http_status_def_t;
```

The type is `ngx_http_status_def_t`, with the `_def_` in the middle.
`ngx_http_status_t` is a different and unrelated type: it is the state a status
line parser keeps, declared in `src/http/ngx_http.h` and used by
`ngx_http_parse_status_line()` and the upstream modules that call it, in 12
places across the tree. It also has a member named `code`, so the two are easy
to confuse and a mistake between them may compile further than it should.

### Range

| Constant | Value | Meaning |
| -------- | ----- | ------- |
| `NGX_HTTP_STATUS_MIN` | 100 | lowest code the registry may describe, inclusive |
| `NGX_HTTP_STATUS_MAX` | 600 | upper bound, **exclusive**; 599 is the highest code, 600 is not a code |
| `NGX_HTTP_STATUS_RANGE` | 500 | the width of that range, which also sizes the lookup index |

`ngx_http_status_in_range(s)` is the range test. It is a parameterized macro and
not a function because the sources are ANSI C, which has no keyword asking for a
function to be expanded at its call site, and because a function with internal
linkage defined in a header would be reported as unused in every translation
unit that does not call it, which is fatal when warnings are errors. The NGINX
development guide's convention of naming macro constants in uppercase and
parameterized macros in lowercase is what makes the lowercase name the idiomatic
one here rather than an exception. As with other nginx range macros the argument
is expanded more than once, so it must not have side effects.

### Footprint

The registry is static data written only while a configuration is parsed, which
is strictly before workers fork, and read only afterwards.

| Component | Arithmetic | LP64 | ILP32 |
| --------- | ---------- | ---- | ----- |
| `ngx_http_status_defs[]` | `NGX_HTTP_STATUS_MAX_DEFS` (64) rows | 2,560 bytes | 1,280 bytes |
| the lookup index | `NGX_HTTP_STATUS_RANGE` (500) entries of `u_short` | 1,000 bytes | 1,000 bytes |
| **Total** | | **3,560 bytes** | **2,280 bytes** |

One row measures 40 bytes on LP64 and 20 on ILP32. Of the 64 rows, one is a
reserved sentinel and 48 hold the built-in definitions, which leaves 15 for
`ngx_http_status_register()`. Row zero is the sentinel so that a zero in the
lookup index means "not registered" without a second array recording which codes
are present.

Because both arrays are written before workers fork and only read afterwards,
copy on write is never triggered for them and a worker's private memory does not
grow. Nothing is allocated per request.

### Querying the registry

Two of the five functions declared in `src/http/ngx_http.h` read the metadata on
this page:

```c
const ngx_str_t *ngx_http_status_reason(ngx_uint_t status);
ngx_uint_t ngx_http_status_is_cacheable(ngx_uint_t status);
```

`ngx_http_status_reason()` returns a pointer to the row's `reason` and never to
the row itself, so a caller can reach neither `flags` nor `rfc_section` through
it and cannot alter a row. It returns `NULL` for an unregistered code and never
for a registered row whose phrase is empty, as
[explained above](#unregistered-is-not-the-same-as-phraseless).
`ngx_http_status_is_cacheable()` returns zero for an unregistered code, which is
the conservative answer.

The other three functions set, validate, and extend the registry.
`ngx_http_status_expires_ok()`, declared in `src/http/ngx_http_status.h`,
answers the `EXPIRES_OK` question. How to call any of them, when
`ngx_http_status_register()` is accepted and when it is refused, and how the
strict build behaves are covered in the migration guide rather than here.

## See also

- The Status Code API migration guide, `docs/migration/status_code_api.md` — the
  five functions, the registry's lifecycle, and how to convert a direct
  assignment to `r->headers_out.status`
- [Documentation home](../index.md)
