import type { ReactNode } from "react";

export function Card({ children, className = "" }: { children: ReactNode; className?: string }) {
  return (
    <div className={`rounded-lg border border-[var(--border)] bg-[var(--surface)] p-5 ${className}`}>
      {children}
    </div>
  );
}
