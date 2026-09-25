# Demo guide

A 2–4 minute walkthrough of FlowForge, using the real fixtures in `engine/tests/fixtures/`. The
repository contains no screenshots; record the demo against a running stack.

## Before you start

1. PostgreSQL migrated, and the server running **with** `FLOWFORGE_DATABASE_URL` set (so results
   persist) and Tesseract installed (for the image step). See the README, "Getting started".
2. Dashboard running at <http://localhost:3000>.
3. Keep these files at hand:
   - `engine/tests/fixtures/products_bulk_100.csv` (100 rows, 5 deliberately invalid)
   - `engine/tests/fixtures/user_table_bulk_100.png` (an image of a 100-row user table)
4. Optional: open a terminal for the `/ready` and `/metrics` step.

If the database already holds these fixtures from an earlier run, the imports update the existing
rows in place (upsert by SKU or email) instead of adding new ones. That is expected behavior, but
totals on the list pages will not change. Use a fresh database for a clean recording.

## Sequence

| # | Time | Screen | What to do | What to point out |
|---|---|---|---|---|
| 1 | 0:00 | **Overview** (`/`) | Open the dashboard. | API connectivity, readiness of the database, scheduler, worker pool, and retry dispatcher, and workload/job counts from the API. |
| 2 | 0:15 | **Processing Center** (`/processing`) | Select **Products**, then **CSV**. | One screen serves every source × target combination. |
| 3 | 0:25 | Upload | Choose `products_bulk_100.csv`, click **Extract Data**. | Extraction and validation happen server-side. Nothing has been created yet. |
| 4 | 0:35 | **Preview** | Scroll the table and the *Invalid rows* list. | **95 valid / 5 invalid**, each rejection with a row number and a reason. |
| 5 | 0:55 | **Confirm** | Click **Process Valid Records**. | The server re-validates every record, then creates **1 workload** with **95 jobs**. |
| 6 | 1:05 | **Execution progress** | Watch the progress panel. | Live queued/running/succeeded/failed counts as the worker pool executes the jobs. Polling stops when the workload is terminal. |
| 7 | 1:20 | **Workload** (**View Workload**) | Open the workload detail page. | Per-item status for each job; progress is computed from job state on every read. |
| 8 | 1:35 | **Job** | Open any job from the workload. | Execution attempt history: worker, outcome, duration, and any error. |
| 9 | 1:50 | **Products** (`/products`) | Open the list and page through it. | Persisted rows, each linked to the job that wrote it. |
| 10 | 2:05 | **Image / OCR** | Back in the Processing Center, select **Users** → **Image**, upload `user_table_bulk_100.png`, click **Extract Data**. | Real Tesseract OCR: about **97 valid / 3 invalid** for this fixture, with an OCR confidence figure. Point out a misread value that still passed validation (for example `personl@example.com`), which is why the preview exists. |
| 11 | 2:40 | Confirm and **Users** (`/users`) | Click **Process Valid Records**, wait for completion, and open `/users`. | The same workload/job pipeline for a different domain and source. |
| 12 | 3:05 | **System health** (`/health`) | Open the page. | Live `/ready` checks. Optionally show `curl localhost:8080/metrics` in a terminal: job, scheduler, worker, retry, and database counters. |
| 13 | 3:30 | Wrap-up | — | Summarize: CSV/OCR → preview → workload → concurrent C++ execution → PostgreSQL, with retries, dead letters, and observability. |

## Notes for presenters

- The 95/5 split is fixed by the CSV fixture. The 97/3 split for the image depends on the fixture
  and the Tesseract version; do not present it as a general OCR accuracy figure.
- To show failure handling, import a category whose parent does not exist: the workload ends as
  `failed`, and the job's error names the missing parent.
- Queues, Logs, and Settings are intentionally marked "Not yet implemented"; skip them in a short
  demo.
- There is no authentication. Run the demo on a trusted machine or network.
