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

}  // namespace
}  // namespace flowforge::persistence
