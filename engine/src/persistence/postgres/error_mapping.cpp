#include "flowforge/persistence/postgres/error_mapping.hpp"

#include <pqxx/pqxx>

namespace flowforge::persistence::postgres {

Error map_exception(const std::exception& e, std::string_view context) {
  const std::string ctx(context);

  // Ordered most-specific to least-specific: pqxx's constraint-violation
  // exceptions all derive from pqxx::sql_error, and pqxx::broken_connection
  // derives from pqxx::failure (a sibling of sql_error), so a single
  // dynamic_cast chain -- not a switch -- is what correctly distinguishes
  // them.
  if (dynamic_cast<const pqxx::unique_violation*>(&e) != nullptr) {
    return make_error(ErrorCode::Conflict, ctx + ": a record with this identity already exists");
  }
  if (dynamic_cast<const pqxx::foreign_key_violation*>(&e) != nullptr) {
    return make_error(ErrorCode::Validation, ctx + ": references an entity that does not exist");
  }
  if (dynamic_cast<const pqxx::check_violation*>(&e) != nullptr ||
      dynamic_cast<const pqxx::not_null_violation*>(&e) != nullptr) {
    return make_error(ErrorCode::Validation, ctx + ": value violates a database constraint");
  }
  if (dynamic_cast<const pqxx::broken_connection*>(&e) != nullptr) {
    return make_error(ErrorCode::Infrastructure, ctx + ": database connection failed");
  }
  if (dynamic_cast<const pqxx::sql_error*>(&e) != nullptr) {
    return make_error(ErrorCode::Database, ctx + ": database operation failed");
  }
  return make_error(ErrorCode::Database, ctx + ": " + e.what());
}

}  // namespace flowforge::persistence::postgres
