#include "flowforge/persistence/in_memory_repositories.hpp"

#include <gtest/gtest.h>

namespace flowforge::persistence {
namespace {

domain::Workload make_workload(std::string type = "user.process", std::size_t total_items = 2) {
  return {infra::WorkloadId::generate(), std::move(type), total_items, std::chrono::system_clock::now()};
}

TEST(InMemoryWorkloadRepositoryTest, InsertThenFindById) {
  InMemoryWorkloadRepository repo;
  domain::Workload workload = make_workload();
  ASSERT_TRUE(repo.insert(workload).has_value());

  auto found = repo.find_by_id(workload.id());
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->id(), workload.id());
  EXPECT_EQ(found->type(), "user.process");
  EXPECT_EQ(found->total_items(), 2u);
}

TEST(InMemoryWorkloadRepositoryTest, FindByIdReturnsNotFoundForUnknownId) {
  InMemoryWorkloadRepository repo;
  auto found = repo.find_by_id(infra::WorkloadId::generate());
  ASSERT_FALSE(found.has_value());
  EXPECT_EQ(found.error().code(), ErrorCode::NotFound);
}

TEST(InMemoryWorkloadRepositoryTest, DuplicateInsertReturnsConflict) {
  InMemoryWorkloadRepository repo;
  domain::Workload workload = make_workload();
  ASSERT_TRUE(repo.insert(workload).has_value());
  auto second = repo.insert(workload);
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error().code(), ErrorCode::Conflict);
}

TEST(InMemoryWorkloadRepositoryTest, ListRespectsLimitAndOffsetInInsertionOrder) {
  InMemoryWorkloadRepository repo;
  std::vector<infra::WorkloadId> ids;
  for (int i = 0; i < 5; ++i) {
    domain::Workload workload = make_workload();
    ids.push_back(workload.id());
    ASSERT_TRUE(repo.insert(workload).has_value());
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
}

}  // namespace
}  // namespace flowforge::persistence
