/**
 * Explicit "this isn't real yet" placeholder. Used on pages/sections that
 * have no backing API endpoint in this phase, so the dashboard never
 * presents invented numbers or empty-but-plausible-looking data as if it
 * were live. See docs/architecture/overview.md ("Frontend: placeholder
 * vs. real data").
 */
export function NotYetImplemented({ feature, note }: { feature: string; note?: string }) {
  return (
    <div className="rounded-lg border border-dashed border-[var(--border)] bg-transparent p-8 text-center">
      <div className="mx-auto mb-3 inline-flex rounded-full bg-white/5 px-2.5 py-0.5 text-[11px] font-medium uppercase tracking-wide text-[var(--muted)]">
        Not yet implemented
      </div>
      <p className="text-sm text-[var(--foreground)]">{feature} has no backend yet.</p>
      {note && <p className="mt-1 text-sm text-[var(--muted)]">{note}</p>}
    </div>
  );
}
