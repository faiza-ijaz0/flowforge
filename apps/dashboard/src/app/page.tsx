import { Card } from "@/components/ui/card";
import { PageHeader } from "@/components/ui/page-header";
import { apiClient, ApiError } from "@/lib/api-client";
import { apiBaseUrl } from "@/lib/config";

async function loadSystemStatus() {
  try {
    const ready = await apiClient.ready();
    return { reachable: true as const, ready };
  } catch (error) {
    return {
      reachable: false as const,
      message: error instanceof ApiError ? error.message : "Unknown error contacting the API",
    };
  }
}

export default async function OverviewPage() {
  const status = await loadSystemStatus();

  return (
    <div>
      <PageHeader
        title="Overview"
        description="Live connectivity to the FlowForge API. Job/workflow/worker summary panels arrive once the scheduler (Phase 2) is implemented."
      />

      <div className="grid grid-cols-1 gap-4 sm:grid-cols-2 lg:grid-cols-3">
        <Card>
          <div className="text-xs uppercase tracking-wide text-[var(--muted)]">API status</div>
          <div className="mt-2 flex items-center gap-2">
            <span
              className={`h-2.5 w-2.5 rounded-full ${status.reachable ? "bg-emerald-400" : "bg-red-400"}`}
            />
            <span className="text-lg font-semibold">{status.reachable ? "Reachable" : "Unreachable"}</span>
          </div>
          <div className="mt-2 text-sm text-[var(--muted)]">{apiBaseUrl}</div>
        </Card>

        {status.reachable ? (
          <>
            <Card>
              <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Environment</div>
              <div className="mt-2 text-lg font-semibold capitalize">{status.ready.environment}</div>
            </Card>
            <Card>
              <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Uptime</div>
              <div className="mt-2 text-lg font-semibold">{status.ready.uptime_seconds}s</div>
            </Card>
          </>
        ) : (
          <Card className="sm:col-span-2 lg:col-span-2">
            <div className="text-sm text-[var(--muted)]">{status.message}</div>
            <div className="mt-2 text-sm text-[var(--muted)]">
              Start the server with <code className="text-[var(--foreground)]">cmake --build build --target flowforge_server</code>{" "}
              and run the resulting binary, or use{" "}
              <code className="text-[var(--foreground)]">docker compose up server</code>.
            </div>
          </Card>
        )}
      </div>

      <div className="mt-8 text-sm text-[var(--muted)]">
        Job/workflow/worker throughput panels are intentionally absent here rather than showing invented
        numbers -- see the Jobs page for real, live job data, and{" "}
        <a href="/metrics" className="text-[var(--accent)] underline underline-offset-2">
          Metrics
        </a>{" "}
        for the raw metrics feed.
      </div>
    </div>
  );
}
