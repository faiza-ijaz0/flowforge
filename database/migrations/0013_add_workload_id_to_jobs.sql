-- Job <-> Workload relationship (Phase 3A -- see
-- docs/architecture/workload-model.md, "Job <-> Workload relationship").
-- Nullable and purely additive: every job created before this migration,
-- and every job created outside a workload afterward, has
-- workload_id = NULL and continues to work exactly as before --
-- domain::Job::workload_id() defaults to std::nullopt, and nothing in
-- JobService/the job HTTP API requires this column.
--
-- ON DELETE SET NULL (not CASCADE): a Job is the source of truth for its
-- own execution state/history (see workload-model.md, "Job remains the
-- source of truth"); deleting a Workload (grouping metadata) must not
-- destroy the Job rows or job_attempts history it grouped. Nothing in
-- this phase deletes a Workload (no DELETE endpoint exists yet), so this
-- is a deliberately conservative, forward-looking choice over CASCADE,
-- not a behavior exercised by this phase's own code paths.
ALTER TABLE jobs ADD COLUMN workload_id UUID REFERENCES workloads (id) ON DELETE SET NULL;

-- Supports services::WorkloadService's progress-aggregation query
-- (IJobRepository::list_by_workload_id) -- "every job belonging to
-- workload X" -- without a sequential scan of jobs.
CREATE INDEX idx_jobs_workload_id ON jobs (workload_id);
