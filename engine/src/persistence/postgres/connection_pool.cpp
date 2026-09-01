#include "flowforge/persistence/postgres/connection_pool.hpp"

#include "flowforge/persistence/postgres/error_mapping.hpp"

namespace flowforge::persistence::postgres {

namespace {
constexpr std::string_view kComponent = "postgres_connection_pool";
}  // namespace

PgConnectionPool::LeasedConnection::~LeasedConnection() {
  if (connection_ == nullptr) {
    return;
  }
  if (auto pool = pool_.lock()) {
    pool->release(std::move(connection_));
  }
  // If the pool has already been destroyed, `connection_`'s own
  // destructor closes it -- there is nothing to return it to.
}

PgConnectionPool::PgConnectionPool(PrivateTag, PgPoolConfig config, std::shared_ptr<infra::Logger> logger,
                                   std::shared_ptr<infra::MetricsRegistry> metrics)
    : config_(std::move(config)),
      logger_(std::move(logger)),
      metrics_(std::move(metrics)),
      available_(config_.pool_size) {}

PgConnectionPool::~PgConnectionPool() {
  available_.close();
}

Result<std::shared_ptr<PgConnectionPool>> PgConnectionPool::create(
    PgPoolConfig config, std::shared_ptr<infra::Logger> logger,
    std::shared_ptr<infra::MetricsRegistry> metrics) {
  if (config.connection_string.empty()) {
    return std::unexpected(
        make_error(ErrorCode::Configuration, "PostgreSQL connection string must not be empty"));
  }
  if (config.pool_size == 0) {
    return std::unexpected(make_error(ErrorCode::Configuration, "PostgreSQL pool_size must be >= 1"));
  }

  auto pool = std::make_shared<PgConnectionPool>(PrivateTag{}, config, logger, metrics);
  pool->pool_size_ = config.pool_size;

  for (std::size_t i = 0; i < config.pool_size; ++i) {
    try {
      auto connection = std::make_unique<pqxx::connection>(pool->config_.connection_string);
      if (!connection->is_open()) {
        return std::unexpected(make_error(ErrorCode::Infrastructure, "failed to open PostgreSQL connection " +
                                                                         std::to_string(i + 1) + "/" +
                                                                         std::to_string(config.pool_size)));
      }
      pool->available_.push(std::move(connection));
    } catch (const std::exception& e) {
      if (logger) {
        logger->critical(kComponent, "failed to establish PostgreSQL connection at startup",
                         {{.key = "error", .value = e.what()}});
      }
      return std::unexpected(map_exception(e, "connection_pool.create"));
    }
  }

  if (metrics) {
    metrics->set_gauge("flowforge_db_pool_size", static_cast<double>(config.pool_size));
  }
  if (logger) {
    logger->info(kComponent, "PostgreSQL connection pool ready",
                 {{.key = "pool_size", .value = std::to_string(config.pool_size)}});
  }
  return pool;
}

Result<PgConnectionPool::LeasedConnection> PgConnectionPool::acquire() {
  auto connection = available_.pop();
  if (!connection.has_value()) {
    return std::unexpected(make_error(ErrorCode::Infrastructure, "PostgreSQL connection pool is closed"));
  }
  if (metrics_) {
    metrics_->increment_counter("flowforge_db_connections_acquired_total");
    leased_count_.fetch_add(1, std::memory_order_relaxed);
    metrics_->set_gauge("flowforge_db_connections_leased",
                        static_cast<double>(leased_count_.load(std::memory_order_relaxed)));
  }
  return LeasedConnection(weak_from_this(), std::move(*connection));
}

bool PgConnectionPool::is_available() {
  if (available_.closed()) {
    return false;
  }
  auto connection = available_.try_pop();
  if (!connection) {
    // Every connection is currently checked out -- ordinary load, not
    // evidence of an outage.
    return true;
  }
  const bool healthy = (*connection)->is_open();
  if (healthy) {
    available_.push(std::move(*connection));
  } else if (logger_) {
    logger_->warn(kComponent, "readiness check found and dropped a broken PostgreSQL connection", {});
  }
  return healthy;
}

void PgConnectionPool::release(std::unique_ptr<pqxx::connection> connection) {
  if (metrics_) {
    leased_count_.fetch_sub(1, std::memory_order_relaxed);
    metrics_->set_gauge("flowforge_db_connections_leased",
                        static_cast<double>(leased_count_.load(std::memory_order_relaxed)));
  }
  if (connection && connection->is_open()) {
    available_.push(std::move(connection));
  } else if (logger_) {
    // A connection that died mid-use (e.g. server restart, network blip)
    // is dropped rather than returned -- returning a dead connection would
    // just move the failure to whichever caller acquires it next. The pool
    // permanently shrinks by one in this case; recovering by reopening a
    // replacement is future work (see the class-level design note).
    logger_->warn(kComponent, "dropping broken PostgreSQL connection instead of returning it to the pool",
                  {});
  }
}

}  // namespace flowforge::persistence::postgres
