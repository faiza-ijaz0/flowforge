#include "http/error_response.hpp"

namespace flowforge::server {

int http_status_for(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::Validation:
      return 400;
    case ErrorCode::NotFound:
      return 404;
    case ErrorCode::Conflict:
      return 409;
    case ErrorCode::Configuration:
    case ErrorCode::Infrastructure:
    case ErrorCode::Database:
    case ErrorCode::JobExecution:
    case ErrorCode::Internal:
      return 500;
    case ErrorCode::Network:
      return 502;
  }
  return 500;
}

nlohmann::json to_error_body(const Error& error) {
  return nlohmann::json{
      {"error", {{"code", std::string(to_string(error.code()))}, {"message", error.message()}}}};
}

}  // namespace flowforge::server
