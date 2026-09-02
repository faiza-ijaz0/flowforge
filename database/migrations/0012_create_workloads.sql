-- Phase 3A: a first-class aggregate for grouping related jobs submitted
-- as one logical unit (e.g. one CSV import) -- see
-- docs/architecture/workload-model.md. Mirrors
-- flowforge::domain::Workload (engine/include/flowforge/domain/workload.hpp).
--
-- Deliberately minimal: there is no `status`, `completed_items`, or
-- `failed_items` column. Those are computed on demand by
-- services::WorkloadService from the jobs.workload_id rows that reference
-- this table (see migration 0013), so they can never drift out of sync
-- with the Job rows that are the actual source of truth for execution
-- outcome, and no write path is needed on the job-execution hot path to
-- keep a separate counter current. See docs/architecture/workload-model.md,
-- "Why there is no update()".
CREATE TABLE workloads (
    id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    type         TEXT NOT NULL,
    total_items  INTEGER NOT NULL DEFAULT 0 CHECK (total_items >= 0),
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at   TIMESTAMPTZ NOT NULL DEFAULT now()
);
