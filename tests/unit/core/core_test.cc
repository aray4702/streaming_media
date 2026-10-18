#include <cmath>
#include <cstdlib>
#include <numbers>

#include "sm/core/dsp.h"
#include "sm/core/event_log.h"
#include "sm/core/fft.h"
#include "sm/core/metrics.h"
#include "sm/core/random.h"
#include "sm/core/rtp_packet.h"
#include "sm/core/seq_num.h"
#include "sm/core/units.h"
#include "sm/sim/network_link.h"
#include "sm_test.h"

using namespace sm;

SM_TEST(Units, Arithmetic) {
  Timestamp t = Timestamp::Millis(100);
  SM_EXPECT_EQ((t + TimeDelta::Millis(5)).us(), 105000);
  SM_EXPECT_EQ((Timestamp::Millis(30) - Timestamp::Millis(10)).ms(), 20);
  DataRate r = DataRate::KilobitsPerSec(800);
  SM_EXPECT_EQ((r * TimeDelta::Millis(10)).bytes(), 1000);
  SM_EXPECT_EQ(DataRate::FromSizeAndTime(DataSize::Bytes(1000), TimeDelta::Millis(10)).bps(), 800000);
  SM_EXPECT_TRUE(TimeDelta::PlusInfinity() > TimeDelta::Seconds(1000000));
  SM_EXPECT_FALSE(TimeDelta::PlusInfinity().IsFinite());
}

SM_TEST(SeqNum, AheadOfWraps) {
  SM_EXPECT_TRUE(AheadOf<uint16_t>(1, 0));
  SM_EXPECT_TRUE(AheadOf<uint16_t>(0, 65535));
  SM_EXPECT_FALSE(AheadOf<uint16_t>(65535, 0));
  SM_EXPECT_FALSE(AheadOf<uint16_t>(5, 5));
}

SM_TEST(SeqNum, UnwrapForwardAndBackward) {
  SeqNumUnwrapper<uint16_t> u;
  SM_EXPECT_EQ(u.Unwrap(65534), 65534);
  SM_EXPECT_EQ(u.Unwrap(65535), 65535);
  SM_EXPECT_EQ(u.Unwrap(0), 65536);
  SM_EXPECT_EQ(u.Unwrap(2), 65538);
  SM_EXPECT_EQ(u.Unwrap(65535), 65535);  // Reordered across the wrap.
  SM_EXPECT_EQ(u.Unwrap(3), 65539);
  SeqNumUnwrapper<uint32_t> t;
  SM_EXPECT_EQ(t.Unwrap(0xFFFFFF00u), 0xFFFFFF00ll);
  SM_EXPECT_EQ(t.Unwrap(0x100u), 0x100000100ll);
}

SM_TEST(Histogram, PercentilesAreApproximatelyRight) {
  Histogram h;
  for (int i = 1; i <= 1000; ++i) h.Add(i);
  SM_EXPECT_EQ(h.count(), 1000u);
  SM_EXPECT_NEAR(h.mean(), 500.5, 1e-9);
  SM_EXPECT_NEAR(h.Percentile(50), 500, 50);
  SM_EXPECT_NEAR(h.Percentile(95), 950, 95);
  SM_EXPECT_EQ(h.max(), 1000.0);
  SM_EXPECT_EQ(h.min(), 1.0);
  Histogram z;
  z.Add(0);
  z.Add(0.0001);
  SM_EXPECT_EQ(z.Percentile(50), 0.0);
}

SM_TEST(Histogram, Merge) {
  Histogram a, b;
  a.Add(1);
  b.Add(100);
  a.Merge(b);
  SM_EXPECT_EQ(a.count(), 2u);
  SM_EXPECT_EQ(a.max(), 100.0);
}

namespace {
struct Src : MetricsSource {
  void ExportMetrics(MetricsWriter& w) const override {
    w.Counter("packets", 7);
    w.Gauge("rate", 1.5);
    Histogram h;
    h.Add(10);
    w.Hist("delay_ms", h);
  }
};
}  // namespace

SM_TEST(Metrics, RegistryJsonAndPrometheus) {
  Src s;
  MetricsRegistry reg;
  reg.Register("video.nack", &s);
  std::string json = reg.ToJson();
  SM_EXPECT_TRUE(json.find("\"video.nack.packets\":7") != std::string::npos);
  SM_EXPECT_TRUE(json.find("\"video.nack.delay_ms.p50\"") != std::string::npos);
  std::string prom = reg.ToPrometheus();
  SM_EXPECT_TRUE(prom.find("sm_video_nack_packets 7") != std::string::npos);
  SM_EXPECT_TRUE(prom.find("# TYPE sm_video_nack_delay_ms summary") != std::string::npos);
  reg.Unregister(&s);
  SM_EXPECT_EQ(reg.size(), 0u);
}

SM_TEST(EventLog, RingBufferAndCallback) {
  EventLog log(4);
  int called = 0;
  log.SetCallback([&](const Event&) { ++called; });
  for (int i = 0; i < 10; ++i) SM_EVENT(&log, Timestamp::Millis(i), "m", "e", i, 0);
  SM_EXPECT_EQ(log.size(), 4u);
  SM_EXPECT_EQ(log.total(), 10u);
  SM_EXPECT_EQ(called, 10);
  auto snap = log.Snapshot();
  SM_EXPECT_EQ(snap.front().value, 6.0);
  SM_EXPECT_EQ(snap.back().value, 9.0);
  SM_EXPECT_TRUE(log.ToJsonLines().find("\"module\":\"m\"") != std::string::npos);
}

SM_TEST(Fft, ComplexRoundTripAndTone) {
  Fft fft(64);
  std::vector<Complex> x(64);
  for (int i = 0; i < 64; ++i) x[i] = Complex(static_cast<float>(std::cos(2 * std::numbers::pi * 5 * i / 64)), 0);
  auto y = x;
  fft.Forward(y.data());
  SM_EXPECT_NEAR(std::abs(y[5]), 32, 1e-3);
  SM_EXPECT_NEAR(std::abs(y[6]), 0, 1e-3);
  fft.Inverse(y.data());
  for (int i = 0; i < 64; ++i) SM_EXPECT_NEAR(y[i].real(), x[i].real(), 1e-5);
}

SM_TEST(Fft, RealMatchesComplex) {
  const size_t n = 256;
  RealFft rf(n);
  Fft cf(n);
  Random rng(3);
  std::vector<float> x(n);
  for (auto& v : x) v = static_cast<float>(rng.Gaussian());
  std::vector<Complex> c(n), r(n / 2 + 1);
  for (size_t i = 0; i < n; ++i) c[i] = Complex(x[i], 0);
  cf.Forward(c.data());
  rf.Forward(x.data(), r.data());
  for (size_t k = 0; k <= n / 2; ++k) SM_EXPECT_NEAR(std::abs(r[k] - c[k]), 0, 1e-3);
  std::vector<float> back(n);
  rf.Inverse(r.data(), back.data());
  for (size_t i = 0; i < n; ++i) SM_EXPECT_NEAR(back[i], x[i], 1e-4);
}

SM_TEST(Dsp, PitchAndLpc) {
  // 200 Hz sawtooth-ish at 16 kHz: pitch lag 80.
  std::vector<float> x(1600);
  for (size_t i = 0; i < x.size(); ++i) {
    double t = static_cast<double>(i) / 16000;
    x[i] = static_cast<float>(std::sin(2 * std::numbers::pi * 200 * t) + 0.5 * std::sin(2 * std::numbers::pi * 400 * t));
  }
  auto p = dsp::EstimatePitch(x, 40, 400, 320);
  SM_EXPECT_EQ(p.lag, 80);
  SM_EXPECT_GT(p.correlation, 0.99f);
  double r[3], a[3], k[2];
  dsp::Autocorrelation(x, 2, r);
  double err = dsp::LevinsonDurbin(r, 2, a, k);
  SM_EXPECT_GE(err, 0.0);
  SM_EXPECT_LT(err, r[0]);
  SM_EXPECT_NEAR(dsp::LinearToDb(1.0f), 0, 1e-6);
  SM_EXPECT_NEAR(dsp::DbToLinear(-6.0206f), 0.5, 1e-4);
}

SM_TEST(Dsp, HighPassRemovesDc) {
  dsp::Biquad hp(dsp::BiquadCoeffs::HighPass(80, 16000));
  float y = 0;
  for (int i = 0; i < 16000; ++i) y = hp.Process(1.0f);
  SM_EXPECT_NEAR(y, 0, 1e-3);
}

SM_TEST(RtpPacket, SerializeParseRoundTrip) {
  RtpPacket p;
  p.payload_type = 96;
  p.marker = true;
  p.seq = 0xBEEF;
  p.timestamp = 0x12345678;
  p.ssrc = 0xCAFEBABE;
  p.extension_profile = 0xBEDE;
  p.extension = {0x10, 0xAA, 0, 0};
  p.payload = {1, 2, 3, 4, 5};
  auto bytes = p.Serialize();
  SM_EXPECT_EQ(bytes.size(), p.size());
  auto q = RtpPacket::Parse(bytes);
  SM_ASSERT_TRUE(q.has_value());
  SM_EXPECT_EQ(q->seq, p.seq);
  SM_EXPECT_EQ(q->timestamp, p.timestamp);
  SM_EXPECT_EQ(q->ssrc, p.ssrc);
  SM_EXPECT_TRUE(q->marker);
  SM_EXPECT_EQ(q->payload, p.payload);
  SM_EXPECT_EQ(q->extension, p.extension);
  SM_EXPECT_FALSE(RtpPacket::Parse(std::vector<uint8_t>{0x80, 0}).has_value());
}

SM_TEST(RtpPacket, PaddingOnlyAndPaddedPackets) {
  // Padding-only packet (empty payload), as used to pad or probe a link.
  RtpPacket pad;
  pad.payload_type = 100;
  pad.seq = 7;
  pad.ssrc = 5;
  pad.padding_size = 200;
  auto b = pad.Serialize();
  SM_EXPECT_EQ(b.size(), 12u + 200u);
  SM_EXPECT_EQ(b.size(), pad.size());
  SM_EXPECT_EQ(b[0] & 0x20, 0x20);  // P bit.
  SM_EXPECT_EQ(b.back(), 200);
  SM_EXPECT_EQ(b[b.size() - 2], 0);
  auto q = RtpPacket::Parse(b);
  SM_ASSERT_TRUE(q.has_value());
  SM_EXPECT_TRUE(q->payload.empty());
  SM_EXPECT_EQ(q->padding_size, 200);
  // Payload plus the smallest padding: one byte, which is the count itself.
  RtpPacket p;
  p.payload = {9, 8, 7};
  p.padding_size = 1;
  auto pb = p.Serialize();
  SM_EXPECT_EQ(pb.size(), 12u + 3u + 1u);
  SM_EXPECT_EQ(pb.back(), 1);
  auto pq = RtpPacket::Parse(pb);
  SM_ASSERT_TRUE(pq.has_value());
  SM_EXPECT_EQ(pq->payload, p.payload);
  SM_EXPECT_EQ(pq->padding_size, 1);
  // No padding: no P bit, parse reports zero.
  RtpPacket plain;
  plain.payload = {1};
  SM_EXPECT_EQ(plain.Serialize()[0] & 0x20, 0);
  SM_EXPECT_EQ(RtpPacket::Parse(plain.Serialize())->padding_size, 0);
  // Padding count of zero or larger than the packet is malformed.
  auto bad = b;
  bad.back() = 0;
  SM_EXPECT_FALSE(RtpPacket::Parse(bad).has_value());
  bad.back() = 255;
  SM_EXPECT_FALSE(RtpPacket::Parse(bad).has_value());
}

SM_TEST(NetworkLink, LossDelayAndCapacity) {
  sim::LinkConfig cfg;
  cfg.base_delay = TimeDelta::Millis(50);
  cfg.loss_rate = 0.1;
  sim::NetworkLink<int> link(cfg, 42);
  for (int i = 0; i < 10000; ++i) link.Send(Timestamp::Millis(i), i, DataSize::Bytes(100));
  auto got = link.Receive(Timestamp::Millis(10049));
  double loss = 1.0 - static_cast<double>(got.size()) / 10000;
  SM_EXPECT_NEAR(loss, 0.1, 0.015);
  SM_EXPECT_EQ(link.Receive(Timestamp::Millis(10060)).size() + got.size(), link.stats().delivered);

  // 1 Mbps bottleneck: 125 packets of 1000 B take one second to drain.
  sim::LinkConfig cap;
  cap.base_delay = TimeDelta::Zero();
  cap.capacity = DataRate::KilobitsPerSec(1000);
  sim::NetworkLink<int> bottleneck(cap, 1);
  for (int i = 0; i < 125; ++i) bottleneck.Send(Timestamp::Millis(0), i, DataSize::Bytes(1000));
  SM_EXPECT_NEAR(bottleneck.QueuedBytes(Timestamp::Millis(0)).bytes(), 125000, 10);
  SM_EXPECT_EQ(bottleneck.Receive(Timestamp::Millis(500)).size(), 62u);
  SM_EXPECT_EQ(bottleneck.Receive(Timestamp::Millis(1000)).size(), 63u);
}

SM_TEST(NetworkLink, CountsSentLostAndDroppedPerKind) {
  // 80 kbps with a 1000 B queue: 100 B packets leave every 10 ms.
  sim::LinkConfig cfg;
  cfg.capacity = DataRate::KilobitsPerSec(80);
  cfg.queue_limit = DataSize::Bytes(1000);
  sim::NetworkLink<int> link(cfg, 1);
  for (int i = 0; i < 20; ++i) {
    link.Send(Timestamp::Millis(0), i, DataSize::Bytes(100), i % 2 ? sim::LinkKind::kAudio : sim::LinkKind::kVideo);
  }
  link.Send(Timestamp::Millis(0), 0, DataSize::Bytes(50));
  const auto& st = link.stats();
  SM_EXPECT_EQ(st.of(sim::LinkKind::kAudio).sent, 10u);
  SM_EXPECT_EQ(st.of(sim::LinkKind::kVideo).sent, 10u);
  SM_EXPECT_EQ(st.of(sim::LinkKind::kAudio).bytes_sent, 1000);
  SM_EXPECT_EQ(st.of(sim::LinkKind::kOther).sent, 1u);
  // The first ten packets fill the queue; the other ten (five of each kind) overflow it.
  SM_EXPECT_EQ(st.of(sim::LinkKind::kAudio).queue_dropped, 5u);
  SM_EXPECT_EQ(st.of(sim::LinkKind::kVideo).queue_dropped, 5u);
  SM_EXPECT_EQ(st.of(sim::LinkKind::kOther).queue_dropped, 1u);
  SM_EXPECT_EQ(st.queue_dropped, 11u);

  sim::LinkConfig down;
  down.down = true;
  sim::NetworkLink<int> out(down, 1);
  out.Send(Timestamp::Millis(0), 0, DataSize::Bytes(10), sim::LinkKind::kRtx);
  SM_EXPECT_EQ(out.stats().of(sim::LinkKind::kRtx).lost, 1u);
  SM_EXPECT_EQ(out.stats().of(sim::LinkKind::kRtx).sent, 1u);
}

SM_TEST(NetworkLink, GilbertBurstsAreLongerThanBernoulli) {
  sim::LinkConfig cfg;
  cfg.gilbert = true;
  cfg.p_good_to_bad = 0.02;
  cfg.p_bad_to_good = 0.25;  // Mean burst 4 packets.
  sim::NetworkLink<int> link(cfg, 9);
  for (int i = 0; i < 20000; ++i) link.Send(Timestamp::Millis(i), i, DataSize::Bytes(100));
  auto got = link.Receive(Timestamp::Seconds(100));
  int bursts = 0, lost = 0, prev = -1;
  for (int v : got) {
    if (v != prev + 1) {
      ++bursts;
      lost += v - prev - 1;
    }
    prev = v;
  }
  double mean_burst = static_cast<double>(lost) / bursts;
  SM_EXPECT_GT(mean_burst, 3.0);
}
