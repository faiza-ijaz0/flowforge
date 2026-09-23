"use client";

import Link from "next/link";
import { useEffect, useState } from "react";

import type { WorkloadJobItem } from "@flowforge/shared";

import { Card } from "@/components/ui/card";
import { StatusBadge } from "@/components/ui/status-badge";
import { apiClient, ApiError } from "@/lib/api-client";

const PAGE_SIZE = 25;

/**
 * Bounded, paginated view of one workload's individual job results (see
 * docs/architecture/user-import.md, "Bounded item retrieval") --
 * GET /api/v1/workloads/{id}/items never returns more than PAGE_SIZE rows
 * per request regardless of how many users were imported.
 */
export function WorkloadItemsTable({ workloadId }: { workloadId: string }) {
  const [offset, setOffset] = useState(0);
  const [items, setItems] = useState<WorkloadJobItem[]>([]);
  const [total, setTotal] = useState(0);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    setError(null);
    apiClient
      .getWorkloadItems(workloadId, PAGE_SIZE, offset)
      .then((page) => {
        if (cancelled) return;
        setItems(page.items);
        setTotal(page.total);
      })
      .catch((err) => {
        if (cancelled) return;
        setError(err instanceof ApiError ? err.message : "Failed to load items");
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, [workloadId, offset]);

  const from = total === 0 ? 0 : offset + 1;
  const to = Math.min(offset + PAGE_SIZE, total);

  return (
    <Card className="p-0">
      <div className="flex items-center justify-between p-4 pb-0">
        <div className="text-sm font-medium">Items</div>
        <div className="text-xs text-[var(--muted)]">
          {total === 0 ? "No items" : `Showing ${from}-${to} of ${total}`}
        </div>
      </div>

      {error ? (
        <div className="p-4 text-sm text-red-400">{error}</div>
      ) : (
        <div className="overflow-x-auto">
          <table className="w-full text-left text-sm">
            <thead>
              <tr className="border-b border-[var(--border)] text-xs uppercase tracking-wide text-[var(--muted)]">
                <th className="px-4 py-3 font-medium">Job</th>
                <th className="px-4 py-3 font-medium">Name</th>
                <th className="px-4 py-3 font-medium">Email</th>
                <th className="px-4 py-3 font-medium">Status</th>
                <th className="px-4 py-3 font-medium">Attempts</th>
                <th className="px-4 py-3 font-medium">Result / error</th>
                <th className="px-4 py-3 font-medium">Updated</th>
              </tr>
            </thead>
            <tbody>
              {loading && (
                <tr>
                  <td colSpan={7} className="px-4 py-8 text-center text-[var(--muted)]">
                    Loading…
                  </td>
                </tr>
              )}
              {!loading && items.length === 0 && (
                <tr>
                  <td colSpan={7} className="px-4 py-8 text-center text-[var(--muted)]">
                    No items to show.
                  </td>
                </tr>
              )}
              {!loading &&
                items.map((item) => (
                  <tr key={item.job_id} className="border-b border-[var(--border)] last:border-0">
                    <td className="px-4 py-3">
                      <Link
                        href={`/jobs/${item.job_id}`}
                        className="font-mono text-xs text-[var(--accent)]"
                        title={item.job_id}
                      >
                        {item.job_id.slice(0, 8)}…
                      </Link>
                    </td>
                    <td className="px-4 py-3">{item.name ?? "—"}</td>
                    <td className="px-4 py-3 text-[var(--muted)]">{item.email ?? "—"}</td>
                    <td className="px-4 py-3">
                      <StatusBadge status={item.status} />
                    </td>
                    <td className="px-4 py-3 text-[var(--muted)]">{item.attempt_count}</td>
                    <td className="px-4 py-3 text-red-400">{item.last_error ?? "—"}</td>
                    <td className="px-4 py-3 text-[var(--muted)]">
                      {new Date(item.updated_at).toLocaleString()}
                    </td>
                  </tr>
                ))}
            </tbody>
          </table>
        </div>
      )}

      <div className="flex items-center justify-end gap-2 p-4">
        <button
          type="button"
          onClick={() => setOffset(Math.max(0, offset - PAGE_SIZE))}
          disabled={offset === 0 || loading}
          className="rounded-md border border-[var(--border)] px-3 py-1.5 text-xs font-medium disabled:cursor-not-allowed disabled:opacity-40"
        >
          Previous
        </button>
        <button
          type="button"
          onClick={() => setOffset(offset + PAGE_SIZE)}
          disabled={offset + PAGE_SIZE >= total || loading}
          className="rounded-md border border-[var(--border)] px-3 py-1.5 text-xs font-medium disabled:cursor-not-allowed disabled:opacity-40"
        >
          Next
        </button>
      </div>
    </Card>
  );
}
