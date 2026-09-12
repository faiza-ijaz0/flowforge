-- Phase 3E: the Product domain's persisted read model -- see
-- docs/architecture/product-processing.md. Mirrors flowforge::domain::Product
-- (engine/include/flowforge/domain/product.hpp).
--
-- Unlike `workloads` (migration 0012), this table IS written by a job
-- handler at execution time (handlers::ProductProcessHandler), not only by
-- a service layer at creation time -- see product-processing.md, "Why
-- ProductProcessHandler writes to PostgreSQL directly" for why that does
-- not violate IJobHandler's "no database connection" contract (the
-- repository is a constructor-injected dependency of the handler
-- instance, never reached through ExecutionContext).
--
-- job_id (not workload_id): a product's import provenance is recoverable
-- via jobs.workload_id through this column, without duplicating the
-- workload_id foreign key onto this table too -- see
-- flowforge::domain::Product's class comment. ON DELETE SET NULL mirrors
-- migration 0013's jobs.workload_id choice: a Product row is real
-- business data, not grouping metadata, and must survive its originating
-- Job being deleted (nothing in this codebase deletes a Job today, but
-- the same conservative choice applies for the same reason).
CREATE TABLE products (
    id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    sku            TEXT NOT NULL,
    name           TEXT NOT NULL,
    price          NUMERIC(12, 2) NOT NULL CHECK (price >= 0),
    currency       TEXT NOT NULL,
    category       TEXT,
    description    TEXT,
    stock_quantity INTEGER NOT NULL DEFAULT 0 CHECK (stock_quantity >= 0),
    job_id         UUID REFERENCES jobs (id) ON DELETE SET NULL,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    -- Case-normalized at the application layer (domain::
    -- validate_and_normalize_product_record uppercases every SKU before
    -- it ever reaches this table), so a plain UNIQUE constraint is
    -- sufficient -- no functional/expression index needed to catch
    -- "same SKU, different case".
    CONSTRAINT products_sku_unique UNIQUE (sku)
);

-- Supports IProductRepository::list()'s pagination query (ORDER BY
-- created_at, id -- see PostgresWorkloadRepository's identical pattern)
-- without a sequential scan as the table grows.
CREATE INDEX idx_products_created_at ON products (created_at, id);

-- Supports the "which products came from this import batch" query a
-- future /workloads/{id} product drill-down could use, mirroring
-- idx_jobs_workload_id's rationale (migration 0013).
CREATE INDEX idx_products_job_id ON products (job_id);
