#include "http/cors.hpp"

namespace flowforge::server {

namespace {

// The API currently only ever serves GET and POST (see health_routes.cpp,
// job_routes.cpp, worker_routes.cpp, workflow_routes.cpp) -- OPTIONS is
// listed too since the preflight request itself uses it. Kept as an
// explicit, accurate list rather than a broad "*" -- update this if a new
// HTTP method is ever added to a route.
constexpr const char* kAllowedMethods = "GET, POST, OPTIONS";

// The dashboard's api-client.ts (apps/dashboard/src/lib/api-client.ts)
// unconditionally sends Content-Type on every request; that is the only
// non-CORS-safelisted header this API needs to accept cross-origin.
constexpr const char* kAllowedHeaders = "Content-Type";

// How long (seconds) a browser may cache a successful preflight response
// before re-checking it -- a standard CORS optimization, not required for
// correctness.
constexpr const char* kPreflightMaxAgeSeconds = "600";

[[nodiscard]] bool origin_is_allowed(const std::string& allowed_origin, const std::string& request_origin) {
  return !allowed_origin.empty() && !request_origin.empty() && request_origin == allowed_origin;
}

}  // namespace

void register_cors(httplib::Server& server, const std::string& allowed_origin) {
  // Preflight-only headers (Allow-Methods/Allow-Headers/Max-Age are
  // meaningless on a non-preflight response, so they belong here, not in
  // the post-routing handler below). Deliberately does NOT also set
  // Access-Control-Allow-Origin/Vary: httplib::Response::set_header()
  // always appends to a multimap rather than replacing (see
  // Response::set_header's `headers.emplace(key, val)`), and
  // `set_post_routing_handler`'s callback runs unconditionally for
  // *every* response actually written to the wire -- including this
  // short-circuited OPTIONS one -- so setting the same header in both
  // places would emit it twice, which browsers treat as an invalid CORS
  // response (the spec requires exactly one Access-Control-Allow-Origin
  // value) even when both copies are identical.
  server.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
    if (req.method != "OPTIONS") {
      return httplib::Server::HandlerResponse::Unhandled;
    }
    res.set_header("Access-Control-Allow-Methods", kAllowedMethods);
    res.set_header("Access-Control-Allow-Headers", kAllowedHeaders);
    res.set_header("Access-Control-Max-Age", kPreflightMaxAgeSeconds);
    res.status = 204;
    return httplib::Server::HandlerResponse::Handled;
  });

  // Runs for every response (preflight and real alike) -- the single
  // place Access-Control-Allow-Origin/Vary are set, so each appears at
  // most once.
  server.set_post_routing_handler([allowed_origin](const httplib::Request& req, httplib::Response& res) {
    if (origin_is_allowed(allowed_origin, req.get_header_value("Origin"))) {
      res.set_header("Access-Control-Allow-Origin", req.get_header_value("Origin"));
      res.set_header("Vary", "Origin");
    }
  });
}

}  // namespace flowforge::server
