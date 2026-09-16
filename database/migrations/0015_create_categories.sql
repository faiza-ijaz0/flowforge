-- Phase 3F: the Category domain's persisted read model -- see
-- docs/architecture/category-processing.md. Mirrors
-- flowforge::domain::Category (engine/include/flowforge/domain/category.hpp).
--
-- Written by a job handler at execution time (handlers::
-- CategoryProcessHandler), exactly like migration 0014's `products` table
-- is written by handlers::ProductProcessHandler -- see that migration's
-- header comment for why a constructor-injected repository does not
-- violate IJobHandler's "no database connection" contract.
--
-- parent_slug is a bare TEXT column, deliberately NOT
-- `REFERENCES categories(slug)`:
--   1. `InMemoryCategoryRepository` (used by every non-Postgres-gated
--      test) has no equivalent of a foreign-key constraint, and
--      `CategoryProcessHandler`'s parent-existence/cycle validation (see
--      that handler's class comment) already performs the check
--      explicitly through `ICategoryRepository::find_by_slug` -- a DB-level
--      FK would just duplicate that check for the Postgres backend only,
--      with different failure behavior (in-memory: a clean
--      `ErrorCode::Validation`; Postgres: a `foreign_key_violation`
--      exception that would need SQLSTATE-specific translation to avoid
--      being misclassified as a retryable infrastructure error).
--   2. A missing parent must be a non-retryable Validation failure (see
--      category-processing.md, "Why parent-existence is checked at
--      execution time, not preview/confirm time") -- doing the check in
--      application code, once, keeps that classification correct on both
--      backends instead of only correct on the one whose repository
--      happens to check first.
CREATE TABLE categories (
    id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name           TEXT NOT NULL,
    slug           TEXT NOT NULL,
    description    TEXT,
    parent_slug    TEXT,
    job_id         UUID REFERENCES jobs (id) ON DELETE SET NULL,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    -- Case-normalized at the application layer (domain::slugify always
    -- lowercases), so a plain UNIQUE constraint is sufficient -- no
    -- functional/expression index needed to catch "same slug, different
    -- case".
    CONSTRAINT categories_slug_unique UNIQUE (slug)
);

-- Supports ICategoryRepository::list()'s pagination query (ORDER BY
-- created_at, id -- see PostgresProductRepository's identical pattern)
-- without a sequential scan as the table grows.
CREATE INDEX idx_categories_created_at ON categories (created_at, id);

-- Supports the "which categories came from this import batch" query,
-- mirroring idx_products_job_id's rationale (migration 0014).
CREATE INDEX idx_categories_job_id ON categories (job_id);

-- Supports CategoryProcessHandler's parent-chain walk
-- (find_by_slug(parent_slug), repeated up the chain) and a future
-- "children of this category" query without a sequential scan.
CREATE INDEX idx_categories_parent_slug ON categories (parent_slug);
