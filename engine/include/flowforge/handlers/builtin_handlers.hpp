#pragma once

#include "flowforge/engine/handler_registry.hpp"

namespace flowforge::handlers {

/// Registers every built-in handler (echo, delay, transform) into
/// `registry`. Intended to be called once from an application
/// composition root -- deliberately not done via static/global
/// initialization (see IJobHandler's "no global mutable state"
/// constraint in the phase brief), so registration order and failure are
/// explicit, observable, and testable rather than happening silently
/// before `main()`.
[[nodiscard]] Result<void> register_builtin_handlers(engine::HandlerRegistry& registry);

}  // namespace flowforge::handlers
