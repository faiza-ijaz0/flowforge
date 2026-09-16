#include "flowforge/persistence/in_memory_repositories.hpp"

#include <gtest/gtest.h>

namespace flowforge::persistence {
namespace {

domain::NormalizedCategoryRecord make_record(std::string slug = "electronics") {
  return {.name = "Electronics",
          .slug = std::move(slug),
          .description = std::nullopt,
          .parent_slug = std::nullopt};
}

TEST(InMemoryCategoryRepositoryTest, UpsertThenFindBySlug) {
  InMemoryCategoryRepository repo;
  auto job_id = infra::JobId::generate();
  ASSERT_TRUE(repo.upsert(job_id, make_record()).has_value());

  auto found = repo.find_by_slug("electronics");
  ASSERT_TRUE(found.has_value());
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ((*found)->slug, "electronics");
  EXPECT_EQ((*found)->name, "Electronics");
  EXPECT_EQ((*found)->job_id, job_id);
}

TEST(InMemoryCategoryRepositoryTest, FindBySlugReturnsNulloptForUnknownSlug) {
  InMemoryCategoryRepository repo;
  auto found = repo.find_by_slug("nope");
  ASSERT_TRUE(found.has_value());
  EXPECT_FALSE(found->has_value());
}

TEST(InMemoryCategoryRepositoryTest, ReimportingTheSameSlugUpdatesInPlace) {
  InMemoryCategoryRepository repo;
  domain::NormalizedCategoryRecord v1{
      .name = "Electronics", .slug = "electronics", .description = "v1", .parent_slug = std::nullopt};
  ASSERT_TRUE(repo.upsert(infra::JobId::generate(), v1).has_value());
  auto first_id = (*repo.find_by_slug("electronics"))->id;

  domain::NormalizedCategoryRecord v2{
      .name = "Electronics", .slug = "electronics", .description = "v2", .parent_slug = std::nullopt};
  auto second_job_id = infra::JobId::generate();
  ASSERT_TRUE(repo.upsert(second_job_id, v2).has_value());

  auto found = repo.find_by_slug("electronics");
  ASSERT_TRUE(found.has_value() && found->has_value());
  EXPECT_EQ((*found)->id, first_id) << "the row's own id must be preserved across an upsert";
  EXPECT_EQ((*found)->description, "v2");
  EXPECT_EQ((*found)->job_id, second_job_id);

  auto count = repo.count();
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, 1u) << "an upsert of an existing slug must never create a second row";
}

TEST(InMemoryCategoryRepositoryTest, CountReflectsDistinctSlugsOnly) {
  InMemoryCategoryRepository repo;
  ASSERT_TRUE(repo.upsert(infra::JobId::generate(), make_record("electronics")).has_value());
  ASSERT_TRUE(repo.upsert(infra::JobId::generate(), make_record("home")).has_value());
  ASSERT_TRUE(repo.upsert(infra::JobId::generate(), make_record("electronics")).has_value());  // re-import

  auto count = repo.count();
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, 2u);
}

TEST(InMemoryCategoryRepositoryTest, ListRespectsLimitAndOffsetInInsertionOrder) {
  InMemoryCategoryRepository repo;
  std::vector<std::string> slugs;
  for (int i = 0; i < 5; ++i) {
    std::string slug = "cat-" + std::to_string(i);
    slugs.push_back(slug);
    ASSERT_TRUE(repo.upsert(infra::JobId::generate(), make_record(slug)).has_value());
  }

  auto page1 = repo.list(2, 0);
  ASSERT_TRUE(page1.has_value());
  ASSERT_EQ(page1->size(), 2u);
  EXPECT_EQ((*page1)[0].slug, slugs[0]);
  EXPECT_EQ((*page1)[1].slug, slugs[1]);

  auto page2 = repo.list(2, 4);
  ASSERT_TRUE(page2.has_value());
  ASSERT_EQ(page2->size(), 1u);
  EXPECT_EQ((*page2)[0].slug, slugs[4]);
}

}  // namespace
}  // namespace flowforge::persistence
