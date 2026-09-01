-- Phase 2A (PostgreSQL persistence): flowforge::domain::Workflow::steps()
-- is an ordered std::vector<WorkflowStep>, and the API/UI must be able to
-- reconstruct that exact order after a round trip through PostgreSQL.
-- workflow_steps had no column recording insertion order, and ordering by
-- created_at is not reliable here: all steps of one workflow are inserted
-- inside a single transaction (see PostgresWorkflowRepository::insert),
-- and now() is transaction-stable in PostgreSQL, so every step in the same
-- INSERT batch gets an identical created_at timestamp.
--
-- This is additive and backward compatible: existing rows default to 0,
-- and nothing reads workflow_steps.position until the repository code
-- introduced alongside this migration starts writing it.
ALTER TABLE workflow_steps ADD COLUMN position INTEGER NOT NULL DEFAULT 0;

CREATE INDEX idx_workflow_steps_workflow_id_position ON workflow_steps (workflow_id, position);
