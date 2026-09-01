#include "flowforge/persistence/postgres/connection_pool.hpp"

#include <atomic>
#include <thread>
#include <vector>

#include "postgres_test_support.hpp"

namespace flowforge::persistence::postgres {
namespace {

using test::PostgresIntegrationTest;
using ConnectionPoolTest = PostgresIntegrationTest;

TEST_F(ConnectionPoolTest, AcquireReturnsAWorkingConnection) {
  auto conn = pool_->acquire();
  ASSERT_TRUE(conn.has_value());
  pqxx::work txn(**conn);
  auto result = txn.exec("SELECT 1");
  txn.commit();
  EXPECT_EQ(result[0][0].as<int>(), 1);
}

TEST_F(ConnectionPoolTest, ConnectionIsReturnedToPoolOnLeaseDestruction) {
  {
    auto conn = pool_->acquire();
    ASSERT_TRUE(conn.has_value());
  }  // LeasedConnection destructor runs here.
  // If release() didn't work, a second acquire() from a pool_size==2 pool
  // (see PostgresIntegrationTest::SetUp) would still succeed here since
  // only one of two was taken -- so this alone doesn't prove release()
  // works. Draining the full pool size does.
  auto first = pool_->acquire();
  auto second = pool_->acquire();
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
}

TEST_F(ConnectionPoolTest, ConcurrentAcquireReleaseFromMultipleThreadsStaysConsistent) {
  // Regression coverage for section 18 ("concurrency safety"): many
  // threads hammering acquire()/release() must never crash, deadlock, or
  // hand out the same physical connection to two threads at once. Each
  // thread runs real queries on its leased connection.
  constexpr int kThreads = 8;
  constexpr int kIterationsPerThread = 20;
  std::atomic<int> failures{0};

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([this, &failures] {
      for (int i = 0; i < kIterationsPerThread; ++i) {
        auto conn = pool_->acquire();
        if (!conn.has_value()) {
          ++failures;
          continue;
        }
        try {
          pqxx::work txn(**conn);
          auto result = txn.exec("SELECT 1");
          txn.commit();
          if (result[0][0].as<int>() != 1) {
            ++failures;
          }
        } catch (const std::exception&) {
          ++failures;
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(failures.load(), 0);
}

TEST_F(ConnectionPoolTest, IsAvailableReturnsTrueWhenAConnectionIsIdle) {
  EXPECT_TRUE(pool_->is_available());
}

TEST_F(ConnectionPoolTest, IsAvailableReturnsTrueWhenPoolIsFullyLeasedNotClosed) {
  // Phase 2B-5: exhausted-but-alive must read as "ok" for readiness
  // purposes -- that's ordinary load, not evidence PostgreSQL is down.
  // PostgresIntegrationTest's fixture pool has pool_size == 2.
  auto first = pool_->acquire();
  auto second = pool_->acquire();
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_TRUE(pool_->is_available());
}

TEST_F(ConnectionPoolTest, CreateFailsClearlyForAnUnreachableDatabase) {
  // "Fail clearly and safely rather than silently falling back" (phase
  // brief, section 14) starts here: a bad connection string must be a
  // Result error, not a crash, not a connection that "succeeds" and fails
  // on first use, and not a fallback to some other persistence mode.
  auto result = PgConnectionPool::create(
      PgPoolConfig{.connection_string = "postgresql://nouser:nopass@127.0.0.1:1/nonexistent_db_xyz",
                   .pool_size = 1},
      logger_);
  ASSERT_FALSE(result.has_value());
  EXPECT_TRUE(result.error().code() == ErrorCode::Infrastructure ||
              result.error().code() == ErrorCode::Database);
}

}  // namespace
}  // namespace flowforge::persistence::postgres
