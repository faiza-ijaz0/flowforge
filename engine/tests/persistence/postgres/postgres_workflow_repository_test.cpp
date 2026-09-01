#include "flowforge/persistence/postgres/postgres_workflow_repository.hpp"

#include "flowforge/persistence/postgres/postgres_job_repository.hpp"
#include "postgres_test_support.hpp"

namespace flowforge::persistence::postgres {
namespace {

using test::PostgresIntegrationTest;
using PostgresWorkflowRepositoryTest = PostgresIntegrationTest;

domain::Job make_job() {
  return domain::Job(infra::JobId::generate(), "integration-tests", "{}", domain::RetryPolicy{},
                     std::chrono::system_clock::now());
}

// workflow_steps.job_id has a foreign key to jobs.id, so every step in
// these tests must reference an already-inserted job.
infra::JobId insert_job(PostgresJobRepository& jobs) {
  domain::Job job = make_job();
  EXPECT_TRUE(jobs.insert(job).has_value());
  return job.id();
}

TEST_F(PostgresWorkflowRepositoryTest, InsertThenFindByIdRoundTripsStepsInOrderWithDependencies) {
  PostgresJobRepository job_repo(pool_, logger_);
  PostgresWorkflowRepository repo(pool_, logger_);

  const auto job_a = insert_job(job_repo);
  const auto job_b = insert_job(job_repo);
  const auto job_c = insert_job(job_repo);

  domain::WorkflowStep step_a{
      .id = infra::WorkflowStepId::generate(), .name = "fetch", .job_id = job_a, .depends_on = {}};
  domain::WorkflowStep step_b{.id = infra::WorkflowStepId::generate(),
                              .name = "transform",
                              .job_id = job_b,
                              .depends_on = {step_a.id}};
  domain::WorkflowStep step_c{.id = infra::WorkflowStepId::generate(),
                              .name = "load",
                              .job_id = job_c,
                              .depends_on = {step_a.id, step_b.id}};

  domain::Workflow workflow(infra::WorkflowId::generate(), "etl-pipeline", {step_a, step_b, step_c},
                            std::chrono::system_clock::now());
  ASSERT_TRUE(repo.insert(workflow).has_value());

  auto found = repo.find_by_id(workflow.id());
  ASSERT_TRUE(found.has_value()) << found.error().message();
  EXPECT_EQ(found->name(), "etl-pipeline");
  EXPECT_EQ(found->status(), domain::WorkflowStatus::Pending);
  ASSERT_EQ(found->steps().size(), 3u);

  // Position must be preserved even though all three rows share the same
  // transaction timestamp (see migration 0010's rationale comment).
  EXPECT_EQ(found->steps()[0].name, "fetch");
  EXPECT_EQ(found->steps()[1].name, "transform");
  EXPECT_EQ(found->steps()[2].name, "load");

  EXPECT_TRUE(found->steps()[0].depends_on.empty());
  ASSERT_EQ(found->steps()[1].depends_on.size(), 1u);
  EXPECT_EQ(found->steps()[1].depends_on[0], step_a.id);
  ASSERT_EQ(found->steps()[2].depends_on.size(), 2u);
}

TEST_F(PostgresWorkflowRepositoryTest, InsertWithUnknownJobIdFailsAndWritesNothing) {
  PostgresWorkflowRepository repo(pool_, logger_);
  domain::WorkflowStep step{.id = infra::WorkflowStepId::generate(),
                            .name = "orphan",
                            .job_id = infra::JobId::generate(),  // never inserted
                            .depends_on = {}};
  domain::Workflow workflow(infra::WorkflowId::generate(), "broken", {step},
                            std::chrono::system_clock::now());

  auto result = repo.insert(workflow);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);

  // The whole transaction (workflow row included) must have rolled back.
  auto found = repo.find_by_id(workflow.id());
  ASSERT_FALSE(found.has_value());
  EXPECT_EQ(found.error().code(), ErrorCode::NotFound);
}

TEST_F(PostgresWorkflowRepositoryTest, FindByIdReturnsNotFoundForUnknownId) {
  PostgresWorkflowRepository repo(pool_, logger_);
  auto found = repo.find_by_id(infra::WorkflowId::generate());
  ASSERT_FALSE(found.has_value());
  EXPECT_EQ(found.error().code(), ErrorCode::NotFound);
}

TEST_F(PostgresWorkflowRepositoryTest, ListReturnsWorkflowsWithTheirSteps) {
  PostgresJobRepository job_repo(pool_, logger_);
  PostgresWorkflowRepository repo(pool_, logger_);

  for (int i = 0; i < 3; ++i) {
    domain::WorkflowStep step{.id = infra::WorkflowStepId::generate(),
                              .name = "step",
                              .job_id = insert_job(job_repo),
                              .depends_on = {}};
    domain::Workflow workflow(infra::WorkflowId::generate(), "wf-" + std::to_string(i), {step},
                              std::chrono::system_clock::now());
    ASSERT_TRUE(repo.insert(workflow).has_value());
  }

  auto listed = repo.list(10, 0);
  ASSERT_TRUE(listed.has_value()) << listed.error().message();
  ASSERT_EQ(listed->size(), 3u);
  for (const auto& workflow : *listed) {
    ASSERT_EQ(workflow.steps().size(), 1u);
  }
}

TEST_F(PostgresWorkflowRepositoryTest, UpdatePersistsStatusWithoutTouchingSteps) {
  PostgresJobRepository job_repo(pool_, logger_);
  PostgresWorkflowRepository repo(pool_, logger_);

  domain::WorkflowStep step{.id = infra::WorkflowStepId::generate(),
                            .name = "only-step",
                            .job_id = insert_job(job_repo),
                            .depends_on = {}};
  domain::Workflow workflow(infra::WorkflowId::generate(), "wf", {step}, std::chrono::system_clock::now());
  ASSERT_TRUE(repo.insert(workflow).has_value());

  workflow.transition_to(domain::WorkflowStatus::Running, std::chrono::system_clock::now());
  ASSERT_TRUE(repo.update(workflow).has_value());

  auto found = repo.find_by_id(workflow.id());
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->status(), domain::WorkflowStatus::Running);
  ASSERT_EQ(found->steps().size(), 1u);
  EXPECT_EQ(found->steps()[0].name, "only-step");
}

TEST_F(PostgresWorkflowRepositoryTest, UpdateUnknownWorkflowReturnsNotFound) {
  PostgresWorkflowRepository repo(pool_, logger_);
  domain::Workflow workflow(infra::WorkflowId::generate(), "ghost", {}, std::chrono::system_clock::now());
  auto result = repo.update(workflow);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
}

}  // namespace
}  // namespace flowforge::persistence::postgres
