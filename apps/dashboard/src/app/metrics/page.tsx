import { Card } from "@/components/ui/card";
import { PageHeader } from "@/components/ui/page-header";
import { apiBaseUrl } from "@/lib/config";

async function fetchRawMetrics(): Promise<{ ok: true; text: string } | { ok: false; message: string }> {
  try {
    const res = await fetch(`${apiBaseUrl}/metrics`, { cache: "no-store" });
    if (!res.ok) {
      return { ok: false, message: `/metrics returned HTTP ${res.status}` };
    }
    return { ok: true, text: await res.text() };
  } catch {
    return { ok: false, message: `Could not reach ${apiBaseUrl}/metrics. Is the server running?` };
  }
}

export default async function MetricsPage() {
  const result = await fetchRawMetrics();

  return (
    <div>
      <PageHeader
        title="Metrics"
        description="Raw output of GET /metrics: the server's in-memory counters, gauges, and histograms as plain text (not Prometheus exposition format)."
      />
      <Card>
        {result.ok ? (
          result.text.trim().length > 0 ? (
            <pre className="overflow-x-auto text-xs">{result.text}</pre>
          ) : (
            <div className="text-sm text-[var(--muted)]">
              No metrics recorded yet. Create a job on the Jobs page to see counters populate.
            </div>
          )
        ) : (
          <div className="text-sm text-red-400">{result.message}</div>
        )}
      </Card>
    </div>
  );
}
