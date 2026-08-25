-- Mirrors flowforge::domain::Worker (engine/include/flowforge/domain/worker.hpp).
-- Created before `jobs`/`job_attempts` so job_attempts.worker_id can
-- reference it directly.
CREATE TABLE workers (
    id               UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    hostname         TEXT NOT NULL,
    status           TEXT NOT NULL DEFAULT 'idle'
                         CHECK (status IN ('idle', 'busy', 'offline')),
    registered_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
    last_heartbeat   TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_workers_status ON workers (status);
