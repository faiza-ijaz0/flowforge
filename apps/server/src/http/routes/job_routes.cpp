#include "http/routes/job_routes.hpp"

#include <charconv>

#include "http/error_response.hpp"
#include "json/job_json.hpp"

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

void register_job_routes(httplib::Server& server, const std::shared_ptr<services::JobService>& job_service,
                         const std::shared_ptr<infra::MetricsRegistry>& metrics) {
  server.Post("/api/v1/jobs", [job_service, metrics](const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
      body = nlohmann::json::parse(req.body);
    } catch (const nlohmann::json::parse_error& e) {
      write_error(res, make_error(ErrorCode::Validation, std::string("invalid JSON body: ") + e.what()));
      return;
    }

    auto request = parse_create_job_request(body);
    if (!request) {
      write_error(res, request.error());
      return;
    }

    auto created = job_service->create_job(*request);
    if (!created) {
      write_error(res, created.error());
      return;
    }

    metrics->increment_counter("flowforge_jobs_created");
    res.status = 201;
    res.set_content(to_json(*created).dump(), "application/json");
  });

  server.Get("/api/v1/jobs", [job_service](const httplib::Request& req, httplib::Response& res) {
    const std::size_t limit = parse_size_param(req, "limit", 50);
    const std::size_t offset = parse_size_param(req, "offset", 0);

    auto jobs = job_service->list_jobs(limit, offset);
    if (!jobs) {
      write_error(res, jobs.error());
      return;
    }

    nlohmann::json items = nlohmann::json::array();
    for (const auto& job : *jobs) {
      items.push_back(to_json(job));
    }
    res.set_content(nlohmann::json{{"jobs", items}}.dump(), "application/json");
  });

  server.Get("/api/v1/jobs/:id", [job_service](const httplib::Request& req, httplib::Response& res) {
    auto job = job_service->get_job(req.path_params.at("id"));
    if (!job) {
      write_error(res, job.error());
      return;
    }
    res.set_content(to_json(*job).dump(), "application/json");
  });

  server.Post("/api/v1/jobs/:id/cancel",
              [job_service, metrics](const httplib::Request& req, httplib::Response& res) {
                auto job = job_service->cancel_job(req.path_params.at("id"));
                if (!job) {
                  write_error(res, job.error());
                  return;
                }
                metrics->increment_counter("flowforge_jobs_cancelled");
                res.set_content(to_json(*job).dump(), "application/json");
              });
}

}  // namespace flowforge::server
