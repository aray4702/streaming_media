#include "sm/core/metrics.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace sm {

int Histogram::BucketOf(double v) {
  if (!(v >= std::ldexp(1.0, kMinExp))) return 0;  // Also catches NaN.
  int exp = 0;
  double mant = std::frexp(v, &exp);  // v = mant * 2^exp, mant in [0.5, 1).
  int e = exp - 1;                    // v = (2*mant) * 2^e, 2*mant in [1, 2).
  if (e > kMaxExp) return kNumBuckets - 1;
  int sub = static_cast<int>((mant * 2.0 - 1.0) * kSubBuckets);
  sub = std::min(sub, kSubBuckets - 1);
  return 1 + (e - kMinExp) * kSubBuckets + sub;
}

double Histogram::BucketMid(int idx) {
  if (idx == 0) return 0.0;
  int e = (idx - 1) / kSubBuckets + kMinExp;
  int sub = (idx - 1) % kSubBuckets;
  double lo = std::ldexp(1.0 + static_cast<double>(sub) / kSubBuckets, e);
  double hi = std::ldexp(1.0 + static_cast<double>(sub + 1) / kSubBuckets, e);
  return 0.5 * (lo + hi);
}

void Histogram::Add(double v) {
#if SM_ENABLE_METRICS
  if (v < 0) v = 0;
  if (count_ == 0) {
    min_ = max_ = v;
  } else {
    min_ = std::min(min_, v);
    max_ = std::max(max_, v);
  }
  ++count_;
  sum_ += v;
  ++buckets_[BucketOf(v)];
#else
  (void)v;
#endif
}

void Histogram::Reset() { *this = Histogram(); }

void Histogram::Merge(const Histogram& o) {
  if (o.count_ == 0) return;
  if (count_ == 0) {
    min_ = o.min_;
    max_ = o.max_;
  } else {
    min_ = std::min(min_, o.min_);
    max_ = std::max(max_, o.max_);
  }
  count_ += o.count_;
  sum_ += o.sum_;
  for (int i = 0; i < kNumBuckets; ++i) buckets_[i] += o.buckets_[i];
}

double Histogram::Percentile(double p) const {
  if (count_ == 0) return 0.0;
  if (p <= 0) return min_;
  if (p >= 100) return max_;
  uint64_t rank = static_cast<uint64_t>(std::ceil(p / 100.0 * static_cast<double>(count_)));
  uint64_t seen = 0;
  for (int i = 0; i < kNumBuckets; ++i) {
    seen += buckets_[i];
    if (seen >= rank) return std::clamp(BucketMid(i), min_, max_);
  }
  return max_;
}

// --- MapMetricsWriter --------------------------------------------------------

void MapMetricsWriter::Counter(std::string_view name, uint64_t value) {
  values_[std::string(name)] = static_cast<double>(value);
}
void MapMetricsWriter::Gauge(std::string_view name, double value) {
  values_[std::string(name)] = value;
}
void MapMetricsWriter::Hist(std::string_view name, const Histogram& h) {
  std::string n(name);
  values_[n + ".count"] = static_cast<double>(h.count());
  values_[n + ".mean"] = h.mean();
  values_[n + ".p50"] = h.Percentile(50);
  values_[n + ".p95"] = h.Percentile(95);
  values_[n + ".p99"] = h.Percentile(99);
  values_[n + ".max"] = h.max();
}
double MapMetricsWriter::Get(const std::string& name, double fallback) const {
  auto it = values_.find(name);
  return it == values_.end() ? fallback : it->second;
}
std::string MapMetricsWriter::ToJson() const {
  std::string out = "{";
  bool first = true;
  char buf[64];
  for (const auto& [k, v] : values_) {
    if (!first) out += ",";
    first = false;
    if (std::isfinite(v)) {
      std::snprintf(buf, sizeof(buf), "%.6g", v);
    } else {
      std::snprintf(buf, sizeof(buf), "null");
    }
    out += "\"" + k + "\":" + buf;
  }
  return out + "}";
}

// --- PrometheusMetricsWriter -------------------------------------------------

std::string PrometheusMetricsWriter::Sanitize(std::string_view name) const {
  std::string s = "sm_";
  for (char c : name) s += (std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
  return s;
}
void PrometheusMetricsWriter::Counter(std::string_view name, uint64_t value) {
  std::string n = Sanitize(name);
  text_ += "# TYPE " + n + " counter\n" + n + " " + std::to_string(value) + "\n";
}
void PrometheusMetricsWriter::Gauge(std::string_view name, double value) {
  std::string n = Sanitize(name);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.6g", value);
  text_ += "# TYPE " + n + " gauge\n" + n + " " + buf + "\n";
}
void PrometheusMetricsWriter::Hist(std::string_view name, const Histogram& h) {
  std::string n = Sanitize(name);
  char buf[160];
  text_ += "# TYPE " + n + " summary\n";
  for (double q : {0.5, 0.95, 0.99}) {
    std::snprintf(buf, sizeof(buf), "%s{quantile=\"%g\"} %.6g\n", n.c_str(), q, h.Percentile(q * 100));
    text_ += buf;
  }
  std::snprintf(buf, sizeof(buf), "%s_sum %.6g\n%s_count %llu\n", n.c_str(), h.sum(), n.c_str(),
                static_cast<unsigned long long>(h.count()));
  text_ += buf;
}

// --- MetricsRegistry ----------------------------------------------------------

namespace {
class PrefixWriter : public MetricsWriter {
 public:
  PrefixWriter(const std::string& prefix, MetricsWriter& inner) : prefix_(prefix + "."), inner_(inner) {}
  void Counter(std::string_view n, uint64_t v) override { inner_.Counter(prefix_ + std::string(n), v); }
  void Gauge(std::string_view n, double v) override { inner_.Gauge(prefix_ + std::string(n), v); }
  void Hist(std::string_view n, const Histogram& h) override { inner_.Hist(prefix_ + std::string(n), h); }

 private:
  std::string prefix_;
  MetricsWriter& inner_;
};
}  // namespace

void MetricsRegistry::Register(std::string prefix, const MetricsSource* source) {
  sources_.emplace_back(std::move(prefix), source);
}
void MetricsRegistry::Unregister(const MetricsSource* source) {
  std::erase_if(sources_, [&](const auto& p) { return p.second == source; });
}
void MetricsRegistry::Export(MetricsWriter& w) const {
#if SM_ENABLE_METRICS
  for (const auto& [prefix, src] : sources_) {
    PrefixWriter pw(prefix, w);
    src->ExportMetrics(pw);
  }
#else
  (void)w;
#endif
}
std::string MetricsRegistry::ToJson() const {
  MapMetricsWriter w;
  Export(w);
  return w.ToJson();
}
std::string MetricsRegistry::ToPrometheus() const {
  PrometheusMetricsWriter w;
  Export(w);
  return w.text();
}

std::map<std::string, double> CollectMetrics(const MetricsSource& s) {
  MapMetricsWriter w;
  s.ExportMetrics(w);
  return w.values();
}

}  // namespace sm
