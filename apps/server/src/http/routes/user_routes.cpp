#include "http/routes/user_routes.hpp"

#include <algorithm>
#include <charconv>

#include "http/error_response.hpp"
#include "json/user_json.hpp"

namespace flowforge::server {

namespace {

std::size_t parse_size_param(const httplib::Request& req, const char* name, std::size_t default_value) {
  if (!req.has_param(name)) {
    return default_value;
  }
  const std::string raw = req.get_param_value(name);
  std::size_t value{};
  auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), value);
  if (ec != std::errc{} || ptr != raw.data() + raw.size()) {
    return default_value;
  }
  return value;
}

void write_error(httplib::Response& res, const Error& error) {
  res.status = http_status_for(error.code());
  res.set_content(to_error_body(error).dump(), "application/json");
}

}  // namespace

void register_user_routes(httplib::Server& server,
                          const std::shared_ptr<persistence::IUserRepository>& user_repository) {
  server.Get("/api/v1/users", [user_repository](const httplib::Request& req, httplib::Response& res) {
    // Mirrors GET /api/v1/products'/GET /api/v1/categories' pagination
    // convention exactly (see product_routes.cpp) -- capped at 200 for
    // the same reason: a dashboard table page, not a bulk-export endpoint.
    const std::size_t limit = std::min(parse_size_param(req, "limit", 50), std::size_t{200});
    const std::size_t offset = parse_size_param(req, "offset", 0);

    auto users = user_repository->list(limit, offset);
    if (!users) {
      write_error(res, users.error());
      return;
    }
    auto total = user_repository->count();
    if (!total) {
      write_error(res, total.error());
      return;
    }

    nlohmann::json items = nlohmann::json::array();
    for (const auto& user : *users) {
      items.push_back(to_json(user));
    }
    res.set_content(
        nlohmann::json{{"users", items}, {"total", *total}, {"limit", limit}, {"offset", offset}}.dump(),
        "application/json");
  });
}

}  // namespace flowforge::server
