-- Logical job queues. Mirrors flowforge::domain::QueueConfig
-- (engine/include/flowforge/domain/queue.hpp). `name` is the natural key
-- jobs reference; there is deliberately no surrogate id since queue names
-- are operator-chosen, stable identifiers (e.g. "emails", "reports").
CREATE TABLE queues (
    name        TEXT PRIMARY KEY,
    capacity    INTEGER NOT NULL DEFAULT 1024 CHECK (capacity > 0),
    priority    INTEGER NOT NULL DEFAULT 0,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
