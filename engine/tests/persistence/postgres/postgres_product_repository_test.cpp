#include "flowforge/persistence/postgres/postgres_product_repository.hpp"

#include "flowforge/persistence/postgres/postgres_job_repository.hpp"
#include "postgres_test_support.hpp"

namespace flowforge::persistence::postgres {
namespace {

using test::PostgresIntegrationTest;
using PostgresProductRepositoryTest = PostgresIntegrationTest;

domain::NormalizedProductRecord make_record(std::string sku = "WID-1", double price = 19.99) {
  return {.sku = std::move(sku),
          .name = "Widget",
          .price = price,
          .currency = "USD",
          .category = "Tools",
          .description = "A fine widget.",
          .stock_quantity = 5};
}

/// `products.job_id` is a real foreign key (database/migrations/
/// 0014_create_products.sql) -- every test needs one genuine, already-
/// persisted `jobs` row to reference, mirroring how
/// postgres_workload_repository_test.cpp's Job-referencing tests would.
infra::JobId insert_test_job(const std::shared_ptr<PgConnectionPool>& pool,
                             const std::shared_ptr<infra::Logger>& logger) {
  PostgresJobRepository jobs(pool, logger);
  domain::Job job(infra::JobId::generate(), "integration-tests", R"({"hello":"world"})",
                  domain::RetryPolicy{}, std::chrono::system_clock::now());
  auto inserted = jobs.insert(job);
  if (!inserted) {
    ADD_FAILURE() << "failed to insert prerequisite job row: " << inserted.error().message();
  }
  return job.id();
}

TEST_F(PostgresProductRepositoryTest, UpsertThenFindBySkuRoundTripsFields) {
  auto job_id = insert_test_job(pool_, logger_);
  PostgresProductRepository repo(pool_, logger_);
  ASSERT_TRUE(repo.upsert(job_id, make_record()).has_value());

  auto found = repo.find_by_sku("WID-1");
  ASSERT_TRUE(found.has_value()) << found.error().message();
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ((*found)->sku, "WID-1");
  EXPECT_EQ((*found)->name, "Widget");
  EXPECT_DOUBLE_EQ((*found)->price, 19.99);
  EXPECT_EQ((*found)->currency, "USD");
  ASSERT_TRUE((*found)->category.has_value());
  EXPECT_EQ(*(*found)->category, "Tools");
  EXPECT_EQ((*found)->stock_quantity, 5);
  ASSERT_TRUE((*found)->job_id.has_value());
  EXPECT_EQ(*(*found)->job_id, job_id);
}

TEST_F(PostgresProductRepositoryTest, FindBySkuReturnsNulloptForUnknownSku) {
  PostgresProductRepository repo(pool_, logger_);
  auto found = repo.find_by_sku("NOPE");
  ASSERT_TRUE(found.has_value()) << found.error().message();
  EXPECT_FALSE(found->has_value());
}

TEST_F(PostgresProductRepositoryTest, ReimportingTheSameSkuUpdatesInPlaceRatherThanConflicting) {
  auto job_id_1 = insert_test_job(pool_, logger_);
  auto job_id_2 = insert_test_job(pool_, logger_);
  PostgresProductRepository repo(pool_, logger_);

  ASSERT_TRUE(repo.upsert(job_id_1, make_record("WID-1", 10.0)).has_value());
  auto first_id = (*repo.find_by_sku("WID-1"))->id;

  // The whole point of upsert(): a duplicate SKU must never surface as
  // ErrorCode::Conflict -- see IProductRepository's class comment.
  auto second = repo.upsert(job_id_2, make_record("WID-1", 15.0));
  ASSERT_TRUE(second.has_value()) << second.error().message();

  auto found = repo.find_by_sku("WID-1");
  ASSERT_TRUE(found.has_value() && found->has_value());
  EXPECT_EQ((*found)->id, first_id) << "the row's own id must be preserved across an upsert";
  EXPECT_DOUBLE_EQ((*found)->price, 15.0);
  EXPECT_EQ(*(*found)->job_id, job_id_2);

  auto count = repo.count();
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, 1u);
}

TEST_F(PostgresProductRepositoryTest, ListRespectsLimitAndOffsetInCreationOrder) {
  PostgresProductRepository repo(pool_, logger_);
  std::vector<std::string> skus;
  for (int i = 0; i < 3; ++i) {
    auto job_id = insert_test_job(pool_, logger_);
    std::string sku = "WID-" + std::to_string(i);
    skus.push_back(sku);
    ASSERT_TRUE(repo.upsert(job_id, make_record(sku)).has_value());
  }

  auto page1 = repo.list(2, 0);
  ASSERT_TRUE(page1.has_value()) << page1.error().message();
  ASSERT_EQ(page1->size(), 2u);
  EXPECT_EQ((*page1)[0].sku, skus[0]);
  EXPECT_EQ((*page1)[1].sku, skus[1]);

  auto page2 = repo.list(2, 2);
  ASSERT_TRUE(page2.has_value());
  ASSERT_EQ(page2->size(), 1u);
  EXPECT_EQ((*page2)[0].sku, skus[2]);
}

// Restart-persistence: a product upserted by one repository instance is
// visible, unchanged, to a second instance backed by the same connection
// pool/database.
TEST_F(PostgresProductRepositoryTest, PersistsAcrossRepositoryInstances) {
  auto job_id = insert_test_job(pool_, logger_);
  {
    PostgresProductRepository writer(pool_, logger_);
    ASSERT_TRUE(writer.upsert(job_id, make_record("WID-1", 42.0)).has_value());
  }

  PostgresProductRepository reader(pool_, logger_);
  auto found = reader.find_by_sku("WID-1");
  ASSERT_TRUE(found.has_value() && found->has_value());
  EXPECT_DOUBLE_EQ((*found)->price, 42.0);
}

}  // namespace
}  // namespace flowforge::persistence::postgres
