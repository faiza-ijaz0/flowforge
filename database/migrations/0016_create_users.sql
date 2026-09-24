-- Phase 3H: the Users domain's persisted read model -- closes the gap
-- documented in docs/architecture/phase-3g-audit.md §2.4 and
-- docs/architecture/phase-3h-production-readiness.md: unlike Products
-- (migration 0014) and Categories (migration 0015), Users had no dedicated
-- table -- handlers::UserProcessHandler validated/normalized a record but
-- persisted nothing beyond the job's own row. Mirrors flowforge::domain::User
-- (engine/include/flowforge/domain/user.hpp) and products' table shape/
-- conventions exactly.
--
-- email (not sku/slug) is the natural business key: validate_and_normalize_
-- user_record() already trims+lowercases every email before it reaches this
-- table (see domain/user_record.hpp), so a plain UNIQUE constraint is
-- sufficient here too -- no functional/expression index needed.
--
-- job_id / ON DELETE SET NULL: identical rationale to products.job_id
-- (migration 0014) -- provenance recoverable via jobs.workload_id, and a
-- User row is real business data that must survive its originating Job
-- being deleted.
CREATE TABLE users (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name       TEXT NOT NULL,
    email      TEXT NOT NULL,
    phone      TEXT,
    job_id     UUID REFERENCES jobs (id) ON DELETE SET NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    CONSTRAINT users_email_unique UNIQUE (email)
);

-- Supports IUserRepository::list()'s pagination query (ORDER BY
-- created_at, id -- mirrors PostgresProductRepository's identical pattern)
-- without a sequential scan as the table grows.
CREATE INDEX idx_users_created_at ON users (created_at, id);

-- Supports the same "which rows came from this import batch" query
-- idx_products_job_id/idx_categories_job_id already support.
CREATE INDEX idx_users_job_id ON users (job_id);
