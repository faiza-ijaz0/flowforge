#include "flowforge/persistence/postgres/postgres_worker_repository.hpp"

#include "postgres_test_support.hpp"

namespace flowforge::persistence::postgres {
namespace {

using test::PostgresIntegrationTest;
using PostgresWorkerRepositoryTest = PostgresIntegrationTest;

domain::Worker make_worker(std::string hostname = "worker-1.local") {
  return domain::Worker(infra::WorkerId::generate(), std::move(hostname), std::chrono::system_clock::now());
}

TEST_F(PostgresWorkerRepositoryTest, InsertThenFindByIdRoundTrips) {
  PostgresWorkerRepository repo(pool_, logger_);
  domain::Worker worker = make_worker();

  ASSERT_TRUE(repo.insert(worker).has_value());

  auto found = repo.find_by_id(worker.id());
  ASSERT_TRUE(found.has_value()) << found.error().message();
  EXPECT_EQ(found->id(), worker.id());
  EXPECT_EQ(found->hostname(), "worker-1.local");
  EXPECT_EQ(found->status(), domain::WorkerStatus::Idle);
}

TEST_F(PostgresWorkerRepositoryTest, FindByIdReturnsNotFoundForUnknownId) {
  PostgresWorkerRepository repo(pool_, logger_);
  auto found = repo.find_by_id(infra::WorkerId::generate());
  ASSERT_FALSE(found.has_value());
  EXPECT_EQ(found.error().code(), ErrorCode::NotFound);
}

TEST_F(PostgresWorkerRepositoryTest, DuplicateInsertReturnsConflict) {
  PostgresWorkerRepository repo(pool_, logger_);
  domain::Worker worker = make_worker();
  ASSERT_TRUE(repo.insert(worker).has_value());
  auto second = repo.insert(worker);
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error().code(), ErrorCode::Conflict);
}

TEST_F(PostgresWorkerRepositoryTest, ListReturnsAllWorkers) {
  PostgresWorkerRepository repo(pool_, logger_);
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(repo.insert(make_worker("worker-" + std::to_string(i))).has_value());
  }
  auto listed = repo.list();
  ASSERT_TRUE(listed.has_value());
  EXPECT_EQ(listed->size(), 3u);
}

TEST_F(PostgresWorkerRepositoryTest, UpdatePersistsStatusAndHeartbeat) {
  PostgresWorkerRepository repo(pool_, logger_);
  domain::Worker worker = make_worker();
  ASSERT_TRUE(repo.insert(worker).has_value());

  const auto beat = std::chrono::system_clock::now() + std::chrono::seconds(30);
  worker.set_status(domain::WorkerStatus::Busy);
  worker.heartbeat(beat);
  ASSERT_TRUE(repo.update(worker).has_value());

  auto found = repo.find_by_id(worker.id());
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->status(), domain::WorkerStatus::Busy);
  EXPECT_NEAR(std::chrono::duration<double>(found->last_heartbeat().time_since_epoch()).count(),
              std::chrono::duration<double>(beat.time_since_epoch()).count(), 0.001);
}

TEST_F(PostgresWorkerRepositoryTest, UpdateUnknownWorkerReturnsNotFound) {
  PostgresWorkerRepository repo(pool_, logger_);
  domain::Worker worker = make_worker();
  auto result = repo.update(worker);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
}

}  // namespace
}  // namespace flowforge::persistence::postgres
