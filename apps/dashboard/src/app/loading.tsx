export default function Loading() {
  return (
    <div className="flex h-full flex-col items-center justify-center gap-3 py-24 text-center">
      <span className="h-6 w-6 shrink-0 animate-spin rounded-full border-2 border-[var(--border)] border-t-[var(--accent)]" />
      <p className="text-sm text-[var(--muted)]">Loading…</p>
    </div>
  );
}
