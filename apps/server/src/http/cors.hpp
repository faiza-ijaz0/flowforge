#pragma once

#include <httplib.h>

#include <string>

namespace flowforge::server {

/// Registers CORS support so a browser-based client on a different origin
/// (the Next.js dashboard, e.g. `http://localhost:3000`) can call this
/// JSON API (e.g. `http://localhost:8080`) directly from client-side
/// JavaScript -- required because `fetch()` calls made from a `"use
/// client"` dashboard component (e.g. the Create Job form) execute in the
/// browser itself, unlike a Server Component's `fetch()`, which runs in
/// Node.js and is never subject to CORS. Centralized entirely in the HTTP
/// transport layer: nothing below `apps/server` (`JobService`,
/// `PriorityScheduler`, `LocalWorkerPool`, `JobExecutor`,
/// `HandlerRegistry`, any repository, any domain type) knows this exists
/// or is touched by it.
///
/// Design:
///  - `allowed_origin` is compared for an *exact match* against the
///    request's `Origin` header; only a match gets
///    `Access-Control-Allow-Origin` echoed back -- never a blanket `"*"`.
///    An empty `allowed_origin` disables CORS headers entirely (every
///    browser cross-origin request is then rejected by the browser
///    itself), which is the safe default for an environment with no
///    known browser client. Supporting a different origin in a different
///    environment (e.g. a deployed dashboard's real domain in production)
///    is a matter of changing this one string
///    (`infra::AppConfig::cors_allowed_origin`,
///    `FLOWFORGE_CORS_ALLOWED_ORIGIN`) -- no other code changes.
///  - Preflight `OPTIONS` requests are intercepted globally via
///    `httplib::Server::set_pre_routing_handler` (runs before route
///    matching), so no individual route needs its own `OPTIONS` handler
///    and every current/future route is covered uniformly. A preflight
///    response is `204 No Content` with `Access-Control-Allow-Methods`/
///    `Access-Control-Allow-Headers`/`Access-Control-Max-Age` -- those
///    three are only meaningful on a preflight response, per the CORS
///    spec.
///  - `httplib::Server::set_post_routing_handler` adds
///    `Access-Control-Allow-Origin` to every *actual* response
///    (GET/POST/etc.), not only to preflight responses -- the browser
///    checks this header on the real response too, not just the
///    preflight.
///  - A request with no `Origin` header (`curl`, server-to-server calls,
///    the existing httplib-based integration tests, a Server Component's
///    own `fetch()`) is left untouched either way: CORS is a browser-only
///    enforcement mechanism, so non-browser callers behave exactly as
///    before this change.
void register_cors(httplib::Server& server, const std::string& allowed_origin);

}  // namespace flowforge::server
