"use client";

import Link from "next/link";
import { useEffect, useState } from "react";

import type { Workload } from "@flowforge/shared";

import { Card } from "@/components/ui/card";
import { PageHeader } from "@/components/ui/page-header";
import { StatusBadge } from "@/components/ui/status-badge";
import { apiClient, ApiError } from "@/lib/api-client";

const PAGE_SIZE = 25;

export default function WorkloadsPage() {
  const [workloads, setWorkloads] = useState<Workload[]>([]);
  const [total, setTotal] = useState(0);
  const [offset, setOffset] = useState(0);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  // Bumped to re-run the fetch effect on "Retry" without changing offset.
  const [retryToken, setRetryToken] = useState(0);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    setError(null);
    apiClient
      .listWorkloads(PAGE_SIZE, offset)
      .then((result) => {
        if (cancelled) return;
        setWorkloads(result.workloads);
        setTotal(result.total);
      })
      .catch((err: unknown) => {
        if (cancelled) return;
        setError(err instanceof ApiError ? err.message : "Failed to load workloads");
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, [offset, retryToken]);

  const rangeStart = total === 0 ? 0 : offset + 1;
  const rangeEnd = Math.min(offset + PAGE_SIZE, total);

  return (
    <div>
      <PageHeader
        title="Workloads"
        description="Every workload created from the Processing Center or a user import: a group of jobs submitted together. Progress, retry, and dead-letter counts are computed from each workload's jobs on every read."
        action={
          <Link
            href="/processing"
            className="shrink-0 rounded-md border border-[var(--border)] px-3 py-1.5 text-sm font-medium hover:bg-white/5"
          >
            Open Processing Center
          </Link>
        }
      />

      {error && (
        <Card className="mb-4 border-red-500/30">
          <div className="flex items-center justify-between gap-4">
            <div className="text-sm text-red-400">{error}</div>
            <button
              type="button"
              onClick={() => setRetryToken((t) => t + 1)}
              className="shrink-0 rounded-md border border-[var(--border)] px-3 py-1.5 text-xs font-medium hover:bg-white/5"
            >
              Retry
            </button>
          </div>
        </Card>
      )}

      {loading && (
        <Card>
          <div className="flex items-center gap-3 text-sm text-[var(--muted)]">
            <span className="h-4 w-4 shrink-0 animate-spin rounded-full border-2 border-[var(--border)] border-t-[var(--accent)]" />
            Loading workloads…
          </div>
        </Card>
      )}

      {!loading && !error && workloads.length === 0 && (
        <Card>
          <div className="text-sm text-[var(--muted)]">
            No workloads yet.{" "}
            <Link href="/processing" className="text-[var(--accent)] hover:underline">
              Upload a CSV or image
            </Link>{" "}
            in the Processing Center to create one.
          </div>
        </Card>
      )}

      {!loading && !error && workloads.length > 0 && (
        <>
          <Card className="p-0">
            <div className="overflow-x-auto">
              <table className="w-full text-left text-sm">
                <thead>
                  <tr className="border-b border-[var(--border)] text-xs uppercase tracking-wide text-[var(--muted)]">
                    <th className="px-4 py-3 font-medium">ID</th>
                    <th className="px-4 py-3 font-medium">Type</th>
                    <th className="px-4 py-3 font-medium">Status</th>
                    <th className="px-4 py-3 font-medium">Progress</th>
                    <th className="px-4 py-3 font-medium">Retrying</th>
                    <th className="px-4 py-3 font-medium">Dead-letter</th>
                    <th className="px-4 py-3 font-medium">Created</th>
                  </tr>
                </thead>
                <tbody>
                  {workloads.map((workload) => {
                    const decided = workload.completed_items + workload.failed_items;
                    return (
                      <tr
                        key={workload.id}
                        className="border-b border-[var(--border)] last:border-0 hover:bg-white/5"
                      >
                        <td className="px-4 py-3">
                          <Link
                            href={`/workloads/${workload.id}`}
                            className="font-mono text-xs text-[var(--accent)]"
                            title={workload.id}
                          >
                            {workload.id.slice(0, 8)}…
                          </Link>
                        </td>
                        <td className="px-4 py-3">{workload.type}</td>
                        <td className="px-4 py-3">
                          <StatusBadge status={workload.status} />
                        </td>
                        <td className="px-4 py-3 text-[var(--muted)]">
                          {decided} / {workload.total_items}
                        </td>
                        <td className="px-4 py-3 text-[var(--muted)]">
                          {workload.retrying_items > 0 ? (
                            <span className="text-amber-300">{workload.retrying_items}</span>
                          ) : (
                            "—"
                          )}
                        </td>
                        <td className="px-4 py-3 text-[var(--muted)]">
                          {workload.dead_letter_items > 0 ? (
                            <span className="text-red-400">{workload.dead_letter_items}</span>
                          ) : (
                            "—"
                          )}
                        </td>
                        <td className="px-4 py-3 text-[var(--muted)]">
                          {new Date(workload.created_at).toLocaleString()}
                        </td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </div>
          </Card>

          <div className="mt-4 flex items-center justify-between text-sm text-[var(--muted)]">
            <span>
              Showing {rangeStart}-{rangeEnd} of {total}
            </span>
            <div className="flex gap-2">
              <button
                type="button"
                onClick={() => setOffset(Math.max(0, offset - PAGE_SIZE))}
                disabled={offset === 0}
                className="rounded-md border border-[var(--border)] px-3 py-1.5 text-sm font-medium hover:bg-white/5 disabled:cursor-not-allowed disabled:opacity-50"
              >
                Previous
              </button>
              <button
                type="button"
                onClick={() => setOffset(offset + PAGE_SIZE)}
                disabled={rangeEnd >= total}
                className="rounded-md border border-[var(--border)] px-3 py-1.5 text-sm font-medium hover:bg-white/5 disabled:cursor-not-allowed disabled:opacity-50"
              >
                Next
              </button>
            </div>
          </div>
        </>
      )}
    </div>
  );
}
