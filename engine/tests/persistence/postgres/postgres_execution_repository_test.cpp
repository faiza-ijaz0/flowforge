#include "flowforge/persistence/postgres/postgres_execution_repository.hpp"

#include <tuple>

#include "flowforge/persistence/postgres/postgres_job_repository.hpp"
#include "flowforge/persistence/postgres/postgres_worker_repository.hpp"
#include "postgres_test_support.hpp"

namespace flowforge::persistence::postgres {
namespace {

using test::PostgresIntegrationTest;
using PostgresExecutionRepositoryTest = PostgresIntegrationTest;

/// job_attempts.job_id/worker_id are real foreign keys -- a genuine
/// attempt row needs a real jobs row (and, if worker_id is set, a real
/// workers row) to reference. This mirrors how PostgresJobRepositoryTest
/// provisions its own `queues` row rather than assuming one exists.
infra::JobId insert_job(const std::shared_ptr<PgConnectionPool>& pool,
                        const std::shared_ptr<infra::Logger>& logger) {
  PostgresJobRepository job_repo(pool, logger);
  domain::Job job(infra::JobId::generate(), "execution-tests", "{}", domain::RetryPolicy{},
                  std::chrono::system_clock::now(), 0, "echo");
  std::ignore = job_repo.insert(job);
  return job.id();
}

infra::WorkerId insert_worker(const std::shared_ptr<PgConnectionPool>& pool,
                              const std::shared_ptr<infra::Logger>& logger) {
  PostgresWorkerRepository worker_repo(pool, logger);
  domain::Worker worker(infra::WorkerId::generate(), "worker-test", std::chrono::system_clock::now());
  std::ignore = worker_repo.insert(worker);
  return worker.id();
}

domain::Execution make_execution(infra::JobId job_id, std::optional<infra::WorkerId> worker_id,
                                 std::uint32_t attempt_number = 1) {
  domain::Execution execution;
  execution.id = infra::ExecutionId::generate();
  execution.job_id = std::move(job_id);
  execution.worker_id = std::move(worker_id);
  execution.attempt_number = attempt_number;
  execution.outcome = domain::ExecutionOutcome::Succeeded;
  execution.started_at = std::chrono::system_clock::now();
  execution.finished_at = execution.started_at + std::chrono::seconds(1);
  return execution;
}

TEST_F(PostgresExecutionRepositoryTest, RecordThenHistoryForRoundTripsAllFields) {
  PostgresExecutionRepository repo(pool_, logger_);
  auto job_id = insert_job(pool_, logger_);
  auto worker_id = insert_worker(pool_, logger_);

  domain::Execution execution = make_execution(job_id, worker_id);
  execution.error_message = "boom";
  execution.outcome = domain::ExecutionOutcome::Failed;
  ASSERT_TRUE(repo.record(execution).has_value());

  auto history = repo.history_for(job_id);
  ASSERT_TRUE(history.has_value()) << history.error().message();
  ASSERT_EQ(history->size(), 1u);
  const auto& found = (*history)[0];
  EXPECT_EQ(found.id, execution.id);
  ASSERT_TRUE(found.worker_id.has_value());
  EXPECT_EQ(*found.worker_id, worker_id);
  EXPECT_EQ(found.attempt_number, 1u);
  EXPECT_EQ(found.outcome, domain::ExecutionOutcome::Failed);
  ASSERT_TRUE(found.error_message.has_value());
  EXPECT_EQ(*found.error_message, "boom");
  ASSERT_TRUE(found.finished_at.has_value());
}

TEST_F(PostgresExecutionRepositoryTest, WorkerIdIsNullableAndRoundTripsAsNullopt) {
  PostgresExecutionRepository repo(pool_, logger_);
  auto job_id = insert_job(pool_, logger_);

  auto execution = make_execution(job_id, std::nullopt);
  ASSERT_TRUE(repo.record(execution).has_value());

  auto history = repo.history_for(job_id);
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(history->size(), 1u);
  EXPECT_FALSE((*history)[0].worker_id.has_value());
}

TEST_F(PostgresExecutionRepositoryTest, MultipleAttemptsForSameJobOrderedByAttemptNumber) {
  PostgresExecutionRepository repo(pool_, logger_);
  auto job_id = insert_job(pool_, logger_);

  ASSERT_TRUE(repo.record(make_execution(job_id, std::nullopt, 1)).has_value());
  ASSERT_TRUE(repo.record(make_execution(job_id, std::nullopt, 2)).has_value());

  auto history = repo.history_for(job_id);
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(history->size(), 2u);
  EXPECT_EQ((*history)[0].attempt_number, 1u);
  EXPECT_EQ((*history)[1].attempt_number, 2u);
}

TEST_F(PostgresExecutionRepositoryTest, HistoryForUnknownJobReturnsEmpty) {
  PostgresExecutionRepository repo(pool_, logger_);
  auto history = repo.history_for(infra::JobId::generate());
  ASSERT_TRUE(history.has_value());
  EXPECT_TRUE(history->empty());
}

}  // namespace
}  // namespace flowforge::persistence::postgres
