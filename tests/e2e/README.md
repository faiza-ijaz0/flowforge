# tests/e2e

This directory intentionally contains no test binary of its own. FlowForge has two distinct kinds
of "end-to-end", both real and both exercised today:

## 1. Full-stack automated acceptance (CTest, runs in CI)

`apps/server/tests/process_routes_test.cpp`'s `ProcessRoutesBulkPostgresTest` suite is the closest
thing FlowForge has to a classic e2e test: it boots a real `flowforge_server` process (via
`App::create`) on an ephemeral port, drives it with a real HTTP client through the complete
`upload -> extract -> preview -> confirm -> workload -> jobs -> PriorityScheduler -> LocalWorkerPool
-> JobExecutor -> handler -> PostgreSQL` pipeline, and reconciles every count against what the
database actually persisted. Six such tests exist (opt-in via `FLOWFORGE_TEST_DATABASE_URL`,
`GTEST_SKIP()` otherwise — never silently reported as passing):

| Test | Domain | Source | Records |
|---|---|---|---|
| `HundredUserCsvFlowReconcilesAgainstRealPostgres` | Users | CSV | 100 (95 valid) |
| `HundredRecordImageFlowReconcilesAgainstRealPostgres` | Users | Image (real Tesseract OCR) | 100 |
| `HundredProductCsvFlowReconcilesAgainstRealPostgres` | Products | CSV | 100 (95 valid) |
| `HundredProductImageFlowReconcilesAgainstRealPostgres` | Products | Image (real Tesseract OCR) | 100 |
| `HundredCategoryCsvFlowReconcilesAgainstRealPostgres` | Categories | CSV | 100 (95 valid) |
| `HundredCategoryImageFlowReconcilesAgainstRealPostgres` | Categories | Image (real Tesseract OCR) | 100 |

Each fixture (`engine/tests/fixtures/*_bulk_100.{csv,png}`) is deterministic: exactly 5 rows are
invalid, at the same positions (17/34/51/68/85) across every domain, so "submitted = accepted +
rejected" and "accepted = jobs created = jobs succeeded" are asserted as exact equalities, not
approximations — see docs/architecture/phase-3h-production-readiness.md for the measured results.

Run them directly:

```bash
FLOWFORGE_TEST_DATABASE_URL=postgres://flowforge:flowforge@localhost:5432/flowforge_test \
  ./build/apps/server/tests/flowforge_server_tests --gtest_filter='*BulkPostgresTest*'
```

## 2. Real browser verification (manual, this phase)

Dashboard-driven verification against a running server + PostgreSQL, performed via Claude in Chrome
during Phase 3H: Users/Products/Categories CSV imports (including the same 100-row fixtures above)
through the actual Processing Center UI, workload/job/product/category list pages, and the
`/health` page. See docs/architecture/phase-3h-production-readiness.md, "E2E Status", for the exact
matrix of what was browser-verified versus automated-only versus not verified, and why a dedicated
Playwright/Cypress harness was not introduced (the phase brief's "prefer existing project tooling" —
GoogleTest/CTest already covers the deterministic, repeatable half of e2e; the browser is used for
what only a real browser can confirm: the actual dashboard UI rendering, click flows, and
navigation).
