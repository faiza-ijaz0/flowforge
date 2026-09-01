#include "flowforge/persistence/in_memory_repositories.hpp"

#include <gtest/gtest.h>

namespace flowforge::persistence {
namespace {

domain::Execution make_execution(infra::JobId job_id, std::uint32_t attempt_number = 1) {
  domain::Execution execution;
  execution.id = infra::ExecutionId::generate();
  execution.job_id = std::move(job_id);
  execution.worker_id = infra::WorkerId::generate();
  execution.attempt_number = attempt_number;
  execution.outcome = domain::ExecutionOutcome::Succeeded;
  execution.started_at = std::chrono::system_clock::now();
  execution.finished_at = execution.started_at;
  return execution;
}

TEST(InMemoryExecutionRepositoryTest, RecordThenHistoryForReturnsIt) {
  InMemoryExecutionRepository repo;
  auto job_id = infra::JobId::generate();
  auto execution = make_execution(job_id);
  ASSERT_TRUE(repo.record(execution).has_value());

  auto history = repo.history_for(job_id);
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(history->size(), 1u);
  EXPECT_EQ((*history)[0].id, execution.id);
}

TEST(InMemoryExecutionRepositoryTest, HistoryForUnknownJobReturnsEmpty) {
  InMemoryExecutionRepository repo;
  auto history = repo.history_for(infra::JobId::generate());
  ASSERT_TRUE(history.has_value());
  EXPECT_TRUE(history->empty());
}

TEST(InMemoryExecutionRepositoryTest, DuplicateRecordReturnsConflict) {
  InMemoryExecutionRepository repo;
  auto job_id = infra::JobId::generate();
  auto execution = make_execution(job_id);
  ASSERT_TRUE(repo.record(execution).has_value());

  auto second = repo.record(execution);
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error().code(), ErrorCode::Conflict);
}

TEST(InMemoryExecutionRepositoryTest, MultipleAttemptsForSameJobAreAllRetainedInOrder) {
  InMemoryExecutionRepository repo;
  auto job_id = infra::JobId::generate();
  ASSERT_TRUE(repo.record(make_execution(job_id, 1)).has_value());
  ASSERT_TRUE(repo.record(make_execution(job_id, 2)).has_value());
  ASSERT_TRUE(repo.record(make_execution(job_id, 3)).has_value());

  auto history = repo.history_for(job_id);
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(history->size(), 3u);
  EXPECT_EQ((*history)[0].attempt_number, 1u);
  EXPECT_EQ((*history)[1].attempt_number, 2u);
  EXPECT_EQ((*history)[2].attempt_number, 3u);
}

}  // namespace
}  // namespace flowforge::persistence
