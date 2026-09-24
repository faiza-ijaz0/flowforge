#!/usr/bin/env python3
"""Black-box smoke test for a running FlowForge stack.

Drives the real HTTP API end to end -- no mocks, no test doubles -- so it
can be pointed at any deployment: the docker compose stack in CI, or a
locally started flowforge_server. Standard library only.

Checks:
  1. GET /health is 200 and GET /ready is 200 with every dependency "ok".
  2. A malformed job id is rejected with 400 (never a 500).
  3. Products CSV (the deterministic 100-row fixture): preview reports
     100/95/5 and creates no workload; confirm creates exactly one
     workload of 95 jobs; every job succeeds; every confirmed SKU is
     readable back from GET /api/v1/products (i.e. persisted).
  4. With --ocr: the same flow from the real 100-row product image through
     the server's Tesseract OCR (proves the OCR binary is present and
     working in the deployed image).
  5. With --dashboard-url: the dashboard serves / and /products.

Usage:
  python3 tests/e2e/smoke-test.py --api-url http://localhost:8080 \
      [--dashboard-url http://localhost:3000] [--ocr]

Exits non-zero on the first failed check.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path

FIXTURES = Path(__file__).resolve().parents[2] / "engine" / "tests" / "fixtures"
TERMINAL_WORKLOAD_STATUSES = {"succeeded", "failed"}


class SmokeFailure(Exception):
    pass


def check(condition: bool, message: str) -> None:
    if not condition:
        raise SmokeFailure(message)


def request(method: str, url: str, body: bytes | None = None, content_type: str | None = None):
    req = urllib.request.Request(url, data=body, method=method)
    if content_type:
        req.add_header("Content-Type", content_type)
    try:
        with urllib.request.urlopen(req, timeout=120) as res:
            return res.status, res.read()
    except urllib.error.HTTPError as err:
        return err.code, err.read()


def get_json(url: str):
    status, body = request("GET", url)
    return status, json.loads(body) if body else None


def multipart(fields: dict[str, tuple[str | None, bytes]]) -> tuple[bytes, str]:
    boundary = uuid.uuid4().hex
    parts = []
    for name, (filename, content) in fields.items():
        disposition = f'form-data; name="{name}"'
        if filename:
            disposition += f'; filename="{filename}"'
        parts.append(f"--{boundary}\r\nContent-Disposition: {disposition}\r\n\r\n".encode() + content + b"\r\n")
    parts.append(f"--{boundary}--\r\n".encode())
    return b"".join(parts), f"multipart/form-data; boundary={boundary}"


def workload_total(api: str) -> int:
    status, body = get_json(f"{api}/api/v1/workloads?limit=1")
    check(status == 200, f"GET /workloads returned {status}")
    return body["total"]


def all_product_skus(api: str) -> set[str]:
    skus: set[str] = set()
    offset = 0
    while True:
        status, body = get_json(f"{api}/api/v1/products?limit=200&offset={offset}")
        check(status == 200, f"GET /products returned {status}")
        skus.update(p["sku"] for p in body["products"])
        offset += len(body["products"])
        if not body["products"] or offset >= body["total"]:
            return skus


def check_health(api: str) -> None:
    status, body = get_json(f"{api}/health")
    check(status == 200 and body.get("status") == "ok", f"/health: {status} {body}")
    status, body = get_json(f"{api}/ready")
    check(status == 200, f"/ready returned {status}: {body}")
    not_ok = {k: v for k, v in body.get("checks", {}).items() if v != "ok"}
    check(not not_ok and body.get("checks"), f"/ready has failing checks: {body}")
    print(f"ok      /health 200, /ready 200 ({', '.join(sorted(body['checks']))})")


def check_malformed_id(api: str) -> None:
    status, body = get_json(f"{api}/api/v1/jobs/not-a-uuid")
    check(status == 400, f"malformed job id returned {status}, expected 400: {body}")
    print("ok      malformed job id -> 400")


def run_products_flow(api: str, source: str, filename: str, min_valid: int, exact: bool) -> None:
    payload = (FIXTURES / filename).read_bytes()
    workloads_before = workload_total(api)

    body, ctype = multipart({"source": (None, source.encode()), "target": (None, b"products"),
                             "file": (filename, payload)})
    started = time.monotonic()
    status, raw = request("POST", f"{api}/api/v1/process/preview", body, ctype)
    preview_ms = (time.monotonic() - started) * 1000
    check(status == 200, f"{source} preview returned {status}: {raw[:300]!r}")
    preview = json.loads(raw)
    total, valid, invalid = preview["total_records"], preview["valid_records"], preview["invalid_records"]
    check(total == valid + invalid, f"{source} preview: total {total} != valid {valid} + invalid {invalid}")
    if exact:
        check((total, valid, invalid) == (100, 95, 5), f"{source} preview: expected 100/95/5, got {total}/{valid}/{invalid}")
    else:
        check(valid >= min_valid, f"{source} preview: only {valid} valid records (expected >= {min_valid})")
    check(workload_total(api) == workloads_before, f"{source} preview created a workload")

    confirm_body = json.dumps({"target": "products", "records": preview["records"]}).encode()
    started = time.monotonic()
    status, raw = request("POST", f"{api}/api/v1/process/confirm", confirm_body, "application/json")
    confirm_ms = (time.monotonic() - started) * 1000
    check(status == 201, f"{source} confirm returned {status}: {raw[:300]!r}")
    workload = json.loads(raw)
    job_ids = [item["job_id"] for item in workload["items"]]
    check(len(job_ids) == valid, f"{source} confirm: {len(job_ids)} jobs for {valid} valid records")
    check(len(set(job_ids)) == len(job_ids), f"{source} confirm: duplicate job ids")
    check(workload_total(api) == workloads_before + 1, f"{source} confirm did not create exactly one workload")

    started = time.monotonic()
    deadline = started + 180
    while True:
        status, workload = get_json(f"{api}/api/v1/workloads/{workload['id']}")
        check(status == 200, f"GET workload returned {status}")
        if workload["status"] in TERMINAL_WORKLOAD_STATUSES:
            break
        check(time.monotonic() < deadline, f"{source} workload not terminal after 180s: {workload}")
        time.sleep(0.25)
    execute_ms = (time.monotonic() - started) * 1000
    check(workload["status"] == "succeeded" and workload["completed_items"] == valid and workload["failed_items"] == 0,
          f"{source} workload did not fully succeed: {workload}")

    missing = {r["sku"] for r in preview["records"]} - all_product_skus(api)
    check(not missing, f"{source}: {len(missing)} confirmed SKUs not persisted, e.g. {sorted(missing)[:5]}")
    print(f"ok      products {source}: {total} submitted, {valid} valid, {invalid} invalid, {len(job_ids)} jobs, "
          f"{workload['completed_items']} succeeded, all {valid} SKUs persisted "
          f"(preview {preview_ms:.0f} ms, confirm {confirm_ms:.0f} ms, execution {execute_ms:.0f} ms)")


def check_dashboard(dashboard: str) -> None:
    for path in ("/", "/products"):
        status, body = request("GET", f"{dashboard}{path}")
        check(status == 200 and b"<html" in body.lower(), f"dashboard {path} returned {status}")
    print("ok      dashboard / and /products render (200)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--api-url", required=True)
    parser.add_argument("--dashboard-url")
    parser.add_argument("--ocr", action="store_true", help="also run the image/OCR products flow")
    args = parser.parse_args()
    api = args.api_url.rstrip("/")
    try:
        check_health(api)
        check_malformed_id(api)
        run_products_flow(api, "csv", "products_bulk_100.csv", min_valid=95, exact=True)
        if args.ocr:
            run_products_flow(api, "image", "products_bulk_100.png", min_valid=80, exact=False)
        if args.dashboard_url:
            check_dashboard(args.dashboard_url.rstrip("/"))
    except SmokeFailure as failure:
        print(f"FAIL: {failure}", file=sys.stderr)
        return 1
    print("done: smoke test passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
