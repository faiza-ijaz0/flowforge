// Phase 2B-5: HTTP error observability -- proves that a 5xx-classified
// error's message is replaced with a fixed, generic string in the HTTP
// response body (never the raw exception/database text a repository
// might have embedded in Error::message()), while every 4xx error keeps
// its specific, safe, application-generated message intact.

#include "http/error_response.hpp"

#include <gtest/gtest.h>

namespace flowforge::server {
namespace {

TEST(ErrorResponseTest, StatusCodesMatchErrorCodeSemantics) {
  EXPECT_EQ(http_status_for(ErrorCode::Validation), 400);
  EXPECT_EQ(http_status_for(ErrorCode::NotFound), 404);
  EXPECT_EQ(http_status_for(ErrorCode::Conflict), 409);
  EXPECT_EQ(http_status_for(ErrorCode::Configuration), 500);
  EXPECT_EQ(http_status_for(ErrorCode::Infrastructure), 500);
  EXPECT_EQ(http_status_for(ErrorCode::Database), 500);
  EXPECT_EQ(http_status_for(ErrorCode::JobExecution), 500);
  EXPECT_EQ(http_status_for(ErrorCode::Internal), 500);
  EXPECT_EQ(http_status_for(ErrorCode::Network), 502);
}

TEST(ErrorResponseTest, ValidationErrorMessagePassesThroughUnchanged) {
  const Error error = make_error(ErrorCode::Validation, "queue_name must not be empty");
  const auto body = to_error_body(error);
  EXPECT_EQ(body.at("error").at("code"), "validation_error");
  EXPECT_EQ(body.at("error").at("message"), "queue_name must not be empty");
}

TEST(ErrorResponseTest, NotFoundErrorMessagePassesThroughUnchanged) {
  const Error error = make_error(ErrorCode::NotFound, "job with id 'abc' was not found");
  const auto body = to_error_body(error);
  EXPECT_EQ(body.at("error").at("message"), "job with id 'abc' was not found");
}

TEST(ErrorResponseTest, ConflictErrorMessagePassesThroughUnchanged) {
  const Error error = make_error(ErrorCode::Conflict, "job 'abc' is already in terminal state 'succeeded'");
  const auto body = to_error_body(error);
  EXPECT_EQ(body.at("error").at("message"), "job 'abc' is already in terminal state 'succeeded'");
}

// The core security-relevant assertion: a Database error's message (which
// postgres::map_exception's fallback branch can populate with raw
// pqxx/libpq exception text -- see error_mapping.cpp) must never reach an
// HTTP client verbatim.
TEST(ErrorResponseTest, DatabaseErrorMessageIsReplacedWithGenericText) {
  const Error error =
      make_error(ErrorCode::Database,
                 "job_repository.find_by_id: FATAL: password authentication failed for user \"flowforge\"");
  const auto body = to_error_body(error);
  EXPECT_EQ(body.at("error").at("code"), "database_error");
  EXPECT_EQ(body.at("error").at("message"), "an internal error occurred");
  EXPECT_EQ(body.at("error").at("message").get<std::string>().find("password"), std::string::npos);
}

TEST(ErrorResponseTest, InfrastructureErrorMessageIsReplacedWithGenericText) {
  const Error error = make_error(ErrorCode::Infrastructure, "PostgreSQL connection pool is closed");
  const auto body = to_error_body(error);
  EXPECT_EQ(body.at("error").at("message"), "an internal error occurred");
}

TEST(ErrorResponseTest, InternalErrorMessageIsReplacedWithGenericText) {
  const Error error =
      make_error(ErrorCode::Internal, "handler threw an exception: /etc/secret/path not found");
  const auto body = to_error_body(error);
  EXPECT_EQ(body.at("error").at("message"), "an internal error occurred");
}

TEST(ErrorResponseTest, ConfigurationErrorMessageIsReplacedWithGenericText) {
  const Error error =
      make_error(ErrorCode::Configuration, "FLOWFORGE_DATABASE_URL is required in production");
  const auto body = to_error_body(error);
  EXPECT_EQ(body.at("error").at("message"), "an internal error occurred");
}

}  // namespace
}  // namespace flowforge::server
