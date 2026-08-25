-- Mirrors flowforge::domain::Execution (engine/include/flowforge/domain/execution.hpp).
-- One row per attempt to run a job; a job with retry_policy.max_attempts
-- = 3 may accumulate up to three rows here.
CREATE TABLE job_attempts (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    job_id          UUID NOT NULL REFERENCES jobs (id) ON DELETE CASCADE,
    worker_id       UUID REFERENCES workers (id) ON DELETE SET NULL,
    attempt_number  INTEGER NOT NULL CHECK (attempt_number > 0),
    outcome         TEXT NOT NULL DEFAULT 'running'
                        CHECK (outcome IN ('running', 'succeeded', 'failed', 'timed_out', 'cancelled')),
    started_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    finished_at     TIMESTAMPTZ,
    error_message   TEXT,

    UNIQUE (job_id, attempt_number)
);

CREATE INDEX idx_job_attempts_job_id ON job_attempts (job_id);
CREATE INDEX idx_job_attempts_worker_id ON job_attempts (worker_id);
