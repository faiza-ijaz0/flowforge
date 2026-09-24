#include "flowforge/handlers/builtin_handlers.hpp"

#include <memory>

#include "flowforge/handlers/delay_handler.hpp"
#include "flowforge/handlers/echo_handler.hpp"
#include "flowforge/handlers/transform_handler.hpp"

namespace flowforge::handlers {

Result<void> register_builtin_handlers(engine::HandlerRegistry& registry) {
  if (auto result = registry.register_handler(std::make_shared<EchoHandler>()); !result) {
    return result;
  }
  if (auto result = registry.register_handler(std::make_shared<DelayHandler>()); !result) {
    return result;
  }
  if (auto result = registry.register_handler(std::make_shared<TransformHandler>()); !result) {
    return result;
  }
  // UserProcessHandler is NOT registered here (Phase 3H) -- like
  // ProductProcessHandler/CategoryProcessHandler, it now takes a
  // constructor-injected IUserRepository (it upserts into the `users`
  // table -- see its class comment), so it is registered separately by
  // the composition root (apps/server/src/http/app.cpp) instead of this
  // dependency-free/default-constructible group.
  return {};
}

}  // namespace flowforge::handlers
