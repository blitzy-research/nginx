# blitzy-nginx

NGINX HTTP status code registry refactoring with RFC 9110 compliance

## Documentation

- [Status Code Registry](api/status_codes.md) — the status codes the registry
  describes: the wire reason phrase a code carries, the semantic flags it is
  described with, and the specification section that defines it.
- [Status Code API Migration](migration/status_code_api.md) — for module
  authors: the functions the registry publishes, what each of them promises,
  and how a direct write to `r->headers_out.status` becomes a call.
