"use client";

import { useEffect, useState } from "react";

import type { Workload } from "@flowforge/shared";

import { Card } from "@/components/ui/card";
import { StatusBadge } from "@/components/ui/status-badge";
import { apiClient, ApiError } from "@/lib/api-client";

/**
 * Real-time-feeling progress for one workload, backed entirely by polling
 * `GET /api/v1/workloads/{id}` -- there is no frontend-only progress state
 * here; every number rendered came from the last successful API response.
 * See docs/architecture/user-import.md, "Progress calculation" and
 * "Polling behavior" for why polling (not WebSockets/SSE) was chosen for
 * this phase, and what a future streaming upgrade would replace.
 */
const POLL_INTERVAL_MS = 2000;
const TERMINAL_STATUSES = new Set(["succeeded", "failed"]);

export function WorkloadProgressPanel({
  workloadId,
  initialWorkload,
}: {
  workloadId: string;
  initialWorkload?: Workload;
}) {
  const [workload, setWorkload] = useState<Workload | null>(initialWorkload ?? null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    let timer: ReturnType<typeof setTimeout> | null = null;

    async function poll() {
      try {
        const latest = await apiClient.getWorkload(workloadId);
        if (cancelled) return;
        setWorkload(latest);
        setError(null);
        // Stop polling once the workload reaches a terminal state -- no
        // more progress will ever be made, so there is nothing further to
        // observe (see docs/architecture/user-import.md, "Polling
        // behavior").
        if (!TERMINAL_STATUSES.has(latest.status)) {
          timer = setTimeout(poll, POLL_INTERVAL_MS);
        }
      } catch (err) {
        if (cancelled) return;
        // A transient network/API error pauses polling messaging but
        // keeps retrying -- it must not silently freeze the UI on the
        // last-known state forever without explanation.
        setError(err instanceof ApiError ? err.message : "Failed to fetch workload progress");
        timer = setTimeout(poll, POLL_INTERVAL_MS);
      }
    }

    // Always poll at least once, even with an initialWorkload, so the
    // panel starts reflecting genuinely live state immediately rather
    // than a snapshot that may already be stale by the time this mounts.
    void poll();

    return () => {
      cancelled = true;
      if (timer) clearTimeout(timer);
    };
    // workloadId is the only reactive dependency -- re-running this effect
    // for `initialWorkload` changes would restart polling pointlessly.
  }, [workloadId]);

  if (!workload) {
    return (
      <Card>
        <div className="text-sm text-[var(--muted)]">{error ?? "Loading workload progress…"}</div>
      </Card>
    );
  }

  const decided = workload.completed_items + workload.failed_items;
  const percent = workload.total_items === 0 ? 100 : Math.round((decided / workload.total_items) * 100);

  return (
    <Card>
      <div className="flex items-start justify-between gap-4">
        <div>
          <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Workload</div>
          <div className="mt-1 font-mono text-sm text-[var(--foreground)]">{workload.id}</div>
        </div>
        <StatusBadge status={workload.status} />
      </div>

      <div className="mt-4">
        <div className="mb-1 flex items-baseline justify-between text-sm">
          <span className="font-medium">
            {decided} / {workload.total_items} completed
          </span>
          <span className="text-[var(--muted)]">{percent}%</span>
        </div>
        <div className="h-2 w-full overflow-hidden rounded-full bg-white/5" role="progressbar"
             aria-valuenow={percent} aria-valuemin={0} aria-valuemax={100}>
          <div
            className="h-full rounded-full bg-[var(--accent)] transition-[width] duration-500"
            style={{ width: `${percent}%` }}
          />
        </div>
      </div>

      <div className="mt-4 grid grid-cols-2 gap-3 sm:grid-cols-4">
        <StatTile label="Queued" value={workload.queued_items} />
        <StatTile label="Running" value={workload.running_items} />
        <StatTile label="Succeeded" value={workload.completed_items} tone="text-emerald-300" />
        <StatTile label="Failed" value={workload.failed_items} tone="text-red-300" />
      </div>

      {/* Retrying/dead-letter are sub-counts of Queued/Failed above (see
          docs/architecture/phase-3g-audit.md §3.2) -- shown only when
          non-zero so a workload with no retries doesn't get two permanent
          zero-tiles cluttering the summary. */}
      {(workload.retrying_items > 0 || workload.dead_letter_items > 0) && (
        <div className="mt-3 grid grid-cols-2 gap-3">
          {workload.retrying_items > 0 && (
            <StatTile label="Retrying" value={workload.retrying_items} tone="text-amber-300" />
          )}
          {workload.dead_letter_items > 0 && (
            <StatTile label="Dead-letter" value={workload.dead_letter_items} tone="text-red-400" />
          )}
        </div>
      )}

      {error && <div className="mt-3 text-xs text-red-400">{error} -- retrying…</div>}
    </Card>
  );
}

function StatTile({ label, value, tone }: { label: string; value: number; tone?: string }) {
  return (
    <div className="rounded-md border border-[var(--border)] px-3 py-2">
      <div className="text-[11px] uppercase tracking-wide text-[var(--muted)]">{label}</div>
      <div className={`mt-0.5 text-lg font-semibold ${tone ?? ""}`}>{value}</div>
    </div>
  );
}
