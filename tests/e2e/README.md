# tests/e2e (placeholder)

Reserved for browser-driven end-to-end tests (e.g. Playwright) exercising `apps/dashboard` against a
running `flowforge_server`. Not implemented in this phase: the dashboard's real functionality so far
(create/list/cancel a job, list workflows/workers, view metrics) is small enough to have been verified
manually against the running dev server; a dedicated e2e harness is worth the setup cost once there's
enough interactive surface area (workflow creation, worker views with live state, etc.) that manual
verification stops scaling.
