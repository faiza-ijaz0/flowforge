"use client";

import Link from "next/link";
import { useEffect, useState } from "react";

import type { User } from "@flowforge/shared";

import { Card } from "@/components/ui/card";
import { PageHeader } from "@/components/ui/page-header";
import { UserImportWizard } from "@/components/users/user-import-wizard";
import { apiClient, ApiError } from "@/lib/api-client";

const PAGE_SIZE = 25;

export default function UsersPage() {
  const [users, setUsers] = useState<User[]>([]);
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
      .listUsers(PAGE_SIZE, offset)
      .then((result) => {
        if (cancelled) return;
        setUsers(result.users);
        setTotal(result.total);
      })
      .catch((err: unknown) => {
        if (cancelled) return;
        setError(err instanceof ApiError ? err.message : "Failed to load users");
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
        title="Users"
        description="Import and process users through FlowForge. Each upload creates one workload; every row becomes a real user.process job, dispatched through the existing PriorityScheduler/WorkerPool, and persisted into a dedicated users table -- the same pattern Products/Categories use. Every row here was written by handlers::UserProcessHandler when its user.process job executed."
        action={
          <Link
            href="/processing"
            className="shrink-0 rounded-md border border-[var(--border)] px-3 py-1.5 text-sm font-medium hover:bg-white/5"
          >
            Open Processing Center
          </Link>
        }
      />

      <div className="mb-6">
        <UserImportWizard />
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
            Loading users…
          </div>
        </Card>
      )}

      {!loading && !error && users.length === 0 && (
        <Card>
          <div className="text-sm text-[var(--muted)]">
            No users yet. Import a CSV or image above to get started.
          </div>
        </Card>
      )}

      {!loading && !error && users.length > 0 && (
        <>
          <Card className="p-0">
            <div className="overflow-x-auto">
              <table className="w-full text-left text-sm">
                <thead>
                  <tr className="border-b border-[var(--border)] text-xs uppercase tracking-wide text-[var(--muted)]">
                    <th className="px-4 py-3 font-medium">Name</th>
                    <th className="px-4 py-3 font-medium">Email</th>
                    <th className="px-4 py-3 font-medium">Phone</th>
                    <th className="px-4 py-3 font-medium">Job</th>
                    <th className="px-4 py-3 font-medium">Updated</th>
                  </tr>
                </thead>
                <tbody>
                  {users.map((user) => (
                    <tr key={user.id} className="border-b border-[var(--border)] last:border-0 hover:bg-white/5">
                      <td className="px-4 py-3">{user.name}</td>
                      <td className="px-4 py-3 text-[var(--muted)]">{user.email}</td>
                      <td className="px-4 py-3 text-[var(--muted)]">{user.phone ?? "—"}</td>
                      <td className="px-4 py-3">
                        {user.job_id ? (
                          <Link
                            href={`/jobs/${user.job_id}`}
                            className="font-mono text-xs text-[var(--accent)]"
                            title={user.job_id}
                          >
                            {user.job_id.slice(0, 8)}…
                          </Link>
                        ) : (
                          <span className="text-[var(--muted)]">—</span>
                        )}
                      </td>
                      <td className="px-4 py-3 text-[var(--muted)]">
                        {new Date(user.updated_at).toLocaleString()}
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
