import Link from "next/link";

import { Card } from "@/components/ui/card";
import { PageHeader } from "@/components/ui/page-header";
import { apiClient } from "@/lib/api-client";
import { apiBaseUrl } from "@/lib/config";

const CHECK_LABELS: Record<string, string> = {
  database: "Database",
  scheduler: "Scheduler",
  worker_pool: "Worker pool",
  retry_dispatcher: "Retry dispatcher",
};

export default async function OverviewPage() {
  const readiness = await apiClient.readiness();

  // Best-effort, real counts -- every number here comes from a live API
  // response's `total` field (never computed/estimated client-side). A
  // failure to fetch either just omits that tile rather than showing a
  // fake zero -- see docs/architecture/phase-3g-audit.md §8 ("no fake
  // dashboard numbers").
  const [workloadsResult, jobsResult] = await Promise.allSettled([
    apiClient.listWorkloads(1, 0),
    apiClient.listJobs(1, 0),
  ]);

  return (
    <div>
      <PageHeader
        title="Overview"
        description="What FlowForge is doing right now -- live connectivity, system health, and real workload/job counts from the API. No invented numbers."
      />

      <div className="grid grid-cols-1 gap-4 sm:grid-cols-2 lg:grid-cols-4">
        <Card>
          <div className="text-xs uppercase tracking-wide text-[var(--muted)]">API status</div>
          <div className="mt-2 flex items-center gap-2">
            <span
              className={`h-2.5 w-2.5 rounded-full ${readiness.reachable ? "bg-emerald-400" : "bg-red-400"}`}
            />
            <span className="text-lg font-semibold">
              {readiness.reachable ? (readiness.body.status === "ok" ? "Healthy" : "Degraded") : "Unreachable"}
            </span>
          </div>
          <div className="mt-2 text-sm text-[var(--muted)]">{apiBaseUrl}</div>
        </Card>

        {readiness.reachable ? (
          <>
            <Card>
              <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Environment</div>
              <div className="mt-2 text-lg font-semibold capitalize">{readiness.body.environment}</div>
            </Card>
            <Card>
              <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Uptime</div>
              <div className="mt-2 text-lg font-semibold">{readiness.body.uptime_seconds}s</div>
            </Card>
          </>
        ) : (
          <Card className="sm:col-span-2 lg:col-span-2">
            <div className="text-sm text-[var(--muted)]">{readiness.message}</div>
            <div className="mt-2 text-sm text-[var(--muted)]">
              Start the server with{" "}
              <code className="text-[var(--foreground)]">cmake --build build --target flowforge_server</code>{" "}
              and run the resulting binary, or use{" "}
              <code className="text-[var(--foreground)]">docker compose up server</code>.
            </div>
          </Card>
        )}

        <Card>
          <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Workloads</div>
          {workloadsResult.status === "fulfilled" ? (
            <Link href="/workloads" className="mt-2 block text-lg font-semibold text-[var(--accent)] hover:underline">
              {workloadsResult.value.total}
            </Link>
          ) : (
            <div className="mt-2 text-sm text-[var(--muted)]">Unavailable</div>
          )}
        </Card>
      </div>

      {readiness.reachable && (
        <div className="mt-4">
          <Card>
            <div className="mb-3 text-xs uppercase tracking-wide text-[var(--muted)]">System health</div>
            <div className="grid grid-cols-2 gap-3 sm:grid-cols-4">
              {Object.entries(readiness.body.checks).map(([key, value]) => (
                <div key={key} className="rounded-md border border-[var(--border)] px-3 py-2">
                  <div className="flex items-center gap-2">
                    <span
                      className={`h-2 w-2 shrink-0 rounded-full ${value === "ok" ? "bg-emerald-400" : "bg-red-400"}`}
                    />
                    <span className="text-[11px] uppercase tracking-wide text-[var(--muted)]">
                      {CHECK_LABELS[key] ?? key}
                    </span>
                  </div>
                  <div className={`mt-1 text-sm font-medium ${value === "ok" ? "" : "text-red-400"}`}>
                    {value === "ok" ? "OK" : "Unavailable"}
                  </div>
                </div>
              ))}
            </div>
          </Card>
        </div>
      )}

      <div className="mt-4 grid grid-cols-1 gap-4 sm:grid-cols-2">
        <Card>
          <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Jobs</div>
          {jobsResult.status === "fulfilled" ? (
            <Link href="/jobs" className="mt-2 block text-lg font-semibold text-[var(--accent)] hover:underline">
              {jobsResult.value.total} total
            </Link>
          ) : (
            <div className="mt-2 text-sm text-[var(--muted)]">Unavailable</div>
          )}
          <div className="mt-1 text-sm text-[var(--muted)]">
            Open <Link href="/jobs" className="text-[var(--accent)] hover:underline">Jobs</Link> for status,
            retry, and dead-letter detail per job.
          </div>
        </Card>
        <Card>
          <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Get started</div>
          <div className="mt-2 text-sm text-[var(--muted)]">
            Upload a CSV or image in the{" "}
            <Link href="/processing" className="text-[var(--accent)] hover:underline">
              Processing Center
            </Link>{" "}
            to create a real workload, or browse existing{" "}
            <Link href="/workloads" className="text-[var(--accent)] hover:underline">
              Workloads
            </Link>
            .
          </div>
        </Card>
      </div>

      <div className="mt-8 text-sm text-[var(--muted)]">
        See{" "}
        <Link href="/health" className="text-[var(--accent)] underline underline-offset-2">
          System health
        </Link>{" "}
        for the full readiness breakdown, or{" "}
        <a href="/metrics" className="text-[var(--accent)] underline underline-offset-2">
          Metrics
        </a>{" "}
        for the raw metrics feed.
      </div>
    </div>
  );
}
