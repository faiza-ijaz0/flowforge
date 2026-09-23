#include "http/routes/job_routes.hpp"

#include <charconv>
#include <tuple>

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
                         const std::shared_ptr<engine::IScheduler>& scheduler,
                         const std::shared_ptr<engine::IWorkerPool>& worker_pool,
                         const std::shared_ptr<engine::IExecutionManager>& execution_manager,
                         const std::shared_ptr<infra::MetricsRegistry>& metrics) {
  server.Post(
      "/api/v1/jobs", [job_service, scheduler, metrics](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        try {
          body = nlohmann::json::parse(req.body);
        } catch (const nlohmann::json::parse_error& e) {
          // Deliberately not logged: a malformed request body is caller
          // input, not an operational event, and e.what() here echoes
          // back arbitrary attacker/caller-controlled body content --
          // logging it verbatim would be an unbounded-content log entry
          // for no operational benefit (see docs/architecture/
          // execution-model.md, "Logging policy").
          metrics->increment_counter("flowforge_jobs_rejected_total");
          write_error(res, make_error(ErrorCode::Validation, std::string("invalid JSON body: ") + e.what()));
          return;
        }

        auto request = parse_create_job_request(body);
        if (!request) {
          metrics->increment_counter("flowforge_jobs_rejected_total");
          write_error(res, request.error());
          return;
        }

        auto created = job_service->create_job(*request);
        if (!created) {
          write_error(res, created.error());
          return;
        }
        metrics->increment_counter("flowforge_jobs_created_total");

        // Additive Phase 2B-2 behavior: a job created with a job_type is also
        // submitted to the Scheduler. This never fails the HTTP request -- the
        // job was genuinely created either way -- the outcome is reported via
        // "scheduling" instead. See job_routes.hpp's class comment.
        domain::Job result_job = *created;
        nlohmann::json scheduling{{"scheduled", false}};
        if (!result_job.job_type().empty()) {
          auto scheduled = scheduler->schedule(result_job);
          if (scheduled) {
            auto queued = job_service->mark_queued(result_job.id().value());
            if (queued) {
              result_job = *queued;
              scheduling = {{"scheduled", true}};
            } else {
              scheduling = {{"scheduled", false}, {"reason", queued.error().message()}};
            }
          } else {
            scheduling = {{"scheduled", false}, {"reason", scheduled.error().message()}};
          }
        }

        res.status = 201;
        nlohmann::json response_body = to_json(result_job);
        response_body["scheduling"] = scheduling;
        res.set_content(response_body.dump(), "application/json");
      });

  server.Get("/api/v1/jobs", [job_service](const httplib::Request& req, httplib::Response& res) {
    const std::size_t limit = parse_size_param(req, "limit", 50);
    const std::size_t offset = parse_size_param(req, "offset", 0);

    auto jobs = job_service->list_jobs(limit, offset);
    if (!jobs) {
      write_error(res, jobs.error());
      return;
    }
    auto total = job_service->count_jobs();
    if (!total) {
      write_error(res, total.error());
      return;
    }

    nlohmann::json items = nlohmann::json::array();
    for (const auto& job : *jobs) {
      items.push_back(to_json(job));
    }
    res.set_content(
        nlohmann::json{{"jobs", items}, {"total", *total}, {"limit", limit}, {"offset", offset}}.dump(),
        "application/json");
  });

  server.Get("/api/v1/jobs/:id", [job_service](const httplib::Request& req, httplib::Response& res) {
    auto job = job_service->get_job(req.path_params.at("id"));
    if (!job) {
      write_error(res, job.error());
      return;
    }
    res.set_content(to_json(*job).dump(), "application/json");
  });

  // Additive (Phase 2B-3): execution attempt history for one job. Not a
  // redesign of the existing job API -- a new, separate endpoint.
  server.Get("/api/v1/jobs/:id/attempts",
             [execution_manager](const httplib::Request& req, httplib::Response& res) {
               auto attempts = execution_manager->history_for(infra::JobId{req.path_params.at("id")});
               if (!attempts) {
                 write_error(res, attempts.error());
                 return;
               }
               nlohmann::json items = nlohmann::json::array();
               for (const auto& attempt : *attempts) {
                 items.push_back(to_json(attempt));
               }
               res.set_content(nlohmann::json{{"attempts", items}}.dump(), "application/json");
             });

  server.Post("/api/v1/jobs/:id/cancel", [job_service, scheduler, worker_pool, metrics](
                                             const httplib::Request& req, httplib::Response& res) {
    const std::string id = req.path_params.at("id");
    auto job = job_service->cancel_job(id);
    if (!job) {
      write_error(res, job.error());
      return;
    }
    metrics->increment_counter("flowforge_jobs_cancelled_total");

    // Best-effort: propagate the cancellation into the
    // in-memory dispatch/execution pipeline too. Neither
    // result affects the response -- see job_routes.hpp's
    // class comment. NotFound is the expected/common outcome
    // (the job had already been dispatched, or wasn't
    // pending/running in this process at all).
    std::ignore = scheduler->cancel(infra::JobId{id});
    std::ignore = worker_pool->request_cancellation(infra::JobId{id});

    res.set_content(to_json(*job).dump(), "application/json");
  });
}

}  // namespace flowforge::server
