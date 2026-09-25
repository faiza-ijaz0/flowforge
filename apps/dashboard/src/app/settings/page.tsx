import { Card } from "@/components/ui/card";
import { NotYetImplemented } from "@/components/ui/not-yet-implemented";
import { PageHeader } from "@/components/ui/page-header";
import { apiBaseUrl } from "@/lib/config";

export default function SettingsPage() {
  return (
    <div>
      <PageHeader
        title="Settings"
        description="Server configuration comes from environment variables (see .env.example) and is read once at startup. It cannot be edited from the dashboard."
      />

      <Card className="mb-4">
        <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Dashboard configuration</div>
        <dl className="mt-2 text-sm">
          <div className="flex justify-between gap-4 py-1">
            <dt className="text-[var(--muted)]">NEXT_PUBLIC_API_URL</dt>
            <dd className="font-mono">{apiBaseUrl}</dd>
          </div>
        </dl>
      </Card>

      <NotYetImplemented
        feature="Editable settings"
        note="There is no settings API; runtime configuration is read once at server startup from environment variables."
      />
    </div>
  );
}
