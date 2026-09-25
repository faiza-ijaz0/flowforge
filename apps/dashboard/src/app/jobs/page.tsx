"use client";

import Link from "next/link";
import { useEffect, useState } from "react";

import type { Job } from "@flowforge/shared";

import { CreateJobForm } from "@/components/jobs/create-job-form";
import { Card } from "@/components/ui/card";
import { PageHeader } from "@/components/ui/page-header";
import { StatusBadge } from "@/components/ui/status-badge";
import { apiClient, ApiError } from "@/lib/api-client";

const PAGE_SIZE = 25;

export default function JobsPage() {
  const [jobs, setJobs] = useState<Job[]>([]);
  const [total, setTotal] = useState(0);
  const [offset, setOffset] = useState(0);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [refreshToken, setRefreshToken] = useState(0);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    setError(null);
    apiClient
      .listJobs(PAGE_SIZE, offset)
      .then((result) => {
        if (cancelled) return;
        setJobs(result.jobs);
        setTotal(result.total);
      })
      .catch((err: unknown) => {
        if (cancelled) return;
        setError(err instanceof ApiError ? err.message : "Failed to load jobs");
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, [offset, refreshToken]);

  const rangeStart = total === 0 ? 0 : offset + 1;
  const rangeEnd = Math.min(offset + PAGE_SIZE, total);

  return (
    <div>
      <PageHeader
        title="Jobs"
        description="Every job known to the server. Jobs with a job type are scheduled and executed by the worker pool; open a job to see its execution attempts."
      />

      <div className="mb-6">
        <Card>
          <div className="mb-3 text-sm font-medium">Create a job</div>
          <CreateJobForm onCreated={() => setRefreshToken((t) => t + 1)} />
        </Card>
      </div>

      {error && (
        <Card className="mb-4 border-red-500/30">
          <div className="flex items-center justify-between gap-4">
            <div className="text-sm text-red-400">{error}</div>
            <button
              type="button"
              onClick={() => setRefreshToken((t) => t + 1)}
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
            Loading jobs…
          </div>
        </Card>
      )}

      {!loading && !error && jobs.length === 0 && (
        <Card>
          <div className="text-sm text-[var(--muted)]">No jobs yet. Create one above.</div>
        </Card>
      )}

      {!loading && !error && jobs.length > 0 && (
        <>
          <Card className="p-0">
            <div className="overflow-x-auto">
              <table className="w-full text-left text-sm">
                <thead>
                  <tr className="border-b border-[var(--border)] text-xs uppercase tracking-wide text-[var(--muted)]">
                    <th className="px-4 py-3 font-medium">ID</th>
                    <th className="px-4 py-3 font-medium">Queue</th>
                    <th className="px-4 py-3 font-medium">Type</th>
                    <th className="px-4 py-3 font-medium">Priority</th>
                    <th className="px-4 py-3 font-medium">Status</th>
                    <th className="px-4 py-3 font-medium">Attempts</th>
                    <th className="px-4 py-3 font-medium">Workload</th>
                    <th className="px-4 py-3 font-medium">Created</th>
                  </tr>
                </thead>
                <tbody>
                  {jobs.map((job) => (
                    <tr key={job.id} className="border-b border-[var(--border)] last:border-0 hover:bg-white/5">
                      <td className="px-4 py-3">
                        <Link href={`/jobs/${job.id}`} className="font-mono text-xs text-[var(--accent)]">
                          {job.id.slice(0, 8)}…
                        </Link>
                      </td>
                      <td className="px-4 py-3">{job.queue_name}</td>
                      <td className="px-4 py-3 text-[var(--muted)]">{job.job_type || "—"}</td>
                      <td className="px-4 py-3 text-[var(--muted)]">{job.priority}</td>
                      <td className="px-4 py-3">
                        <StatusBadge status={job.status} />
                      </td>
                      <td className="px-4 py-3 text-[var(--muted)]">
                        {job.attempt_count} / {job.max_attempts}
                      </td>
                      <td className="px-4 py-3">
                        {job.workload_id ? (
                          <Link
                            href={`/workloads/${job.workload_id}`}
                            className="font-mono text-xs text-[var(--accent)]"
                            title={job.workload_id}
                          >
                            {job.workload_id.slice(0, 8)}…
                          </Link>
                        ) : (
                          <span className="text-[var(--muted)]">—</span>
                        )}
                      </td>
                      <td className="px-4 py-3 text-[var(--muted)]">
                        {new Date(job.created_at).toLocaleString()}
                      </td>
                    </tr>
                  ))}
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
