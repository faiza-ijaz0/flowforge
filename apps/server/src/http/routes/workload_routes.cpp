#include "http/routes/workload_routes.hpp"

#include <charconv>

#include "http/error_response.hpp"
#include "json/workload_json.hpp"

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

void register_workload_routes(httplib::Server& server,
                              const std::shared_ptr<services::WorkloadService>& workload_service,
                              const std::shared_ptr<infra::MetricsRegistry>& metrics) {
  server.Post(
      "/api/v1/workloads", [workload_service, metrics](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        try {
          body = nlohmann::json::parse(req.body);
        } catch (const nlohmann::json::parse_error& e) {
          // Deliberately not logged: a malformed request body is
          // caller input, not an operational event -- mirrors
          // job_routes.cpp's identical handling.
          metrics->increment_counter("flowforge_workloads_rejected_total");
          write_error(res, make_error(ErrorCode::Validation, std::string("invalid JSON body: ") + e.what()));
          return;
        }

        auto request = parse_create_workload_request(body);
        if (!request) {
          metrics->increment_counter("flowforge_workloads_rejected_total");
          write_error(res, request.error());
          return;
        }

        auto created = workload_service->create_workload(*request);
        if (!created) {
          write_error(res, created.error());
          return;
        }

        nlohmann::json items = nlohmann::json::array();
        for (const auto& item : created->items) {
          nlohmann::json entry{{"job_id", item.job_id.value()}, {"scheduled", item.scheduled}};
          if (item.reason) {
            entry["reason"] = *item.reason;
          }
          items.push_back(std::move(entry));
        }

        nlohmann::json response_body = to_json(created->workload);
        response_body["items"] = items;
        res.status = 201;
        res.set_content(response_body.dump(), "application/json");
      });

  server.Get("/api/v1/workloads", [workload_service](const httplib::Request& req, httplib::Response& res) {
    const std::size_t limit = parse_size_param(req, "limit", 50);
    const std::size_t offset = parse_size_param(req, "offset", 0);

    auto workloads = workload_service->list_workloads(limit, offset);
    if (!workloads) {
      write_error(res, workloads.error());
      return;
    }
    nlohmann::json items = nlohmann::json::array();
    for (const auto& workload : *workloads) {
      items.push_back(to_json(workload));
    }
    res.set_content(nlohmann::json{{"workloads", items}}.dump(), "application/json");
  });

  server.Get("/api/v1/workloads/:id",
             [workload_service](const httplib::Request& req, httplib::Response& res) {
               auto workload = workload_service->get_workload(req.path_params.at("id"));
               if (!workload) {
                 write_error(res, workload.error());
                 return;
               }
               res.set_content(to_json(*workload).dump(), "application/json");
             });
}

}  // namespace flowforge::server
