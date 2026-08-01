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

A default build sends what it always sent. Every status line, every error page
body, every `$status` value, and every HTTP/2 and HTTP/3 `:status` field is byte
for byte what it was before the registry existed. Strict validation is a
separate thing a build may be configured to do, with
`--with-http_status_validation`, and it is off unless it is asked for; see
[the strict build](#the-strict-build).

One bound is new, and it belongs to two encodings rather than to the API: a
status of four digits or more cannot be carried over HTTP/2 or HTTP/3, because
each reserves exactly three bytes for the digits of `:status` and the `%03ui`
conversion that writes them pads a narrower value but never truncates a wider
one. It is held where each of those encodings has it — in the two header
filters, before either reserves those bytes — and at the embedded Perl
`status()` method, which is handed whatever integer a script passes and is the
one place a status arrives from outside nginx at all.

It is deliberately not held where a status is chosen, so nothing about adopting
this API narrows what a call site may ask for. An HTTP/1.x status line reserves
`NGX_INT_T_LEN` bytes for the same number and carries any width, and a
configuration has always been able to ask for one: `error_page 404 =1234 /wide;`
is accepted and answers `HTTP/1.1 1234 `. Nothing nginx chooses for itself comes
near that width in any case. [The range](#range) names the macro,
[HTTP/2 and HTTP/3](#http2-and-http3) says what the two encoders do with it, and
[converting direct assignments](#6-converting-direct-assignments) says what a
caller does with a result.

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

Direct stores of `r->headers_out.status` are kept in the core beside it in three
places, and a module converting call sites of its own has no use for any of
them:
an upstream's status being relayed into the response, which
[upstream-originated statuses](#7-upstream-originated-statuses) covers; the two
teardown stores made only so that the access log has a status, which [a site
that only records an already-decided
status](#a-site-that-only-records-an-already-decided-status) sets out; and the
`NGX_HTTP_OK` that the embedded Perl `send_http_header()` falls back to for a
script that set no status at all.

One further store is made by the registry module itself rather than at a call
site of it: `ngx_http_status_promote()`, which moves the error status of a
request over its response status where `ngx_http_send_header()` finishes a
response, and which [a status that is merely being
moved](#a-status-that-is-merely-being-moved) sets out. Nothing else in the tree
writes that member.

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

Reporting is once for a request; refusal is once for a call. The bit the request
carries decides only the first of them — it holds the log to one line however
many of that request's statuses are refused — and it grants nothing. A status
refused once and then set again, from the same caller or from another, is
examined and refused again, so no sequence of calls arrives at a status a strict
build was configured to object to.

That is why `ngx_http_send_header()` moves the error status of a request over
its response status with `ngx_http_status_promote()` and not with this function;
see [a status that is merely being
moved](#a-status-that-is-merely-being-moved). A status one of [the two
gates](#statuses-a-handler-returns) reported and deliberately let stand is not
taken away from the response by the move that finishes it, and it is not
reported a second time either.

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
effects. `ngx_http_status_wire_width_ok()` is the separate bound that the two
field encoders impose, described in [HTTP/2 and HTTP/3](#http2-and-http3); it is
a macro for the same reasons, but expands its argument only once.

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
`src/http/ngx_http.h` declares the helpers the HTTP core and the modules that
ship with it use:

```c
ngx_int_t ngx_http_status_init(ngx_conf_t *cf);
void ngx_http_status_seal(void);
ngx_uint_t ngx_http_status_effective(ngx_http_request_t *r);
ngx_uint_t ngx_http_status_expires_ok(ngx_uint_t status);
```

They are declared there rather than in `src/http/ngx_http_status.h` because each
takes a type that arrives through that aggregator, `ngx_conf_t` or
`ngx_http_request_t`, and declaring them there is what lets
`src/http/ngx_http_status.h` include nothing but `ngx_config.h` and `ngx_core.h`
and so be included directly, on its own, from anywhere.

These are not the module-facing API — a module adopting the registry calls the
[five public functions](#2-public-status-api) — but they are not private either.
Each has **external linkage**, because each is called from a translation unit
other than the one that defines it: `init()` and `seal()` from the HTTP core
module, `effective()` from the log module and the variable evaluator, and
`expires_ok()` from the headers filter. Only the registry's own lookup and the
helper that discards a configuration's registrations are `static`. A module may
call `expires_ok()` and `effective()`, described in [`CACHEABLE` is not
`EXPIRES_OK`](#cacheable-is-not-expires_ok) and [effective
status](#effective-status); the other two belong to the core and a module has no
reason to call them.

The row of the error page table that a status selects is **not** among them.
That mapping belongs to `src/http/ngx_http_special_response.c`, which owns the
table and is the only thing that reads it, and it is file-local there: a span
table, a compile-time assertion that the spans account for exactly as many rows
as the table has, and a `static` function. The registry publishes nothing about
it.

### The window

```text
core preconfiguration          ngx_http_status_init(cf)
  |                              unseals, discards registrations, and
  |                              derives the built-in rows the first time
  |  configuration parsing     <-- ngx_http_status_register() is accepted here
  |  every postconfiguration   <-- and here
  v
core init module               ngx_http_status_seal()
  |                              registration now answers NGX_ERROR always
  v
workers fork                   read only, for the life of the configuration
```

`ngx_http_status_init()` runs from the HTTP core module's preconfiguration,
which is before any `http{}` directive is parsed, and `ngx_http_status_seal()`
from the HTTP core module's `init module` handler, which runs after every
module's postconfiguration has. The window between them is the parsing of a
configuration and the postconfiguration pass that follows it, both strictly
before workers fork. Once it has closed, `ngx_http_status_register()` answers
`NGX_ERROR` unconditionally, and the registry is read only for the life of that
configuration. A worker only reads it.

Register from a directive handler, from a module's own preconfiguration, or from
its postconfiguration, and never from a worker. Sealing from the `init module`
handler rather than from the core's postconfiguration is what makes the third of
those safe: were the seal set during the postconfiguration pass, whether a
registration were accepted would depend on where a module sat in the module
order.

### Registration must be repeated for every configuration

Preconfiguration runs again for every configuration that is parsed — that is on
every `nginx -t` and on every reload — so `ngx_http_status_init()` is idempotent
by design. It is not, however, a rebuild. The built-in definitions and the
lookup index over them do not depend on the configuration, so they are derived
once in the life of the process, by the first call, and every later call finds
them exactly as the first one left them. What a later call does is release the
seal and discard what the configuration before it registered — and a
configuration that registered nothing leaves the registry untouched. It
allocates nothing either way.

That is deliberate rather than incidental. A reload writes the seal, and the
rows and index entries belonging to what the configuration before it registered
and to what the new one registers — and nothing besides. Where neither
registered anything of its own, which is every build that ships, the seal is the
only thing written after the workers forked and began sharing those pages.

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
what lets `ngx_http_status_init()` discard registrations by forgetting them.

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
given, whatever the request has been given before it. The report, by contrast,
is made once for a request rather than once per call or once per place, so a
request whose status is chosen more than once produces one line in the log and
not several.

### Validation is scoped to the origin of the response

Validation applies only when `r->upstream == NULL`. A status on a request that
has an upstream attached is exempt, and it is exempt on the origin of the
response and never on the value of the status.
[Upstream-originated statuses](#7-upstream-originated-statuses) says why the
exemption has to be written that way.

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
same request. What a gate leaves behind suppresses a second line in the log and
nothing more. No status is rewritten to a different one at any of the three.

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

The bit is written by hand in exactly two places, both of them the core's own:
the teardown paths `ngx_http_terminate_request()` and `ngx_http_free_request()`,
which store a status for the access log where a refusal could not be answered
and keep the bookkeeping true by setting the bit beside the store. That is a
narrow exemption and not a pattern to copy; the conditions it rests on are set
out in [a site that only records an already-decided
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
the same way: both go through `ngx_http_status_set()`, and there is no second
entry point for either.

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

There is one narrow exemption, and it is the core's own. Where a status is being
recorded for the log **and** the function has no way whatever to answer a
refusal, the status is stored directly and `r->status_final` is set beside it,
so that the log is given the status it was given. That is what the two teardown
paths do, `ngx_http_terminate_request()` in `src/http/ngx_http_request.c`:

```c
    if (rc > 0 && (mr->headers_out.status == 0 || mr->connection->sent == 0)) {
        mr->headers_out.status = rc;
        mr->status_final = 1;
    }
```

and `ngx_http_free_request()`, which writes the same two members under the same
guard. Three things together are what make the exemption right there, and a
module should hold itself to all three before claiming it:

- the status is not being chosen for a response, and no response carries it —
  `NGX_HTTP_CLOSE` and `NGX_HTTP_CLIENT_CLOSED_REQUEST` are among the codes that
  reach these two paths, and neither is ever sent;
- the request is being torn down or freed, so there is no caller to return a
  result to, nothing left to answer with, and no response to answer it in;
- losing the status would be the worse outcome of the two, the whole purpose of
  the store being the access log record.

A module call site almost never satisfies the second of those: a handler can
return, a filter can return, and a `void` handler can finalize. Where any of
those is possible, the setter and its result are what to use, and the direct
store is not an alternative that is open.

#### A status that is merely being moved

Moving a status that was already examined where it was chosen is not choosing a
status, and it does not go through the setter. There is exactly one such site in
the tree — `ngx_http_send_header()` moving `err_status` over the response status
— and it is served by an internal seam, `ngx_http_status_promote()`, declared in
`src/http/ngx_http.h` beside the lifecycle helpers:

```c
    if (r->err_status) {
        ngx_http_status_promote(r);

        r->headers_out.status_line.len = 0;
    }
```

The seam makes the same two stores the setter makes and nothing else. It
examines nothing, reports nothing, and returns `void`, so it structurally cannot
refuse. That is deliberate: the status it moves was examined where it was
chosen, and where one of the two gates reported a status the registry does not
describe it deliberately let it stand, having no caller of its own to answer a
refusal to. Refusing the move would take away the very response that gate exists
to produce and leave a request which had already gone wrong with no response at
all rather than with a worse one.

Keeping the move out of the setter is what lets the setter refuse **every**
unregistered status it is given, whatever the request has been given before it.
A setter that stood aside for an already-reported request would, from the second
call onward, store exactly what a strict build was configured to object to.

**A module never calls the seam.** It is not part of the API a module writes
against: it exists for this one core site, for one status, and it is declared
among the helpers precisely to say so. A module with a status to choose calls
`ngx_http_status_set()` and uses the three-line wrapper above.

### Keep the side effects that were already there

The setter writes the response status and `status_final` and nothing else, so
anything a call site did alongside the old assignment stays at the call site. In
`ngx_http_send_header()`, for instance, the explicit

```c
    r->headers_out.status_line.len = 0;
```

remains beside the move, precisely because the API does not clear it.
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
the digits are the reason for `ngx_http_status_wire_width_ok()`, the width bound
[the range](#range) names: a status of four digits or more is refused by each of
these two filters, with an alert, before either reserves those bytes, and by the
embedded Perl `status()` method that is the one place such a value can arrive
from outside nginx. Nothing else consults it. See
[HTTP/2 and HTTP/3](../api/status_codes.md#http2-and-http3).

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
  build behaves as it did.
- `status_final:1` and `status_reported:1` are added to `ngx_http_request_t`
  unconditionally, never under the build option, and at the end of an existing
  bit field run where the compiler already had padding. The layout of the
  request structure therefore does not vary with the option:
  `sizeof(ngx_http_request_t)` is identical in a default and in a strict build
  of the same configuration, no member offset moves, and the module signature is
  identical between the two.
- HTTP/1.x status lines, error page bodies, `$status`, and HTTP/2 and HTTP/3
  `:status` output are byte compatible in a default build.
- What this work adds is registry metadata and the ability to validate against
  it. It adds no built-in status code and corrects no reason phrase.

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
