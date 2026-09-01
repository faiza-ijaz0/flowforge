#include <benchmark/benchmark.h>

#include <memory>
#include <string>

#include "flowforge/domain/execution_result.hpp"
#include "flowforge/engine/handler_registry.hpp"

namespace {

using flowforge::Result;
using flowforge::domain::ExecutionResult;
using flowforge::engine::ExecutionContext;
using flowforge::engine::HandlerRegistry;
using flowforge::engine::IJobHandler;

class NoopHandler final : public IJobHandler {
 public:
  explicit NoopHandler(std::string type) : type_(std::move(type)) {}
  [[nodiscard]] std::string_view job_type() const noexcept override { return type_; }
  [[nodiscard]] Result<ExecutionResult> execute(const ExecutionContext&, const std::string&) override {
    return ExecutionResult::success("", std::chrono::milliseconds{0});
  }

 private:
  std::string type_;
};

/// Read-mostly lookup throughput on `HandlerRegistry::resolve()` -- the
/// operation a future WorkerPool/Executor calls once per dispatched job.
/// `state.range(0)` is the number of *distinct* registered handler types,
/// to show whether lookup cost is sensitive to registry size (it
/// shouldn't be, for an unordered_map-backed registry -- this benchmark
/// is what would catch a regression to something O(n)).
void BM_HandlerRegistryResolve(benchmark::State& state) {
  HandlerRegistry registry;
  for (int i = 0; i < state.range(0); ++i) {
    std::ignore = registry.register_handler(std::make_shared<NoopHandler>("type-" + std::to_string(i)));
  }
  for (auto _ : state) {
    benchmark::DoNotOptimize(registry.resolve("type-0"));
  }
}
BENCHMARK(BM_HandlerRegistryResolve)->Arg(1)->Arg(8)->Arg(64);

}  // namespace
