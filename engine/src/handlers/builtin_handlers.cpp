#include "flowforge/handlers/builtin_handlers.hpp"

#include <memory>

#include "flowforge/handlers/delay_handler.hpp"
#include "flowforge/handlers/echo_handler.hpp"
#include "flowforge/handlers/transform_handler.hpp"
#include "flowforge/handlers/user_process_handler.hpp"

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
  if (auto result = registry.register_handler(std::make_shared<UserProcessHandler>()); !result) {
    return result;
  }
  return {};
}

}  // namespace flowforge::handlers
