#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace flowforge::infra {

/// Observability boundary for FlowForge. This is intentionally a small,
/// vendor-neutral interface (counter/gauge/histogram) rather than a
/// concrete Prometheus/OpenTelemetry integration: pulling in a metrics
/// exporter is Phase 2+ work (see docs/architecture/overview.md), but
/// engine/API code should be written against this interface from day one
/// so instrumentation doesn't require an invasive refactor later. The
/// in-memory implementation below is real (thread-safe, accumulates
/// values) and is what currently backs the `/metrics` endpoint, just
/// rendered as plain text instead of the Prometheus exposition format.
class MetricsRegistry {
 public:
  MetricsRegistry() = default;
  virtual ~MetricsRegistry() = default;
  MetricsRegistry(const MetricsRegistry&) = delete;
  MetricsRegistry& operator=(const MetricsRegistry&) = delete;
  MetricsRegistry(MetricsRegistry&&) = delete;
  MetricsRegistry& operator=(MetricsRegistry&&) = delete;

  virtual void increment_counter(std::string_view name, std::int64_t delta = 1) = 0;
  virtual void set_gauge(std::string_view name, double value) = 0;
  virtual void observe_histogram(std::string_view name, double value) = 0;

  struct Snapshot {
    std::map<std::string, std::int64_t> counters;
    std::map<std::string, double> gauges;
    struct HistogramStats {
      std::uint64_t count = 0;
      double sum = 0.0;
      double min = 0.0;
      double max = 0.0;
    };
    std::map<std::string, HistogramStats> histograms;
  };
  [[nodiscard]] virtual Snapshot snapshot() const = 0;
};

class InMemoryMetricsRegistry final : public MetricsRegistry {
 public:
  void increment_counter(std::string_view name, std::int64_t delta = 1) override;
  void set_gauge(std::string_view name, double value) override;
  void observe_histogram(std::string_view name, double value) override;
  [[nodiscard]] Snapshot snapshot() const override;

 private:
  mutable std::mutex mutex_;
  std::map<std::string, std::int64_t> counters_;
  std::map<std::string, double> gauges_;
  std::map<std::string, Snapshot::HistogramStats> histograms_;
};

[[nodiscard]] std::shared_ptr<MetricsRegistry> make_in_memory_metrics_registry();

/// Renders a snapshot as simple `name value` text lines. Not the
/// Prometheus exposition format (no HELP/TYPE metadata, no label sets) --
/// see docs/architecture/overview.md for why a real Prometheus exporter is
/// deferred rather than half-implemented here.
[[nodiscard]] std::string render_metrics_text(const MetricsRegistry::Snapshot& snapshot);

}  // namespace flowforge::infra
