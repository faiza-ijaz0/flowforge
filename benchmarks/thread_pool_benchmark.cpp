#include <benchmark/benchmark.h>

#include <atomic>
#include <vector>

#include "flowforge/engine/thread_pool.hpp"

namespace {

/// Throughput of submitting and completing no-op tasks -- a proxy for the
/// engine's raw task-dispatch overhead, independent of any future
/// job-specific scheduling cost.
void BM_ThreadPoolSubmitNoopTasks(benchmark::State& state) {
  flowforge::engine::ThreadPool pool(static_cast<std::size_t>(state.range(0)));
  constexpr int kTasks = 1000;
  for (auto _ : state) {
    std::vector<std::future<void>> futures;
    futures.reserve(kTasks);
    for (int i = 0; i < kTasks; ++i) {
      futures.push_back(pool.submit([] {}));
    }
    for (auto& f : futures) {
      f.wait();
    }
  }
  state.SetItemsProcessed(state.iterations() * kTasks);
}
BENCHMARK(BM_ThreadPoolSubmitNoopTasks)->Arg(1)->Arg(2)->Arg(4)->Arg(8);

}  // namespace
