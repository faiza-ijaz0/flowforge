#include "flowforge/persistence/postgres/postgres_job_repository.hpp"

#include <pqxx/pqxx>

#include <charconv>
#include <chrono>
#include <optional>
#include <sstream>

#include "flowforge/persistence/postgres/error_mapping.hpp"
#include "pg_time.hpp"

namespace flowforge::persistence::postgres {

namespace {

constexpr std::string_view kComponent = "postgres_job_repository";

// --- retry_policy <-> jsonb -------------------------------------------
//
// retry_policy is a fixed, flat shape of four numeric fields (see
// domain::RetryPolicy), so a small hand-rolled encoder/decoder is used
// here instead of pulling nlohmann::json into the engine library just for
// this. PostgreSQL's jsonb storage does NOT preserve key order or
// spacing -- reading back a value written by this same function can come
// back with keys in a different order -- so the decoder below looks each
// key up by name rather than assuming positional order.

std::string retry_policy_to_json(const domain::RetryPolicy& policy) {
  std::ostringstream out;
  out << "{\"max_attempts\":" << policy.max_attempts
      << ",\"initial_backoff_ms\":" << policy.initial_backoff.count()
      << ",\"max_backoff_ms\":" << policy.max_backoff.count() << ",\"backoff_multiplier\":" << std::fixed
      << policy.backoff_multiplier << "}";
  return out.str();
}

std::optional<double> extract_json_number(std::string_view json, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\":";
  const auto key_pos = json.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t pos = key_pos + needle.size();
  while (pos < json.size() && json[pos] == ' ') {
    ++pos;
  }
  std::size_t end = pos;
  while (end < json.size() &&
         (std::isdigit(static_cast<unsigned char>(json[end])) != 0 || json[end] == '-' || json[end] == '.' ||
          json[end] == 'e' || json[end] == 'E' || json[end] == '+')) {
    ++end;
  }
  double value{};
  auto [ptr, ec] = std::from_chars(json.data() + pos, json.data() + end, value);
  if (ec != std::errc{}) {
    return std::nullopt;
  }
  return value;
}

Result<domain::RetryPolicy> retry_policy_from_json(std::string_view json) {
  domain::RetryPolicy policy;
  auto max_attempts = extract_json_number(json, "max_attempts");
  auto initial_backoff_ms = extract_json_number(json, "initial_backoff_ms");
  auto max_backoff_ms = extract_json_number(json, "max_backoff_ms");
  auto backoff_multiplier = extract_json_number(json, "backoff_multiplier");
  if (!max_attempts || !initial_backoff_ms || !max_backoff_ms || !backoff_multiplier) {
    return std::unexpected(
        make_error(ErrorCode::Database, "stored retry_policy is missing one or more expected fields"));
  }
  policy.max_attempts = static_cast<std::uint32_t>(*max_attempts);
  policy.initial_backoff = std::chrono::milliseconds{static_cast<std::int64_t>(*initial_backoff_ms)};
  policy.max_backoff = std::chrono::milliseconds{static_cast<std::int64_t>(*max_backoff_ms)};
  policy.backoff_multiplier = *backoff_multiplier;
  return policy;
}

// payload is an opaque, already-serialized string at the domain level
// (see domain::Job's class comment) -- it is NOT guaranteed to itself be
// valid JSON (e.g. a plain string payload). `to_jsonb(text)` wraps *any*
// string as a valid JSON string scalar (PostgreSQL escapes it), and
// `payload #>> '{}'` unwraps it back to the exact original text on read.
// Binding it straight into a `::jsonb` cast would throw on a non-JSON
// payload, which would make job creation fail unpredictably depending on
// payload content -- exactly the "explicit and justified" conversion
// section 11 of the phase brief calls for.
// pqxx::zview (not std::string_view): transaction_base::exec_params()
// requires a zview -- a "zero-terminated" view -- specifically because it
// hands the pointer to libpq's C API, which expects a NUL-terminated C
// string. std::string_view carries no such guarantee, so pqxx makes the
// std::string_view -> zview conversion `explicit` on purpose; string
// literals convert to zview implicitly (and safely) instead.
constexpr pqxx::zview kInsertJob =
    "INSERT INTO jobs (id, queue_name, payload, priority, status, attempt_count, retry_policy, last_error, "
    "created_at, updated_at, job_type, workload_id) "
    "VALUES ($1, $2, to_jsonb($3::text), $4, $5, $6, $7::jsonb, $8, to_timestamp($9), to_timestamp($10), "
    "$11, $12)";

constexpr pqxx::zview kUpsertQueue = "INSERT INTO queues (name) VALUES ($1) ON CONFLICT (name) DO NOTHING";

constexpr std::string_view kSelectJobColumns =
    "id, queue_name, payload #>> '{}' AS payload, priority, status, attempt_count, retry_policy::text AS "
    "retry_policy, last_error, extract(epoch from created_at) AS created_at_epoch, extract(epoch from "
    "updated_at) "
    "AS updated_at_epoch, job_type, workload_id";

constexpr pqxx::zview kSelectJobsByStatus =
    "SELECT id, queue_name, payload #>> '{}' AS payload, priority, status, attempt_count, "
    "retry_policy::text AS retry_policy, last_error, extract(epoch from created_at) AS created_at_epoch, "
    "extract(epoch from updated_at) AS updated_at_epoch, job_type, workload_id FROM jobs WHERE status = $1 "
    "ORDER BY updated_at ASC LIMIT $2";

constexpr pqxx::zview kSelectJobsByWorkloadId =
    "SELECT id, queue_name, payload #>> '{}' AS payload, priority, status, attempt_count, "
    "retry_policy::text AS retry_policy, last_error, extract(epoch from created_at) AS created_at_epoch, "
    "extract(epoch from updated_at) AS updated_at_epoch, job_type, workload_id FROM jobs "
    "WHERE workload_id = $1 ORDER BY created_at ASC LIMIT $2";

constexpr pqxx::zview kUpdateJob =
    "UPDATE jobs SET queue_name = $2, payload = to_jsonb($3::text), priority = $4, status = $5, "
    "attempt_count = $6, retry_policy = $7::jsonb, last_error = $8, updated_at = to_timestamp($9), "
    "job_type = $10, workload_id = $11 WHERE id = $1";

Result<domain::Job> row_to_job(const pqxx::row& row) {
  auto status = domain::job_status_from_string(row["status"].as<std::string>());
  if (!status) {
    return std::unexpected(make_error(ErrorCode::Database, "job row has an unrecognized status value"));
  }
  auto retry_policy = retry_policy_from_json(row["retry_policy"].as<std::string>());
  if (!retry_policy) {
    return std::unexpected(retry_policy.error());
  }
  std::optional<infra::WorkloadId> workload_id;
  if (auto raw = row["workload_id"].as<std::optional<std::string>>()) {
    workload_id = infra::WorkloadId{*raw};
  }
  return domain::Job::restore(infra::JobId{row["id"].as<std::string>()}, row["queue_name"].as<std::string>(),
                              row["payload"].as<std::string>(), *retry_policy, row["priority"].as<int>(),
                              *status, row["attempt_count"].as<std::uint32_t>(),
                              row["last_error"].as<std::optional<std::string>>(),
                              from_epoch_seconds(row["created_at_epoch"].as<double>()),
                              from_epoch_seconds(row["updated_at_epoch"].as<double>()),
                              row["job_type"].as<std::string>(), std::move(workload_id));
}

/// pqxx binds `std::optional<std::string>` as NULL when empty -- used for
/// `jobs.workload_id`, which is nullable (see migration 0013).
std::optional<std::string> workload_id_param(const domain::Job& job) {
  if (!job.workload_id().has_value()) {
    return std::nullopt;
  }
  return job.workload_id()->value();
}

}  // namespace

PostgresJobRepository::PostgresJobRepository(std::shared_ptr<PgConnectionPool> pool,
                                             std::shared_ptr<infra::Logger> logger,
                                             std::shared_ptr<infra::MetricsRegistry> metrics)
    : pool_(std::move(pool)), logger_(std::move(logger)), metrics_(std::move(metrics)) {}

Result<void> PostgresJobRepository::insert(const domain::Job& job) {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    txn.exec_params(kUpsertQueue, pqxx::params{job.queue_name()});
    txn.exec_params(kInsertJob,
                    pqxx::params{job.id().value(), job.queue_name(), job.payload(), job.priority(),
                                 std::string(domain::to_string(job.status())), job.attempt_count(),
                                 retry_policy_to_json(job.retry_policy()), job.last_error(),
                                 to_epoch_seconds(job.created_at()), to_epoch_seconds(job.updated_at()),
                                 job.job_type(), workload_id_param(job)});
    txn.commit();
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_job_inserts_total");
    }
    return {};
  } catch (const std::exception& e) {
    logger_->error(kComponent, "insert failed",
                   {{.key = "job_id", .value = job.id().value()}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "job_repository.insert"));
  }
}

Result<domain::Job> PostgresJobRepository::find_by_id(const infra::JobId& id) const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result = txn.exec_params("SELECT " + std::string(kSelectJobColumns) + " FROM jobs WHERE id = $1",
                                  pqxx::params{id.value()});
    txn.commit();
    if (result.empty()) {
      return std::unexpected(
          make_error(ErrorCode::NotFound, "job with id '" + id.value() + "' was not found"));
    }
    return row_to_job(result[0]);
  } catch (const std::exception& e) {
    logger_->error(kComponent, "find_by_id failed",
                   {{.key = "job_id", .value = id.value()}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "job_repository.find_by_id"));
  }
}

Result<std::vector<domain::Job>> PostgresJobRepository::list(std::size_t limit, std::size_t offset) const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result =
        txn.exec_params("SELECT " + std::string(kSelectJobColumns) +
                            " FROM jobs ORDER BY created_at ASC, id ASC LIMIT $1 OFFSET $2",
                        pqxx::params{static_cast<long long>(limit), static_cast<long long>(offset)});
    txn.commit();

    std::vector<domain::Job> jobs;
    jobs.reserve(static_cast<std::size_t>(result.size()));
    for (const auto& row : result) {
      auto job = row_to_job(row);
      if (!job) {
        return std::unexpected(job.error());
      }
      jobs.push_back(std::move(*job));
    }
    return jobs;
  } catch (const std::exception& e) {
    logger_->error(kComponent, "list failed", {{.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "job_repository.list"));
  }
}

Result<void> PostgresJobRepository::update(const domain::Job& job) {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result = txn.exec_params(
        kUpdateJob, pqxx::params{job.id().value(), job.queue_name(), job.payload(), job.priority(),
                                 std::string(domain::to_string(job.status())), job.attempt_count(),
                                 retry_policy_to_json(job.retry_policy()), job.last_error(),
                                 to_epoch_seconds(job.updated_at()), job.job_type(), workload_id_param(job)});
    txn.commit();
    if (result.affected_rows() == 0) {
      return std::unexpected(
          make_error(ErrorCode::NotFound, "job with id '" + job.id().value() + "' was not found"));
    }
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_job_updates_total");
    }
    return {};
  } catch (const std::exception& e) {
    logger_->error(kComponent, "update failed",
                   {{.key = "job_id", .value = job.id().value()}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "job_repository.update"));
  }
}

Result<std::vector<domain::Job>> PostgresJobRepository::list_by_status(domain::JobStatus status,
                                                                       std::size_t limit) const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result = txn.exec_params(kSelectJobsByStatus, pqxx::params{std::string(domain::to_string(status)),
                                                                    static_cast<long long>(limit)});
    txn.commit();

    std::vector<domain::Job> jobs;
    jobs.reserve(static_cast<std::size_t>(result.size()));
    for (const auto& row : result) {
      auto job = row_to_job(row);
      if (!job) {
        return std::unexpected(job.error());
      }
      jobs.push_back(std::move(*job));
    }
    return jobs;
  } catch (const std::exception& e) {
    logger_->error(kComponent, "list_by_status failed", {{.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "job_repository.list_by_status"));
  }
}

Result<std::vector<domain::Job>> PostgresJobRepository::list_by_workload_id(
    const infra::WorkloadId& workload_id, std::size_t limit) const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result = txn.exec_params(kSelectJobsByWorkloadId,
                                  pqxx::params{workload_id.value(), static_cast<long long>(limit)});
    txn.commit();

    std::vector<domain::Job> jobs;
    jobs.reserve(static_cast<std::size_t>(result.size()));
    for (const auto& row : result) {
      auto job = row_to_job(row);
      if (!job) {
        return std::unexpected(job.error());
      }
      jobs.push_back(std::move(*job));
    }
    return jobs;
  } catch (const std::exception& e) {
    logger_->error(
        kComponent, "list_by_workload_id failed",
        {{.key = "workload_id", .value = workload_id.value()}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "job_repository.list_by_workload_id"));
  }
}

}  // namespace flowforge::persistence::postgres
