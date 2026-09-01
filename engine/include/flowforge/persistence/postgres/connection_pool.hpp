#pragma once

#include <pqxx/pqxx>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include "flowforge/engine/blocking_queue.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/result.hpp"

namespace flowforge::persistence::postgres {

/// Configuration for a PgConnectionPool. `connection_string` is passed
/// straight through to libpq (it accepts both `postgres://...` URIs and
/// keyword/value strings), so FlowForge never hand-parses connection URLs.
struct PgPoolConfig {
  std::string connection_string;
  std::size_t pool_size = 4;
};

/// A small, real (not mocked) fixed-size PostgreSQL connection pool.
///
/// Design note (Phase 2A): this is intentionally the simplest pool that is
/// still correct and thread-safe -- a fixed number of connections opened
/// eagerly at `create()` time, handed out on a first-come-first-served
/// basis via `engine::BlockingQueue<T>` (the same primitive `ThreadPool`
/// already uses), with no acquire timeout. That is a real limitation: a
/// caller can block indefinitely if every connection is checked out. It is
/// an acceptable one for this phase because nothing yet drives sustained
/// concurrent load against the pool (no Scheduler/WorkerPool exists yet --
/// see docs/architecture/overview.md). When that lands, this is the seam
/// to extend with an acquire timeout, a wait-queue-depth metric, and
/// dynamic growth/shrink -- not a class to redesign; `acquire()`'s
/// signature (`Result<LeasedConnection>`) already accommodates a timeout
/// failure becoming a real error path.
class PgConnectionPool final : public std::enable_shared_from_this<PgConnectionPool> {
 public:
  /// RAII handle to one pooled connection. Returns the connection to the
  /// pool on destruction (or drops it if the pool itself is already gone),
  /// so a repository method can never leak a connection by forgetting to
  /// "give it back," including when it returns early via an exception.
  class LeasedConnection {
   public:
    LeasedConnection(const LeasedConnection&) = delete;
    LeasedConnection& operator=(const LeasedConnection&) = delete;
    LeasedConnection(LeasedConnection&&) noexcept = default;
    LeasedConnection& operator=(LeasedConnection&&) noexcept = default;
    ~LeasedConnection();

    [[nodiscard]] pqxx::connection& operator*() noexcept { return *connection_; }
    [[nodiscard]] pqxx::connection* operator->() noexcept { return connection_.get(); }

   private:
    friend class PgConnectionPool;
    LeasedConnection(std::weak_ptr<PgConnectionPool> pool, std::unique_ptr<pqxx::connection> connection)
        : pool_(std::move(pool)), connection_(std::move(connection)) {}

    std::weak_ptr<PgConnectionPool> pool_;
    std::unique_ptr<pqxx::connection> connection_;
  };

  /// Opens `config.pool_size` connections eagerly and validates each one
  /// (`is_open()`), so a broken connection string or unreachable database
  /// fails here -- at startup -- rather than surfacing as a mysterious
  /// failure on the first request. This is the "startup connectivity
  /// validation" the persistence layer is required to perform.
  [[nodiscard]] static Result<std::shared_ptr<PgConnectionPool>> create(
      PgPoolConfig config, std::shared_ptr<infra::Logger> logger,
      std::shared_ptr<infra::MetricsRegistry> metrics = nullptr);

  ~PgConnectionPool();
  PgConnectionPool(const PgConnectionPool&) = delete;
  PgConnectionPool& operator=(const PgConnectionPool&) = delete;
  PgConnectionPool(PgConnectionPool&&) = delete;
  PgConnectionPool& operator=(PgConnectionPool&&) = delete;

  /// Blocks until a connection is available. See the class-level note
  /// above about the current lack of a timeout.
  [[nodiscard]] Result<LeasedConnection> acquire();

  [[nodiscard]] std::size_t pool_size() const noexcept { return pool_size_; }

  /// Cheap, non-blocking readiness signal (Phase 2B-5) -- deliberately NOT
  /// a query against PostgreSQL (see docs/architecture/execution-model.md,
  /// "Health vs readiness": readiness must never perform an expensive or
  /// unbounded operation). Never blocks: if every connection is currently
  /// checked out, that is ordinary load, not an outage, so this returns
  /// `true`; only a closed pool (the process is shutting down) or a
  /// popped-and-found-dead idle connection (dropped here exactly like
  /// `release()` already drops one, never returned to the pool) reports
  /// `false`. This cannot detect "PostgreSQL is down but every currently
  /// idle connection happens to still look open" -- a known, accepted
  /// limitation of a check that must stay cheap enough to run on every
  /// `/ready` request.
  [[nodiscard]] bool is_available();

 private:
  friend class LeasedConnection;
  struct PrivateTag {};

 public:
  // Not part of the public construction API -- callers must go through
  // create(). Public only so std::make_shared can call it; `PrivateTag`
  // makes it uncallable from outside this translation unit's friends.
  PgConnectionPool(PrivateTag, PgPoolConfig config, std::shared_ptr<infra::Logger> logger,
                   std::shared_ptr<infra::MetricsRegistry> metrics);

 private:
  void release(std::unique_ptr<pqxx::connection> connection);

  PgPoolConfig config_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
  std::size_t pool_size_ = 0;
  std::atomic<std::size_t> leased_count_{0};
  engine::BlockingQueue<std::unique_ptr<pqxx::connection>> available_;
};

}  // namespace flowforge::persistence::postgres
