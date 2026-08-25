import type { Metadata } from "next";

import { Sidebar } from "@/components/layout/sidebar";

import "./globals.css";

export const metadata: Metadata = {
  title: "FlowForge",
  description: "Job processing and workflow orchestration dashboard",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body className="antialiased">
        <div className="flex min-h-screen">
          <Sidebar />
          <main className="flex-1 overflow-y-auto px-8 py-6">{children}</main>
        </div>
      </body>
    </html>
  );
}
