# services/scheduler (placeholder)

Reserved for the Phase 2 standalone scheduler service/deployable. In this phase, scheduling logic is
represented only as the `flowforge::engine::IScheduler` interface
(`engine/include/flowforge/engine/scheduler.hpp`) with no implementation — see
`docs/architecture/overview.md` §6 for why.

When implemented, this directory is expected to hold either a separate CMake executable target (if
the scheduler runs as its own process communicating with `apps/server` via the database) or simply
document that scheduling runs in-process inside `flowforge_server` — that decision hasn't been made
yet and shouldn't be until the `IScheduler` implementation itself is designed.
