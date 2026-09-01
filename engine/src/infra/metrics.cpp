#include "flowforge/infra/metrics.hpp"

#include <algorithm>
#include <sstream>

namespace flowforge::infra {

void InMemoryMetricsRegistry::increment_counter(std::string_view name, std::int64_t delta) {
  std::lock_guard lock(mutex_);
  counters_[std::string(name)] += delta;
}

void InMemoryMetricsRegistry::set_gauge(std::string_view name, double value) {
  std::lock_guard lock(mutex_);
  gauges_[std::string(name)] = value;
}

void InMemoryMetricsRegistry::observe_histogram(std::string_view name, double value) {
  std::lock_guard lock(mutex_);
  auto& stats = histograms_[std::string(name)];
  if (stats.count == 0) {
    stats.min = value;
    stats.max = value;
  } else {
    stats.min = std::min(stats.min, value);
    stats.max = std::max(stats.max, value);
  }
  stats.sum += value;
  stats.count += 1;
}

MetricsRegistry::Snapshot InMemoryMetricsRegistry::snapshot() const {
  std::lock_guard lock(mutex_);
  Snapshot snap;
  snap.counters = counters_;
  snap.gauges = gauges_;
  snap.histograms = histograms_;
  return snap;
}

std::shared_ptr<MetricsRegistry> make_in_memory_metrics_registry() {
  return std::make_shared<InMemoryMetricsRegistry>();
}

std::string render_metrics_text(const MetricsRegistry::Snapshot& snapshot) {
  std::ostringstream oss;
  // Phase 2B-5: the name a caller passes to increment_counter() is
  // rendered verbatim -- no implicit "_total" suffix. Every counter in
  // the codebase already spells out "_total" itself where that suffix is
  // wanted (the dominant, established convention -- see
  // docs/architecture/execution-model.md §20.1); auto-appending it here
  // too silently doubled it to "..._total_total" for every one of them.
  for (const auto& [name, value] : snapshot.counters) {
    oss << name << ' ' << value << '\n';
  }
  for (const auto& [name, value] : snapshot.gauges) {
    oss << name << ' ' << value << '\n';
  }
  for (const auto& [name, stats] : snapshot.histograms) {
    oss << name << "_count " << stats.count << '\n';
    oss << name << "_sum " << stats.sum << '\n';
    if (stats.count > 0) {
      oss << name << "_min " << stats.min << '\n';
      oss << name << "_max " << stats.max << '\n';
    }
  }
  return oss.str();
}

}  // namespace flowforge::infra
