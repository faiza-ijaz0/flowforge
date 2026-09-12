#include "flowforge/persistence/in_memory_repositories.hpp"

#include <gtest/gtest.h>

namespace flowforge::persistence {
namespace {

domain::NormalizedProductRecord make_record(std::string sku = "WID-1", double price = 19.99) {
  return {.sku = std::move(sku),
          .name = "Widget",
          .price = price,
          .currency = "USD",
          .category = std::nullopt,
          .description = std::nullopt,
          .stock_quantity = 0};
}

TEST(InMemoryProductRepositoryTest, UpsertThenFindBySku) {
  InMemoryProductRepository repo;
  auto job_id = infra::JobId::generate();
  ASSERT_TRUE(repo.upsert(job_id, make_record()).has_value());

  auto found = repo.find_by_sku("WID-1");
  ASSERT_TRUE(found.has_value());
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ((*found)->sku, "WID-1");
  EXPECT_EQ((*found)->name, "Widget");
  EXPECT_DOUBLE_EQ((*found)->price, 19.99);
  EXPECT_EQ((*found)->job_id, job_id);
}

TEST(InMemoryProductRepositoryTest, FindBySkuReturnsNulloptForUnknownSku) {
  InMemoryProductRepository repo;
  auto found = repo.find_by_sku("NOPE");
  ASSERT_TRUE(found.has_value());
  EXPECT_FALSE(found->has_value());
}

TEST(InMemoryProductRepositoryTest, ReimportingTheSameSkuUpdatesInPlace) {
  InMemoryProductRepository repo;
  ASSERT_TRUE(repo.upsert(infra::JobId::generate(), make_record("WID-1", 10.0)).has_value());
  auto first_id = (*repo.find_by_sku("WID-1"))->id;

  auto second_job_id = infra::JobId::generate();
  ASSERT_TRUE(repo.upsert(second_job_id, make_record("WID-1", 15.0)).has_value());

  auto found = repo.find_by_sku("WID-1");
  ASSERT_TRUE(found.has_value() && found->has_value());
  EXPECT_EQ((*found)->id, first_id) << "the row's own id must be preserved across an upsert";
  EXPECT_DOUBLE_EQ((*found)->price, 15.0);
  EXPECT_EQ((*found)->job_id, second_job_id);

  auto count = repo.count();
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, 1u) << "an upsert of an existing SKU must never create a second row";
}

TEST(InMemoryProductRepositoryTest, CountReflectsDistinctSkusOnly) {
  InMemoryProductRepository repo;
  ASSERT_TRUE(repo.upsert(infra::JobId::generate(), make_record("WID-1")).has_value());
  ASSERT_TRUE(repo.upsert(infra::JobId::generate(), make_record("WID-2")).has_value());
  ASSERT_TRUE(repo.upsert(infra::JobId::generate(), make_record("WID-1")).has_value());  // re-import

  auto count = repo.count();
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, 2u);
}

TEST(InMemoryProductRepositoryTest, ListRespectsLimitAndOffsetInInsertionOrder) {
  InMemoryProductRepository repo;
  std::vector<std::string> skus;
  for (int i = 0; i < 5; ++i) {
    std::string sku = "WID-" + std::to_string(i);
    skus.push_back(sku);
    ASSERT_TRUE(repo.upsert(infra::JobId::generate(), make_record(sku)).has_value());
  }

  auto page1 = repo.list(2, 0);
  ASSERT_TRUE(page1.has_value());
  ASSERT_EQ(page1->size(), 2u);
  EXPECT_EQ((*page1)[0].sku, skus[0]);
  EXPECT_EQ((*page1)[1].sku, skus[1]);

  auto page2 = repo.list(2, 4);
  ASSERT_TRUE(page2.has_value());
  ASSERT_EQ(page2->size(), 1u);
  EXPECT_EQ((*page2)[0].sku, skus[4]);
}

}  // namespace
}  // namespace flowforge::persistence
