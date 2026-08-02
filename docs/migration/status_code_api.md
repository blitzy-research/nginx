# Status Code API Migration

How an nginx module adopts the HTTP status code registry: the functions the
registry publishes, what each of them promises, and how a direct write to
`r->headers_out.status` becomes a call.

For the codes the registry describes — the wire reason phrase each one carries,
the flags it is described with, and the specification section it comes from —
see the [Status Code Registry](../api/status_codes.md). This guide covers the
API and does not repeat that catalog.

## 1. Scope and compatibility

This guide is for the authors of nginx modules, in the tree and out of it, who
choose to adopt the registry API. Adoption is voluntary. A module that is never
touched keeps working: writing `r->headers_out.status` directly still compiles
and still behaves exactly as it did, so nothing in this guide has to be done on
any particular occasion, or at all.

Nothing around a module has to be migrated either. There is no repository to
move to, no package to depend on, no deployment step, and no `nginx.conf`
change: no directive was added, renamed, or removed. The status constants in
`src/http/ngx_http_request.h` are unchanged and remain the way a module names a
status; no existing function signature changed; no linker-visible symbol was
removed. The registry is additive, and this is not a breaking change.

A default build sends what it always sent, with the two exceptions named in the
two paragraphs below. Every status line, every error page body, every `$status`
value, and every HTTP/2 and HTTP/3 `:status` field is byte for byte what it was
before the registry existed. Strict validation is a separate thing a build may
be configured to do, with `--with-http_status_validation`, and it is off unless
it is asked for; see [the strict build](#the-strict-build).

One bound is new, and it belongs to two encodings rather than to the API: a
status of four digits or more cannot be carried over HTTP/2 or HTTP/3, because
each reserves exactly three bytes for the digits of `:status` and the `%03ui`
conversion that writes them pads a narrower value but never truncates a wider
one. It is held where each of those encodings has it — in the two header
filters, before either reserves those bytes, each filter naming the bound itself
as `NGX_HTTP_V2_STATUS_MAX` or `NGX_HTTP_V3_STATUS_MAX` and neither publishing
it, because how wide a status is written is not a fact about a status code.

The embedded Perl `status()` method is bounded too, and more narrowly, because
it is handed whatever integer a script passes and is the one place a status
arrives from outside nginx at all. What bounds it is the range of the registry
rather than a width: every status below 100 except zero, and every status of 600
or more, is refused by asking `ngx_http_status_in_range()`, which refuses every
status too wide for either encoding along with them.

Zero is the one value below 100 that method admits. It is the value a request
already carries before a status has been chosen, and it is what `SvIV()` answers
for an undefined argument and for one that is not a number at all, so a script
that passes it is asking for the status the request already had: the value is
forwarded unchanged, `send_http_header()` answers 200 for it, and that is what a
default build did before this bound existed. Zero written as three digits is
`000`, so admitting it takes nothing away from what the bound is for. A strict
build refuses zero along with every other status the registry does not describe,
and refuses it at `ngx_http_status_set()` rather than at the range test.

It is deliberately not held where a status is chosen, so nothing about adopting
this API narrows what a call site may ask for. An HTTP/1.x status line reserves
`NGX_INT_T_LEN` bytes for the same number and carries any width, and a
configuration has always been able to ask for one: `error_page 404 =1234
/wide;` is accepted and, in a build without the validation switch, answers
`HTTP/1.1 1234 `. Nothing nginx chooses for itself comes near that width in any
case. [The range](#range) names that macro and [HTTP/2 and
HTTP/3](#http2-and-http3) says what each of the two encoders holds and does,
while [converting direct assignments](#6-converting-direct-assignments) says
what a caller does with a result.

What a module gains by adopting the API is one place at which a status is set,
and metadata it would otherwise keep a copy of: the wire reason phrase of a
status, whether a cache may store it heuristically, and whether nginx's
`expires` processing applies to it.

## 2. Public status API

Five functions make up the module-facing API. They are declared in
`src/http/ngx_http.h`:

```c
ngx_int_t ngx_http_status_set(ngx_http_request_t *r, ngx_uint_t status);
ngx_int_t ngx_http_status_validate(ngx_uint_t status);
const ngx_str_t *ngx_http_status_reason(ngx_uint_t status);
ngx_uint_t ngx_http_status_is_cacheable(ngx_uint_t status);
ngx_int_t ngx_http_status_register(ngx_http_status_def_t *def);
```

`src/http/ngx_http.h` includes `src/http/ngx_http_status.h`, so a module that
already includes `<ngx_http.h>` — as every HTTP module does — reaches these five
functions, the record type, the flags, and the range with no include of its own.
No per-file include edit is normally required.

### `ngx_http_status_set()`

The entry point for a status nginx or a module chose itself. It answers `NGX_OK`
or `NGX_ERROR`, and a caller is expected to act on the result.

Direct stores of `r->headers_out.status` are kept in the core beside it at
**two call sites, which fall into two categories**, and a module converting call
sites of its own has no use for either: an upstream's status being relayed into
the response, which
[upstream-originated statuses](#7-upstream-originated-statuses) covers, and the
`NGX_HTTP_OK` that the embedded Perl `send_http_header()` falls back to for a
script that set no status at all.

One further store is made by the registry module itself rather than at a call
site of it, the one `ngx_http_status_set()` makes for a status it has accepted.
Those two sites and that one are **three assignments** to that member in the
whole of `src/http`, and nothing else in the tree writes it.

Every other write of a response status goes through this function, including the
two kinds that are not a status being chosen: the move that finishes a response,
which [a status that is merely being
moved](#a-status-that-is-merely-being-moved) sets out, and the teardown stores
made only so that the access log has a status, which [a site that only records
an already-decided
status](#a-site-that-only-records-an-already-decided-status) sets out.

On success it writes exactly two members of the request: the response status
`r->headers_out.status`, and `r->status_final = 1`, which records that a
response status has been chosen. The `status_final` store is made in a default
and in a strict build alike. It writes nothing else — not
`r->headers_out.status_line`, which a caller supplies verbatim or leaves empty,
and not `r->err_status`, which is a different status for a different purpose.
Anything else a call site used to do next it still has to do; see
[converting direct assignments](#6-converting-direct-assignments).

It answers `NGX_ERROR` in one case, and it neither clamps, substitutes, nor
silently rewrites a status:

- **A status the registry does not describe**, in a build configured with
  `--with-http_status_validation` only, and only for a status nginx originated —
  that is, one set on a request with no upstream attached. It is reported at
  `NGX_LOG_ALERT` as `unregistered HTTP status NNN` and `NGX_ERROR` is returned
  *before* the status is stored, so the request keeps whatever status it already
  carried and the caller's own error handling answers it. Every call is
  examined, so this happens however many statuses that request has been given
  already. A default build stores such a status and answers `NGX_OK`, which is
  why what a default build sends is unchanged.

How wide a status is has nothing to do with this. A status of four digits is a
status the registry does not describe like any other: a default build stores it
and answers `NGX_OK`, and a strict build reports and refuses it in the same
breath as a narrower one it does not describe. The bound on width is the two
header filters' own, as [scope and compatibility](#1-scope-and-compatibility)
explains.

Reporting is once for a call, as refusal is. Nothing recorded on the request
makes a later objection silent and nothing grants a status: a status refused
once and then set again, from the same caller or from another, is examined and
refused again, and reported again. No sequence of calls arrives at a status a
strict build was configured to object to.

`ngx_http_send_header()` moves the error status of a request over its response
status through this function like every other write of a response status, so in
a strict build that move is examined too and a status one of [the two
gates](#statuses-a-handler-returns) reported and let stand is refused there.
`ngx_http_send_header()` answers `NGX_ERROR` to whoever asked it for the header
and the response is not sent; see [a status that is merely being
moved](#a-status-that-is-merely-being-moved).

It never refuses a status merely because `r->status_final` is already set. A
status may legitimately be set again after a response has been decided, and a
strict build notes that at debug level and stores it.

### `ngx_http_status_validate()`

Answers `NGX_OK` for a code the registry describes and `NGX_ERROR` for any
other. It is stateless, takes no request, and is defined in either build, so
configuration-time code may call it to check a status a directive named before
anything depends on it. Before `ngx_http_status_init()` has run, the lookup
index is zeroed and every code answers unregistered.

### `ngx_http_status_reason()`

Returns a pointer to the row's `reason` member and never to the row itself, so
a caller reaches neither `flags` nor `rfc_section` through it and can alter no
part of the registry. Keep the `const` on the pointee.

It returns `NULL` for an unregistered code, and **never** for a registered code
whose phrase is empty: such a code yields a non-NULL pointer to an `ngx_str_t`
whose `len` is zero. The two are different answers to different questions, and
[unregistered is not the same as
phraseless](#unregistered-is-not-the-same-as-phraseless) says how a caller tells
them apart.

### `ngx_http_status_is_cacheable()`

Answers non-zero for a code the registry describes as heuristically cacheable
and zero for any other, including zero for an unregistered code, which is the
conservative answer. It reports metadata and does not itself control nginx
caching; nor is it a substitute for the `expires` eligibility test, as
[`CACHEABLE` is not `EXPIRES_OK`](#cacheable-is-not-expires_ok) sets out.

### `ngx_http_status_register()`

Adds a definition a caller supplies, and is accepted only during the window in
which a configuration is mutable. Its rejections, its copy semantics, and the
lifetime it demands of the strings it is given are in
[lifecycle and custom registration](#4-lifecycle-and-custom-registration).

## 3. Registry definition, flags, and range

A row of the registry is one `ngx_http_status_def_t`, declared in
`src/http/ngx_http_status.h`:

```c
typedef struct {
    ngx_uint_t   code;
    ngx_str_t    reason;
    ngx_uint_t   flags;
    const char  *rfc_section;
} ngx_http_status_def_t;
```

**The type is `ngx_http_status_def_t`, with `_def_` in the middle.**
`ngx_http_status_t` is a different and entirely unrelated type: it is the state
a status line parser keeps, declared in `src/http/ngx_http.h` and used by
`ngx_http_parse_status_line()` and the upstream modules that call it. It has a
member named `code` as well, so a confusion between the two can compile further
than it should. A registry definition is always an `ngx_http_status_def_t`.

`reason` holds the fused `"NNN Phrase"` form — the number written out again in
front of the phrase, exactly the bytes that follow `HTTP/1.1 ` in a status line
— and not a bare phrase. [Why `reason` is the fused
form](#why-reason-is-the-fused-form) says why. A zero length `reason` is
legitimate and means the code is emitted as a bare number.

`rfc_section` names the section that defines the code, or marks the code as
internal to nginx. It is the registry's own record of where a code comes from,
and no function hands it out.

### Flags

`flags` is a mask over the following, declared in
`src/http/ngx_http_status.h`:

| Flag | Value | What it records |
| ---- | ----- | --------------- |
| `NGX_HTTP_STATUS_CACHEABLE` | `0x0001` | a cache may store the response heuristically, per RFC 9110, section 15.1 |
| `NGX_HTTP_STATUS_INFORMATIONAL` | `0x0002` | the code is 1xx |
| `NGX_HTTP_STATUS_CLIENT_ERROR` | `0x0004` | the code is 4xx, nginx's own 4xx codes included |
| `NGX_HTTP_STATUS_SERVER_ERROR` | `0x0008` | the code is 5xx |
| `NGX_HTTP_STATUS_EXPIRES_OK` | `0x0010` | nginx's `expires` processing applies to the response |
| `NGX_HTTP_STATUS_INTERNAL` | `0x0020` | the code is internal to nginx and has no standing outside it |

`CACHEABLE` and `EXPIRES_OK` are not the same set and answer different
questions; [`CACHEABLE` is not `EXPIRES_OK`](#cacheable-is-not-expires_ok) says
why one must not be tested in place of the other. No function yields `flags`
itself: `ngx_http_status_is_cacheable()` and `ngx_http_status_expires_ok()` each
return a mask over one flag, and the class flags are read by the registry and by
the code that consults it.

### Range

| Constant | Value | Meaning |
| -------- | ----- | ------- |
| `NGX_HTTP_STATUS_MIN` | 100 | lowest code the registry may describe, inclusive |
| `NGX_HTTP_STATUS_MAX` | 600 | upper bound, **exclusive**: 599 is the highest code and 600 is not a code |
| `NGX_HTTP_STATUS_RANGE` | 500 | the width of that range, which also sizes the lookup index |

The range test is a macro:

```c
#define ngx_http_status_in_range(s)                                          \
    ((s) >= NGX_HTTP_STATUS_MIN && (s) < NGX_HTTP_STATUS_MAX)
```

It is a lowercase parameterized macro deliberately, and not an inline function:
these sources are ANSI C, which has no keyword asking for a function to be
expanded at its call site, and a function with internal linkage defined in a
header would be reported as unused in every translation unit that does not call
it, which is fatal because warnings are treated as errors. As in other nginx
range macros, the argument is expanded more than once and must not have side
effects.

The bound the two field encoders impose is a separate thing and is not here:
each of them names it in its own file, as described in
[HTTP/2 and HTTP/3](#http2-and-http3). The registry publishes no width bound.

### Membership at a glance

The registry describes **48** status codes. **36** of them carry a wire reason
phrase and the remaining **12** are registered with an empty one. Which codes
those are, and why each is a member, is in
[Registry membership](../api/status_codes.md#registry-membership) and
[Registered status codes](../api/status_codes.md#registered-status-codes); this
guide does not reproduce that table.

The definition array is sized by `NGX_HTTP_STATUS_MAX_DEFS`, currently 64: one
reserved sentinel row, the 48 built-in rows, and the rest kept spare for
registration. That number is a file-local implementation detail of
`src/http/ngx_http_status.c` and no part of a module ABI. Treat the spare rows
as finite and handle `NGX_ERROR` from `ngx_http_status_register()` rather than
counting on them.

## 4. Lifecycle and custom registration

Alongside the [five public functions](#2-public-status-api),
`src/http/ngx_http_status.h` declares the **four** helpers the HTTP core and
two of the modules that ship with it use. Those four and the five above are the
whole of what the registry defines, and no third header declares any of them:

```c
ngx_int_t ngx_http_status_init(ngx_conf_t *cf);
void ngx_http_status_seal(void);
ngx_uint_t ngx_http_status_effective(ngx_http_request_t *r);
ngx_uint_t ngx_http_status_expires_ok(ngx_uint_t status);
```

They are declared there rather than beside the five because they are not part of
the API a module writes against, and keeping the two surfaces in two files is
what lets each be read for what it is. `src/http/ngx_http_status.h` includes
`<ngx_http.h>`, which is where `ngx_http_request_t` arrives from, so
`ngx_http_status_effective()` is declared with the request type it takes; and
`<ngx_http.h>` includes `src/http/ngx_http_status.h` in turn, which is how a
module that includes only the aggregator reaches the record type, the flags and
the range. Include the aggregator, as every HTTP source already does, rather
than the registry's own header on its own.

These are not the module-facing API — a module adopting the registry calls the
[five public functions](#2-public-status-api) — but they are not private either.
Each has **external linkage**, because each is called from a translation unit
other than the one that defines it: `init()` and `seal()` from the HTTP core
module, `effective()` from the log module and the variable evaluator, and
`expires_ok()` from the headers filter. Only the registry's own lookup is
`static`, and of the ten functions the registry defines it is the one no header
declares. A module may call `expires_ok()` and `effective()`, described in
[`CACHEABLE` is not `EXPIRES_OK`](#cacheable-is-not-expires_ok) and [effective
status](#effective-status); `init()` and `seal()` belong to the HTTP core, which
runs the lifecycle, and a module has no reason to call either.

Which row of the compiled in error page table a status selects is **not** a
question the registry answers, and no helper of it hands out a row. The bodies,
the bounds of the three spans of consecutive codes they are held for, the row
each span begins at, the row count derived from those bounds, and the function
that walks them are all private to `src/http/ngx_http_special_response.c` — the
bounds as macros of that file, the function with internal linkage — beside the
table they index and nowhere else. The
table is held to the rows those spans account for by an assertion on its own
size, so a row added to it or taken from it is a compile error rather than a
silent hole. A module has nothing to convert there and nothing to call.

### The window

```text
core preconfiguration          ngx_http_status_init(cf)
  |                              unseals, clears any registered rows and the
  |                              whole index, derives the index over the
  |                              built-in rows
  |  module preconfiguration   <-- ngx_http_status_register() is accepted here
  |  configuration parsing     <-- and here
  v
core postconfiguration         ngx_http_status_seal()
  |                              registration now answers NGX_ERROR always
  v
workers fork                   read only, for the life of the configuration
```

`ngx_http_status_init()` runs from the HTTP core module's preconfiguration,
which is the first thing the parsing of an `http{}` block does and so is before
any module can register a code of its own, and `ngx_http_status_seal()` runs
from that same module's postconfiguration, once the whole of the block has been
parsed. The window between them is every HTTP module's preconfiguration and the
parsing of every directive, all of it strictly before workers fork. Once it has
closed, `ngx_http_status_register()` answers `NGX_ERROR` unconditionally, and
the registry is read only for the life of that configuration. A worker only
reads it.

The window does **not** extend to postconfiguration. The HTTP core module is the
first HTTP module, so its postconfiguration runs before any other module's, and
the seal is already set by the time a module's own postconfiguration is reached.
Register from a module's preconfiguration, or from a directive handler of it;
never from postconfiguration, and never from a worker.

### Registration must be repeated for every configuration

Preconfiguration runs again for every configuration that is parsed — that is on
every `nginx -t` and on every reload — so `ngx_http_status_init()` is idempotent
by design, and it is idempotent by rebuilding rather than by remembering. Every
call releases the seal, clears the rows a registration may have written, zeroes
the whole of the lookup index, and derives that index over the built-in rows
again from the built-in table. Nothing carries over from the configuration
before, nothing about the state a call leaves behind depends on how many calls
came before it, and it allocates nothing.

That determinism is the point rather than a side effect. A configuration test
and a reload leave the registry in exactly the state a first start leaves it in:
the codes nginx was built with, the whole of the headroom free, and registration
open. Every page it writes is written in the master process, and a worker only
ever reads the two arrays, so no page of either is copied on write on a worker's
account.

A registration therefore lasts as long as the configuration that made it, and a
module that owns a custom definition must register it again during each
configuration cycle. A module that registers once and assumes the row survives a
reload is wrong: after the reload the code is unregistered again.

### What a registration is refused for

`ngx_http_status_register()` answers `NGX_ERROR`, in this order, for:

1. a null definition;
2. a registry that has already been sealed;
3. a code outside `NGX_HTTP_STATUS_MIN` to `NGX_HTTP_STATUS_MAX`;
4. a code the registry already describes, whether built in or registered
   earlier;
5. a definition array with no row left.

It writes nothing to the log of its own, so a caller is expected to report the
refusal. A registration always appends and never alters a built-in row, which is
what lets `ngx_http_status_init()` discard registrations by clearing the rows
above the built-in ones and rebuilding the index over what is left.

### What is copied and what is not

The definition is copied into the registry by value, so a caller may pass one
from its stack: the registry does not keep the pointer to the struct. That copy
duplicates the `ngx_str_t`, which is a length and a pointer, and **not** the
bytes the pointer addresses; `rfc_section` is copied as a pointer as well.

`reason.data` and `rfc_section` therefore remain borrowed, and each must stay
valid for as long as the configuration that registered it is in use — not merely
until parsing ends, but for the whole life of the workers serving that
configuration, since `ngx_http_status_reason()` reads those bytes while a
response is being sent. Static storage satisfies that, and so does the
configuration pool, whose allocations live as long as the cycle and survive the
fork with it. A buffer on the stack of the registering function, a per-request
pool such as `r->pool`, or a string the module later frees or reuses does not,
and leaves the registry holding a pointer into released memory.

### A module-defined code

The code below is defined by a module and is not an nginx built-in; nothing in
nginx knows 420, and registering it makes the registry able to describe it and
nothing more.

```c
/* file scope, so it outlives every configuration and every worker */

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

Note the fused `"NNN Phrase"` reason, the flags that describe the code rather
than decide anything about it, a code that is in range and not already
described, and a refusal that is reported and returned rather than ignored.
Because this is a module's preconfiguration, it runs for every configuration
parsed, which is exactly what the registration needs.

## 5. Setting and validating statuses

### Default and strict builds

In a default build `ngx_http_status_set()` stores the status and answers
`NGX_OK`, whatever the status is. It looks at nothing at all: the whole of the
function is two stores, and the code that reports an unregistered status is
inside a bare `#if (NGX_HTTP_STATUS_VALIDATION)` and is absent from the object
code of a default build altogether. This is why a default build sends what it
always sent.

A build configured with `--with-http_status_validation` additionally looks for a
status that nginx or a configuration chose and that the registry does not
describe, reports it at `NGX_LOG_ALERT` as `unregistered HTTP status NNN`, and —
in the setter alone — refuses it. The setter refuses every such status it is
given, whatever the request has been given before it, and each of the three
places that looks reports every status it objects to. Reporting is per call and
not per request, so a request whose status is objected to twice is two lines in
the log.

### Validation is scoped to the origin of the response

A status nginx or a module chose is examined only when `r->upstream == NULL`. A
status on a request that has an upstream attached is exempt, and it is exempt on
the origin of the response and never on the value of the status.
[Upstream-originated statuses](#7-upstream-originated-statuses) says why the
exemption has to be written that way.

The exemption is applied at the two places an upstream's status can reach — the
setter, and the gate in `ngx_http_special_response_handler()`, which is handed
one whenever `proxy_intercept_errors` diverts an upstream error into it — and
deliberately not at the `error_page ... =NNN` gate, because a configuration
authored that number whatever answered the request. `error_page 599 =599 /uri`
for an upstream that answered 599 is an overwrite and not a relayed response, so
it is reported there even though the two numbers are equal.

### Internal codes are registered, not violations

nginx's own codes 444, 494, 495, 496, 497, and 499 are rows of the registry like
any others, described with `NGX_HTTP_STATUS_INTERNAL`. They validate
successfully, and a strict build reports no violation for them.

### Statuses a handler returns

A handler that returns a status rather than setting one needs no change. Every
such return converges through `ngx_http_finalize_request()` into
`ngx_http_special_response_handler()`, and a strict build examines the status
there, once, at that convergence. Do not convert `return NGX_HTTP_*;` statements
one site at a time; there is nothing to convert.

That gate reports and does not refuse, and it stores the status it was given:
`r->err_status` is set to the `error` it was called with, unchanged. The same
holds at the gate in `ngx_http_send_error_page()`, which sees the status an
`error_page ... =NNN` overwrite named. Both are what answers a request that has
already gone wrong, so refusing there would leave it with no response at all
rather than with a worse one. Only `ngx_http_status_set()` refuses, because only
it has a caller with a result to act on, and it refuses every status the
registry does not describe — including one a gate has already reported for the
same request, which it reports again. A gate leaves nothing behind that silences
a later line. No status is rewritten to a different one at any of the three.

### `status_final` is a record, not a lock

`r->status_final` records that a response status has been chosen for the
request. It is not a lock: nothing refuses a store because it is already set,
and nothing in a module should test it in order to reject a later write. A
status is legitimately set again after a response has been decided — so that the
access log records that a client closed the connection, for one — and those
writes must go through.

Do not write `r->headers_out.status_line`, `r->err_status`, or `r->status_final`
by hand as a way of getting what the API does. `status_line` and `err_status`
have their own meanings, which the setter deliberately leaves alone, and
`status_final` is bookkeeping the setter maintains.

The bit is not written by hand anywhere in the tree. `ngx_http_status_set()` is
the one thing that writes it, and it writes it wherever it stores a status — the
teardown paths included, which write a status for the access log through the
setter like every other write; see [a site that only records an already-decided
status](#a-site-that-only-records-an-already-decided-status).

## 6. Converting direct assignments

The conversion replaces a **write** to `r->headers_out.status`. Before:

```c
    r->headers_out.status = status;
```

After:

```c
    if (ngx_http_status_set(r, status) != NGX_OK) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "invalid status");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
```

Keep the recipe as it stands. `NGX_LOG_ALERT` is the level, `"invalid status"`
is the message, and the request is answered `NGX_HTTP_INTERNAL_SERVER_ERROR`.
Do not lower the log level, reword the message, discard the result, or carry on
executing after a failure: a status the setter refused is one the response
cannot or should not carry, and the enclosing function has to stop.

A compile-time constant and a value computed at run time are converted exactly
the same way: both go through `ngx_http_status_set()`, and neither has a form of
its own.

### Adapting to the shape of the enclosing function

The three lines above suit a function that returns `ngx_int_t`. Where it does
not, keep the same log call and the same outcome, and express it the way the
function can.

#### A `void` request handler

Such a handler cannot return a status, so it finalizes with one instead. This is
the idiom `ngx_http_dav_put_handler()` already uses:

```c
    if (ngx_http_status_set(r, status) != NGX_OK) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "invalid status");
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
```

#### Perl XS code

XS code reports an error by `croak()` and not by returning an nginx status, so
use the idiom already established in `src/http/modules/perl/nginx.xs` rather
than introducing a return value the surrounding code does not expect.

#### A site that only records an already-decided status

A site that writes a status purely so that the access log has it calls the same
`ngx_http_status_set()` and keeps its own guard and control flow exactly as they
were, and handles the result with whichever idiom the enclosing function allows.
There is no separate exempt setter to call, and none should be invented.

**Never discard the result.** `(void) ngx_http_status_set(r, rc);` is not an
idiom this guide offers: a strict build answers `NGX_ERROR` without storing, so
casting the result away leaves the request carrying whatever status it had
before — which is precisely the status such a site exists to replace — and the
log then records that older status, or none.

The core's own two sites of this kind are the teardown paths, and they are
converted like any other. `ngx_http_terminate_request()` in
`src/http/ngx_http_request.c` keeps its guard exactly as it was and calls the
setter inside it:

```c
    if (rc > 0 && (mr->headers_out.status == 0 || mr->connection->sent == 0)) {
        if (ngx_http_status_set(mr, (ngx_uint_t) rc) != NGX_OK) {
            ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                          "invalid status");
        }
    }
```

and `ngx_http_free_request()` does the same under the same guard for the request
it is freeing. Both report a refusal and carry on, which is the whole of what is
left to them: neither returns a result to a caller, the request is being torn
down or freed, and there is no response left to answer a refusal in. Where a
refusal is reported the log keeps whatever status the request already carried.
Neither path provokes one in practice — `NGX_HTTP_CLOSE` and
`NGX_HTTP_CLIENT_CLOSED_REQUEST` are among the codes that reach them and the
registry describes both — and a default build refuses nothing at all.

Reporting a refusal and carrying on is right only where there is nothing else
the function can do. A module call site almost never is such a place: a handler
can return, a filter can return, and a `void` handler can finalize. Where any of
those is possible, act on the result rather than report it and continue.

#### A status that is merely being moved

Moving a status that was chosen somewhere else is still a write of a response
status, and it goes through the setter like every other. There is exactly one
such site in the tree — `ngx_http_send_header()` moving `err_status` over the
response status, last of all before the filters run — and it is written the way
any conversion is, in the shape [the enclosing
function](#adapting-to-the-shape-of-the-enclosing-function) allows:

```c
    if (r->err_status) {
        if (ngx_http_status_set(r, r->err_status) != NGX_OK) {
            ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                          "invalid status");
            return NGX_ERROR;
        }

        r->headers_out.status_line.len = 0;
    }
```

Two things about that shape are deliberate. The status line is cleared only
where the setter accepted the move, so a refused move leaves the request as it
was rather than half changed. And the refusal is returned rather than reported
and passed over: `ngx_http_send_header()` answers `NGX_ERROR`, which its callers
already handle as they handle any other failure of that function, and the
response is not sent.

**Nothing a request has accumulated stands aside for it.** The setter refuses
*every* unregistered status it is given, whatever the request was given before
it, and no path is to be added that would relax that. A path which stood aside
for a request whose status had already been reported would, from the second call
onward, send exactly what a strict build was configured to object to.

So what a strict build does with a status one of [the two
gates](#statuses-a-handler-returns) reported and let stand is refuse it here and
send nothing, rather than send a response the switch was asked to object to. A
default build sends it, and what a default build sends is what nginx has always
sent. None of this is a module's concern: a module with a status to choose calls
`ngx_http_status_set()` and uses the three-line wrapper above.

### Keep the side effects that were already there

The setter writes the response status and `status_final` and nothing else, so
anything a call site did alongside the old assignment stays at the call site. In
`ngx_http_send_header()`, for instance, the explicit

```c
    r->headers_out.status_line.len = 0;
```

remains beside the move, inside the branch in which the setter accepted it,
precisely because the API does not clear it.
Fused effects of a status on the rest of a response — the `header_only` and the
header clearing that 204 and 304 bring about in the header filter — likewise
stay where the decision is made, and are not registry flags.

### What must not be converted

- **Reads and comparisons.** A test such as
  `if (r->headers_out.status == NGX_HTTP_NOT_MODIFIED)` is a read, not a write;
  leave it alone. The conversion is about writes only.
- **Handler return statements.** They are examined once, centrally; see
  [statuses a handler returns](#statuses-a-handler-returns).
- **Upstream pass-through assignments.** `u->headers_in.status_n`, and the copy
  of it into `r->headers_out.status`, stay direct; see
  [upstream-originated statuses](#7-upstream-originated-statuses).
- **`err_status` bookkeeping.** `r->err_status` is a different status for a
  different purpose and the setter never touches it.

## 7. Upstream-originated statuses

nginx's contract with an upstream is to relay what it answered, faithfully,
including a code nginx has never heard of. A status an upstream chose is
therefore never validated, and none of the assignments that carry one is
converted.

The upstream boundary is crossed in **two** places, and this is the fact the
whole design of the exemption turns on.

**The direct relay**, in `ngx_http_upstream_process_headers()`, copies the
status and the status line the upstream sent straight into the response:

```c
    r->headers_out.status = u->headers_in.status_n;
    r->headers_out.status_line = u->headers_in.status_line;
```

**The interception path** is the other. When an upstream answers with an error
status and `proxy_intercept_errors` is on,
`ngx_http_upstream_intercept_errors()` reads `u->headers_in.status_n` and
eventually calls `ngx_http_upstream_finalize_request(r, u, status)` with it,
carrying an upstream-authored status into nginx's own error page machinery — the
very machinery whose central gate examines a status a handler returned.

Both crossings stay direct and unchanged. The exemption lives inside the setter
and at the central gate, and it is keyed on the origin of the response,
`r->upstream != NULL`, and not on anything recorded at a particular site.

**Why a site-scoped exemption would be wrong.** Marking the direct relay as
exempt and leaving it at that would cover the first crossing and miss the
second: with `proxy_intercept_errors` on, a perfectly valid upstream response
would reach the central gate and be reported as a violation. Keying on the
origin covers both crossings, and every future one, because it asks who chose
the status rather than where it was written.

`r->upstream` is an unconditional member of `ngx_http_request_t`, present in
every build configuration, so the predicate is always valid and never depends on
a build option.

### A status below 100 is legitimate and must be relayed

The HTTP/1.x status line parser accumulates exactly three digits and performs no
range check at all. An upstream that answers

```text
HTTP/1.1 042 Weird
```

therefore yields a numeric status of 42, and nginx relays it. Statuses like this
occur today and must keep working. A strict build must relay 42 unchanged: it
may not clamp it to 100, reject the response, or normalize the number because
`NGX_HTTP_STATUS_MIN` is 100. The registry's range describes what the registry
itself can hold, not what a response may carry, and the origin-scoped exemption
is what keeps the two apart.

The same applies to a code above the registry's membership but within three
digits: an upstream answering 599 is relayed as 599 whether or not the registry
describes it.

All of the `u->headers_in.status_n` assignments in the upstream protocol
modules — proxy, HTTP/2 proxy, FastCGI, uwsgi, SCGI, gRPC, and memcached — are
untouched, and a module that relays a status of someone else's choosing should
leave its own assignments alone on the same grounds.

## 8. Reason phrases and wire compatibility

### Why `reason` is the fused form

The HTTP/1.x emitter writes the version and then copies the rest of the status
line in one go: `"HTTP/1.1 "` followed by a single `ngx_copy()` of the value the
registry holds. Storing the fused `"NNN Phrase"` form is what preserves that
single copy. A bare phrase would force the number to be formatted separately, so
the data model carries the number twice — once as `code`, and once at the front
of `reason` — on purpose.

### Unregistered is not the same as phraseless

`ngx_http_status_reason()` has two distinct negative-looking answers, and they
mean different things:

| Case | Returns | Meaning |
| ---- | ------- | ------- |
| the code is not registered | `NULL` | the registry knows nothing about this code |
| the code is registered with an empty phrase | non-NULL, `len == 0` | the registry knows the code and records that it carries no phrase |

Both preserved fallbacks in the header filter emit the same bytes for these two
cases — the code as `%03ui` followed by a space, before CRLF — but a caller that
conflates the two answers is reading the registry wrongly, and an `ngx_str_t`
whose `len` is zero must not be dereferenced as though it had bytes. See
[why](../api/status_codes.md#unregistered-is-not-the-same-as-phraseless).

The 12 codes registered with an empty phrase are
`{100, 101, 102, 103, 203, 300, 444, 494, 495, 496, 497, 499}`. The full phrase
table is in
[Wire reason phrases](../api/status_codes.md#wire-reason-phrases).

Two interim responses have full status lines of their own and are emitted from
standalone constants rather than from the registry: `100 Continue` and
`103 Early Hints`. The registry does not replace those literals, and adopting
the API does not change how either is sent.

### Eight phrases that must not be corrected

nginx's wire phrases for the following differ from the name RFC 9110 recommends,
and they are kept byte for byte as nginx has always sent them:

| Status | nginx sends |
| ------ | ----------- |
| 302 | `302 Moved Temporarily` |
| 405 | `405 Not Allowed` |
| 408 | `408 Request Time-out` |
| 413 | `413 Request Entity Too Large` |
| 414 | `414 Request-URI Too Large` |
| 416 | `416 Requested Range Not Satisfiable` |
| 503 | `503 Service Temporarily Unavailable` |
| 504 | `504 Gateway Time-out` |

None of them was corrected, standardized, or brought into the RFC's spelling,
and none may be. A reason phrase is a recommendation only, so nginx's bytes stay
on the wire while the name from the RFC is recorded in the row's `rfc_section`.
Changing any of these would be a regression, not an improvement; see
[the reference](../api/status_codes.md#reason-phrases-that-differ-from-rfc-9110)
for the table of RFC names beside them.

### Status-driven side effects stay where they are

The effects 204 and 304 have on the rest of a response — setting `header_only`,
clearing the content type, the last-modified time, and the content length — are
decisions about a response and not properties of a status, so they remain in the
header filter and are deliberately not expressed as registry flags. The registry
describes a status; it never decides anything.

### HTTP/2 and HTTP/3

Both emit `:status` as a number and no reason phrase at all, so the `reason`
member is unused on those paths and no phrase-related change is possible there.
Their observable status output is unchanged. The three bytes each reserves for
the digits are why each of the two filters names a width bound of its own —
`NGX_HTTP_V2_STATUS_MAX` and `NGX_HTTP_V3_STATUS_MAX`, each of them 999 and each
private to the file that reserves those bytes. A status of four digits or more
is refused there, with an alert, before either filter reserves them. The
registry publishes no such bound and nothing else in the tree tests one; the
embedded Perl `status()` method, which is the one place such a value can arrive
from outside nginx, refuses it by asking `ngx_http_status_in_range()` instead.
See [HTTP/2 and HTTP/3](../api/status_codes.md#http2-and-http3).

## 9. Metadata and build behavior

### `CACHEABLE` is not `EXPIRES_OK`

These two flags answer different questions and describe different sets. Testing
one where the other is meant changes which responses get headers, so they are
kept separate on purpose.

| | `NGX_HTTP_STATUS_CACHEABLE` | `NGX_HTTP_STATUS_EXPIRES_OK` |
| - | - | - |
| Question | may a cache store this response heuristically? | does nginx's `expires` processing apply to this response? |
| Authority | RFC 9110, section 15.1 | nginx's own long-standing set |
| Members | 200, 203, 204, 206, 300, 301, 308, 404, 405, 410, 414, 501 | 200, 201, 204, 206, 301, 302, 303, 304, 307, 308 |
| Read by | `ngx_http_status_is_cacheable()` | `ngx_http_status_expires_ok()` |

Neither set contains the other. `EXPIRES_OK` has 201, 302, 303, 304, and 307,
which are not heuristically cacheable; `CACHEABLE` has 203, 404, 405, 410, 414,
and 501, which `expires` processing does not apply to. **Never call
`ngx_http_status_is_cacheable()` where `ngx_http_status_expires_ok()` is
meant**, or the other way round: substituting one would change which responses
receive an `Expires` header. See
[the reference](../api/status_codes.md#cacheable-is-not-expires_ok).

`CACHEABLE` is metadata and nothing more. It stores no policy and drives no
caching decision: what nginx actually caches comes from the configuration, from
`proxy_cache_valid` and its relatives, whose default set when only a time is
given is `{200, 301, 302}` — a third set again, and one this flag has no bearing
on.

### Effective status

`ngx_http_status_effective()` is the one implementation of the `$status`
selection that the log module and the variable evaluator both need. It answers,
in this order:

1. `r->err_status`, if it is set;
2. otherwise `r->headers_out.status`, if it is set;
3. otherwise the literal `9`, if the request used HTTP/0.9
   (`r->http_version == NGX_HTTP_VERSION_9`);
4. otherwise `0`.

It returns a number and never formatted text. Callers keep their own `%03ui`
formatting, which is why the literal 9 appears in a log as `009` and the final
fallback as `000`. A module that needs the same value should call this rather
than reimplement the cascade.

### The strict build

`--with-http_status_validation` is a **configure option**, not an nginx
configuration directive. There is no directive for it, and nothing in
`nginx.conf` turns it on or off.

| | |
| - | - |
| Configure option | `--with-http_status_validation` |
| Default | **off** |
| Shell variable in `auto/` | `HTTP_STATUS_VALIDATION` |
| C macro the option emits | `NGX_HTTP_STATUS_VALIDATION` |

```sh
./auto/configure --with-http_status_validation
```

With the option absent, `NGX_HTTP_STATUS_VALIDATION` is not defined, the
reporting code is not compiled, and behavior is what it always was. The strict
build is for development and conformance work, not for production traffic.

### What the registry guarantees not to have changed

- All 45 status constants in `src/http/ngx_http_request.h` survive verbatim: 43
  named `NGX_HTTP_*`, plus `NGX_HTTPS_CERT_ERROR` and `NGX_HTTPS_NO_CERT`. That
  includes both spellings of 494, `NGX_HTTP_NGINX_CODES` and
  `NGX_HTTP_REQUEST_HEADER_TOO_LARGE`. None was removed or renamed.
- No existing public function signature changed and no linker-visible symbol was
  removed. The registry functions are additions, and a module compiled against
  the previous headers still compiles and still links.
- A module that already includes `<ngx_http.h>` reaches the new API
  transitively, with no include to add.
- No `nginx.conf` directive was added, removed, or renamed.
- Strict validation is chosen at build time and is off by default; a default
  build behaves as it did, except at the embedded Perl setter and the HTTP/2 and
  HTTP/3 width bounds, both of which are unconditional and are described in
  [scope and compatibility](#1-scope-and-compatibility) and in section 8,
  [reason phrases and wire
  compatibility](#8-reason-phrases-and-wire-compatibility).
- One bit, `status_final:1`, is added to `ngx_http_request_t`, unconditionally
  rather than under the build option, and at the end of an existing bit field
  run where the compiler already had padding. The layout of the request
  structure therefore does not vary with the option:
  `sizeof(ngx_http_request_t)` is identical in a default and in a strict build
  of the same configuration, no member offset moves, and the module signature is
  identical between the two.
- HTTP/1.x status lines, error page bodies, `$status`, and HTTP/2 and HTTP/3
  `:status` output are byte compatible in a default build for every status that
  reaches an encoder. The two unconditional bounds above decide which statuses
  do: a script may no longer set a status below 100, zero excepted, or one of
  600 or more, and a status of four digits or more is no longer written into the
  three bytes an HTTP/2 or HTTP/3 `:status` reserves. Neither bound changes the
  bytes of any status that was already inside them.
- What this work adds is registry metadata and the ability to validate against
  it. It adds no built-in status code and corrects no reason phrase.

### What the registry does not back, and why

Four places in the HTTP subsystem still answer a question about a status without
reading the registry. Each is deliberate, each is behaviour-neutral, and each is
recorded here so that a reader who expects the registry to be behind everything
is not left to guess.

**The four class flags have no reader.** `NGX_HTTP_STATUS_INFORMATIONAL`,
`NGX_HTTP_STATUS_CLIENT_ERROR`, `NGX_HTTP_STATUS_SERVER_ERROR` and
`NGX_HTTP_STATUS_INTERNAL` are set on every row that belongs to them, and no C
code reads them. `ngx_http_status_is_cacheable()` reads `CACHEABLE` and
`ngx_http_status_expires_ok()` reads `EXPIRES_OK`, and those two are the only
flags any code reads anywhere in the tree; the C driver checks both of those
memberships in full, code by code, through the two accessors that expose them,
and it cannot reach the other four at all, because none of the five published
functions hands out a row's `flags` and that surface is closed. They are checked
all the same, by the `metadata` target of `misc/status_test/GNUmakefile`, which
reads the registry source rather than running the driver and holds every row to
carrying the flag of its class and no other, and the six codes nginx keeps for
itself to being the six rows flagged internal.

No read site was converted to a flag test either. The reason is that the reads a
class flag could stand in for are not class tests: they are equality tests
against one particular code — `status != NGX_HTTP_OK`,
`status == NGX_HTTP_NO_CONTENT`, `status == NGX_HTTP_NOT_MODIFIED` — each
selecting a specific encoding or a specific side effect, and a flag saying
"some 2xx" or "some 4xx" cannot express any of them. Converting them would
change what the code decides, which is the one thing this work must not do, so
the conversion was deliberately deferred; deferring it changes no behaviour,
because every one of those reads goes on deciding exactly what it decided
before. The four flags are therefore metadata a future consumer may read, held
correct by the `metadata` target until one does, and a reader for them would be
an addition to the published surface that nothing yet needs. The six internal
codes are accepted by strict validation because they are registry rows, not
because of the flag, so nothing depends on the flag being read.

**The error page row index is file-private.** `ngx_http_special_response.c`
selects the row of its own body table with `ngx_http_error_page_index()` and a
set of named `NGX_HTTP_ERROR_PAGE_*` span constants, not from the registry. The
registry record has exactly the four members it was specified with — `code`,
`reason`, `flags`, `rfc_section` — and none of them can carry an index into
another module's table, and no published function hands out a row for a caller
to extend. What the registry did remove there is the duplication: the offset
macros that file used to define are gone, and with them the hazard that made
them worth removing. All seven of them shared their names with seven macros in
the header filter, and four of those seven pairs disagreed on the value —
`NGX_HTTP_LAST_2XX` was 202 here and 207 there, and the three `OFF_` macros that
chain off it came out 1, 9 and 39 here against 6, 14 and 44 there — so a reader
who found one definition had no way of telling which of the two tables it was
right for. The mapping now lives in one named function, called from the two
places in that file which need it, with named spans in place of open-coded
arithmetic.

That trade is worth stating plainly, because the count of macros went up and not
down: ten `NGX_HTTP_ERROR_PAGE_*` constants stand where seven stood before. What
went down is what a reader has to hold in mind. Each of the ten names the span
it bounds, all ten sit above the table rather than inside its initializer, and
not one of them appears in any other file in the tree.

**The HTTP/2 and HTTP/3 filters name no registry symbol.** Neither encoding
carries a reason phrase at all: HPACK and QPACK both carry `:status` as a
number, so there is nothing in either for `ngx_http_status_reason()` to supply,
and the status tests that remain in those filters are the equality tests
described above — a `switch` on the code in the HTTP/2 filter and a chain of
comparisons in the HTTP/3 one — each selecting a static table index or one of
the fused side effects that stay where they are. What both filters gained
instead is the width bound described in
[scope and compatibility](#1-scope-and-compatibility), which is theirs and is
not a registry question.

**The two 1xx status lines stay as constants.** `103 Early Hints` is
`ngx_http_early_hints_status_line` in the header filter and `100 Continue` is a
literal in `ngx_http_request_body.c`, and both are whole status lines: each
carries the `HTTP/1.1 ` prefix, the number, the phrase and a trailing CRLF. A
`reason` member is only the bytes that follow `HTTP/1.1 ` and carries no CRLF,
so neither constant has the shape of one. Both codes are registry rows, with the
zero-length `reason` every code nginx sends without a phrase from the table has;
what is not in the registry is the line, not the code.


## 10. Adoption checklist

- Find the direct writes to `r->headers_out.status` in the module, and separate
  them from reads: a comparison is not a write and is not converted.
- Replace each write of a status the module itself chose with
  `ngx_http_status_set()`, and handle `NGX_ERROR` with the idiom the enclosing
  function allows — return `NGX_HTTP_INTERNAL_SERVER_ERROR`, finalize with it in
  a `void` handler, or `croak()` in XS.
- Keep everything the old assignment sat beside: adjacent stores such as
  `r->headers_out.status_line.len = 0`, guards, and control flow are unchanged
  by the conversion.
- Leave handler returns alone, and leave upstream pass-through assignments as
  they are.
- If the module registers a status of its own, register it during every
  configuration cycle, before sealing, and check the result.
- Keep `reason.data` and `rfc_section` alive for as long as the configuration
  that registered them is in use; static storage or the configuration pool,
  never the stack and never a per-request pool.
- Query metadata through the right accessor: `ngx_http_status_is_cacheable()`
  for heuristic cacheability, `ngx_http_status_expires_ok()` for `expires`
  eligibility, and never one for the other.
- Build and test both ways: a default build and one configured with
  `--with-http_status_validation`.
- Verify the wire output and the logs are unchanged for the statuses the module
  already produced — the internal codes 444 and 499 among them, an HTTP/0.9
  request logging `009`, an unregistered code falling back to its bare number,
  and an upstream `042` relayed as it arrived.

### References

- [Status Code Registry](../api/status_codes.md) — every registered code, its
  wire reason phrase, its flags, and its RFC provenance.
- [Documentation index](../index.md) — the project documentation landing page.
