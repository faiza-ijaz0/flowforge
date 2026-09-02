#include "flowforge/persistence/postgres/postgres_workload_repository.hpp"

#include "postgres_test_support.hpp"

namespace flowforge::persistence::postgres {
namespace {

using test::PostgresIntegrationTest;
using PostgresWorkloadRepositoryTest = PostgresIntegrationTest;

domain::Workload make_workload(std::string type = "user.process", std::size_t total_items = 3) {
  return {infra::WorkloadId::generate(), std::move(type), total_items, std::chrono::system_clock::now()};
}

TEST_F(PostgresWorkloadRepositoryTest, InsertThenFindByIdRoundTripsFields) {
  PostgresWorkloadRepository repo(pool_, logger_);
  domain::Workload workload = make_workload();
  ASSERT_TRUE(repo.insert(workload).has_value());

  auto found = repo.find_by_id(workload.id());
  ASSERT_TRUE(found.has_value()) << found.error().message();
  EXPECT_EQ(found->id(), workload.id());
  EXPECT_EQ(found->type(), "user.process");
  EXPECT_EQ(found->total_items(), 3u);
  // find_by_id() returns progress at its defaults -- see
  // IWorkloadRepository's class comment: real progress is computed by
  // services::WorkloadService from child Job rows, not stored here.
  EXPECT_EQ(found->completed_items(), 0u);
  EXPECT_EQ(found->failed_items(), 0u);
  EXPECT_EQ(found->status(), domain::WorkloadStatus::Pending);
}

TEST_F(PostgresWorkloadRepositoryTest, FindByIdReturnsNotFoundForUnknownId) {
  PostgresWorkloadRepository repo(pool_, logger_);
  auto found = repo.find_by_id(infra::WorkloadId::generate());
  ASSERT_FALSE(found.has_value());
  EXPECT_EQ(found.error().code(), ErrorCode::NotFound);
}

TEST_F(PostgresWorkloadRepositoryTest, ZeroItemWorkloadRoundTrips) {
  PostgresWorkloadRepository repo(pool_, logger_);
  domain::Workload workload = make_workload("user.process", 0);
  ASSERT_TRUE(repo.insert(workload).has_value());

  auto found = repo.find_by_id(workload.id());
  ASSERT_TRUE(found.has_value()) << found.error().message();
  EXPECT_EQ(found->total_items(), 0u);
}

TEST_F(PostgresWorkloadRepositoryTest, ListRespectsLimitAndOffsetInCreationOrder) {
  PostgresWorkloadRepository repo(pool_, logger_);
  std::vector<infra::WorkloadId> ids;
  for (int i = 0; i < 3; ++i) {
    domain::Workload workload = make_workload();
    ids.push_back(workload.id());
    ASSERT_TRUE(repo.insert(workload).has_value());
  }

  auto page1 = repo.list(2, 0);
  ASSERT_TRUE(page1.has_value()) << page1.error().message();
  ASSERT_EQ(page1->size(), 2u);
  EXPECT_EQ((*page1)[0].id(), ids[0]);
  EXPECT_EQ((*page1)[1].id(), ids[1]);

  auto page2 = repo.list(2, 2);
  ASSERT_TRUE(page2.has_value());
  ASSERT_EQ(page2->size(), 1u);
  EXPECT_EQ((*page2)[0].id(), ids[2]);
}

// Restart-persistence: a workload inserted by one repository instance is
// visible, unchanged, to a second instance backed by the same connection
// pool/database -- proving the row genuinely round-trips through
// PostgreSQL rather than only ever being read back from the same
// in-process object.
TEST_F(PostgresWorkloadRepositoryTest, PersistsAcrossRepositoryInstances) {
  domain::Workload workload = make_workload("user.process", 5);
  {
    PostgresWorkloadRepository writer(pool_, logger_);
    ASSERT_TRUE(writer.insert(workload).has_value());
  }

  PostgresWorkloadRepository reader(pool_, logger_);
  auto found = reader.find_by_id(workload.id());
  ASSERT_TRUE(found.has_value()) << found.error().message();
  EXPECT_EQ(found->type(), "user.process");
  EXPECT_EQ(found->total_items(), 5u);
}

}  // namespace
}  // namespace flowforge::persistence::postgres
