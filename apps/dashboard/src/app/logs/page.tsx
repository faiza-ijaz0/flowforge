import { NotYetImplemented } from "@/components/ui/not-yet-implemented";
import { PageHeader } from "@/components/ui/page-header";

export default function LogsPage() {
  return (
    <div>
      <PageHeader
        title="Logs"
        description="FlowForge's structured logger (engine/include/flowforge/infra/logger.hpp) writes to stdout today."
      />
      <NotYetImplemented
        feature="Log aggregation"
        note="There is no log shipping/query endpoint yet -- read server logs directly from stdout (or `docker compose logs server`) until a log pipeline is built."
      />
    </div>
  );
}
