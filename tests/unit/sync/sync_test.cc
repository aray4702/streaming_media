// Section 8 unit tests.
#include <cmath>
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
  Timestamp local_send = Timestamp::Seconds(100);
  NtpTime sr = NtpTime::FromTimestamp(local_send + TimeDelta::Millis(2500));
  le.OnSenderReport(sr, local_send + TimeDelta::Millis(40), TimeDelta::Millis(80));
  SM_EXPECT_NEAR(le.remote_to_local_offset()->ms_f(), 2500, 0.1);
  // Frame captured at local 100.2 s (remote 102.7 s), rendered at 100.35 s.
  AbsoluteCaptureTime cap{NtpTime::FromTimestamp(Timestamp::Millis(102700)).value, std::nullopt};
  auto lat = le.OnRendered(cap, Timestamp::Millis(100350));
  SM_ASSERT_TRUE(lat.has_value());
  SM_EXPECT_NEAR(lat->ms_f(), 150, 0.5);
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
  SM_EXPECT_FALSE(ParseHeaderExtensions(0xBEDE, std::vector<uint8_t>{0x15, 1}).has_value());  // Truncated.
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
