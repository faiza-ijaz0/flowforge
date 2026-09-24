#include "flowforge/persistence/postgres/postgres_user_repository.hpp"

#include <pqxx/pqxx>

#include "flowforge/persistence/postgres/error_mapping.hpp"
#include "pg_time.hpp"

namespace flowforge::persistence::postgres {

namespace {

constexpr std::string_view kComponent = "postgres_user_repository";

constexpr std::string_view kSelectUserColumns =
    "id, name, email, phone, job_id, "
    "extract(epoch from created_at) AS created_at_epoch, extract(epoch from updated_at) AS updated_at_epoch";

domain::User row_to_user(const pqxx::row& row) {
  domain::User user{
      .id = infra::UserId{row["id"].as<std::string>()},
      .name = row["name"].as<std::string>(),
      .email = row["email"].as<std::string>(),
      .phone = row["phone"].as<std::optional<std::string>>(),
      .job_id = std::nullopt,
      .created_at = from_epoch_seconds(row["created_at_epoch"].as<double>()),
      .updated_at = from_epoch_seconds(row["updated_at_epoch"].as<double>()),
  };
  if (auto job_id = row["job_id"].as<std::optional<std::string>>(); job_id.has_value()) {
    user.job_id = infra::JobId{*job_id};
  }
  return user;
}

}  // namespace

PostgresUserRepository::PostgresUserRepository(std::shared_ptr<PgConnectionPool> pool,
                                               std::shared_ptr<infra::Logger> logger,
                                               std::shared_ptr<infra::MetricsRegistry> metrics)
    : pool_(std::move(pool)), logger_(std::move(logger)), metrics_(std::move(metrics)) {}

Result<void> PostgresUserRepository::upsert(const infra::JobId& job_id,
                                            const domain::NormalizedUserRecord& record) {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    txn.exec_params(
        "INSERT INTO users (name, email, phone, job_id) "
        "VALUES ($1, $2, $3, $4) "
        "ON CONFLICT (email) DO UPDATE SET "
        "name = EXCLUDED.name, phone = EXCLUDED.phone, job_id = EXCLUDED.job_id, updated_at = now()",
        pqxx::params{record.name, record.email, record.phone, job_id.value()});
    txn.commit();
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_user_upserts_total");
    }
    return {};
  } catch (const std::exception& e) {
    logger_->error(kComponent, "upsert failed",
                   {{.key = "email", .value = record.email}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "user_repository.upsert"));
  }
}

Result<std::optional<domain::User>> PostgresUserRepository::find_by_email(const std::string& email) const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result = txn.exec_params(
        "SELECT " + std::string(kSelectUserColumns) + " FROM users WHERE email = $1", pqxx::params{email});
    txn.commit();
    if (result.empty()) {
      return std::optional<domain::User>(std::nullopt);
    }
    return std::optional<domain::User>(row_to_user(result[0]));
  } catch (const std::exception& e) {
    logger_->error(kComponent, "find_by_email failed",
                   {{.key = "email", .value = email}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "user_repository.find_by_email"));
  }
}

Result<std::vector<domain::User>> PostgresUserRepository::list(std::size_t limit, std::size_t offset) const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result =
        txn.exec_params("SELECT " + std::string(kSelectUserColumns) +
                            " FROM users ORDER BY created_at ASC, id ASC LIMIT $1 OFFSET $2",
                        pqxx::params{static_cast<long long>(limit), static_cast<long long>(offset)});
    txn.commit();

    std::vector<domain::User> users;
    users.reserve(static_cast<std::size_t>(result.size()));
    for (const auto& row : result) {
      users.push_back(row_to_user(row));
    }
    return users;
  } catch (const std::exception& e) {
    logger_->error(kComponent, "list failed", {{.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "user_repository.list"));
  }
}

Result<std::size_t> PostgresUserRepository::count() const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result = txn.exec("SELECT count(*) FROM users");
    txn.commit();
    return result[0][0].as<std::size_t>();
  } catch (const std::exception& e) {
    logger_->error(kComponent, "count failed", {{.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "user_repository.count"));
  }
}

}  // namespace flowforge::persistence::postgres
