-- Generic append-only audit trail across entity types (job, workflow,
-- worker, queue). Deliberately not foreign-keyed to any single entity
-- table: audit rows must remain even if the entity they describe is later
-- deleted, and a single table covering all entity types keeps "show me
-- everything that happened to X" a single query.
CREATE TABLE audit_logs (
    id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    entity_type  TEXT NOT NULL,
    entity_id    TEXT NOT NULL,
    action       TEXT NOT NULL,
    actor        TEXT,
    metadata     JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_audit_logs_entity ON audit_logs (entity_type, entity_id);
CREATE INDEX idx_audit_logs_created_at ON audit_logs (created_at);
