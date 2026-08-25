#include <benchmark/benchmark.h>

#include <thread>

#include "flowforge/engine/blocking_queue.hpp"

namespace {

/// Single-producer/single-consumer throughput of BlockingQueue, the
/// primitive future queue-management components will be built on.
void BM_BlockingQueueSpsc(benchmark::State& state) {
  constexpr int kItems = 10'000;
  for (auto _ : state) {
    flowforge::engine::BlockingQueue<int> queue;
    std::thread producer([&queue] {
      for (int i = 0; i < kItems; ++i) {
        queue.push(i);
      }
      queue.close();
    });
    int consumed = 0;
    while (queue.pop().has_value()) {
      ++consumed;
    }
    producer.join();
    benchmark::DoNotOptimize(consumed);
  }
  state.SetItemsProcessed(state.iterations() * kItems);
}
BENCHMARK(BM_BlockingQueueSpsc);

}  // namespace
