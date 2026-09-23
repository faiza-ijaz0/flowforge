"use client";

import { useEffect, useState } from "react";

import type { ReadinessResult } from "@/lib/api-client";
import { apiClient } from "@/lib/api-client";
import { Card } from "@/components/ui/card";

/**
 * Polls GET /ready on an interval and renders its real per-component
 * breakdown -- mirrors WorkloadProgressPanel's polling shape (start on
 * mount, clean up the timer on unmount) but deliberately never stops: system
 * health has no terminal state the way a workload does, so this keeps
 * polling for as long as the page is open. 10s (vs. WorkloadProgressPanel's
 * 2s) because every check here is process-wide operational state, not a
 * specific in-flight operation a user is watching complete.
 */
const POLL_INTERVAL_MS = 10_000;

const CHECK_LABELS: Record<string, string> = {
  database: "PostgreSQL database",
  scheduler: "Scheduler",
  worker_pool: "Worker pool",
  retry_dispatcher: "Retry dispatcher",
};

const CHECK_DESCRIPTIONS: Record<string, string> = {
  database: "Connection pool can reach PostgreSQL.",
  scheduler: "Accepting and dispatching new jobs.",
  worker_pool: "Workers are running and able to execute jobs.",
  retry_dispatcher: "Polling for retry-eligible jobs and re-submitting them.",
};

export function HealthPanel() {
  const [result, setResult] = useState<ReadinessResult | null>(null);
  const [lastCheckedAt, setLastCheckedAt] = useState<Date | null>(null);

  useEffect(() => {
    let cancelled = false;
    let timer: ReturnType<typeof setTimeout> | null = null;

    async function poll() {
      const latest = await apiClient.readiness();
      if (cancelled) return;
      setResult(latest);
      setLastCheckedAt(new Date());
      timer = setTimeout(poll, POLL_INTERVAL_MS);
    }

    void poll();

    return () => {
      cancelled = true;
      if (timer) clearTimeout(timer);
    };
  }, []);

  if (!result) {
    return (
      <Card>
        <div className="flex items-center gap-3 text-sm text-[var(--muted)]">
          <span className="h-4 w-4 shrink-0 animate-spin rounded-full border-2 border-[var(--border)] border-t-[var(--accent)]" />
          Checking system health…
        </div>
      </Card>
    );
  }

  if (!result.reachable) {
    return (
      <Card className="border-red-500/30">
        <div className="flex items-center gap-2">
          <span className="h-2.5 w-2.5 shrink-0 rounded-full bg-red-400" />
          <span className="text-lg font-semibold">API unreachable</span>
        </div>
        <div className="mt-2 text-sm text-[var(--muted)]">{result.message}</div>
      </Card>
    );
  }

  const { status, environment, uptime_seconds, checks } = result.body;

  return (
    <div>
      <Card>
        <div className="flex flex-wrap items-center justify-between gap-3">
          <div className="flex items-center gap-2">
            <span className={`h-3 w-3 shrink-0 rounded-full ${status === "ok" ? "bg-emerald-400" : "bg-red-400"}`} />
            <span className="text-lg font-semibold">{status === "ok" ? "All systems operational" : "Degraded"}</span>
          </div>
          <div className="text-xs text-[var(--muted)]">
            {environment} &middot; uptime {uptime_seconds}s
            {lastCheckedAt && <> &middot; checked {lastCheckedAt.toLocaleTimeString()}</>}
          </div>
        </div>
      </Card>

      <div className="mt-4 grid grid-cols-1 gap-4 sm:grid-cols-2">
        {Object.entries(checks).map(([key, value]) => (
          <Card key={key}>
            <div className="flex items-start justify-between gap-3">
              <div>
                <div className="font-medium">{CHECK_LABELS[key] ?? key}</div>
                <div className="mt-1 text-sm text-[var(--muted)]">{CHECK_DESCRIPTIONS[key] ?? ""}</div>
              </div>
              <span
                className={`inline-flex shrink-0 items-center rounded-full px-2 py-0.5 text-xs font-medium ${
                  value === "ok" ? "bg-emerald-500/15 text-emerald-300" : "bg-red-500/15 text-red-300"
                }`}
              >
                {value === "ok" ? "OK" : "Unavailable"}
              </span>
            </div>
          </Card>
        ))}
      </div>

      <div className="mt-4 text-xs text-[var(--muted)]">
        Refreshes automatically every {POLL_INTERVAL_MS / 1000}s. Every check here is a cheap, non-blocking,
        in-memory read on the server -- never a live database query -- so this is safe to keep open.
      </div>
    </div>
  );
}
