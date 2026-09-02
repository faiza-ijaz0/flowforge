#include "http/routes/workload_routes.hpp"

#include <charconv>

#include "flowforge/services/user_import.hpp"
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
                              const std::shared_ptr<infra::Logger>& logger,
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
          items.push_back(to_json(item));
        }

        nlohmann::json response_body = to_json(created->workload);
        response_body["items"] = items;
        res.status = 201;
        res.set_content(response_body.dump(), "application/json");
      });

  // Phase 3B: bulk user import from an uploaded CSV file. A distinct,
  // separately-registered path (not a query param on POST
  // /api/v1/workloads) -- see docs/architecture/user-import.md, "API
  // changes" -- multipart/form-data with a single "file" field, mirroring
  // how the rest of this phase reuses existing conventions rather than
  // inventing a parallel API shape.
  server.Post("/api/v1/workloads/user-imports", [workload_service, logger, metrics](
                                                    const httplib::Request& req, httplib::Response& res) {
    if (!req.is_multipart_form_data()) {
      write_error(
          res, make_error(ErrorCode::Validation, "request must be multipart/form-data with a 'file' field"));
      return;
    }
    if (!req.has_file("file")) {
      write_error(res, make_error(ErrorCode::Validation, "missing required 'file' field"));
      return;
    }

    const auto& file = req.get_file_value("file");
    // The one place the CSV-upload route knows "user import"
    // means "user.process" -- see user_import.hpp's class
    // comment. WorkloadService itself never appears in this
    // sentence.
    auto imported = services::import_users_from_csv(*workload_service, file.content, logger, metrics);
    if (!imported) {
      write_error(res, imported.error());
      return;
    }

    res.status = 201;
    res.set_content(to_json(*imported).dump(), "application/json");
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

  // Bounded, paginated per-item detail view -- see
  // docs/architecture/user-import.md, "Bounded item retrieval". Mirrors
  // `GET /api/v1/jobs/{id}/attempts`'s additive-endpoint convention
  // (job_routes.cpp).
  server.Get("/api/v1/workloads/:id/items", [workload_service](const httplib::Request& req,
                                                               httplib::Response& res) {
    const std::size_t limit = parse_size_param(req, "limit", 50);
    const std::size_t offset = parse_size_param(req, "offset", 0);

    auto page = workload_service->list_items(req.path_params.at("id"), limit, offset);
    if (!page) {
      write_error(res, page.error());
      return;
    }

    nlohmann::json items = nlohmann::json::array();
    for (const auto& job : page->jobs) {
      items.push_back(to_json_workload_item(job));
    }
    res.set_content(
        nlohmann::json{{"items", items}, {"total", page->total}, {"limit", limit}, {"offset", offset}}.dump(),
        "application/json");
  });
}

}  // namespace flowforge::server
