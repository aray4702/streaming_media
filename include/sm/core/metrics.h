// Metrics: counters, gauges and fixed-memory histograms, plus exporters.
//
// Each algorithm keeps its own plain stats struct (cheap, always on, the
// equivalent of WebRTC getStats()) and implements MetricsSource so a
// MetricsRegistry can export everything as JSON or Prometheus text.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sm {

// Log-linear histogram over non-negative values with a fixed 660-byte
// footprint. Four sub-buckets per octave from 2^-10 to 2^30 gives about
// +-9% relative error on percentiles; exact min, max, mean and count.
class Histogram {
 public:
  static constexpr int kSubBuckets = 4;
  static constexpr int kMinExp = -10;
  static constexpr int kMaxExp = 30;
  static constexpr int kNumBuckets = 1 + (kMaxExp - kMinExp + 1) * kSubBuckets;

  void Add(double v);
  void Reset();
  void Merge(const Histogram& o);

  uint64_t count() const { return count_; }
  double sum() const { return sum_; }
  double min() const { return count_ ? min_ : 0.0; }
  double max() const { return count_ ? max_ : 0.0; }
  double mean() const { return count_ ? sum_ / static_cast<double>(count_) : 0.0; }
  // p in [0, 100].
  double Percentile(double p) const;

 private:
  static int BucketOf(double v);
  static double BucketMid(int idx);
  std::array<uint32_t, kNumBuckets> buckets_{};
  uint64_t count_ = 0;
  double sum_ = 0, min_ = 0, max_ = 0;
};

class MetricsWriter {
 public:
  virtual ~MetricsWriter() = default;
  virtual void Counter(std::string_view name, uint64_t value) = 0;
  virtual void Gauge(std::string_view name, double value) = 0;
  virtual void Hist(std::string_view name, const Histogram& h) = 0;
};

class MetricsSource {
 public:
  virtual ~MetricsSource() = default;
  virtual void ExportMetrics(MetricsWriter& w) const = 0;
};

// Flat name -> value map. Histograms expand to .count/.mean/.p50/.p95/.p99/.max.
// Used by tests, benchmarks and the JSON exporter.
class MapMetricsWriter : public MetricsWriter {
 public:
  void Counter(std::string_view name, uint64_t value) override;
  void Gauge(std::string_view name, double value) override;
  void Hist(std::string_view name, const Histogram& h) override;
  const std::map<std::string, double>& values() const { return values_; }
  double Get(const std::string& name, double fallback = -1) const;
  std::string ToJson() const;

 private:
  std::map<std::string, double> values_;
};

// Prometheus text exposition format (counters, gauges, summaries).
class PrometheusMetricsWriter : public MetricsWriter {
 public:
  void Counter(std::string_view name, uint64_t value) override;
  void Gauge(std::string_view name, double value) override;
  void Hist(std::string_view name, const Histogram& h) override;
  const std::string& text() const { return text_; }

 private:
  std::string Sanitize(std::string_view name) const;
  std::string text_;
};

// Holds non-owning pointers to sources under a name prefix
// (e.g. "video.nack"). Not thread-safe: export from the thread that owns the
// sources, or snapshot under the caller's lock.
class MetricsRegistry {
 public:
  void Register(std::string prefix, const MetricsSource* source);
  void Unregister(const MetricsSource* source);
  void Export(MetricsWriter& w) const;
  std::string ToJson() const;
  std::string ToPrometheus() const;
  size_t size() const { return sources_.size(); }

 private:
  std::vector<std::pair<std::string, const MetricsSource*>> sources_;
};

// Convenience: export a single source into a map (tests / benchmarks).
std::map<std::string, double> CollectMetrics(const MetricsSource& s);

}  // namespace sm
