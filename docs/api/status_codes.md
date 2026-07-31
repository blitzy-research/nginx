# Status Code Registry

Reference for the HTTP status codes the registry in
`src/http/ngx_http_status.c` describes: the wire reason phrase a code carries,
where it carries one, the semantic flags it is described with, and the
specification section that defines it.

The registry is the single place that holds this knowledge. What it consolidated
is five pieces of logic that had to be kept consistent by hand: the reason
phrase lookup that was private to the header filter, the error page index
arithmetic in `src/http/ngx_http_special_response.c`, the selection of the
effective `$status` value that the log module and the variable evaluator each
wrote out for themselves, the `expires` eligibility switch that appeared twice
in `src/http/modules/ngx_http_headers_filter_module.c`, and the setting of a
response status, which now goes through `ngx_http_status_set()` wherever nginx
chooses the status itself. Two sets of identically named offset macros holding
different values went with them.

What the registry did not absorb stays where it was. The status constants in
`src/http/ngx_http_request.h` remain, unchanged, for compatibility. The compiled
in error page bodies remain in `src/http/ngx_http_special_response.c`; the
registry supplies only the row of that table a status selects, and never the
HTML. Status comparisons that decide behavior rather than describe a status —
the 204 and 304 handling in the header filter, the keepalive and lingering
close rules of the special response handler — are unchanged too, and are still
written out where the decision is made.

For every status a response can carry, nothing a client can observe changed:
each status line, each error page body, each `$status` value, and each HTTP/2
and HTTP/3 `:status` field is the bytes nginx sent before. The registry
describes a status; it never chooses one, and no status code was added,
renamed, or removed.

Which statuses a response can carry did change, in one deliberate way. A status
of four digits or more is refused rather than sent, because HTTP/2 and HTTP/3
reserve exactly three bytes for the digits of `:status` and such a status
overran the field it was written into. The bound holds in every build, not only
under strict validation, and is applied at each place that can ask for a status;
see [HTTP/2 and HTTP/3](#http2-and-http3) for the constant and the list of
places.

All 45 status constants remain in `src/http/ngx_http_request.h` unchanged, so a
module written against them needs no edit. Forty-three of them are named
`NGX_HTTP_*`; the remaining two are `NGX_HTTPS_CERT_ERROR` and
`NGX_HTTPS_NO_CERT`.

## Registry membership

The registry describes **48** status codes. Membership follows from something
needing to be recorded about a code, not from the code being well known, and
three sources account for all 48 — so that no response that had a reason
phrase or an error page body before lost one:

- the **44** distinct values of the 45 status constants in
  `src/http/ngx_http_request.h`; 44 rather than 45 because 494 is defined twice,
  under two names;
- **402**, **406** and **410**, which have a reason phrase nginx has always sent
  but no constant of their own;
- **203**, which has neither, and is described because RFC 9110, section 15.1,
  lists it among the codes a cache may store heuristically, so
  `ngx_http_status_is_cacheable()` has to be able to answer for it.

44 + 3 + 1 = 48. Being a code that RFC 9110, section 15, defines is not by
itself a reason to be a member: 205 and 407 are section 15 codes and are not
described, because nothing about either of them needs recording.

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
an upstream is never examined at all, because an upstream may answer with a code
nginx has never heard of, or with one below 100, and nginx's contract is to
relay what it received. Which statuses those are is known from the authorship
recorded on the request as a response crosses the upstream boundary, and never
from a status having some particular value: two authors may choose the same
number for one request, so equal numbers would not prove equal authorship.

A status nginx or a configuration chose is a different matter, and what becomes
of it depends on the build. The build everyone runs sends it and reports
nothing: the 306 that a `return` directive may ask for is sendable, and is sent,
without being a registry member. A build configured with
`--with-http_status_validation` sends it as well wherever it arrives with no
caller to answer to, reporting it as an alert and never replacing it. The one
place that build refuses such a status is `ngx_http_status_set()`, which has a
caller with a result to act on. See
[strict validation](#strict-validation) for the whole of that contract.

What a build configured with `--with-http_status_validation` adds is a report
and nothing else. A status that nginx or a configuration chose and that the
registry does not describe is written to the error log once for the request, as
`unregistered HTTP status 306`, and the response still carries that status. A
status nginx relays from an upstream is exempt from the report, because an
upstream may answer with a code nginx has never heard of, or with one below 100,
and nginx's contract is to relay what it received. Refusal is reserved for the
one case that has nothing to do with membership: a status too wide for the wire
is refused in every build, strict or not.

Four codes are worth naming for what they are and are not, because each looks
either like an omission or like an inclusion that wants explaining.

**205** and **407** are not members. nginx emits neither a reason phrase nor an
error page body for either, so nothing in the registry needs to know them.
**203** is the contrast that explains the rule: it has no reason phrase either,
but RFC 9110, section 15.1, lists it among the codes a cache may store
heuristically, so the registry has to describe it for
`ngx_http_status_is_cacheable()` to answer for it. Membership follows from
something needing to be recorded about a code, not from the code being well
known.

**498** is not a member. It appears in `src/http/ngx_http_request.h` only as a
comment, `498 is the canceled code for the requests with invalid host name`, and
has no constant of its own. The error page table nevertheless keeps a row for
it, and that retained row reuses the 404 page rather than carrying a page of its
own. No current code path emits 498 — nothing in the tree sets it, returns it,
or compares against it — so the row is never selected. It is kept because the
error page rows follow the shape of that table, which is a span of consecutive
codes, and not the membership of the registry.

## Registered status codes

The `reason` column is the fused `"NNN Phrase"` form, exactly the bytes that
follow `HTTP/1.1 ` in a status line; see
[the fused form](#the-fused-nnn-phrase-form) for why the digits appear twice.
`_none_` means the row carries a zero-length reason and the code is emitted as
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

Some described codes carry no reason phrase; the table above reads `_none_` for
each. A status with no phrase to copy is emitted as three digits followed by a
space, and that trailing space is part of the bytes a client receives.

**100, 101, 102, 103, 203, 300, 444, 494, 495, 496, 497, 499.**

In an HTTP/1.x status line, a status with no phrase to copy is emitted as three
digits followed by a space, which is what nginx emitted for these codes before
the registry existed. The header filter reserves `NGX_INT_T_LEN + 1 /* SP */`
for it and writes it with `ngx_sprintf(b->last, "%03ui ", status)`. The trailing
space is part of those bytes and is kept. HTTP/2 and HTTP/3 are not concerned by
any of this: their `:status` is numeric, carries no phrase for any code, and has
no trailing space.

Two 1xx status lines nginx does send come from constants of their own and are
not registry driven, so their absence from the table above is not a
contradiction: `HTTP/1.1 100 Continue` and `HTTP/1.1 103 Early Hints` each carry
the `HTTP/1.1 ` prefix and a trailing CRLF, so neither has the shape of a
`reason` member.

### Unregistered is not the same as phraseless

`ngx_http_status_reason()` distinguishes the two cases, and a caller should not
collapse them:

- a **registered** code returns a pointer to the row's `reason`, which may be an
  `ngx_str_t` of zero length;
- an **unregistered** code returns `NULL`.

In an HTTP/1.x status line both end in the same bytes — three digits and a
space — through two fallback branches that the header filter keeps separately,
and over HTTP/2 and HTTP/3 the question does not arise at all, no phrase being
emitted for any code. They are still different answers to different questions.
The first says the registry describes this code and has no phrase for it; the
second says the registry does not describe it at all.

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

Eight of nginx's reason phrases differ from the name RFC 9110 recommends. The
bytes nginx sends are:

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
not a correction.** These bytes are what clients, proxies, and test suites match
on, and they are kept byte for byte.

A related pair is easy to misread. The compiled in error page for 302 in
`src/http/ngx_http_special_response.c` reads `<title>302 Found</title>` and
`<h1>302 Found</h1>`, using the name from the specification, while the reason
phrase on the same response reads `302 Moved Temporarily`. These are two
independent byte streams. Neither is the correct one that the other should be
made to match; both are frozen.

## Semantic flags

Six flags occupy the low six bits of the `flags` member, which leaves the rest
free for anything a later change needs to record without altering the layout.

Two of the six are read at run time, and they are the only ones any code tests.
`CACHEABLE` is what `ngx_http_status_is_cacheable()` answers with, and
`EXPIRES_OK` is what `ngx_http_status_expires_ok()` answers with; each helper
looks a code up and returns a single mask over its own flag, and those two masks
are every read of a `flags` member there is.

The other four — `INFORMATIONAL`, `CLIENT_ERROR`, `SERVER_ERROR` and
`INTERNAL` — are descriptive metadata. They record what class a code belongs to
and where it comes from, and no consumer outside the registry table reads them.
They are recorded because the class of a code is status code knowledge and
belongs with the rest of it, and because a row that describes a code should
describe it completely; they are not a class test that was moved behind a flag.

| Flag | Value | Membership |
| ---- | ----- | ---------- |
| `NGX_HTTP_STATUS_CACHEABLE` | `0x0001` | 200, 203, 204, 206, 300, 301, 308, 404, 405, 410, 414, 501 |
| `NGX_HTTP_STATUS_INFORMATIONAL` | `0x0002` | 100, 101, 102, 103 |
| `NGX_HTTP_STATUS_CLIENT_ERROR` | `0x0004` | every registered 4xx row, the six nginx internal ones included |
| `NGX_HTTP_STATUS_SERVER_ERROR` | `0x0008` | every registered 5xx row |
| `NGX_HTTP_STATUS_EXPIRES_OK` | `0x0010` | 200, 201, 204, 206, 301, 302, 303, 304, 307, 308 |
| `NGX_HTTP_STATUS_INTERNAL` | `0x0020` | 444, 494, 495, 496, 497, 499 |

## `CACHEABLE` is not `EXPIRES_OK`

These two flags answer different questions and have different membership.
Treating either as the other changes which responses receive an `Expires`
header. This is the one thing on this page most worth reading before making a
change.

- `NGX_HTTP_STATUS_CACHEABLE` answers **would RFC 9110, section 15.1, permit a
  cache to store this response heuristically?** Its membership is exactly the
  codes that section lists: **200, 203, 204, 206, 300, 301, 308, 404, 405, 410,
  414, 501**.
- `NGX_HTTP_STATUS_EXPIRES_OK` answers **is this response eligible for the
  expires processing of the `expires` directive?** Its membership is nginx's
  own, narrower set: **200, 201, 204, 206, 301, 302, 303, 304, 307, 308**. It is
  reached through `ngx_http_status_expires_ok()`.

Neither set contains the other:

| | Codes |
| --- | ----- |
| `EXPIRES_OK` but **not** `CACHEABLE` | 201, 302, 303, 304, 307 |
| `CACHEABLE` but **not** `EXPIRES_OK` | 203, 300, 404, 405, 410, 414, 501 |

Only 200, 204, 206, 301 and 308 carry both.

**Testing `ngx_http_status_is_cacheable()` where `EXPIRES_OK` is meant would
change which responses receive `Expires` and `Cache-Control` headers.** A 302
would stop being eligible and a 404 would start being eligible, which is a
change a client can see. The two flags exist so that the two questions stay
apart.

`CACHEABLE` is metadata and nothing more. **It drives no caching decision.**
What nginx caches is decided by configuration, and the set a `proxy_cache_valid`
directive applies is neither of the two flag sets above and is not bounded by
registry membership. RFC 9111, section 4.2.2, likewise makes heuristic freshness
a permission and not an instruction, so a flag recording it cannot be an
instruction to cache.

One nuance follows from the membership: **203** carries `CACHEABLE`, because
section 15.1 lists it, while having no reason phrase at all. A registered row
with a zero-length `reason` is normal, and 203 is the reason the code is
registered rather than left out.

## nginx's own status codes

Six codes have no standing outside nginx. They are internal signals rather than
statuses chosen for a response, and they are load-bearing, so the registry
describes them with `NGX_HTTP_STATUS_INTERNAL` and an `rfc_section` of
`nginx internal`. Being described is what makes strict validation, which a build
enables with `--with-http_status_validation` and which is off unless it is asked
for, pass over them without a report.

| Code | Constant | What it signals |
| ---- | -------- | --------------- |
| 444 | `NGX_HTTP_CLOSE` | close the connection without any response |
| 494 | `NGX_HTTP_NGINX_CODES`, `NGX_HTTP_REQUEST_HEADER_TOO_LARGE` | a request header too large; the value is defined twice, under two names |
| 495 | `NGX_HTTPS_CERT_ERROR` | an error in the client certificate |
| 496 | `NGX_HTTPS_NO_CERT` | the client sent no certificate |
| 497 | `NGX_HTTP_TO_HTTPS` | a plain HTTP request sent to an HTTPS port; it is a registered client error row like any other, and is told apart from an ordinary 400 while an error page is selected |
| 499 | `NGX_HTTP_CLIENT_CLOSED_REQUEST` | the client closed the connection before nginx tried to send the response header |

494 is one registry row even though two constants name it, because the registry
is keyed by the value and not by the name. Both names continue to work.

These codes are largely consumed rather than sent. When an error page body is
selected, 494, 495, 496 and 497 are rewritten to 400, and 444 and 499 select a
zero length body, so what reaches a client is either a 400 response or no
response at all.

Strict validation is silent about them because each one is a registry row, and a
row being present is all that validation tests: `ngx_http_status_validate()`
answers whether the registry describes a code, and nothing else about it.
`NGX_HTTP_STATUS_INTERNAL` records where a code comes from — it is the
counterpart of an `rfc_section` that names a specification — and it implements
no part of the validation policy. Clearing the flag would not make these codes
reported; removing their rows would.

## Codes with no constant of their own

**203, 402, 406** and **410** have no `NGX_HTTP_*` constant in
`src/http/ngx_http_request.h`. A caller names such a code by value, and the
registry describes it like any other row, so each keeps the reason phrase or the
flag recorded for it.

## Codes not defined by RFC 9110

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

`:status` is a numeric pseudo-header in both HPACK and QPACK, so **neither
protocol ever emits a reason phrase** and the `reason` member is unused on those
paths. No phrase can regress over HTTP/2 or HTTP/3, whatever a row holds.

Both protocols reserve exactly three bytes for the digits of a status, which is
why a status must be below 1000 before it reaches a response.
`NGX_HTTP_STATUS_WIRE_MAX` and `ngx_http_status_wire_width_ok()` are that bound,
and it is enforced in every build rather than only under strict validation.

## The record type and the range

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
`ngx_http_parse_status_line()` and the upstream modules that call it. It also
has a member named `code`, so the two are easy to confuse and a mistake between
them may compile further than it should.

### Range

| Constant | Value | Meaning |
| -------- | ----- | ------- |
| `NGX_HTTP_STATUS_MIN` | 100 | lowest code the registry may describe, inclusive |
| `NGX_HTTP_STATUS_MAX` | 600 | upper bound, **exclusive**; 599 is the highest code, 600 is not a code |
| `NGX_HTTP_STATUS_RANGE` | 500 | the width of that range, which also sizes the lookup index |

`ngx_http_status_in_range(s)` is the range test. It is a parameterized macro
rather than a function, so the argument is expanded more than once and must not
have side effects.

### Querying the registry

The functions that read the metadata on this page are declared in
`src/http/ngx_http.h`:

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

`ngx_http_status_expires_ok()`, declared in `src/http/ngx_http_status.h`,
answers the `EXPIRES_OK` question the same way, and two further functions
declared there answer for the two members of a row that no function above reads
out:

```c
ngx_uint_t ngx_http_status_has_flags(ngx_uint_t status, ngx_uint_t flags);
const char *ngx_http_status_rfc_section(ngx_uint_t status);
```

`ngx_http_status_has_flags()` returns those of the flags asked for that the code
carries, so `ngx_http_status_has_flags(444, NGX_HTTP_STATUS_INTERNAL)` is
non-zero and asking after several flags at once answers which of them a code
carries. It lets a caller ask the registry which class a code belongs to instead
of doing arithmetic on the status value, though as said above the only flags any
code in the tree reads at run time are the two the helpers above answer with.
`ngx_http_status_rfc_section()` returns the section reference listed for the
code in the table above.

Neither hands out the address of a row, so the guarantee
`ngx_http_status_reason()` gives holds for them as well: a caller reaches the
value it asked after, reaches no member it did not ask after, and can alter no
part of the registry. Both answer for an unregistered code as the two functions
above do, with zero and with `NULL`.

The remaining three functions set, validate, and extend the registry, and the
sections below say what each of them promises.

### Setting a status

```c
ngx_int_t ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status);
ngx_int_t ngx_http_status_validate(ngx_uint_t status);
```

`ngx_http_status_set()` is the one entry point for a status nginx itself chose.
It writes the response status and the bits recording that nginx chose one and
that it was not relayed, and writes nothing else: not the status line, which a
caller supplies verbatim or leaves empty, and not the error status, which is a
different status for a different purpose. A status an upstream chose does not
come through it — a relayed status is stored directly, at the boundary it is
relayed across — so every status it is given is nginx's own by construction.

It answers `NGX_ERROR` in two cases, and a caller is expected to act on that:

```c
if (ngx_http_status_set(r, NGX_HTTP_OK) != NGX_OK) {
    ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "invalid status");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

The first case holds in **every** build: a status of four digits or more cannot
be sent at all, because the three bytes an HTTP/2 or an HTTP/3 response reserves
for the number are a minimum and not a limit, so a wider one would overrun the
field. See [HTTP/2 and HTTP/3](#http2-and-http3). The second case holds only in
a build configured with `--with-http_status_validation`, and is described below.

`ngx_http_status_validate()` answers `NGX_OK` for a code the registry describes
and `NGX_ERROR` for any other, takes no request, and is defined in either build,
so configuration-time code may call it to check a status a directive named
before anything depends on it.

### Strict validation

A build configured with `--with-http_status_validation` looks for statuses that
nginx or a configuration chose and that the registry does not describe. The
switch is off unless it is asked for, so the build everyone runs sends what it
always sent; the strict build is for development and for conformance work.

That build watches all three points at which such a status is chosen —
`ngx_http_status_set()`, the gate in `ngx_http_special_response_handler()` that
every status a handler returned arrives at, and the gate in
`ngx_http_send_error_page()` that an `error_page ... =NNN` overwrite arrives at
— and writes an alert reading `unregistered HTTP status NNN` for the first such
status a request produces. It is reported once per request and not once per
point, so a request whose status is chosen twice is one line in the log and not
two.

Only the first of the three refuses, because only the first has a caller with a
result to act on: the status is not stored, and the caller's own error handling
answers the request as it answers any other failure. The two gates report and
send, because they are what answers a request that has already gone wrong, and
refusing there would leave it with no response rather than with a worse one. No
status is ever rewritten to a different one at any of the three.

A `return` directive falls on either side of that line, which is worth knowing
before reading a log: nginx answers it with a response of its own, through the
setter, when the directive names a body or a code below 400, and returns the
code to be answered as an error otherwise. In a strict build `return 306;` and
`return 306 "text"` are therefore both answered 500, while `return 480;` is
reported and sent as 480. A build without the switch sends 306 and 480 as
named.

A status an upstream chose is exempt from all of this, on the authorship
recorded as the response crossed the boundary and never on the number. An
`error_page 599 =599 /uri` for an upstream that answered 599 is a configuration
overwrite and not a relayed response, so it is reported, even though the two
numbers are equal.

### Registering a status

```c
ngx_int_t ngx_http_status_register(ngx_http_status_def_t *def);
```

A module may add a code the registry does not describe. Registration is accepted
only while a configuration is being parsed, which is strictly before workers
fork: `ngx_http_status_init()` runs from the HTTP core module's preconfiguration
and `ngx_http_status_seal()` from its postconfiguration, so the window is every
HTTP module's own configuration parsing. Once it has closed,
`ngx_http_status_register()` answers `NGX_ERROR` unconditionally. Call it from a
directive handler or from a module's preconfiguration, never from a worker.

It also answers `NGX_ERROR` for a null definition, for a code outside
`NGX_HTTP_STATUS_MIN` to `NGX_HTTP_STATUS_MAX`, for a code the registry already
describes, and once the [headroom](#footprint) is used up. Because
`ngx_http_status_init()` runs again for every configuration parsed, including
each reload and each `nginx -t`, a registration lasts as long as the
configuration that made it and must be made again by the next one.

**What is copied and what is not.** The definition is copied into the registry
by value, so a caller may pass one from its stack. That copy duplicates the
`ngx_str_t`, which is a length and a pointer, and **not** the bytes the pointer
addresses. `reason.data` and `rfc_section` therefore remain borrowed, and each
must stay valid for as long as the configuration that registered them is in use.
That is not merely until parsing ends, but for the whole life of the workers
serving that configuration, since `ngx_http_status_reason()` reads those bytes
while responses are being sent.

Static storage is the simplest thing that satisfies that, and the configuration
pool is the other, its allocations living as long as the cycle and surviving the
fork with it. Note that `reason` is the fused `"NNN Phrase"` form — the bytes
that follow `HTTP/1.1 ` in a status line, with the number written out again at
the front, as [explained above](#the-fused-nnn-phrase-form) — and that a zero
length `reason` is legitimate and means the code is emitted as a bare number.

```c
/* file scope: outlives every configuration and every worker */

static ngx_http_status_def_t  ngx_example_status_420 = {
    420,
    ngx_string("420 Enhance Your Calm"),
    NGX_HTTP_STATUS_CLIENT_ERROR,
    "vendor extension"
};


static ngx_int_t
ngx_example_preconfiguration(ngx_conf_t *cf)
{
    if (ngx_http_status_register(&ngx_example_status_420) != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "cannot register HTTP status 420");
        return NGX_ERROR;
    }

    return NGX_OK;
}
```

A string built at parse time is safe on the same terms, provided the pool it
comes from is the configuration's:

```c
    def.reason.len = len;
    def.reason.data = ngx_pnalloc(cf->pool, len);   /* lives with the cycle */
```

**What is not safe**, because the registry keeps the pointer and not the bytes:

- a buffer on the stack of the function that registers, which is gone as soon as
  that function returns;
- anything from a pool that is destroyed before the configuration is, or from a
  per-request pool such as `r->pool`, which does not exist yet at registration
  time and would not outlive the request if it did;
- a string a module frees or reuses later.

Each of those leaves the registry holding a pointer into memory that has been
released, which a worker then reads while writing a status line.

## See also

- `src/http/ngx_http.h` — where the five functions are declared, so that every
  file that includes it can reach them
- `src/http/ngx_http_status.h` — the record type, the flags, the range, and
  the seven functions it declares itself, `ngx_http_status_expires_ok()` and
  the two that answer for a row's flags and its section reference among them
- The Status Code API migration guide — how a module author converts a direct
  assignment to `r->headers_out.status`, and what the registry's lifecycle means
  for a module that registers a code of its own
- [Documentation home](../index.md)
