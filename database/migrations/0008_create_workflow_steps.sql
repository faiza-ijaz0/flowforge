-- Mirrors flowforge::domain::WorkflowStep. Each step wraps exactly one
-- job; `workflow_step_dependencies` is a separate edge table (rather than
-- an array column on this table) so the DAG's referential integrity is
-- enforced by foreign keys and can be queried/joined normally.
CREATE TABLE workflow_steps (
    id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    workflow_id  UUID NOT NULL REFERENCES workflows (id) ON DELETE CASCADE,
    job_id       UUID NOT NULL REFERENCES jobs (id) ON DELETE CASCADE,
    name         TEXT NOT NULL,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),

    UNIQUE (workflow_id, name)
);

CREATE INDEX idx_workflow_steps_workflow_id ON workflow_steps (workflow_id);

-- Edges of the workflow DAG: `step_id` depends on `depends_on_step_id`
-- having succeeded first. Cycle detection is application-level (Phase 2
-- scheduler validation), not enforceable via a plain foreign-key/check
-- constraint.
CREATE TABLE workflow_step_dependencies (
    step_id            UUID NOT NULL REFERENCES workflow_steps (id) ON DELETE CASCADE,
    depends_on_step_id UUID NOT NULL REFERENCES workflow_steps (id) ON DELETE CASCADE,

    PRIMARY KEY (step_id, depends_on_step_id),
    CHECK (step_id <> depends_on_step_id)
);
