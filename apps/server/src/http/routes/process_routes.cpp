#include "http/routes/process_routes.hpp"

#include <optional>
#include <string>

#include "flowforge/domain/input_source.hpp"
#include "flowforge/domain/processing_target.hpp"
#include "http/error_response.hpp"
#include "json/process_json.hpp"

namespace flowforge::server {

namespace {

void write_error(httplib::Response& res, const Error& error) {
  res.status = http_status_for(error.code());
  res.set_content(to_error_body(error).dump(), "application/json");
}

/// Shared multipart shape-validation for `process`/`preview`: both accept
/// the identical `source`/`target`/`file` fields and reject the identical
/// malformed-request shapes before ever calling into
/// `InputProcessingService`. Returns `std::nullopt` (having already
/// written the error response) if `req` isn't well-formed enough to
/// build a `services::ProcessRequest` from.
std::optional<services::ProcessRequest> parse_process_request(const httplib::Request& req,
                                                              httplib::Response& res) {
  if (!req.is_multipart_form_data()) {
    write_error(res, make_error(ErrorCode::Validation,
                                "request must be multipart/form-data with 'source', 'target', and "
                                "'file' fields"));
    return std::nullopt;
  }
  if (!req.has_file("source")) {
    write_error(res, make_error(ErrorCode::Validation, "missing required 'source' field"));
    return std::nullopt;
  }
  if (!req.has_file("target")) {
    write_error(res, make_error(ErrorCode::Validation, "missing required 'target' field"));
    return std::nullopt;
  }
  if (!req.has_file("file")) {
    write_error(res, make_error(ErrorCode::Validation, "missing required 'file' field"));
    return std::nullopt;
  }

  const std::string source_raw = req.get_file_value("source").content;
  const std::string target_raw = req.get_file_value("target").content;

  auto source_type = domain::input_source_type_from_string(source_raw);
  if (!source_type) {
    write_error(res, make_error(ErrorCode::Validation, "unrecognized 'source' value '" + source_raw + "'"));
    return std::nullopt;
  }
  auto target = domain::processing_target_from_string(target_raw);
  if (!target) {
    write_error(res, make_error(ErrorCode::Validation, "unrecognized 'target' value '" + target_raw + "'"));
    return std::nullopt;
  }

  return services::ProcessRequest{
      .source_type = *source_type, .target = *target, .payload = req.get_file_value("file").content};
}

}  // namespace

void register_process_routes(
    httplib::Server& server,
    const std::shared_ptr<services::InputProcessingService>& input_processing_service) {
  server.Post("/api/v1/process",
              [input_processing_service](const httplib::Request& req, httplib::Response& res) {
                auto request = parse_process_request(req, res);
                if (!request) {
                  return;
                }

                auto result = input_processing_service->process(*request);
                if (!result) {
                  write_error(res, result.error());
                  return;
                }

                res.status = 201;
                res.set_content(to_json(*result).dump(), "application/json");
              });

  // Phase 3D-1: extraction preview -- creates nothing (no Workload, no
  // Job, no database row of any kind). See
  // `InputProcessingService::preview`'s class comment and
  // docs/architecture/input-processing.md, "Preview".
  server.Post("/api/v1/process/preview",
              [input_processing_service](const httplib::Request& req, httplib::Response& res) {
                auto request = parse_process_request(req, res);
                if (!request) {
                  return;
                }

                auto result = input_processing_service->preview(*request);
                if (!result) {
                  write_error(res, result.error());
                  return;
                }

                res.status = 200;
                res.set_content(to_json(*result).dump(), "application/json");
              });

  // Phase 3D-1: submits previously-previewed records into the workload
  // pipeline. JSON body, not multipart -- there is no file upload on this
  // call, only the already-extracted records the caller is confirming.
  server.Post("/api/v1/process/confirm", [input_processing_service](const httplib::Request& req,
                                                                    httplib::Response& res) {
    nlohmann::json body;
    try {
      body = nlohmann::json::parse(req.body);
    } catch (const nlohmann::json::parse_error& e) {
      write_error(res, make_error(ErrorCode::Validation, std::string("invalid JSON body: ") + e.what()));
      return;
    }

    auto request = parse_confirm_request(body);
    if (!request) {
      write_error(res, request.error());
      return;
    }

    auto result = input_processing_service->confirm(*request);
    if (!result) {
      write_error(res, result.error());
      return;
    }

    res.status = 201;
    res.set_content(to_json(*result).dump(), "application/json");
  });
}

}  // namespace flowforge::server
