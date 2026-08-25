#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "flowforge/infra/config.hpp"
#include "http/app.hpp"

namespace {

flowforge::server::App* g_app = nullptr;

void handle_shutdown_signal(int /*signal*/) {
  if (g_app != nullptr) {
    g_app->stop();
  }
}

}  // namespace

int main() {
  auto config_result = flowforge::infra::AppConfig::load_from_environment();
  if (!config_result) {
    // Configuration errors happen before logging is set up, so this is
    // one of the few places FlowForge writes directly to stderr.
    std::cerr << "FlowForge failed to start: " << config_result.error().message() << '\n';
    return EXIT_FAILURE;
  }

  flowforge::server::App app(*config_result);
  g_app = &app;
  std::signal(SIGINT, handle_shutdown_signal);
  std::signal(SIGTERM, handle_shutdown_signal);

  app.run();
  return EXIT_SUCCESS;
}
