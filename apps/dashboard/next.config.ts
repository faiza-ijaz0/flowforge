import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  // Standalone output produces a self-contained server bundle -- see
  // infra/docker/Dockerfile.dashboard, which copies exactly this output
  // into the runtime image instead of shipping node_modules wholesale.
  output: "standalone",
  reactStrictMode: true,
};

export default nextConfig;
