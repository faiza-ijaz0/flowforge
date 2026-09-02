#include "http/routes/process_routes.hpp"

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

}  // namespace

void register_process_routes(
    httplib::Server& server,
    const std::shared_ptr<services::InputProcessingService>& input_processing_service) {
  server.Post("/api/v1/process", [input_processing_service](const httplib::Request& req,
                                                            httplib::Response& res) {
    if (!req.is_multipart_form_data()) {
      write_error(res, make_error(ErrorCode::Validation,
                                  "request must be multipart/form-data with 'source', 'target', and "
                                  "'file' fields"));
      return;
    }
    if (!req.has_file("source")) {
      write_error(res, make_error(ErrorCode::Validation, "missing required 'source' field"));
      return;
    }
    if (!req.has_file("target")) {
      write_error(res, make_error(ErrorCode::Validation, "missing required 'target' field"));
      return;
    }
    if (!req.has_file("file")) {
      write_error(res, make_error(ErrorCode::Validation, "missing required 'file' field"));
      return;
    }

    const std::string source_raw = req.get_file_value("source").content;
    const std::string target_raw = req.get_file_value("target").content;

    auto source_type = domain::input_source_type_from_string(source_raw);
    if (!source_type) {
      write_error(res, make_error(ErrorCode::Validation, "unrecognized 'source' value '" + source_raw + "'"));
      return;
    }
    auto target = domain::processing_target_from_string(target_raw);
    if (!target) {
      write_error(res, make_error(ErrorCode::Validation, "unrecognized 'target' value '" + target_raw + "'"));
      return;
    }

    services::ProcessRequest request{
        .source_type = *source_type, .target = *target, .payload = req.get_file_value("file").content};
    auto result = input_processing_service->process(request);
    if (!result) {
      write_error(res, result.error());
      return;
    }

    res.status = 201;
    res.set_content(to_json(*result).dump(), "application/json");
  });
}

}  // namespace flowforge::server
