// Section 8 unit tests.
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "sm/sync/capture_time.h"
#include "sm/sync/header_extensions.h"
#include "sm/sync/rtp_to_ntp.h"
#include "sm/sync/stream_sync.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::sync;
using timing::NtpTime;

namespace {
NtpTime Ntp(double ms) { return NtpTime::FromTimestamp(Timestamp::Micros(static_cast<int64_t>(1.7e15 + ms * 1000))); }
double MsOf(Timestamp t) { return (static_cast<double>(t.us()) - 1.7e15) / 1000.0; }
}  // namespace

SM_TEST(RtpToNtp, FitsLineAndHandlesWrap) {
  RtpToNtpEstimator e;
  uint32_t rtp0 = 0xFFFF0000u;  // Wraps after ~0.73 s at 90 kHz.
  for (int i = 0; i < 5; ++i) {
    auto r = e.UpdateMeasurements(Ntp(i * 1000.0), rtp0 + static_cast<uint32_t>(i * 90000));
    SM_EXPECT_TRUE(r == RtpToNtpEstimator::UpdateResult::kNewMeasurement);
  }
  auto t = e.Estimate(rtp0 + 4 * 90000 + 45000);
  SM_ASSERT_TRUE(t.has_value());
  SM_EXPECT_NEAR(MsOf(*t), 4500, 0.01);
  SM_EXPECT_NEAR(*e.EstimatedFrequencyHz(), 90000, 0.1);
  SM_EXPECT_NEAR(e.EstimatedFrequencyKhz(), 90, 1e-4);
  // Equal NTP or equal RTP timestamp: the same measurement.
  SM_EXPECT_TRUE(e.UpdateMeasurements(Ntp(4000.0), rtp0 + 4 * 90000) == RtpToNtpEstimator::UpdateResult::kSameMeasurement);
  SM_EXPECT_TRUE(e.UpdateMeasurements(Ntp(4500.0), rtp0 + 4 * 90000) == RtpToNtpEstimator::UpdateResult::kSameMeasurement);
}

SM_TEST(RtpToNtp, NeedsTwoMeasurementsAndAValidNtp) {
  RtpToNtpEstimator e;
  SM_EXPECT_TRUE(e.UpdateMeasurements(NtpTime{0}, 1000) == RtpToNtpEstimator::UpdateResult::kInvalidMeasurement);
  SM_EXPECT_TRUE(e.UpdateMeasurements(Ntp(0.0), 0) == RtpToNtpEstimator::UpdateResult::kNewMeasurement);
  SM_EXPECT_FALSE(e.Estimate(0).has_value());
  SM_EXPECT_EQ(e.EstimateNtp(0).value, 0u);
  SM_EXPECT_EQ(e.EstimatedFrequencyKhz(), 0.0);
  SM_EXPECT_TRUE(e.UpdateMeasurements(Ntp(20.0), 960) == RtpToNtpEstimator::UpdateResult::kNewMeasurement);
  SM_ASSERT_TRUE(e.Estimate(480).has_value());
  SM_EXPECT_NEAR(MsOf(*e.Estimate(480)), 10, 0.01);
}

SM_TEST(RtpToNtp, ValidatesAgainstTheNewestReport) {
  // libwebrtc's rule: NTP and RTP must move forward from the newest stored
  // SR, NTP by at most 1 h and RTP by at most 1 << 25 ticks.
  RtpToNtpEstimator e;
  e.UpdateMeasurements(Ntp(0.0), 0);
  e.UpdateMeasurements(Ntp(1000.0), 48000);
  using R = RtpToNtpEstimator::UpdateResult;
  SM_EXPECT_TRUE(e.UpdateMeasurements(Ntp(900.0), 96000) == R::kInvalidMeasurement);      // NTP backwards.
  SM_EXPECT_TRUE(e.UpdateMeasurements(Ntp(2000.0), 40000) == R::kInvalidMeasurement);     // RTP backwards.
  e.UpdateMeasurements(Ntp(2000.0), 96000);  // Valid: clears the invalid count.
  SM_EXPECT_TRUE(e.UpdateMeasurements(Ntp(3000.0), 96000 + (1u << 25) + 1) == R::kInvalidMeasurement);
  SM_EXPECT_TRUE(e.UpdateMeasurements(Ntp(2000.0 + 3600e3 + 1), 144000) == R::kInvalidMeasurement);
  e.UpdateMeasurements(Ntp(3000.0), 144000);
  SM_EXPECT_EQ(e.size(), 4u);
  // An SR delayed 200 ms in the sender is off the line but still valid.
  SM_EXPECT_TRUE(e.UpdateMeasurements(Ntp(4200.0), 192000) == R::kNewMeasurement);
  SM_EXPECT_EQ(e.size(), 5u);
}

SM_TEST(RtpToNtp, ClearsAfterThreeConsecutiveInvalidReports) {
  RtpToNtpEstimator e;
  for (int i = 0; i < 5; ++i) e.UpdateMeasurements(Ntp(i * 1000.0), static_cast<uint32_t>(10000000 + i * 48000));
  using R = RtpToNtpEstimator::UpdateResult;
  // The sender restarted its RTP clock: the timestamps go backwards.
  SM_EXPECT_TRUE(e.UpdateMeasurements(Ntp(5000.0), 1000) == R::kInvalidMeasurement);
  SM_EXPECT_TRUE(e.UpdateMeasurements(Ntp(6000.0), 49000) == R::kInvalidMeasurement);
  SM_EXPECT_NEAR(MsOf(*e.Estimate(10000000 + 5 * 48000)), 5000, 0.01);  // Old line still in use.
  // The third one clears the history and starts the new line alone.
  SM_EXPECT_TRUE(e.UpdateMeasurements(Ntp(7000.0), 97000) == R::kNewMeasurement);
  SM_EXPECT_EQ(e.size(), 1u);
  SM_EXPECT_FALSE(e.Estimate(97000).has_value());
  e.UpdateMeasurements(Ntp(8000.0), 145000);
  SM_EXPECT_NEAR(MsOf(*e.Estimate(169000)), 8500, 0.01);
}

SM_TEST(RtpToNtp, KeepsTheLastTwentyReports) {
  RtpToNtpEstimator e;
  // The first 10 SRs map to a 48 kHz clock, the next 20 to 48.048 kHz.
  int64_t rtp = 0;
  for (int i = 0; i < 30; ++i) {
    e.UpdateMeasurements(Ntp(i * 1000.0), static_cast<uint32_t>(rtp));
    rtp += i < 10 ? 48000 : 48048;
  }
  SM_EXPECT_EQ(e.size(), RtpToNtpEstimator::kNumRtcpReportsToUse);
  SM_EXPECT_NEAR(*e.EstimatedFrequencyHz(), 48048, 0.01);
}

namespace {
// Two streams whose SRs share one capture clock: audio 48 kHz, video 90 kHz.
void TrainMeasurements(StreamSynchronization::Measurements& audio, StreamSynchronization::Measurements& video) {
  for (int i = 0; i < 3; ++i) {
    audio.rtp_to_ntp.UpdateMeasurements(Ntp(i * 1000.0), static_cast<uint32_t>(i * 48000));
    video.rtp_to_ntp.UpdateMeasurements(Ntp(i * 1000.0), static_cast<uint32_t>(i * 90000));
  }
}
}  // namespace

SM_TEST(StreamSync, RelativeDelayFromLatestPackets) {
  StreamSynchronization::Measurements audio, video;
  SM_EXPECT_FALSE(StreamSynchronization::ComputeRelativeDelay(audio, video).has_value());  // No SRs yet.
  TrainMeasurements(audio, video);
  // Captured at 2000 ms (audio) and 2010 ms (video); received at 2050 and
  // 2160: video is 100 ms behind.
  audio.latest_timestamp = 2 * 48000;
  audio.latest_receive_time_ms = 2050;
  video.latest_timestamp = 2 * 90000 + 900;
  video.latest_receive_time_ms = 2160;
  SM_EXPECT_EQ(*StreamSynchronization::ComputeRelativeDelay(audio, video), 100);
  // Beyond +-10 s there is no update.
  video.latest_receive_time_ms = 2050 + 10 + 10001;
  SM_EXPECT_FALSE(StreamSynchronization::ComputeRelativeDelay(audio, video).has_value());
  video.latest_receive_time_ms = 2050 + 10 + 10000;
  SM_EXPECT_EQ(*StreamSynchronization::ComputeRelativeDelay(audio, video), 10000);
}

SM_TEST(StreamSync, FilterStartsAtZeroAndResetsAfterAMove) {
  StreamSynchronization s(2, 1);
  // Video 100 ms late, equal current delays: avg = (3 * 0 + 100) / 4 = 25,
  // inside the 30 ms margin.
  SM_EXPECT_FALSE(s.ComputeDelays(100, 0, 0).has_value());
  // avg = (75 + 100) / 4 = 43: move 43 / 2 = 21 ms of audio delay.
  auto d = s.ComputeDelays(100, 0, 0);
  SM_ASSERT_TRUE(d.has_value());
  SM_EXPECT_EQ(d->total_audio_delay_target_ms, 21);
  SM_EXPECT_EQ(d->total_video_delay_target_ms, 0);
  // The average restarted from 0 after the move.
  SM_EXPECT_FALSE(s.ComputeDelays(100, 21, 0).has_value());
}

SM_TEST(StreamSync, StepIsBoundedByMaxChange) {
  StreamSynchronization s(2, 1);
  // Video 1 s late every time: avg = 1000 / 4 = 250, half of it capped at
  // 80 ms per move.
  for (int k = 1; k <= 5; ++k) {
    auto d = s.ComputeDelays(1000, 0, 0);
    SM_ASSERT_TRUE(d.has_value());
    SM_EXPECT_EQ(d->total_audio_delay_target_ms, k * StreamSynchronization::kMaxChangeMs);
    SM_EXPECT_EQ(d->total_video_delay_target_ms, 0);
  }
}

namespace {
// Two moves of 50 ms with video 400 ms late: 100 ms of extra audio delay.
StreamSynchronization WithAudioExtra100() {
  StreamSynchronization s(2, 1);
  s.ComputeDelays(400, 0, 0);
  auto d = s.ComputeDelays(400, 0, 0);
  SM_EXPECT_EQ(d->total_audio_delay_target_ms, 100);
  return s;
}
}  // namespace

SM_TEST(StreamSync, ChangesOneStreamAtATime) {
  StreamSynchronization s = WithAudioExtra100();
  // Video 120 ms early: avg -30, move -15 ms. The extra audio delay comes
  // off first and video keeps its last output.
  auto d = s.ComputeDelays(-120, 0, 0);
  SM_ASSERT_TRUE(d.has_value());
  SM_EXPECT_EQ(d->total_audio_delay_target_ms, 85);
  SM_EXPECT_EQ(d->total_video_delay_target_ms, 0);
  // 70, 55, 40, 25, 10, then the extra audio delay is spent (-5): audio
  // keeps its last output, 10, and video gets the extra delay from then on.
  for (int k = 0; k < 6; ++k) d = s.ComputeDelays(-120, 0, 0);
  SM_EXPECT_EQ(d->total_audio_delay_target_ms, 10);
  SM_EXPECT_EQ(d->total_video_delay_target_ms, 0);
  d = s.ComputeDelays(-120, 0, 0);
  SM_EXPECT_EQ(d->total_audio_delay_target_ms, 10);
  SM_EXPECT_EQ(d->total_video_delay_target_ms, 15);
}

SM_TEST(StreamSync, ReduceAudioDelay) {
  StreamSynchronization s = WithAudioExtra100();
  s.ReduceAudioDelay();  // A failed SetMinimumPlayoutDelay: 100 -> 90.
  auto d = s.ComputeDelays(-120, 0, 0);
  SM_ASSERT_TRUE(d.has_value());
  SM_EXPECT_EQ(d->total_audio_delay_target_ms, 75);
}

SM_TEST(StreamSync, BaseTargetDelayAndCap) {
  StreamSynchronization s(2, 1);
  s.SetTargetBufferingDelay(150);
  // Video 100 ms early: avg -25 (no move), then -43: video gets 21 ms on
  // top of the base and audio stays at the base.
  SM_EXPECT_FALSE(s.ComputeDelays(-100, 0, 0).has_value());
  auto d = s.ComputeDelays(-100, 0, 0);
  SM_ASSERT_TRUE(d.has_value());
  SM_EXPECT_EQ(d->total_video_delay_target_ms, 171);
  SM_EXPECT_EQ(d->total_audio_delay_target_ms, 150);
  // Video far late for a long time: the output is capped at base + 10 s.
  // The video extra delay drops back to the base, so video keeps its last
  // output (171), as libwebrtc's `last_ms` rule does.
  for (int k = 0; k < 400; ++k) {
    auto n = s.ComputeDelays(10000, 0, 0);
    if (n) d = n;
  }
  SM_EXPECT_EQ(d->total_audio_delay_target_ms, 150 + StreamSynchronization::kMaxDeltaDelayMs);
  SM_EXPECT_EQ(d->total_video_delay_target_ms, 171);
}

namespace {
struct FakeSyncable : Syncable {
  uint32_t ssrc = 0;
  std::optional<Info> info;
  std::optional<PlayoutInfo> playout;
  bool accept = true;
  std::vector<int> min_delays;
  uint32_t id() const override { return ssrc; }
  std::optional<Info> GetInfo() const override { return info; }
  std::optional<PlayoutInfo> GetPlayoutRtpTimestamp() const override { return playout; }
  bool SetMinimumPlayoutDelay(TimeDelta d) override {
    min_delays.push_back(static_cast<int>(d.ms()));
    return accept;
  }
  void SetEstimatedPlayoutNtpTimestamp(NtpTime, Timestamp) override {}
};
}  // namespace

SM_TEST(StreamSync, SynchronizerRunsEverySecondOnNewPackets) {
  FakeSyncable audio, video;
  audio.ssrc = 1;
  video.ssrc = 2;
  RtpStreamsSynchronizer sync(&video);
  sync.ConfigureSync(&audio, Timestamp::Millis(0));
  SM_EXPECT_EQ(sync.NextProcessTime()->ms(), 1000);
  // Video 200 ms behind audio, both with 50 ms of playout delay.
  auto feed = [&](int64_t t, int sr) {
    audio.info = Syncable::Info{Timestamp::Millis(t + 20), static_cast<uint32_t>(t * 48), static_cast<uint32_t>(sr * 48000),
                                Ntp(sr * 1000.0), TimeDelta::Millis(50)};
    video.info = Syncable::Info{Timestamp::Millis(t + 220), static_cast<uint32_t>(t * 90), static_cast<uint32_t>(sr * 90000),
                                Ntp(sr * 1000.0), TimeDelta::Millis(50)};
  };
  int runs_with_delays = 0;
  for (int64_t t = 0; t <= 10000; t += 10) {
    feed(t, static_cast<int>(t / 1000));
    if (t == 6000) {
      audio.info->latest_receive_time = Timestamp::Millis(5020);  // No new audio packet this second.
    }
    size_t before = audio.min_delays.size();
    sync.Process(Timestamp::Millis(t));
    if (audio.min_delays.size() > before) ++runs_with_delays;
    if (t == 6000) SM_EXPECT_EQ(audio.min_delays.size(), before);
  }
  SM_EXPECT_GT(runs_with_delays, 2);
  SM_EXPECT_EQ(audio.min_delays.size(), video.min_delays.size());
  SM_EXPECT_GT(audio.min_delays.back(), 0);
  // Stopping the sync stops the task.
  sync.ConfigureSync(nullptr, Timestamp::Millis(10000));
  SM_EXPECT_FALSE(sync.NextProcessTime().has_value());
}

SM_TEST(StreamSync, StreamSyncOffset) {
  FakeSyncable audio, video;
  audio.ssrc = 1;
  video.ssrc = 2;
  RtpStreamsSynchronizer sync(&video);
  sync.ConfigureSync(&audio, Timestamp::Millis(0));
  for (int i = 0; i <= 2; ++i) {
    audio.info = Syncable::Info{Timestamp::Millis(i * 1000 + 20), static_cast<uint32_t>(i * 48000), static_cast<uint32_t>(i * 48000),
                                Ntp(i * 1000.0), TimeDelta::Millis(50)};
    video.info = Syncable::Info{Timestamp::Millis(i * 1000 + 60), static_cast<uint32_t>(i * 90000), static_cast<uint32_t>(i * 90000),
                                Ntp(i * 1000.0), TimeDelta::Millis(50)};
    sync.UpdateDelay(Timestamp::Millis(i * 1000 + 100));
  }
  // Audio played out capture 2000 at 2080; a video frame captured at 1950
  // renders at 2120. At 2100 audio plays capture 2020 and video 1930.
  audio.playout = Syncable::PlayoutInfo{Timestamp::Millis(2080), 2 * 48000};
  auto off = sync.GetStreamSyncOffsetInMs(static_cast<uint32_t>(1950 * 90), 2120, Timestamp::Millis(2100));
  SM_ASSERT_TRUE(off.has_value());
  SM_EXPECT_EQ(off->stream_offset_ms, 90);
  SM_EXPECT_NEAR(off->estimated_freq_khz, 90, 1e-4);
}

SM_TEST(StreamSync, GroupsByCname) {
  MediaSyncGroups g;
  g.AddStream(10, "alice", MediaSyncGroups::Kind::kAudio);
  g.AddStream(11, "alice", MediaSyncGroups::Kind::kVideo);
  g.AddStream(20, "bob", MediaSyncGroups::Kind::kAudio);
  g.AddStream(30, "carol", MediaSyncGroups::Kind::kVideo);
  auto p = g.SyncPairs();
  SM_ASSERT_EQ(p.size(), 1u);
  SM_EXPECT_EQ(p[0].first, 10u);
  SM_EXPECT_EQ(p[0].second, 11u);
  g.AddStream(21, "bob", MediaSyncGroups::Kind::kVideo);
  SM_EXPECT_EQ(g.SyncPairs().size(), 2u);
}

SM_TEST(CaptureTime, WireFormatAndInterpolation) {
  AbsoluteCaptureTime a{0x0123456789ABCDEFull, -5};
  auto b = SerializeAbsoluteCaptureTime(a);
  SM_EXPECT_EQ(b.size(), 16u);
  auto back = ParseAbsoluteCaptureTime(b);
  SM_ASSERT_TRUE(back.has_value());
  SM_EXPECT_EQ(back->capture_ntp, a.capture_ntp);
  SM_EXPECT_EQ(*back->clock_offset, -5);
  SM_EXPECT_FALSE(ParseAbsoluteCaptureTime(std::vector<uint8_t>(5)).has_value());
  AbsoluteCaptureTimeInterpolator interp;
  AbsoluteCaptureTime first{uint64_t{1000} << 32, std::nullopt};
  interp.OnPacket(1, 0, 90000, first, Timestamp::Millis(0));
  auto i = interp.OnPacket(1, 45000, 90000, std::nullopt, Timestamp::Millis(500));
  SM_ASSERT_TRUE(i.has_value());
  SM_EXPECT_EQ(i->capture_ntp, (uint64_t{1000} << 32) + (uint64_t{1} << 31));  // +0.5 s.
  SM_EXPECT_FALSE(interp.OnPacket(2, 0, 90000, std::nullopt, Timestamp::Millis(600)).has_value());
}

SM_TEST(CaptureTime, SenderOmitsInterpolatableExtensions) {
  AbsoluteCaptureTimeSender s;
  int sent = 0;
  for (int i = 0; i < 300; ++i) {  // 10 s at 30 fps, regular capture.
    AbsoluteCaptureTime a{(uint64_t{100} << 32) + static_cast<uint64_t>((static_cast<long double>(i) / 30) * 4294967296.0L), std::nullopt};
    sent += s.OnPacket(1, static_cast<uint32_t>(i * 3000), 90000, a, Timestamp::Millis(i * 33)).has_value();
  }
  SM_EXPECT_LE(sent, 11);  // ~1 per second.
  // A capture-time jump (not explained by RTP) is sent immediately.
  AbsoluteCaptureTime jump{(uint64_t{200} << 32), std::nullopt};
  SM_EXPECT_TRUE(s.OnPacket(1, 300 * 3000, 90000, jump, Timestamp::Millis(300 * 33)).has_value());
}

SM_TEST(CaptureTime, LatencyWithClockOffset) {
  LatencyEstimator le;
  // Remote clock is 2.5 s ahead; one-way delay 40 ms, RTT 80 ms.
  // RemoteNtpTimeEstimator gives an offset only once 3 SRs are in.
  AbsoluteCaptureTime cap{NtpTime::FromTimestamp(Timestamp::Millis(104700)).value, 0};
  for (int i = 0; i < 3; ++i) {
    SM_EXPECT_FALSE(le.remote_to_local_offset().has_value());
    SM_EXPECT_FALSE(le.OnRendered(cap, Timestamp::Millis(102350)).has_value());
    Timestamp local_send = Timestamp::Seconds(100 + i);
    NtpTime sr = NtpTime::FromTimestamp(local_send + TimeDelta::Millis(2500));
    le.OnSenderReport(sr, local_send + TimeDelta::Millis(40), TimeDelta::Millis(80));
  }
  SM_ASSERT_TRUE(le.remote_to_local_offset().has_value());
  SM_EXPECT_NEAR(le.remote_to_local_offset()->ms_f(), 2500, 0.1);
  // Frame captured at local 102.2 s (remote 104.7 s), rendered at 102.35 s.
  auto lat = le.OnRendered(cap, Timestamp::Millis(102350));
  SM_ASSERT_TRUE(lat.has_value());
  SM_EXPECT_NEAR(lat->ms_f(), 150, 1.0);
  // libwebrtc derives no local capture offset from an extension without one.
  SM_EXPECT_FALSE(le.OnRendered({cap.capture_ntp, std::nullopt}, Timestamp::Millis(102350)).has_value());
}

// absolute_capture_time_sender.cc, ShouldSendExtension: each rule.
SM_TEST(CaptureTime, SenderRulesAsLibwebrtc) {
  const uint64_t base = uint64_t{1000} << 32;
  const uint64_t ms = (uint64_t{1} << 32) / 1000;
  {
    AbsoluteCaptureTimeSender s;
    Timestamp t0 = Timestamp::Seconds(10);
    SM_EXPECT_TRUE(s.OnPacket(1, 0, 90000, {base, 0}, t0).has_value());  // First.
    // Exactly interpolatable and 1 s old: not sent (the rule is "> 1 s").
    SM_EXPECT_FALSE(s.OnPacket(1, 90000, 90000, {base + (uint64_t{1} << 32), 0}, t0 + TimeDelta::Seconds(1)).has_value());
    SM_EXPECT_TRUE(s.OnPacket(1, 90000, 90000, {base + (uint64_t{1} << 32), 0},
                              t0 + TimeDelta::Seconds(1) + TimeDelta::Micros(1)).has_value());
  }
  {
    AbsoluteCaptureTimeSender s;
    Timestamp t = Timestamp::Seconds(10);
    s.OnPacket(1, 0, 90000, {base, 0}, t);
    SM_EXPECT_TRUE(s.OnPacket(2, 0, 90000, {base, 0}, t).has_value());          // Source.
    SM_EXPECT_TRUE(s.OnPacket(2, 0, 48000, {base, 0}, t).has_value());          // Rate.
    SM_EXPECT_TRUE(s.OnPacket(2, 0, 48000, {base, 5}, t).has_value());          // Offset.
    SM_EXPECT_TRUE(s.OnPacket(2, 0, 48000, {base, std::nullopt}, t).has_value());
    SM_EXPECT_FALSE(s.OnPacket(2, 0, 48000, {base, std::nullopt}, t).has_value());
    SM_EXPECT_TRUE(s.OnPacket(2, 0, 48000, {base, std::nullopt}, t, /*force=*/true).has_value());
    // Interpolation error in whole rounded ms must exceed 1: 1.4 ms rounds
    // to 1 (kept), 1.6 ms to 2 (sent), in either direction.
    SM_EXPECT_FALSE(s.OnPacket(2, 0, 48000, {base + ms * 14 / 10, std::nullopt}, t).has_value());
    SM_EXPECT_FALSE(s.OnPacket(2, 0, 48000, {base - ms * 14 / 10, std::nullopt}, t).has_value());
    SM_EXPECT_TRUE(s.OnPacket(2, 0, 48000, {base - ms * 16 / 10, std::nullopt}, t).has_value());
  }
  {
    AbsoluteCaptureTimeSender s;
    Timestamp t = Timestamp::Seconds(10);
    SM_EXPECT_TRUE(s.OnPacket(1, 0, 0, {base, 0}, t).has_value());
    SM_EXPECT_TRUE(s.OnPacket(1, 0, 0, {base, 0}, t).has_value());  // A rate of 0 always sends.
  }
  const uint32_t csrcs[] = {7, 8};
  SM_EXPECT_EQ(AbsoluteCaptureTimeSender::GetSource(3, csrcs), 7u);
  SM_EXPECT_EQ(AbsoluteCaptureTimeSender::GetSource(3, {}), 3u);
}

// absolute_capture_time_interpolator.cc.
SM_TEST(CaptureTime, InterpolatorRulesAsLibwebrtc) {
  const uint64_t base = uint64_t{1000} << 32;
  // A timestamp behind the last one interpolates backwards; integer rule.
  SM_EXPECT_EQ(AbsoluteCaptureTimeInterpolator::Interpolate(0xFFFFFFFFu - 44999u, 90000, 0, base),
               base - (uint64_t{1} << 31));
  SM_EXPECT_EQ(AbsoluteCaptureTimeInterpolator::Interpolate(1, 3, 0, 0), ((uint64_t{1} << 32) / 3));
  AbsoluteCaptureTimeInterpolator in;
  Timestamp t = Timestamp::Seconds(1);
  SM_EXPECT_FALSE(in.OnPacket(1, 0, 90000, std::nullopt, t).has_value());
  in.OnPacket(1, 0, 90000, AbsoluteCaptureTime{base, 0}, t);
  auto at5 = in.OnPacket(1, 90000, 90000, std::nullopt, t + TimeDelta::Seconds(5));
  SM_ASSERT_TRUE(at5.has_value());
  SM_EXPECT_EQ(at5->capture_ntp, base + (uint64_t{1} << 32));
  SM_EXPECT_EQ(*at5->clock_offset, 0);
  SM_EXPECT_FALSE(in.OnPacket(1, 90000, 90000, std::nullopt, t + TimeDelta::Seconds(5) + TimeDelta::Micros(1)).has_value());
  // A packet that can't be interpolated retires the stored extension.
  in.OnPacket(1, 0, 90000, AbsoluteCaptureTime{base, 0}, t);
  SM_EXPECT_FALSE(in.OnPacket(2, 0, 90000, std::nullopt, t).has_value());
  SM_EXPECT_FALSE(in.OnPacket(1, 0, 90000, std::nullopt, t).has_value());
  in.OnPacket(1, 0, 90000, AbsoluteCaptureTime{base, 0}, t);
  SM_EXPECT_FALSE(in.OnPacket(1, 0, 48000, std::nullopt, t).has_value());
  SM_EXPECT_FALSE(in.OnPacket(1, 0, 90000, std::nullopt, t).has_value());
  in.OnPacket(1, 0, 0, AbsoluteCaptureTime{base, 0}, t);
  SM_EXPECT_FALSE(in.OnPacket(1, 0, 0, std::nullopt, t).has_value());  // Rate 0.
}

// capture_clock_offset_updater.cc.
SM_TEST(CaptureTime, CaptureClockOffsetUpdater) {
  CaptureClockOffsetUpdater u;
  SM_EXPECT_FALSE(u.AdjustEstimatedCaptureClockOffset(5).has_value());
  u.SetRemoteToLocalClockOffset(-7);
  SM_EXPECT_FALSE(u.AdjustEstimatedCaptureClockOffset(std::nullopt).has_value());
  SM_EXPECT_EQ(*u.AdjustEstimatedCaptureClockOffset(5), -2);
  u.SetRemoteToLocalClockOffset(1);
  SM_EXPECT_EQ(*u.AdjustEstimatedCaptureClockOffset(std::numeric_limits<int64_t>::max()), std::numeric_limits<int64_t>::min());
  u.SetRemoteToLocalClockOffset(std::nullopt);
  SM_EXPECT_FALSE(u.AdjustEstimatedCaptureClockOffset(5).has_value());
  SM_EXPECT_FALSE(CaptureClockOffsetUpdater::ConvertToTimeDelta(std::nullopt).has_value());
  const int64_t ms = (int64_t{1} << 32) / 1000;
  SM_EXPECT_EQ(CaptureClockOffsetUpdater::ConvertToTimeDelta(ms * 26 / 10)->us(), 3000);  // 2.6 ms rounds to 3.
  SM_EXPECT_EQ(CaptureClockOffsetUpdater::ConvertToTimeDelta(-ms * 14 / 10)->us(), -1000);
}

// moving_percentile_filter.h with percentile 0.5: the lower median.
SM_TEST(CaptureTime, MovingMedianFilter) {
  MovingMedianFilter f(7);
  SM_EXPECT_EQ(f.GetFilteredValue(), 0);
  f.Insert(10);
  f.Insert(2);
  SM_EXPECT_EQ(f.GetFilteredValue(), 2);  // Two samples: index 0.
  for (int64_t v : {8, 4, 6, 100, -50}) f.Insert(v);
  SM_EXPECT_EQ(f.GetFilteredValue(), 6);  // {-50,2,4,6,8,10,100}.
  f.Insert(0);                            // Drops 10.
  SM_EXPECT_EQ(f.GetNumberOfSamplesStored(), 7u);
  SM_EXPECT_EQ(f.GetFilteredValue(), 4);
}

// remote_ntp_time_estimator.cc.
SM_TEST(CaptureTime, RemoteNtpTimeEstimator) {
  RemoteNtpTimeEstimator e;
  const TimeDelta ahead = TimeDelta::Millis(2500), rtt = TimeDelta::Millis(80);
  // SRs every second; one-way delays 40, 40, 140 (queued), 40 ms.
  const int delays[] = {40, 40, 140, 40};
  for (int i = 0; i < 4; ++i) {
    SM_EXPECT_EQ(e.EstimateRemoteToLocalClockOffset().has_value(), i >= 3);
    Timestamp send = Timestamp::Seconds(100 + i);
    NtpTime sr = NtpTime::FromTimestamp(send + ahead);
    SM_EXPECT_TRUE(e.UpdateRtcpTimestamp(rtt, sr, static_cast<uint32_t>(i * 90000), send + TimeDelta::Millis(delays[i])));
    // The same SR again is not a new sample.
    SM_EXPECT_TRUE(e.UpdateRtcpTimestamp(rtt, sr, static_cast<uint32_t>(i * 90000), send + TimeDelta::Millis(900)));
  }
  auto off = e.EstimateRemoteToLocalClockOffset();
  SM_ASSERT_TRUE(off.has_value());
  // Median of {-2500, -2500, -2400, -2500} ms: -2500 (the queued SR is ignored).
  SM_EXPECT_EQ(Q32x32ToInt64Ms(*off), -2500);
  // RTP 45000 is remote 100.5 + 2.5 s, local 100.5 s.
  SM_EXPECT_NEAR((e.EstimateNtp(45000).ToTimestamp() - Timestamp::Millis(100500)).ms_f(), 0, 0.01);
  RemoteNtpTimeEstimator empty;
  SM_EXPECT_EQ(empty.EstimateNtp(0).value, 0u);
}

SM_TEST(HeaderExtensions, OneAndTwoByteForms) {
  RtpPacket p;
  SM_ASSERT_TRUE(WriteHeaderExtensions({{1, {0xAA}}, {14, std::vector<uint8_t>(16, 7)}}, p));
  SM_EXPECT_EQ(p.extension_profile, 0xBEDE);
  SM_EXPECT_EQ(p.extension.size() % 4, 0u);
  auto parsed = ParseHeaderExtensions(p.extension_profile, p.extension);
  SM_ASSERT_TRUE(parsed.has_value());
  SM_ASSERT_EQ(parsed->size(), 2u);
  SM_EXPECT_EQ((*parsed)[1].data.size(), 16u);
  // id 20 or a 17-byte element forces the two-byte form.
  RtpPacket q;
  SM_ASSERT_TRUE(WriteHeaderExtensions({{20, {1, 2}}, {3, std::vector<uint8_t>(17, 1)}}, q));
  SM_EXPECT_EQ(q.extension_profile, 0x1000);
  auto pq = ParseHeaderExtensions(q.extension_profile, q.extension);
  SM_ASSERT_TRUE(pq.has_value());
  SM_EXPECT_EQ((*pq)[0].id, 20);
  SM_EXPECT_EQ((*pq)[1].data.size(), 17u);
  // Survives full RTP serialization.
  auto rt = RtpPacket::Parse(q.Serialize());
  SM_EXPECT_EQ(ParseHeaderExtensions(rt->extension_profile, rt->extension)->size(), 2u);
  // Truncated: the packet still parses, without the oversized element (rtp_packet.cc:540-544).
  SM_EXPECT_EQ(ParseHeaderExtensions(0xBEDE, std::vector<uint8_t>{0x15, 1})->size(), 0u);
}

// RtpPacket::ParseBuffer's element loop (rtp_packet.cc:512-571).
SM_TEST(HeaderExtensions, ParseRulesAsLibwebrtc) {
  using B = std::vector<uint8_t>;
  // Elements before an oversized one are kept.
  auto a = ParseHeaderExtensions(0xBEDE, B{0x10, 0xAA, 0x23, 1, 2});
  SM_ASSERT_EQ(a->size(), 1u);
  SM_EXPECT_EQ((*a)[0].id, 1);
  // The reserved id 15 stops parsing; so does a non-zero byte with id 0.
  SM_EXPECT_EQ(ParseHeaderExtensions(0xBEDE, B{0x10, 0xAA, 0xF0, 0x20, 0xBB, 0, 0, 0})->size(), 1u);
  SM_EXPECT_EQ(ParseHeaderExtensions(0xBEDE, B{0x10, 0xAA, 0x01, 0x20, 0xBB, 0, 0, 0})->size(), 1u);
  // Padding bytes are skipped.
  SM_EXPECT_EQ(ParseHeaderExtensions(0xBEDE, B{0, 0, 0x10, 0xAA, 0, 0x20, 0xBB, 0})->size(), 2u);
  // A repeated id overwrites the earlier value, keeping one element.
  auto d = ParseHeaderExtensions(0xBEDE, B{0x10, 0xAA, 0x20, 0xBB, 0x10, 0xCC, 0, 0});
  SM_ASSERT_EQ(d->size(), 2u);
  SM_EXPECT_EQ((*d)[0].id, 1);
  SM_EXPECT_EQ((*d)[0].data, B{0xCC});
  // A header byte with no room for even one data byte is not read.
  SM_EXPECT_EQ(ParseHeaderExtensions(0xBEDE, B{0x10, 0xAA, 0, 0x30})->size(), 1u);
  // Two-byte form: app bits are ignored and zero-length elements are allowed,
  // but a header in the last two bytes is not read.
  auto t = ParseHeaderExtensions(0x1001, B{5, 0, 6, 1, 0xAA, 0, 0, 0});
  SM_ASSERT_EQ(t->size(), 2u);
  SM_EXPECT_TRUE((*t)[0].data.empty());
  SM_EXPECT_EQ(ParseHeaderExtensions(0x1000, B{6, 1, 0xAA, 0, 0, 0, 7, 0})->size(), 1u);
  // Neither profile: no elements.
  SM_EXPECT_FALSE(ParseHeaderExtensions(0xABCD, B{0x10, 0xAA, 0, 0}).has_value());
}

// RtpHeaderExtensionMap::RegisterByUri (rtp_header_extension_map.cc:100-170).
SM_TEST(HeaderExtensions, MapRegistrationAsLibwebrtc) {
  ExtensionMap m;
  SM_EXPECT_FALSE(m.extmap_allow_mixed());
  SM_EXPECT_FALSE(m.Register(1, "urn:example:not-a-webrtc-extension"));  // Unknown URI.
  SM_EXPECT_FALSE(m.Register(0, uri::kMid));                              // Below kMinId.
  SM_EXPECT_TRUE(m.Register(1, uri::kMid));
  SM_EXPECT_TRUE(m.Register(1, uri::kMid));          // The same pair again.
  SM_EXPECT_FALSE(m.Register(1, uri::kTransportCc));  // Id used by another URI: not overwritten.
  SM_EXPECT_EQ(*m.UriOf(1), uri::kMid);
  SM_EXPECT_FALSE(m.Register(2, uri::kMid));  // URI already at id 1.
  SM_EXPECT_TRUE(m.Register(255, uri::kAbsCaptureTime));
  SM_EXPECT_TRUE(m.Register(14, "http://www.webrtc.org/experiments/rtp-hdrext/playout-delay"));
  m.Deregister(uri::kMid);
  SM_EXPECT_FALSE(m.IdOf(uri::kMid).has_value());
  SM_EXPECT_TRUE(m.Register(2, uri::kMid));
}

// RtpPacket::AllocateExtension and AllocateRawExtension (rtp_packet.cc:242-353, 620-639).
SM_TEST(HeaderExtensions, AllocationAsLibwebrtc) {
  using B = std::vector<uint8_t>;
  ExtensionMap one;  // No extmap-allow-mixed: one-byte form only.
  one.Register(1, uri::kTransportCc);
  one.Register(2, uri::kMid);
  one.Register(20, uri::kAbsCaptureTime);
  ExtensionAccessor a(one);
  RtpPacket p;
  SM_EXPECT_FALSE(a.SetRaw(p, uri::kAbsCaptureTime, B(8, 1)));  // Id 20 needs the two-byte form.
  SM_EXPECT_FALSE(a.SetString(p, uri::kMid, std::string(17, 'm')));  // 17 bytes too.
  SM_EXPECT_FALSE(a.SetRaw(p, uri::kMid, {}));                    // Empty values are never written.
  SM_EXPECT_FALSE(a.SetRaw(p, uri::kRid, B{1}));                  // Not registered.
  SM_EXPECT_TRUE(p.extension.empty());
  SM_EXPECT_TRUE(a.SetTransportSeq(p, 0x1234));
  SM_EXPECT_TRUE(a.SetString(p, uri::kMid, "ab"));
  SM_EXPECT_EQ(p.extension_profile, 0xBEDE);
  SM_EXPECT_EQ(p.extension, (B{0x11, 0x12, 0x34, 0x21, 'a', 'b', 0, 0}));
  // A rewrite keeps the element's place; another length is refused.
  SM_EXPECT_TRUE(a.SetTransportSeq(p, 0x5678));
  SM_EXPECT_EQ(p.extension, (B{0x11, 0x56, 0x78, 0x21, 'a', 'b', 0, 0}));
  SM_EXPECT_FALSE(a.SetString(p, uri::kMid, "abc"));
  SM_EXPECT_EQ(*a.GetString(p, uri::kMid), "ab");

  // With extmap-allow-mixed, a two-byte element promotes the earlier ones.
  ExtensionMap mixed(true);
  mixed.Register(1, uri::kTransportCc);
  mixed.Register(2, uri::kMid);
  mixed.Register(20, uri::kVideoOrientation);
  ExtensionAccessor m(mixed);
  RtpPacket q;
  SM_EXPECT_TRUE(m.SetTransportSeq(q, 0x1234));
  SM_EXPECT_TRUE(m.SetString(q, uri::kMid, "ab"));
  SM_EXPECT_EQ(q.extension_profile, 0xBEDE);
  SM_EXPECT_TRUE(m.SetRaw(q, uri::kVideoOrientation, B{9}));
  SM_EXPECT_EQ(q.extension_profile, 0x1000);
  SM_EXPECT_EQ(q.extension, (B{1, 2, 0x12, 0x34, 2, 2, 'a', 'b', 20, 1, 9, 0}));
  // A long value in a two-byte packet; a fresh packet starts two-byte.
  SM_EXPECT_TRUE(m.SetString(q, uri::kMid, "ab"));
  RtpPacket r;
  SM_EXPECT_TRUE(m.SetString(r, uri::kMid, std::string(40, 'x')));
  SM_EXPECT_EQ(r.extension_profile, 0x1000);
  SM_EXPECT_EQ(*m.GetString(r, uri::kMid), std::string(40, 'x'));
  auto rt = RtpPacket::Parse(q.Serialize());
  SM_ASSERT_TRUE(rt.has_value());
  SM_EXPECT_EQ(*m.GetTransportSeq(*rt), 0x1234);
  SM_EXPECT_EQ(*m.GetRaw(*rt, uri::kVideoOrientation), B{9});
}

SM_TEST(HeaderExtensions, TypedAccessors) {
  ExtensionMap m;
  SM_EXPECT_TRUE(m.Register(1, uri::kMid));
  SM_EXPECT_TRUE(m.Register(2, uri::kTransportCc));
  SM_EXPECT_TRUE(m.Register(3, uri::kAbsSendTime));
  SM_EXPECT_FALSE(m.Register(4, uri::kMid));  // Duplicate URI.
  ExtensionAccessor a(m);
  RtpPacket p;
  a.SetString(p, uri::kMid, "video0");
  a.SetTransportSeq(p, 0xBEEF);
  a.SetAbsSendTime(p, Timestamp::Millis(1500));
  SM_EXPECT_EQ(*a.GetString(p, uri::kMid), "video0");
  SM_EXPECT_EQ(*a.GetTransportSeq(p), 0xBEEF);
  SM_EXPECT_EQ(*a.GetAbsSendTime24(p), static_cast<uint32_t>(1.5 * (1 << 18)));
  a.SetTransportSeq(p, 7);  // Replace, not duplicate.
  SM_EXPECT_EQ(ParseHeaderExtensions(p.extension_profile, p.extension)->size(), 3u);
  SM_EXPECT_FALSE(a.GetString(p, uri::kRid).has_value());
}

SM_TEST(HeaderExtensions, DemuxRules) {
  ExtensionMap m;
  m.Register(1, uri::kMid);
  m.Register(2, uri::kRid);
  ExtensionAccessor a(m);
  RtpDemuxer d(m);
  d.AddSink(1, "audio", "", {111});
  d.AddSink(2, "video", "h", {96});
  d.AddSink(3, "video", "l", {96});
  RtpPacket v;
  v.ssrc = 5;
  v.payload_type = 96;
  a.SetString(v, uri::kMid, "video");
  a.SetString(v, uri::kRid, "l");
  SM_EXPECT_EQ(*d.Demux(v), 3);
  RtpPacket v2;  // Same SSRC, extensions dropped after the first packets.
  v2.ssrc = 5;
  v2.payload_type = 96;
  SM_EXPECT_EQ(*d.Demux(v2), 3);
  RtpPacket au;  // Unknown SSRC, unique payload type.
  au.ssrc = 9;
  au.payload_type = 111;
  SM_EXPECT_EQ(*d.Demux(au), 1);
  RtpPacket amb;  // Unknown SSRC, ambiguous payload type 96.
  amb.ssrc = 77;
  amb.payload_type = 96;
  SM_EXPECT_FALSE(d.Demux(amb).has_value());
  SM_EXPECT_EQ(d.stats().unroutable, 1u);
}

namespace {
// A packet with the given SSRC, payload type and identification extensions
// (empty strings are left out).
RtpPacket DemuxPacket(const ExtensionAccessor& a, uint32_t ssrc, uint8_t pt, const std::string& mid = "",
                      const std::string& rid = "", const std::string& rrid = "") {
  RtpPacket p;
  p.ssrc = ssrc;
  p.payload_type = pt;
  if (!mid.empty()) a.SetString(p, uri::kMid, mid);
  if (!rid.empty()) a.SetString(p, uri::kRid, rid);
  if (!rrid.empty()) a.SetString(p, uri::kRepairedRid, rrid);
  return p;
}

ExtensionMap DemuxMap() {
  ExtensionMap m;
  m.Register(1, uri::kMid);
  m.Register(2, uri::kRid);
  m.Register(3, uri::kRepairedRid);
  return m;
}

RtpDemuxerCriteria Criteria(const std::string& mid, const std::string& rsid, std::vector<uint32_t> ssrcs = {},
                            std::vector<uint8_t> pts = {}) {
  RtpDemuxerCriteria c;
  c.mid = mid;
  c.rsid = rsid;
  c.ssrcs = std::move(ssrcs);
  c.payload_types = std::move(pts);
  return c;
}
}  // namespace

// RtpDemuxer::ResolveSink (call/rtp_demuxer.cc:318-428): a packet with a MID
// no sink knows is dropped, even when its SSRC is bound to a sink.
SM_TEST(RtpDemuxer, UnknownMidDroppedEvenWithBoundSsrc) {
  const ExtensionMap m = DemuxMap();
  ExtensionAccessor a(m);
  RtpDemuxer d(m);
  SM_EXPECT_TRUE(d.AddSink(0, Criteria("a", "", {5})));
  SM_EXPECT_EQ(*d.Demux(DemuxPacket(a, 5, 96)), 0);
  SM_EXPECT_FALSE(d.Demux(DemuxPacket(a, 5, 96, "zz")).has_value());
  SM_EXPECT_EQ(d.stats().unroutable, 1u);
}

// A MID learned from one packet routes the SSRC's later packets, and a sink
// added after the MID was learned gets them.
SM_TEST(RtpDemuxer, LatchedMidRoutesLaterPackets) {
  const ExtensionMap m = DemuxMap();
  ExtensionAccessor a(m);
  RtpDemuxer d(m);
  SM_EXPECT_TRUE(d.AddSink(0, Criteria("a", "", {}, {111})));
  SM_EXPECT_TRUE(d.AddSink(1, Criteria("v", "l")));
  // MID "v" is known (through its RSID sink) but the packet has no RSID: dropped,
  // and the MID is latched for SSRC 7.
  SM_EXPECT_FALSE(d.Demux(DemuxPacket(a, 7, 96, "v")).has_value());
  SM_EXPECT_TRUE(d.RemoveSink(1));
  SM_EXPECT_TRUE(d.AddSink(2, Criteria("v", "")));
  SM_EXPECT_EQ(*d.Demux(DemuxPacket(a, 7, 96)), 2);  // No MID on the packet.
  SM_EXPECT_EQ(d.stats().by_ssrc, 1u);
  SM_EXPECT_EQ(d.stats().by_mid, 0u);
  SM_EXPECT_EQ(d.GetSsrcsForSink(2).size(), 1u);
  SM_EXPECT_EQ(d.GetSsrcsForSink(2).front(), 7u);
}

// The repaired RSID wins over the RSID; an RSID without a MID routes through
// an RSID-only sink.
SM_TEST(RtpDemuxer, RsidRules) {
  const ExtensionMap m = DemuxMap();
  ExtensionAccessor a(m);
  RtpDemuxer d(m);
  SM_EXPECT_TRUE(d.AddSink(0, Criteria("v", "h")));
  SM_EXPECT_TRUE(d.AddSink(1, Criteria("v", "l")));
  SM_EXPECT_TRUE(d.AddSink(2, Criteria("", "s")));
  SM_EXPECT_EQ(*d.Demux(DemuxPacket(a, 1, 96, "v", "h", "l")), 1);
  SM_EXPECT_EQ(*d.Demux(DemuxPacket(a, 2, 96, "", "s")), 2);
  SM_EXPECT_EQ(d.stats().by_rsid, 1u);
  SM_EXPECT_FALSE(d.Demux(DemuxPacket(a, 3, 96, "v", "m")).has_value());  // A MID's unknown RSID.
}

// A signalled SSRC is trusted over a payload type; payload-type routing needs
// exactly one sink with that type and can be turned off.
SM_TEST(RtpDemuxer, SsrcBeforePayloadType) {
  const ExtensionMap m = DemuxMap();
  ExtensionAccessor a(m);
  RtpDemuxer d(m);
  SM_EXPECT_TRUE(d.AddSink(0, Criteria("", "", {10}, {96})));
  SM_EXPECT_TRUE(d.AddSink(1, Criteria("", "", {20}, {97})));
  SM_EXPECT_EQ(*d.Demux(DemuxPacket(a, 20, 96)), 1);
  SM_EXPECT_EQ(*d.Demux(DemuxPacket(a, 30, 96)), 0);  // Binds SSRC 30 to sink 0.
  SM_EXPECT_EQ(*d.Demux(DemuxPacket(a, 30, 97)), 0);
  // Signalling SSRC 30 for sink 2 replaces the learned binding.
  SM_EXPECT_TRUE(d.AddSink(2, Criteria("", "", {30})));
  SM_EXPECT_EQ(*d.Demux(DemuxPacket(a, 30, 97)), 2);
  d.set_use_payload_type_demuxing(false);
  SM_EXPECT_FALSE(d.Demux(DemuxPacket(a, 40, 97)).has_value());
}

// RtpDemuxer::CriteriaWouldConflict and RtpDemuxerCriteria::IsValid.
SM_TEST(RtpDemuxer, ConflictingAndInvalidCriteria) {
  const ExtensionMap m = DemuxMap();
  RtpDemuxer d(m);
  SM_EXPECT_FALSE(d.AddSink(0, RtpDemuxerCriteria{}));                       // Matches nothing.
  SM_EXPECT_FALSE(d.AddSink(0, Criteria(std::string(17, 'm'), "")));        // MID too long.
  SM_EXPECT_TRUE(d.AddSink(0, Criteria("v", "h", {1})));
  SM_EXPECT_FALSE(d.AddSink(1, Criteria("v", "h")));                          // Same pair.
  SM_EXPECT_FALSE(d.AddSink(1, Criteria("v", "")));                           // MID already known.
  SM_EXPECT_FALSE(d.AddSink(1, Criteria("", "", {1})));                       // SSRC signalled.
  SM_EXPECT_TRUE(d.AddSink(1, Criteria("a", "")));
  SM_EXPECT_FALSE(d.AddSink(2, Criteria("a", "h")));                          // Shadowed by MID "a".
  RtpDemuxerCriteria any;
  any.match_any = true;
  SM_EXPECT_FALSE(d.AddSink(2, any));                                         // Not empty.
  d.RemoveAllSinks();
  SM_EXPECT_TRUE(d.IsEmpty());
  SM_EXPECT_TRUE(d.AddSink(2, any));
  SM_EXPECT_FALSE(d.AddSink(3, Criteria("a", "")));                           // After match-any.
  ExtensionAccessor a(m);
  SM_EXPECT_EQ(*d.Demux(DemuxPacket(a, 9, 1, "zz")), 2);
}

// At most kMaxSsrcBindings SSRCs are bound, against a peer that sends many.
SM_TEST(RtpDemuxer, SsrcBindingsBounded) {
  const ExtensionMap m = DemuxMap();
  ExtensionAccessor a(m);
  RtpDemuxer d(m);
  SM_EXPECT_TRUE(d.AddSink(0, Criteria("", "", {}, {96})));
  for (uint32_t ssrc = 1; ssrc <= 1100; ++ssrc) SM_EXPECT_EQ(*d.Demux(DemuxPacket(a, ssrc, 96)), 0);
  SM_EXPECT_EQ(d.GetSsrcsForSink(0).size(), RtpDemuxer::kMaxSsrcBindings);
  SM_EXPECT_EQ(d.stats().ssrc_bindings, 1000u);
  SM_EXPECT_EQ(d.stats().ssrc_binding_limit, 100u);
}

// BaseRtpStringExtension::Parse: an empty value or one starting with \0 is
// not a string, and a \0 inside ends it.
SM_TEST(HeaderExtensions, StringParsedAsLibwebrtc) {
  ExtensionMap m(true);
  m.Register(1, uri::kMid);
  ExtensionAccessor a(m);
  RtpPacket p;
  a.SetRaw(p, uri::kMid, {'a', 0, 'b'});
  SM_EXPECT_EQ(*a.GetString(p, uri::kMid), "a");
  RtpPacket q;
  a.SetRaw(q, uri::kMid, {0, 'b'});
  SM_EXPECT_FALSE(a.GetString(q, uri::kMid).has_value());
}
