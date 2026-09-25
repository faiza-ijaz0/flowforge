import { HealthPanel } from "@/components/health/health-panel";
import { PageHeader } from "@/components/ui/page-header";

export default function HealthPage() {
  return (
    <div>
      <PageHeader
        title="System health"
        description="Live readiness of the server's dependencies: database, scheduler, worker pool, and retry dispatcher. No secrets or configuration values are shown."
      />
      <HealthPanel />
    </div>
  );
}
