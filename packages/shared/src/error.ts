/**
 * Mirrors flowforge::ErrorCode (engine/include/flowforge/error.hpp) as
 * serialized by apps/server/src/http/error_response.cpp.
 */
export type ApiErrorCode =
  | "validation_error"
  | "configuration_error"
  | "infrastructure_error"
  | "database_error"
  | "network_error"
  | "job_execution_error"
  | "not_found"
  | "conflict"
  | "internal_error";

export interface ApiErrorBody {
  error: {
    code: ApiErrorCode;
    message: string;
  };
}
