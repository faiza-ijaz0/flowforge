import { NotYetImplemented } from "@/components/ui/not-yet-implemented";
import { PageHeader } from "@/components/ui/page-header";

export default function LogsPage() {
  return (
    <div>
      <PageHeader
        title="Logs"
        description="The server writes structured logs to standard output."
      />
      <NotYetImplemented
        feature="Log aggregation"
        note="There is no log query API yet. Read the server's standard output directly, or run `docker compose logs server` when using Docker."
      />
    </div>
  );
}
