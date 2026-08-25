#include "flowforge/infra/metrics.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace flowforge::infra {
namespace {

TEST(InMemoryMetricsRegistryTest, CounterAccumulates) {
  InMemoryMetricsRegistry registry;
  registry.increment_counter("jobs_processed");
  registry.increment_counter("jobs_processed", 4);
  auto snap = registry.snapshot();
  EXPECT_EQ(snap.counters.at("jobs_processed"), 5);
}

TEST(InMemoryMetricsRegistryTest, GaugeIsOverwritten) {
  InMemoryMetricsRegistry registry;
  registry.set_gauge("queue_depth", 10.0);
  registry.set_gauge("queue_depth", 3.0);
  auto snap = registry.snapshot();
  EXPECT_DOUBLE_EQ(snap.gauges.at("queue_depth"), 3.0);
}

TEST(InMemoryMetricsRegistryTest, HistogramTracksCountSumMinMax) {
  InMemoryMetricsRegistry registry;
  registry.observe_histogram("latency_ms", 10.0);
  registry.observe_histogram("latency_ms", 30.0);
  registry.observe_histogram("latency_ms", 20.0);
  auto snap = registry.snapshot();
  const auto& stats = snap.histograms.at("latency_ms");
  EXPECT_EQ(stats.count, 3u);
  EXPECT_DOUBLE_EQ(stats.sum, 60.0);
  EXPECT_DOUBLE_EQ(stats.min, 10.0);
  EXPECT_DOUBLE_EQ(stats.max, 30.0);
}

TEST(InMemoryMetricsRegistryTest, ConcurrentIncrementsAreNotLost) {
  InMemoryMetricsRegistry registry;
  constexpr int kThreads = 8;
  constexpr int kIncrementsPerThread = 1000;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&registry] {
      for (int j = 0; j < kIncrementsPerThread; ++j) {
        registry.increment_counter("concurrent_counter");
      }
    });
  }
  for (auto& t : threads) t.join();

  auto snap = registry.snapshot();
  EXPECT_EQ(snap.counters.at("concurrent_counter"), kThreads * kIncrementsPerThread);
}

TEST(RenderMetricsTextTest, ProducesReadableLines) {
  InMemoryMetricsRegistry registry;
  registry.increment_counter("jobs_processed", 5);
  registry.set_gauge("queue_depth", 2.0);
  const std::string rendered = render_metrics_text(registry.snapshot());
  EXPECT_NE(rendered.find("jobs_processed_total 5"), std::string::npos);
  EXPECT_NE(rendered.find("queue_depth 2"), std::string::npos);
}

}  // namespace
}  // namespace flowforge::infra
