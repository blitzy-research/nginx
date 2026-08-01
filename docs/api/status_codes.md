# Status Code Registry

Reference for the HTTP status codes the registry in
`src/http/ngx_http_status.c` describes: the wire reason phrase a code carries,
where it carries one, the semantic flags it is described with, and the
specification section that defines it.

The registry is the single place that holds this knowledge, and four pieces of
logic read it rather than each keeping a copy of their own: the reason phrase of
a status line, the effective `$status` value that the log module and the
variable evaluator both need, the `expires` eligibility of a response, and the
setting of a response status, which goes through `ngx_http_status_set()`
wherever nginx chooses a status for a response of its own.

Outside that last one the response status is stored directly at **two call
sites, which fall into two categories**, and
[setting a status](#setting-a-status) says what each is: an upstream's status
relayed into the response, and the `NGX_HTTP_OK` the embedded Perl
`send_http_header()` falls back to for a script that set none. One further store
is made inside the registry module itself rather than at a call site of it, the
one the setter makes for a status it has accepted, which makes **three**
assignments to the response status in the whole of `src/http`.

What the registry does not hold stays where it is. The status constants in
`src/http/ngx_http_request.h` are unchanged and remain the way a module names a
status. The compiled in error page bodies belong to
`src/http/ngx_http_special_response.c`, and so does the answer to which of them
a status selects: the bounds of the three spans of consecutive codes those
bodies are held for are named in that same file, beside the table they index,
and the table is held to the rows those spans account for by an assertion on its
own size. Those rows follow the shape of that table rather than the membership
of the registry, which is why a span covers a code the registry does not
describe as readily as one it does, and why the registry is not the one asked.
Status comparisons that decide behavior rather than describe a status — the 204
and 304 handling in the header filter, the keepalive and lingering close rules
of the special response handler — are written out where the decision is made.

For every status a response can carry, the bytes a client receives are the bytes
nginx has always sent: each status line, each error page body, each `$status`
value, and each HTTP/2 and HTTP/3 `:status` field. The registry describes a
status; it never chooses one.

One bound belongs to two of the three encodings rather than to the registry: a
status of four digits or more cannot be carried over HTTP/2 or HTTP/3, because
each reserves exactly three bytes for the digits of `:status` and a wider one
would be written past the end of that field. It is a bound on those encodings
and not a rule about which statuses nginx may use — an HTTP/1.x status line
reserves `NGX_INT_T_LEN` bytes for the same number and carries any width, and
`error_page 404 =1234 /wide;` has always been accepted and has always answered
`HTTP/1.1 1234 ` — so it is held where each encoding has it and not where a
status is chosen. See [HTTP/2 and HTTP/3](#http2-and-http3) for where each
encoding holds it.

Of the 45 status constants, 43 are named `NGX_HTTP_*` and the remaining two are
`NGX_HTTPS_CERT_ERROR` and `NGX_HTTPS_NO_CERT`. A module written against any of
them needs no edit.

## Registry membership

The registry describes **48** status codes. Membership follows from something
needing to be recorded about a code, not from the code being well known, and
three sources account for all 48, covering every code nginx gives a reason
phrase or an error page body:

- the **44** distinct values of the 45 status constants in
  `src/http/ngx_http_request.h`; 44 rather than 45 because 494 is defined twice,
  under two names;
- **402**, **406** and **410**, which have a reason phrase nginx has always sent
  but no constant of their own;
- **203**, which has neither, and is described because RFC 9110, section 15.1,
  lists it among the codes a cache may store heuristically, so
  `ngx_http_status_is_cacheable()` has to be able to answer for it.

44 + 3 + 1 = 48. Being a code that RFC 9110, section 15, defines is not by
itself a reason to be a member: **205** and **407** are section 15 codes and are
not described, because nginx emits neither a reason phrase nor an error page
body for either, and neither is on the heuristically cacheable list that 203 is
on, so nothing about them needs recording.

That total is not kept by hand. The `metadata` target of
`misc/status_test/GNUmakefile` reads the registry table and requires the number
of rows in it to agree with the count the registry declares, and then requires
the table below to describe every one of those rows and nothing else, with the
same phrase, the same flags and the same provenance. A row added or removed
without this page being brought along fails a check rather than passing quietly,
and so does a provenance mistyped or moved from one code to another in either
file.

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
an upstream is not examined at all: the exemption is `r->upstream` being set on
the request the status answers, which is the origin of the response, and never
the value of the status. It has to be the origin, because two origins may choose
the same number for one request and equal numbers would not tell them apart; and
it has to exist, because an upstream may answer with a code nginx has never
heard of, or with one below 100, and nginx's contract is to relay what it
received.

A status nginx or a configuration chose is examined, and what follows depends on
the build. A build without `--with-http_status_validation` sends it and reports
nothing: the 306 a `return` directive may ask for is sendable, and is sent,
without being a registry member. A build configured with the switch writes one
alert for the request, reading `unregistered HTTP status 306`, and still sends
that status wherever it arrives with no caller to answer to; the one place it
refuses such a status is `ngx_http_status_set()`, which has a caller with a
result to act on. No status is replaced by a different one either way, and how
wide a status is has nothing to do with any of this: a status of four digits is
a status the registry does not describe like any other, and is sent by a build
without the switch exactly as 306 is. See
[strict validation](#strict-validation) for the whole of that contract.

**498** looks like an omission and is not a member either. It appears in
`src/http/ngx_http_request.h` only as a comment,
`498 is the canceled code for the requests with invalid host name`, and has no
constant of its own. The error page table nevertheless keeps a row for it, and
that retained row reuses the 404 page rather than carrying a page of its own. No
current code path emits 498 — nothing in the tree sets it, returns it, or
compares against it — so the row is never selected. It is kept because the error
page rows follow the shape of that table, which is a span of consecutive codes,
and not the membership of the registry.

## Registered status codes

The `reason` column is the fused `"NNN Phrase"` form, exactly the bytes that
follow `HTTP/1.1 ` in a status line; see
[the fused form](#the-fused-nnn-phrase-form) for why the digits appear twice.
`_none_` means the row carries a zero-length reason and the code is emitted as a
bare number; see
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

Of the 48 described codes, 36 carry a reason phrase and 12 do not; the table
above reads `_none_` for each of those 12:

**100, 101, 102, 103, 203, 300, 444, 494, 495, 496, 497, 499.**

In an HTTP/1.x status line a status with no phrase to copy is emitted as three
digits followed by a space, and that trailing space is part of the bytes a
client receives. HTTP/2 and HTTP/3 are not concerned by any of this: their
`:status` is numeric, carries no phrase for any code, and has no trailing space.

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
space — and over HTTP/2 and HTTP/3 the question does not arise at all, no phrase
being emitted for any code. They are still different answers to different
questions: the first says the registry describes this code and has no phrase for
it, the second says the registry does not describe it at all.

## The fused `"NNN Phrase"` form

The `reason` member holds the number and the phrase together, as `200 OK` rather
than `OK`. The digits therefore appear twice for a registered code, once in
`code` and once at the front of `reason`. That is deliberate.

The bytes in `reason` are exactly the bytes that follow `HTTP/1.1 ` in a status
line, so the header filter emits a status line with a single copy:

```c
b->last = ngx_cpymem(b->last, "HTTP/1.1 ", sizeof("HTTP/1.x ") - 1);

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
`INTERNAL` — are descriptive metadata rather than a class test in disguise. They
record what class a code belongs to and where it comes from, and no consumer
outside the registry table reads them. They are recorded because the class of a
code is status code knowledge and belongs with the rest of it, and because a row
that describes a code should describe it completely. Having no consumer to catch
a wrong one, they are checked against the table itself: the `metadata` target of
`misc/status_test/GNUmakefile` reads every row and requires its class flags to
agree with the class its code falls in, and its `rfc_section` to agree with the
kind of code it is, and requires the row published above to carry the same flags
and the same provenance as the row the registry is built from.

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

Both protocols reserve exactly three bytes for the digits of a status, and the
width in a `%03ui` conversion is a minimum and never a limit, so it pads a
shorter number but never truncates a longer one: a status of four digits would
be written past the end of that field. Each of the two filters therefore holds
that bound itself, as `NGX_HTTP_V2_STATUS_MAX` and `NGX_HTTP_V3_STATUS_MAX`,
each of them 999 and each private to the file that reserves the bytes it is
about. The registry publishes no such bound, because the bound is not a fact
about a status code.

It is tested in exactly two places, in every build, and nowhere else:

- `ngx_http_v2_header_filter()`, before it reserves those three bytes. A
  response it refuses is not written, so the stream is reset and an alert reads
  `HTTP status NNN too wide for an HTTP/2 response`.
- `ngx_http_v3_header_filter()`, the same for QPACK.

One further place bounds a status, and bounds it more narrowly still by asking
for the range of the registry rather than for a width: the `status()` method of
the embedded Perl module, which is the one place a status arrives from outside
nginx. An integer of a script's choosing is neither parsed from a response nor
written by nginx, so it is the only value bounded by neither a parser nor a
table. `ngx_http_status_in_range()` is what bounds it, so a status below 100 or
of 600 or more is refused with `croak("status(): invalid status code")` — and
every status too wide for either filter, being of 600 or more as well, is
refused there with it.

It is deliberately **not** tested where a status is chosen. An HTTP/1.x status
line reserves `NGX_INT_T_LEN` bytes for the same number and carries any width,
and a configuration has always been able to ask for one:
`error_page 404 =1234 /wide;` is accepted and answers `HTTP/1.1 1234 `.
Refusing that would change what a build without the switch sends, which is the
one thing that must not change.

Every other source of a status is already bounded to three digits before it gets
this far. The HTTP/1.x status line parser counts digits and stops at three, the
FastCGI module converts exactly three characters, and the gRPC and HTTP/2 proxy
modules reject a `:status` whose length is not three.

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

Rows are written only while a configuration is being parsed, and are read-only
from the moment a worker begins serving one. A worker therefore allocates
nothing for the registry, and a lookup costs no memory whatever the response.

### Range

| Constant | Value | Meaning |
| -------- | ----- | ------- |
| `NGX_HTTP_STATUS_MIN` | 100 | lowest code the registry may describe, inclusive |
| `NGX_HTTP_STATUS_MAX` | 600 | upper bound, **exclusive**; 599 is the highest code, 600 is not a code |
| `NGX_HTTP_STATUS_RANGE` | 500 | the width of that range, which also sizes the lookup index |

`ngx_http_status_in_range(s)` is the range test. It is a parameterized macro
rather than a function, so the argument is expanded more than once and must not
have side effects.

### Error page rows

Which row of the compiled in error page table a status selects is **not** a
question the registry answers. The bounds of the three spans of consecutive
codes that table is held for, the row each span begins at, the row count derived
from those bounds, and the function that walks them are all private to
`src/http/ngx_http_special_response.c` — the bounds as macros of that file, the
function with internal linkage — beside the table they index and nowhere else.
No accessor of the registry hands out a row, and no other file asks for one.

The two are kept apart because they answer to different things. A span covers
every code in its range whether the registry describes it or not, the rows
following the shape of the table of bodies: the row for
[498](#registry-membership) holds the 404 page although no row of the registry
describes 498, and 444 falls between two spans and selects the zero length row
although the registry describes it perfectly well. Rows 1 to 8 hold the bodies
for 301 to 308, rows 9 to 38 those for 400 to 429, and rows 39 to 52 those for
494 to 507; row 0 holds no body and is what a code no span covers selects.

Two checks made at build time hold the table and its spans together. The table
is `sizeof`-asserted against the row count its spans account for, so a row added
to it or taken from it fails to compile; and that row count is derived from the
span bounds rather than written out a second time, so a bound edited on its own
moves the count with it rather than being silently disagreed with. Neither check
involves the registry, and neither can be satisfied by editing it.

### Footprint

The registry is static data: the two arrays a status is described by and the two
words of state that go with them. Both arrays and both words are written only
while a configuration is being parsed — which is strictly before workers fork —
and read only afterwards.

| Object | Entries | Entry size (LP64) | Bytes |
| ------ | ------- | ----------------- | ----- |
| `ngx_http_status_defs[]` | 64 | 40, one `ngx_http_status_def_t` | 2,560 |
| `ngx_http_status_index[]` | 500 | 2, one `u_short` | 1,000 |
| **the two arrays alone** | | | **3,560** |
| the state beside them | 2 | 8, one `ngx_uint_t` | 16 |
| **Total** | | | **3,576** |

The two figures are given separately because they answer different questions:
**3,560 bytes** is what the registry's own two arrays occupy, and **3,576
bytes** is that plus the 16 bytes of state held beside them, which is the count
of rows in use and the sealed flag and nothing else. On ILP32 a definition row
measures 20 bytes and a word 4, which makes the same objects 1,280 and 1,000
bytes, or **2,280** for the two arrays and **2,288** with the state. Either way
the registry costs a fraction of the 10 KB it is allowed, and that is the whole
of what it costs: nothing is allocated for a connection, and nothing for a
request.

Row zero of the definitions is a reserved sentinel, so that a zero in the index
means "not described" and no second table has to record which codes are present.
Of the 64 rows, that sentinel and the 48 built-in definitions are accounted for,
which leaves **15 rows of headroom** for [registration](#registering-a-status).
How many there are is deliberately private to `src/http/ngx_http_status.c`: a
caller learns the headroom is used up from `ngx_http_status_register()`
answering `NGX_ERROR`, and not by consulting a number.

Both arrays are returned to their built state by every `ngx_http_status_init()`
— the rows a registration appended are cleared, and the whole of the lookup
index is zeroed and then derived again over the built-in rows — which is to say
for every configuration parsed, including each reload and each `nginx -t`. What
a configuration registered is discarded with it, the headroom is whole again,
and the registry is open to registration once more; nothing carries over from
the configuration before, and nothing about the state left behind depends on
how many configurations came before it. A worker only ever reads the arrays,
whichever configuration it was forked for, so no page of either is copied on
write and a worker's private memory does not grow on their account. The only
per-request cost is the single bit of bookkeeping the registry keeps on
`ngx_http_request_t`, which occupies padding the compiler had already
allocated.

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

One more of the registry's answers is read the same way, by a helper that
`src/http/ngx_http_status.h` declares rather than one of the five
`src/http/ngx_http.h` publishes: `ngx_http_status_expires_ok()` answers the
`EXPIRES_OK` question on the same terms and returns zero for an unregistered
code as well. It is one of the four helpers that header declares, none of which
is part of the API a module writes against; the [migration
guide](../migration/status_code_api.md#4-lifecycle-and-custom-registration)
lists all four and says which of them a module may call.

No function hands out the address of a row: a caller reaches the value it asked
after, reaches no member it did not ask after, and can alter no part of the
registry. `flags` and `rfc_section` are read by the registry itself and are not
published through an accessor of their own — the flags through
`ngx_http_status_is_cacheable()` and `ngx_http_status_expires_ok()`, which each
answer over one flag, and `rfc_section` not at all. A
caller that needs to know what a row records reads it from the tables on this
page.

The remaining three functions set, validate, and extend the registry, and the
sections below say what each of them promises.

### Setting a status

```c
ngx_int_t ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status);
ngx_int_t ngx_http_status_validate(ngx_uint_t status);
```

`ngx_http_status_set()` is the entry point for a status nginx itself chose for a
response. It writes exactly two things: the response status, and the bit
recording that a status has been set for the request. It writes nothing else —
not the status line, which a caller supplies verbatim or leaves empty, and not
the error status, which is a different status for a different purpose. A status
an upstream chose is stored directly, at the boundary it is relayed across, and
a request that has an upstream attached is exempt from every check the setter
makes on what it is given, on `r->upstream` and never on the number.

That boundary is one of the **two call sites** where a store stays direct, which
fall into **two categories**. The other is the embedded Perl
`send_http_header()`, which falls back to `NGX_HTTP_OK` for a script that set no
status at all; a script that does set one goes through the setter, after the
integer it named has been held to the range of the registry.

| Category | Call site | What it stores |
| -------- | --------- | -------------- |
| relayed | `ngx_http_upstream.c` | the status an upstream chose |
| default | `nginx.xs`, `send_http_header()` | `NGX_HTTP_OK`, for a script that set none |

One further store is made inside the registry module rather than at a call site
of it, which makes **three** assignments to the response status in the whole of
`src/http`: the one `ngx_http_status_set()` makes for the status it was given.

Everything else goes through the setter, including the two kinds of write that
are not a status being chosen for a response:

- **the move that finishes a response.** `ngx_http_send_header()` moves the
  error status of a request over its response status, last of all before the
  filters run, and it makes that move by handing the error status to the setter
  and clearing the status line where the setter accepted it.
- **the teardown paths.** `ngx_http_terminate_request()` and
  `ngx_http_free_request()` each write a status under their own guard, purely so
  that the access log is given the status the request ended with — a code such
  as `NGX_HTTP_CLIENT_CLOSED_REQUEST` or `NGX_HTTP_CLOSE`, which no response
  ever carries. Both go through the setter as well. Neither returns anything to
  a caller, so a refusal there is reported and the log is left the status the
  request already had; both of those codes are nginx's own and the registry
  describes both, so no build refuses either.

It answers `NGX_ERROR` in one case, and a caller is expected to act on that:

```c
if (ngx_http_status_set(r, NGX_HTTP_OK) != NGX_OK) {
    ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "invalid status");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}
```

That case arises only in a build configured with
`--with-http_status_validation`, and only for a status the registry does not
describe on a request that has no upstream attached; it is described below.
Every call is examined and every refusal is reported, so such a status is
refused and written to the log however many statuses that request has been given
already. A build without the switch answers `NGX_OK` for every status, whatever
its value and however wide it is, and the whole of the function is two stores.
The wrapper is written all the same, so that a module compiled against one build
behaves in the other, and it costs a branch that the compiler removes when the
switch is absent.

`ngx_http_status_validate()` answers `NGX_OK` for a code the registry describes
and `NGX_ERROR` for any other, takes no request, and is defined in either build,
so configuration-time code may call it to check a status a directive named
before anything depends on it.

### Strict validation

A build configured with `--with-http_status_validation` looks for statuses that
nginx or a configuration chose and that the registry does not describe. The
switch is off unless it is asked for, so a build without it sends what it always
sent; the strict build is for development and for conformance work.

That build watches all three points at which such a status is chosen —
`ngx_http_status_set()`, the gate in `ngx_http_special_response_handler()` that
every status a handler returned arrives at, and the gate in
`ngx_http_send_error_page()` that an `error_page ... =NNN` overwrite arrives at
— and each of the three writes an alert reading `unregistered HTTP status NNN`
for a status it objects to. Reporting is per call and not per request: nothing
recorded on the request makes a later objection silent, so a request whose
status is objected to twice is two lines in the log.

Only the first of the three refuses, because only the first has a caller with a
result to act on: the status is not stored, the response status the request
already had is left as it was, and the caller's own error handling answers the
request as it answers any other failure. The two gates report and send, because
they are what answers a request that has already gone wrong, and refusing at a
gate would leave it with no response rather than with a worse one. No status is
ever rewritten to a different one at any of the three.

Every call to `ngx_http_status_set()` examines the status it is given, whatever
was chosen for the request before it, so a status refused once and then set
again — from the same caller or from another — is examined and refused again,
and reported again. Nothing a request accumulates grants a status or silences a
line. A build in which something did would send, from the second call onward,
exactly what asking for the switch asked it to object to.

Moving the error status of a request over its response status, which is the last
thing `ngx_http_send_header()` does before the filters run, goes through the
setter like every other write of a response status — so in a strict build that
move is examined too, and a status one of the gates reported and let stand is
refused there. `ngx_http_send_header()` then answers `NGX_ERROR` to whoever
asked it for the header, which is answered as any other failure of that function
is, and the response is not sent. A build without the switch sends it, and what
a build without the switch sends is what nginx has always sent.

That is the whole of the difference the switch makes, and a `return` directive
shows both halves of it. nginx answers such a directive with a response of its
own, through the setter, when it names a body or a code below 400, and returns
the code to be answered as an error otherwise:

| Directive | Default build | Strict build |
| --------- | ------------- | ------------ |
| `return 404;` | `404 Not Found` | `404 Not Found`, no alert |
| `return 306;` | `HTTP/1.1 306 ` | `500`, one report and one refusal |
| `return 306 "text";` | `HTTP/1.1 306 ` with the body | `500`, one report and one refusal |
| `return 480;` | `HTTP/1.1 480 ` with the 404 page | no response; two reports, `$status` 480 |

The two reports for `return 480;` are the gate reporting the status a handler
returned and the setter reporting it again where the move that finishes the
response is refused. The access log still records 480, because it reads the
error status of the request and not the response status.

A status an upstream chose is exempt, on `r->upstream` being set on the request
it answers and never on the number. The exemption is applied where the origin of
a status can be an upstream: in the setter, and at the gate in
`ngx_http_special_response_handler()`, which is handed an upstream's status
whenever `proxy_intercept_errors` diverts an upstream error into it. It is not
applied at the `error_page ... =NNN` gate, because a configuration authored that
number whatever answered the request: an `error_page 599 =599 /uri` for an
upstream that answered 599 is an overwrite and not a relayed response, so it is
reported even though the two numbers are equal.

### Registering a status

```c
ngx_int_t ngx_http_status_register(ngx_http_status_def_t *def);
```

A module may add a code the registry does not describe. Registration is accepted
only while a configuration is being parsed, which is strictly before workers
fork: `ngx_http_status_init()` runs from the HTTP core module's
preconfiguration, which is the first thing the parsing of an `http{}` block
does, and `ngx_http_status_seal()` from that same module's postconfiguration,
which runs once the whole of the block has been parsed. The window is therefore
every HTTP module's preconfiguration and the parsing of every directive, and it
does **not** extend to postconfiguration: the HTTP core module is the first HTTP
module, so its postconfiguration runs before any other module's. Register from a
directive handler or from a module's preconfiguration; never from
postconfiguration, and never from a worker. Once the window has closed,
`ngx_http_status_register()` answers `NGX_ERROR` unconditionally.

It also answers `NGX_ERROR` for a null definition, for a code outside
`NGX_HTTP_STATUS_MIN` to `NGX_HTTP_STATUS_MAX`, for a code the registry already
describes, and once the [rows the registry keeps spare for
registration](#footprint) are used up. It writes nothing to the log of its own,
so a caller is expected to report a refusal, as the example below does. Because
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

- `src/http/ngx_http.h` — where the five functions of the API are declared, and
  the only place they are, so that every file which includes that aggregator can
  reach them. It declares no other function of the registry
- `src/http/ngx_http_status.h` — the record type, the six flags, the range
  constants and the range macro, and the four helpers the HTTP core and the
  modules of it use: `ngx_http_status_init()`, `ngx_http_status_seal()`,
  `ngx_http_status_effective()` and `ngx_http_status_expires_ok()`. None of the
  four is part of the API a module writes against, which is why they are
  declared here and not beside the five. This header is reached through
  `src/http/ngx_http.h`, which includes it, and includes that aggregator in turn
  so that `ngx_http_status_effective()` can be declared with the request type it
  takes
- [Status Code API migration guide](../migration/status_code_api.md) — how a
  module author converts a direct assignment to `r->headers_out.status`, and
  what the registry's lifecycle means for a module that registers a code of its
  own
- [Documentation home](../index.md)
