-- Mirrors flowforge::domain::Job (engine/include/flowforge/domain/job.hpp).
-- `retry_policy` is stored as jsonb rather than three separate columns:
-- it is always read/written as a unit (see RetryPolicy::compute_backoff)
-- and this keeps the table stable if the policy grows fields (e.g. a
-- future jitter setting) without another migration.
CREATE TABLE jobs (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    queue_name      TEXT NOT NULL REFERENCES queues (name),
    payload         JSONB NOT NULL,
    priority        INTEGER NOT NULL DEFAULT 0,
    status          TEXT NOT NULL DEFAULT 'pending'
                        CHECK (status IN (
                            'pending', 'queued', 'running', 'succeeded',
                            'failed', 'retrying', 'cancelled', 'dead_letter'
                        )),
    attempt_count   INTEGER NOT NULL DEFAULT 0 CHECK (attempt_count >= 0),
    retry_policy    JSONB NOT NULL DEFAULT '{
                        "max_attempts": 3,
                        "initial_backoff_ms": 1000,
                        "max_backoff_ms": 60000,
                        "backoff_multiplier": 2.0
                    }'::jsonb,
    last_error      TEXT,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- The scheduler's core query is "next runnable job for queue X, highest
-- priority first, oldest first within a priority tier" -- this index
-- supports exactly that access pattern.
CREATE INDEX idx_jobs_queue_status_priority ON jobs (queue_name, status, priority DESC, created_at ASC);
CREATE INDEX idx_jobs_status ON jobs (status);
