// JavaScript bindings (Emscripten embind) for the browser port.
//
// Exposes the call test bench and the receive/send-side components a web
// application would drive itself (WebTransport, WebSocket or data-channel
// media): the NetEq-lite audio jitter buffer, GCC bandwidth estimation,
// transport-wide feedback, NACK generation and freeze detection. Times are
// milliseconds as JS numbers (e.g. performance.now()).
#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <string>
#include <vector>

#include "audio_jitter_buffer_binding.h"
#include "call.h"
#include "scenario.h"
#include "sm/cc/send_side_bwe.h"
#include "sm/cc/transport_feedback.h"
#include "sm/core/metrics.h"
#include "sm/loss/nack_tracker.h"
#include "sm/timing/audio_jitter_buffer.h"
#include "sm/video/freeze_detector.h"

using emscripten::val;

namespace {

sm::Timestamp Ms(double ms) { return sm::Timestamp::Micros(static_cast<int64_t>(ms * 1000.0)); }

template <class T>
val ToTypedArray(const char* type, const std::vector<T>& v) {
  // Copy out of the Wasm heap: a view would be invalidated by memory growth.
  return val::global(type).new_(emscripten::typed_memory_view(v.size(), v.data())).template call<val>("slice");
}

std::string MetricsJson(const sm::MetricsSource& s) {
  sm::MapMetricsWriter w;
  s.ExportMetrics(w);
  return w.ToJson();
}

// --- Test bench ----------------------------------------------------------------

std::string RunScenario(const std::string& text, bool timeseries) {
  auto parsed = sm::testbench::ParseScenario(text);
  if (!parsed.scenario) return "{\"error\": \"" + parsed.error + "\"}";
  sm::testbench::RunOptions opt;
  opt.timeseries = timeseries;
  opt.library_metrics = true;
  return sm::testbench::ToJson(sm::testbench::RunCall(*parsed.scenario, opt), timeseries, true);
}

// Returns "" if valid, else the error; canonical text via FormatScenario.
std::string CheckScenario(const std::string& text) {
  auto parsed = sm::testbench::ParseScenario(text);
  return parsed.scenario ? "" : parsed.error;
}

std::string CanonicalScenario(const std::string& text) {
  auto parsed = sm::testbench::ParseScenario(text);
  return parsed.scenario ? sm::testbench::FormatScenario(*parsed.scenario) : "";
}

val MetricCatalog() {
  val out = val::array();
  for (const auto& [name, meaning] : sm::testbench::MetricCatalog()) {
    val e = val::object();
    e.set("name", name);
    e.set("meaning", meaning);
    out.call<void>("push", e);
  }
  return out;
}

// --- Audio jitter buffer -------------------------------------------------------

class AudioJitterBuffer {
 public:
  // NetEq at `sample_rate` when it supports it, else at 48 kHz with the
  // audio resampled at both ends (audio_jitter_buffer_binding.h, #579).
  explicit AudioJitterBuffer(int sample_rate) : jb_(sample_rate) {}
  // `samples`: Float32Array (or array) with one decoded frame.
  void insertPacket(uint32_t rtp_timestamp, val samples, double arrival_ms) {
    jb_.InsertPacket(rtp_timestamp, emscripten::convertJSArrayToNumberVector<float>(samples), Ms(arrival_ms));
  }
  // Returns `count` samples of playout (call every 10 ms with count = rate/100).
  val getAudio(int count, double now_ms) {
    out_.assign(static_cast<size_t>(count), 0.0f);
    last_op_ = sm::timing::ToString(jb_.GetAudio(out_, Ms(now_ms)));
    return ToTypedArray("Float32Array", out_);
  }
  std::string lastOperation() const { return last_op_; }
  int targetDelayMs() const { return jb_.jitter_buffer().TargetDelayMs(); }
  double bufferLevelMs() const { return jb_.jitter_buffer().CurrentLevelMs(); }
  std::string metrics() const { return MetricsJson(jb_.jitter_buffer()); }

 private:
  sm::web::AudioJitterBufferBinding jb_;
  std::vector<float> out_;
  std::string last_op_ = "silence";
};

// --- Send-side bandwidth estimation (GCC) ----------------------------------------

val UpdateToJs(const sm::cc::NetworkControlUpdate& u) {
  val o = val::object();
  o.set("targetKbps", u.target_rate.kbps());
  o.set("pacingKbps", u.pacing_rate.kbps());
  val probes = val::array();
  for (const auto& p : u.probes) probes.call<void>("push", p.rate.kbps());
  o.set("probeKbps", probes);
  return o;
}

class GccController {
 public:
  GccController(double start_kbps, double min_kbps, double max_kbps) : gcc_(Config(start_kbps, min_kbps, max_kbps)) {}
  val start(double now_ms) { return UpdateToJs(gcc_.Start(Ms(now_ms))); }
  void onPacketSent(int transport_seq, double send_ms, int bytes) {
    gcc_.OnPacketSent(static_cast<uint16_t>(transport_seq), Ms(send_ms), sm::DataSize::Bytes(bytes));
  }
  // `feedback`: Uint8Array with an RTCP transport-cc message.
  val onTransportFeedback(val feedback, double now_ms) {
    auto bytes = emscripten::convertJSArrayToNumberVector<uint8_t>(feedback);
    auto fb = sm::cc::ParseTransportFeedback(bytes);
    if (!fb) return val::null();
    return UpdateToJs(gcc_.OnTransportFeedback(*fb, Ms(now_ms)));
  }
  void onRttUpdate(double rtt_ms) { gcc_.OnRttUpdate(sm::TimeDelta::MillisF(rtt_ms)); }
  val onProcessInterval(double now_ms, double send_kbps) {
    return UpdateToJs(gcc_.OnProcessInterval(Ms(now_ms), sm::DataRate::BitsPerSec(static_cast<int64_t>(send_kbps * 1000))));
  }
  double targetKbps() const { return gcc_.target().kbps(); }
  std::string metrics() const { return MetricsJson(gcc_); }

 private:
  static sm::cc::GccConfig Config(double s, double lo, double hi) {
    sm::cc::GccConfig c;
    c.start_rate = sm::DataRate::BitsPerSec(static_cast<int64_t>(s * 1000));
    c.min_rate = sm::DataRate::BitsPerSec(static_cast<int64_t>(lo * 1000));
    c.max_rate = sm::DataRate::BitsPerSec(static_cast<int64_t>(hi * 1000));
    return c;
  }
  sm::cc::GccController gcc_;
};

// --- Receiver feedback: transport-cc and NACK -------------------------------------

class TransportFeedbackBuilder {
 public:
  void onPacket(int transport_seq, double arrival_ms) { b_.OnPacket(static_cast<uint16_t>(transport_seq), Ms(arrival_ms)); }
  // Array of Uint8Array RTCP messages to send to the sender.
  val build() {
    val out = val::array();
    for (const auto& fb : b_.Build()) out.call<void>("push", ToTypedArray("Uint8Array", sm::cc::SerializeTransportFeedback(fb)));
    return out;
  }

 private:
  sm::cc::TransportFeedbackBuilder b_;
};

class NackTracker {
 public:
  void onReceivedPacket(int seq, bool keyframe, double now_ms) { n_.OnReceivedPacket(static_cast<uint16_t>(seq), keyframe, Ms(now_ms)); }
  void updateRtt(double rtt_ms) { n_.UpdateRtt(sm::TimeDelta::MillisF(rtt_ms)); }
  val getNackBatch(double now_ms) {
    auto batch = n_.GetNackBatch(Ms(now_ms));
    std::vector<int> v(batch.begin(), batch.end());
    return val::array(v);
  }
  bool takeKeyframeRequest() { return n_.TakeKeyframeRequest(); }
  std::string metrics() const { return MetricsJson(n_); }

 private:
  sm::loss::NackTracker n_;
};

class FreezeDetector {
 public:
  void onFrameRendered(double now_ms) { f_.OnFrameRendered(Ms(now_ms)); }
  double harmonicFps() const { return f_.HarmonicFps(); }
  int freezeCount() const { return static_cast<int>(f_.stats().freeze_count); }
  double totalFreezesMs() const { return f_.stats().total_freezes_ms; }
  std::string metrics() const { return MetricsJson(f_); }

 private:
  sm::video::FreezeDetector f_;
};

}  // namespace

EMSCRIPTEN_BINDINGS(streaming_media) {
  using namespace emscripten;
  function("runScenario", &RunScenario);
  function("checkScenario", &CheckScenario);
  function("canonicalScenario", &CanonicalScenario);
  function("metricCatalog", &MetricCatalog);

  class_<AudioJitterBuffer>("AudioJitterBuffer")
      .constructor<int>()
      .function("insertPacket", &AudioJitterBuffer::insertPacket)
      .function("getAudio", &AudioJitterBuffer::getAudio)
      .function("lastOperation", &AudioJitterBuffer::lastOperation)
      .function("targetDelayMs", &AudioJitterBuffer::targetDelayMs)
      .function("bufferLevelMs", &AudioJitterBuffer::bufferLevelMs)
      .function("metrics", &AudioJitterBuffer::metrics);
  class_<GccController>("GccController")
      .constructor<double, double, double>()
      .function("start", &GccController::start)
      .function("onPacketSent", &GccController::onPacketSent)
      .function("onTransportFeedback", &GccController::onTransportFeedback)
      .function("onRttUpdate", &GccController::onRttUpdate)
      .function("onProcessInterval", &GccController::onProcessInterval)
      .function("targetKbps", &GccController::targetKbps)
      .function("metrics", &GccController::metrics);
  class_<TransportFeedbackBuilder>("TransportFeedbackBuilder")
      .constructor<>()
      .function("onPacket", &TransportFeedbackBuilder::onPacket)
      .function("build", &TransportFeedbackBuilder::build);
  class_<NackTracker>("NackTracker")
      .constructor<>()
      .function("onReceivedPacket", &NackTracker::onReceivedPacket)
      .function("updateRtt", &NackTracker::updateRtt)
      .function("getNackBatch", &NackTracker::getNackBatch)
      .function("takeKeyframeRequest", &NackTracker::takeKeyframeRequest)
      .function("metrics", &NackTracker::metrics);
  class_<FreezeDetector>("FreezeDetector")
      .constructor<>()
      .function("onFrameRendered", &FreezeDetector::onFrameRendered)
      .function("harmonicFps", &FreezeDetector::harmonicFps)
      .function("freezeCount", &FreezeDetector::freezeCount)
      .function("totalFreezesMs", &FreezeDetector::totalFreezesMs)
      .function("metrics", &FreezeDetector::metrics);
}
