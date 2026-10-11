// Section 8 unit tests.
#include <cmath>

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
  SM_EXPECT_TRUE(e.UpdateMeasurements(Ntp(4000.0), rtp0 + 4 * 90000) == RtpToNtpEstimator::UpdateResult::kSameMeasurement);
}

SM_TEST(RtpToNtp, RejectsOutliersThenResets) {
  RtpToNtpEstimator e;
  for (int i = 0; i < 5; ++i) e.UpdateMeasurements(Ntp(i * 1000.0), static_cast<uint32_t>(i * 48000));
  // An SR whose NTP is 200 ms off is rejected.
  SM_EXPECT_TRUE(e.UpdateMeasurements(Ntp(5200.0), 5 * 48000) == RtpToNtpEstimator::UpdateResult::kRejected);
  SM_EXPECT_NEAR(MsOf(*e.Estimate(6 * 48000)), 6000, 0.01);
  e.UpdateMeasurements(Ntp(6000.0), 6 * 48000);  // Good again: counter cleared.
  // A persistent jump (sender restarted its RTP clock) resets the history
  // after three consecutive rejections.
  e.UpdateMeasurements(Ntp(6500.0), 1000000);
  e.UpdateMeasurements(Ntp(7000.0), 1048000);
  auto r = e.UpdateMeasurements(Ntp(8000.0), 1096000);
  SM_EXPECT_TRUE(r == RtpToNtpEstimator::UpdateResult::kReset);
  e.UpdateMeasurements(Ntp(9000.0), 1144000);
  SM_EXPECT_NEAR(MsOf(*e.Estimate(1168000)), 9500, 0.01);
}

SM_TEST(StreamSync, ConvergesAndPrefersRemovingDelay) {
  StreamSynchronization s;
  // Same capture clock; video arrives 100 ms later than audio for the same
  // capture instant, buffers equal: video lags by 100 ms.
  for (int i = 0; i < 5; ++i) {
    s.audio_map().UpdateMeasurements(Ntp(i * 1000.0), static_cast<uint32_t>(i * 48000));
    s.video_map().UpdateMeasurements(Ntp(i * 1000.0), static_cast<uint32_t>(i * 90000));
  }
  s.OnAudioPacket(4 * 48000, Timestamp::Millis(4050));
  s.OnVideoFrame(4 * 90000, Timestamp::Millis(4150));
  SyncDelays d;
  for (int k = 0; k < 20; ++k) d = *s.Update(TimeDelta::Millis(60), TimeDelta::Millis(60), Timestamp::Millis(5000 + k * 1000));
  SM_EXPECT_NEAR(d.audio_extra.ms_f(), 100, 30);  // Audio delayed to match.
  SM_EXPECT_EQ(d.video_extra.ms(), 0);
  SM_EXPECT_LT(std::fabs(s.last_offset().ms_f()), 35);
  // Now video becomes early: audio extra delay is removed before any video delay is added.
  s.OnVideoFrame(4 * 90000, Timestamp::Millis(4000));
  for (int k = 0; k < 20; ++k) d = *s.Update(TimeDelta::Millis(60), TimeDelta::Millis(60), Timestamp::Millis(30000 + k * 1000));
  SM_EXPECT_LT(d.audio_extra.ms(), 30);
  SM_EXPECT_NEAR(d.video_extra.ms_f(), 50, 30);
}

SM_TEST(StreamSync, StepIsBounded) {
  StreamSyncConfig cfg;
  cfg.max_change = TimeDelta::Millis(80);
  StreamSynchronization s(cfg);
  for (int i = 0; i < 3; ++i) {
    s.audio_map().UpdateMeasurements(Ntp(i * 1000.0), static_cast<uint32_t>(i * 48000));
    s.video_map().UpdateMeasurements(Ntp(i * 1000.0), static_cast<uint32_t>(i * 90000));
  }
  s.OnAudioPacket(2 * 48000, Timestamp::Millis(2000));
  s.OnVideoFrame(2 * 90000, Timestamp::Millis(3000));  // 1 s lag.
  auto d = s.Update(TimeDelta::Zero(), TimeDelta::Zero(), Timestamp::Millis(3000));
  SM_EXPECT_EQ(d->audio_extra.ms(), 80);
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
