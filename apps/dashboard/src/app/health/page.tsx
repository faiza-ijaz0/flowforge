import { HealthPanel } from "@/components/health/health-panel";
import { PageHeader } from "@/components/ui/page-header";

export default function HealthPage() {
  return (
    <div>
      <PageHeader
        title="System health"
        description="Real, live readiness state from GET /ready -- database, scheduler, worker pool, and retry dispatcher. No secrets, environment variables, or credentials are shown here."
      />
      <HealthPanel />
    </div>
  );
}
