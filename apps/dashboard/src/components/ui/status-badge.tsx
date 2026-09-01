const STATUS_STYLES: Record<string, string> = {
  pending: "bg-slate-500/15 text-slate-300",
  queued: "bg-sky-500/15 text-sky-300",
  running: "bg-amber-500/15 text-amber-300",
  succeeded: "bg-emerald-500/15 text-emerald-300",
  failed: "bg-red-500/15 text-red-300",
  retrying: "bg-amber-500/15 text-amber-300",
  cancelled: "bg-slate-500/15 text-slate-400",
  dead_letter: "bg-red-500/20 text-red-400",
  timed_out: "bg-red-500/15 text-red-300",
  idle: "bg-slate-500/15 text-slate-300",
  busy: "bg-amber-500/15 text-amber-300",
  offline: "bg-slate-500/15 text-slate-500",
};

export function StatusBadge({ status }: { status: string }) {
  const style = STATUS_STYLES[status] ?? "bg-slate-500/15 text-slate-300";
  return (
    <span className={`inline-flex items-center rounded-full px-2 py-0.5 text-xs font-medium ${style}`}>
      {status.replace("_", " ")}
    </span>
  );
}
