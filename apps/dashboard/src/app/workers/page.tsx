import { Card } from "@/components/ui/card";
import { NotYetImplemented } from "@/components/ui/not-yet-implemented";
import { PageHeader } from "@/components/ui/page-header";
import { StatusBadge } from "@/components/ui/status-badge";
import { apiClient, ApiError } from "@/lib/api-client";

export default async function WorkersPage() {
  let workers: Awaited<ReturnType<typeof apiClient.listWorkers>> | null = null;
  let loadError: string | null = null;
  try {
    workers = await apiClient.listWorkers();
  } catch (error) {
    loadError = error instanceof ApiError ? error.message : "Failed to load workers";
  }

  return (
    <div>
      <PageHeader
        title="Workers"
        description="Worker records registered by the server's in-process worker pool. Records from earlier server runs are not removed yet, so older entries may appear as idle."
      />

      {loadError ? (
        <Card>
          <div className="text-sm text-red-400">{loadError}</div>
        </Card>
      ) : workers && workers.workers.length > 0 ? (
        <Card className="p-0">
          <table className="w-full text-left text-sm">
            <thead>
              <tr className="border-b border-[var(--border)] text-xs uppercase tracking-wide text-[var(--muted)]">
                <th className="px-4 py-3 font-medium">Hostname</th>
                <th className="px-4 py-3 font-medium">Status</th>
                <th className="px-4 py-3 font-medium">Last heartbeat</th>
              </tr>
            </thead>
            <tbody>
              {workers.workers.map((worker) => (
                <tr key={worker.id} className="border-b border-[var(--border)] last:border-0">
                  <td className="px-4 py-3">{worker.hostname}</td>
                  <td className="px-4 py-3">
                    <StatusBadge status={worker.status} />
                  </td>
                  <td className="px-4 py-3 text-[var(--muted)]">
                    {new Date(worker.last_heartbeat).toLocaleString()}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </Card>
      ) : (
        <NotYetImplemented
          feature="Worker process registration"
          note="No workers are registered. Workers appear once the server starts its worker pool; standalone worker processes are not supported yet."
        />
      )}
    </div>
  );
}
