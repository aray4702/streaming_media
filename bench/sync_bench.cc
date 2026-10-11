// Section 8 benchmarks.
#include "sm/sync/capture_time.h"
#include "sm/sync/header_extensions.h"
#include "sm/sync/rtp_to_ntp.h"
#include "sm/sync/stream_sync.h"
#include "sm_bench.h"

using namespace sm;
using namespace sm::sync;
using timing::NtpTime;

SM_BENCH(sync, RtpToNtp_UpdateAndEstimate) {
  RtpToNtpEstimator e;
  int64_t i = 0;
  for (auto _ : st) {
    e.UpdateMeasurements(NtpTime::FromTimestamp(Timestamp::Seconds(1700000000 + i)), static_cast<uint32_t>(i * 90000));
    bench::DoNotOptimize(e.Estimate(static_cast<uint32_t>(i * 90000 + 1234)));
    ++i;
  }
  st.BudgetNsPerItem(3000);
}

SM_BENCH(sync, RtpToNtp_Estimate) {
  RtpToNtpEstimator e;
  for (int i = 0; i < 20; ++i) e.UpdateMeasurements(NtpTime::FromTimestamp(Timestamp::Seconds(1700000000 + i)), static_cast<uint32_t>(i * 90000));
  uint32_t rtp = 0;
  for (auto _ : st) bench::DoNotOptimize(e.Estimate(rtp += 3000));
  st.BudgetNsPerItem(100);
}

SM_BENCH(sync, StreamSync_Update) {
  StreamSynchronization s;
  for (int i = 0; i < 5; ++i) {
    s.audio_map().UpdateMeasurements(NtpTime::FromTimestamp(Timestamp::Seconds(1700000000 + i)), static_cast<uint32_t>(i * 48000));
    s.video_map().UpdateMeasurements(NtpTime::FromTimestamp(Timestamp::Seconds(1700000000 + i)), static_cast<uint32_t>(i * 90000));
  }
  int64_t t = 0;
  for (auto _ : st) {
    s.OnAudioPacket(static_cast<uint32_t>(4 * 48000 + t * 48), Timestamp::Millis(4050 + t));
    s.OnVideoFrame(static_cast<uint32_t>(4 * 90000 + t * 90), Timestamp::Millis(4120 + t));
    bench::DoNotOptimize(s.Update(TimeDelta::Millis(60), TimeDelta::Millis(40), Timestamp::Millis(5000 + t)));
    ++t;
  }
  st.BudgetNsPerItem(500);
}

SM_BENCH(sync, AbsCaptureTime_SenderAndInterpolator) {
  AbsoluteCaptureTimeSender s;
  AbsoluteCaptureTimeInterpolator in;
  int64_t i = 0;
  for (auto _ : st) {
    AbsoluteCaptureTime a{(uint64_t{100} << 32) + static_cast<uint64_t>(i) * 143165577ull, std::nullopt};
    auto ext = s.OnPacket(1, static_cast<uint32_t>(i * 3000), 90000, a, Timestamp::Millis(i * 33));
    bench::DoNotOptimize(in.OnPacket(1, static_cast<uint32_t>(i * 3000), 90000, ext, Timestamp::Millis(i * 33)));
    ++i;
  }
  st.BudgetNsPerItem(200);
}

SM_BENCH(sync, HeaderExtensions_WriteParse4) {
  ExtensionMap m;
  m.Register(1, uri::kMid);
  m.Register(2, uri::kRid);
  m.Register(3, uri::kTransportCc);
  m.Register(4, uri::kAbsSendTime);
  ExtensionAccessor a(m);
  uint16_t seq = 0;
  for (auto _ : st) {
    RtpPacket p;
    a.SetString(p, uri::kMid, "v");
    a.SetString(p, uri::kRid, "h");
    a.SetTransportSeq(p, seq++);
    a.SetAbsSendTime(p, Timestamp::Millis(seq));
    bench::DoNotOptimize(a.GetTransportSeq(p));
  }
  st.BudgetNsPerItem(5000);
}

SM_BENCH(sync, RtpDemuxer_by_ssrc) {
  ExtensionMap m;
  m.Register(1, uri::kMid);
  RtpDemuxer d(m);
  d.AddSink(0, "a", "", {111});
  d.AddSink(1, "v", "", {96});
  RtpPacket p;
  p.payload_type = 96;
  p.ssrc = 42;
  ExtensionAccessor(m).SetString(p, uri::kMid, "v");
  d.Demux(p);
  RtpPacket q;
  q.ssrc = 42;
  q.payload_type = 96;
  for (auto _ : st) bench::DoNotOptimize(d.Demux(q));
  st.BudgetNsPerItem(300);
}
