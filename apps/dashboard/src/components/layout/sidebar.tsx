"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";

const NAV_ITEMS = [
  { href: "/", label: "Overview" },
  { href: "/processing", label: "Processing Center" },
  { href: "/workloads", label: "Workloads" },
  { href: "/jobs", label: "Jobs" },
  { href: "/users", label: "Users" },
  { href: "/products", label: "Products" },
  { href: "/categories", label: "Categories" },
  { href: "/workflows", label: "Workflows" },
  { href: "/workers", label: "Workers" },
  { href: "/queues", label: "Queues" },
  { href: "/health", label: "System health" },
  { href: "/metrics", label: "Metrics" },
  { href: "/logs", label: "Logs" },
  { href: "/settings", label: "Settings" },
] as const;

export function Sidebar() {
  const pathname = usePathname();

  return (
    <aside className="flex h-screen w-56 shrink-0 flex-col border-r border-[var(--border)] bg-[var(--surface)] px-3 py-5">
      <div className="mb-6 flex items-center gap-2 px-2">
        <div className="h-6 w-6 rounded bg-[var(--accent)]" />
        <span className="text-sm font-semibold tracking-wide">FlowForge</span>
      </div>

      <nav className="flex flex-1 flex-col gap-0.5">
        {NAV_ITEMS.map((item) => {
          const isActive = item.href === "/" ? pathname === "/" : pathname.startsWith(item.href);
          return (
            <Link
              key={item.href}
              href={item.href}
              className={`rounded-md px-3 py-2 text-sm transition-colors ${
                isActive
                  ? "bg-[var(--accent)]/15 text-[var(--foreground)] font-medium"
                  : "text-[var(--muted)] hover:bg-white/5 hover:text-[var(--foreground)]"
              }`}
            >
              {item.label}
            </Link>
          );
        })}
      </nav>

      <div className="px-2 text-xs text-[var(--muted)]">v0.1.0</div>
    </aside>
  );
}
