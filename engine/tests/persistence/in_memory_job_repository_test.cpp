#include "flowforge/persistence/in_memory_repositories.hpp"

#include <gtest/gtest.h>

namespace flowforge::persistence {
namespace {

domain::Job make_job(std::string queue = "default") {
  return domain::Job(infra::JobId::generate(), std::move(queue), "{}", domain::RetryPolicy{},
                     std::chrono::system_clock::now());
}

TEST(InMemoryJobRepositoryTest, InsertThenFindById) {
  InMemoryJobRepository repo;
  domain::Job job = make_job();
  ASSERT_TRUE(repo.insert(job).has_value());

  auto found = repo.find_by_id(job.id());
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->id(), job.id());
  EXPECT_EQ(found->queue_name(), "default");
}

TEST(InMemoryJobRepositoryTest, FindByIdReturnsNotFoundForUnknownId) {
  InMemoryJobRepository repo;
  auto found = repo.find_by_id(infra::JobId::generate());
  ASSERT_FALSE(found.has_value());
  EXPECT_EQ(found.error().code(), ErrorCode::NotFound);
}

TEST(InMemoryJobRepositoryTest, DuplicateInsertReturnsConflict) {
  InMemoryJobRepository repo;
  domain::Job job = make_job();
  ASSERT_TRUE(repo.insert(job).has_value());
  auto second = repo.insert(job);
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error().code(), ErrorCode::Conflict);
}

TEST(InMemoryJobRepositoryTest, ListRespectsLimitAndOffsetInInsertionOrder) {
  InMemoryJobRepository repo;
  std::vector<infra::JobId> ids;
  for (int i = 0; i < 5; ++i) {
    domain::Job job = make_job();
    ids.push_back(job.id());
    ASSERT_TRUE(repo.insert(job).has_value());
  }

  auto page1 = repo.list(2, 0);
  ASSERT_TRUE(page1.has_value());
  ASSERT_EQ(page1->size(), 2u);
  EXPECT_EQ((*page1)[0].id(), ids[0]);
  EXPECT_EQ((*page1)[1].id(), ids[1]);

  auto page2 = repo.list(2, 4);
  ASSERT_TRUE(page2.has_value());
  ASSERT_EQ(page2->size(), 1u);
  EXPECT_EQ((*page2)[0].id(), ids[4]);

  auto out_of_range = repo.list(2, 10);
  ASSERT_TRUE(out_of_range.has_value());
  EXPECT_TRUE(out_of_range->empty());
}

TEST(InMemoryJobRepositoryTest, UpdatePersistsChanges) {
  InMemoryJobRepository repo;
  domain::Job job = make_job();
  ASSERT_TRUE(repo.insert(job).has_value());

  job.transition_to(domain::JobStatus::Cancelled, std::chrono::system_clock::now());
  ASSERT_TRUE(repo.update(job).has_value());

  auto found = repo.find_by_id(job.id());
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->status(), domain::JobStatus::Cancelled);
}

TEST(InMemoryJobRepositoryTest, UpdateUnknownJobReturnsNotFound) {
  InMemoryJobRepository repo;
  domain::Job job = make_job();
  auto result = repo.update(job);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
}

TEST(InMemoryJobRepositoryTest, ListByStatusReturnsOnlyMatchingJobsInInsertionOrder) {
  InMemoryJobRepository repo;
  domain::Job retrying_1 = make_job();
  domain::Job queued = make_job();
  domain::Job retrying_2 = make_job();
  ASSERT_TRUE(repo.insert(retrying_1).has_value());
  ASSERT_TRUE(repo.insert(queued).has_value());
  ASSERT_TRUE(repo.insert(retrying_2).has_value());

  retrying_1.transition_to(domain::JobStatus::Retrying, std::chrono::system_clock::now());
  ASSERT_TRUE(repo.update(retrying_1).has_value());
  queued.transition_to(domain::JobStatus::Queued, std::chrono::system_clock::now());
  ASSERT_TRUE(repo.update(queued).has_value());
  retrying_2.transition_to(domain::JobStatus::Retrying, std::chrono::system_clock::now());
  ASSERT_TRUE(repo.update(retrying_2).has_value());

  auto retrying = repo.list_by_status(domain::JobStatus::Retrying, 10);
  ASSERT_TRUE(retrying.has_value());
  ASSERT_EQ(retrying->size(), 2u);
  EXPECT_EQ((*retrying)[0].id(), retrying_1.id());
  EXPECT_EQ((*retrying)[1].id(), retrying_2.id());

  auto none = repo.list_by_status(domain::JobStatus::DeadLetter, 10);
  ASSERT_TRUE(none.has_value());
  EXPECT_TRUE(none->empty());
}

TEST(InMemoryJobRepositoryTest, ListByStatusRespectsLimit) {
  InMemoryJobRepository repo;
  for (int i = 0; i < 5; ++i) {
    domain::Job job = make_job();
    job.transition_to(domain::JobStatus::Retrying, std::chrono::system_clock::now());
    ASSERT_TRUE(repo.insert(job).has_value());
  }

  auto limited = repo.list_by_status(domain::JobStatus::Retrying, 2);
  ASSERT_TRUE(limited.has_value());
  EXPECT_EQ(limited->size(), 2u);
}

// Phase 3A: list_by_workload_id backs services::WorkloadService's progress
// aggregation (see docs/architecture/workload-model.md).
TEST(InMemoryJobRepositoryTest, ListByWorkloadIdReturnsOnlyMatchingJobs) {
  InMemoryJobRepository repo;
  const auto workload_id = infra::WorkloadId::generate();
  const auto other_workload_id = infra::WorkloadId::generate();

  domain::Job in_workload_1(infra::JobId::generate(), "default", "{}", domain::RetryPolicy{},
                            std::chrono::system_clock::now(), 0, "user.process", workload_id);
  domain::Job in_other_workload(infra::JobId::generate(), "default", "{}", domain::RetryPolicy{},
                                std::chrono::system_clock::now(), 0, "user.process", other_workload_id);
  domain::Job without_workload = make_job();
  domain::Job in_workload_2(infra::JobId::generate(), "default", "{}", domain::RetryPolicy{},
                            std::chrono::system_clock::now(), 0, "user.process", workload_id);

  ASSERT_TRUE(repo.insert(in_workload_1).has_value());
  ASSERT_TRUE(repo.insert(in_other_workload).has_value());
  ASSERT_TRUE(repo.insert(without_workload).has_value());
  ASSERT_TRUE(repo.insert(in_workload_2).has_value());

  auto found = repo.list_by_workload_id(workload_id, 10);
  ASSERT_TRUE(found.has_value());
  ASSERT_EQ(found->size(), 2u);
  EXPECT_EQ((*found)[0].id(), in_workload_1.id());
  EXPECT_EQ((*found)[1].id(), in_workload_2.id());
}

TEST(InMemoryJobRepositoryTest, ListByWorkloadIdReturnsEmptyWhenNoJobsMatch) {
  InMemoryJobRepository repo;
  domain::Job job = make_job();
  ASSERT_TRUE(repo.insert(job).has_value());

  auto found = repo.list_by_workload_id(infra::WorkloadId::generate(), 10);
  ASSERT_TRUE(found.has_value());
  EXPECT_TRUE(found->empty());
}

}  // namespace
}  // namespace flowforge::persistence
