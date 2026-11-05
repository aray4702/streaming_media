// One session receiving several remote participants (C18, #1160): a receive stream per remote section, demultiplexed
// by its MID and SSRC (RtpDemuxer, call/rtp_demuxer.cc), each with its own receive statistics, report block and
// feedback; Call::ConfigureSync's pairing per sync group (call/call.cc:1318-1341); and receive streams destroyed while
// the session runs (Call::DestroyAudioReceiveStream / DestroyVideoReceiveStream).
#include <map>
#include <set>
#include <vector>

#include "rtp_test_payload.h"
#include "sm/rtp/session.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;
using Bytes = std::vector<uint8_t>;

namespace {

const test::TestVideoParser kParser;

class CountingDecoder : public AudioDecoder {
 public:
  std::optional<std::vector<float>> Decode(std::span<const uint8_t> frame, uint32_t) override {
    frames.emplace_back(frame.begin(), frame.end());
    return std::vector<float>(960, 0.0f);
  }
  std::vector<Bytes> frames;
};

SessionConfig Cfg(uint32_t local_ssrc, const std::string& mid_audio, const std::string& mid_video) {
  SessionConfig c;
  c.local_ssrc = local_ssrc;
  c.cname = "p" + std::to_string(local_ssrc);
  c.extensions.Register(1, sync::uri::kTransportCc);
  c.extensions.Register(3, sync::uri::kMid);
  c.mid_audio = mid_audio;
  c.mid_video = mid_video;
  return c;
}

// A remote participant: audio on 0x100 * k, video on 0x100 * k + 1 (RTX + 2), with its own MIDs.
struct Participant {
  Participant(int k, const std::string& mid_audio, const std::string& mid_video)
      : session(Cfg(static_cast<uint32_t>(1000 + k), mid_audio, mid_video)) {
    audio.ssrc = static_cast<uint32_t>(0x100 * k);
    video.ssrc = static_cast<uint32_t>(0x100 * k + 1);
    video.rtx_ssrc = static_cast<uint32_t>(0x100 * k + 2);
    video.parser = &kParser;
    audio_send = session.AddAudioSend(audio);
    video_send = session.AddVideoSend(video);
  }
  Session session;
  AudioStreamConfig audio;
  VideoStreamConfig video;
  AudioSendId audio_send;
  VideoSendId video_send;
};

// The receiving end: one audio and one video receive stream per participant, on the participant's MIDs and SSRCs,
// grouped by participant name.
struct Receiver {
  explicit Receiver(bool av_sync = true) : session([&] {
    SessionConfig c = Cfg(1, "", "");
    c.av_sync = av_sync;
    return c;
  }()) {}
  void Add(const Participant& p, const std::string& name, const std::string& mid_audio, const std::string& mid_video,
           CountingDecoder* dec) {
    AudioStreamConfig a = p.audio;
    a.mid = mid_audio;
    a.sync_group = name;
    VideoStreamConfig v = p.video;
    v.mid = mid_video;
    v.sync_group = name;
    audio.push_back(session.AddAudioReceive(a, dec));
    video.push_back(session.AddVideoReceive(v));
  }
  Session session;
  std::vector<AudioRecvId> audio;
  std::vector<VideoRecvId> video;
};

// Every participant sends one audio frame (payload tagged with its index) and a keyframe; everything goes to the
// receiver, which runs `ms` and returns its RTCP.
std::vector<Bytes> Exchange(std::vector<Participant*> ps, Receiver& r, int64_t from_ms, int64_t ms) {
  for (size_t k = 0; k < ps.size(); ++k) {
    Bytes frame(60, static_cast<uint8_t>(k + 1));
    ps[k]->session.SendAudio(ps[k]->audio_send, {static_cast<uint32_t>(960 * from_ms), frame, false},
                             Timestamp::Millis(from_ms));
    ps[k]->session.SendVideoChunks(ps[k]->video_send, test::MakeTestFrameChunks(true, 3000, 1188),
                                   static_cast<uint32_t>(90 * from_ms), Timestamp::Millis(from_ms));
  }
  std::vector<Bytes> rtcp;
  for (int64_t t = from_ms; t < from_ms + ms; ++t) {
    for (Participant* p : ps) {
      p->session.Process(Timestamp::Millis(t));
      for (auto& o : p->session.TakeOutgoing()) r.session.OnPacket(o.bytes, Timestamp::Millis(t));
    }
    r.session.Process(Timestamp::Millis(t));
    for (auto& o : r.session.TakeOutgoing()) {
      if (o.kind == OutgoingKind::kRtcp) rtcp.push_back(std::move(o.bytes));
    }
  }
  return rtcp;
}

// The SSRCs a participant's session got report blocks about from the receiver's RTCP.
std::set<uint32_t> ReportedSsrcs(Participant& p, const std::vector<Bytes>& rtcp, int64_t at_ms) {
  p.session.TakeEvents();
  for (const Bytes& b : rtcp) p.session.OnPacket(b, Timestamp::Millis(at_ms));
  std::set<uint32_t> out;
  for (const SessionEvent& e : p.session.TakeEvents()) {
    if (e.type == SessionEventType::kReportBlock) out.insert(e.ssrc);
  }
  return out;
}

}  // namespace

// Two participants' streams reach their own receive streams by MID and SSRC, and each stream has its own stats
// (with its MID) and report block.
SM_TEST(SessionMultiStream, EachRemoteStreamReachesItsOwnReceiver) {
  Participant a(1, "0", "1"), b(2, "2", "3");
  CountingDecoder dec_a, dec_b;
  Receiver r;
  r.Add(a, "A", "0", "1", &dec_a);
  r.Add(b, "B", "2", "3", &dec_b);
  // 3 s: the video reports go every second, the audio receive streams' own every 5 s (the first at 2.5 s).
  const std::vector<Bytes> rtcp = Exchange({&a, &b}, r, 0, 3000);
  SM_EXPECT_EQ(r.session.stats().unroutable, 0u);
  SM_ASSERT_EQ(dec_a.frames.size(), 1u);
  SM_ASSERT_EQ(dec_b.frames.size(), 1u);
  SM_EXPECT_EQ(dec_a.frames[0][0], 1);  // Participant A's payload, in A's decoder.
  SM_EXPECT_EQ(dec_b.frames[0][0], 2);
  for (int k = 0; k < 2; ++k) {
    SM_EXPECT_EQ(r.session.video_receiver(r.video[static_cast<size_t>(k)]).stats().media,
                 test::MakeTestFrameChunks(true, 3000, 1188).size());
  }
  StatsReport report;
  SM_ASSERT_TRUE(r.session.GetStats(Timestamp::Millis(3000), &report) == ObsStatus::kOk);
  SM_ASSERT_EQ(report.inbound.size(), 4u);
  std::map<std::string, uint32_t> ssrc_by_mid;
  for (const InboundRtpStats& in : report.inbound) {
    ssrc_by_mid[in.mid] = in.ssrc;
    SM_EXPECT_GT(in.packets_received, 0u);
  }
  SM_EXPECT_EQ(ssrc_by_mid["0"], a.audio.ssrc);
  SM_EXPECT_EQ(ssrc_by_mid["1"], a.video.ssrc);
  SM_EXPECT_EQ(ssrc_by_mid["2"], b.audio.ssrc);
  SM_EXPECT_EQ(ssrc_by_mid["3"], b.video.ssrc);
  // The receiver's reports carry a block for each participant's streams.
  const std::set<uint32_t> to_a = ReportedSsrcs(a, rtcp, 3000);
  const std::set<uint32_t> to_b = ReportedSsrcs(b, rtcp, 3000);
  SM_EXPECT_EQ(to_a.count(a.video.ssrc), 1u);
  SM_EXPECT_EQ(to_a.count(a.audio.ssrc), 1u);
  SM_EXPECT_EQ(to_b.count(b.video.ssrc), 1u);
  SM_EXPECT_EQ(to_b.count(b.audio.ssrc), 1u);
}

// Call::ConfigureSync per group: each participant's video is synced with its own audio, by its own synchronizer; a
// second video in a group is not synced; when a group's audio goes its video stops syncing.
SM_TEST(SessionMultiStream, EachParticipantIsItsOwnSyncGroup) {
  Participant a(1, "0", "1"), b(2, "2", "3"), c(3, "4", "5");
  CountingDecoder dec;
  Receiver r;
  r.Add(a, "A", "0", "1", &dec);
  r.Add(b, "B", "2", "3", &dec);
  VideoStreamConfig second = c.video;
  second.mid = "5";
  second.sync_group = "A";  // A second video in A's group.
  const VideoRecvId extra = r.session.AddVideoReceive(second);
  SM_ASSERT_TRUE(r.session.av_sync(r.video[0]) != nullptr);
  SM_ASSERT_TRUE(r.session.av_sync(r.video[1]) != nullptr);
  SM_EXPECT_TRUE(r.session.av_sync(r.video[0]) != r.session.av_sync(r.video[1]));
  SM_EXPECT_EQ(r.session.synced_audio(r.video[0])->index, r.audio[0].index);
  SM_EXPECT_EQ(r.session.synced_audio(r.video[1])->index, r.audio[1].index);
  SM_EXPECT_FALSE(r.session.synced_audio(extra).has_value());
  SM_EXPECT_TRUE(r.session.av_sync(extra) == nullptr);
  // Both synchronizers run their 1 s task once the session starts.
  Exchange({&a, &b}, r, 0, 2500);
  SM_EXPECT_TRUE(r.session.av_sync(r.video[0])->NextProcessTime().has_value());
  SM_EXPECT_TRUE(r.session.av_sync(r.video[1])->NextProcessTime().has_value());
  // A leaves the call's audio: A's video keeps playing, unsynced; B is untouched.
  SM_ASSERT_TRUE(r.session.RemoveAudioReceive(r.audio[0]));
  SM_EXPECT_FALSE(r.session.synced_audio(r.video[0]).has_value());
  SM_EXPECT_FALSE(r.session.av_sync(r.video[0])->NextProcessTime().has_value());
  SM_EXPECT_EQ(r.session.synced_audio(r.video[1])->index, r.audio[1].index);
  Exchange({&a, &b}, r, 2500, 500);  // Runs on with the pairing changed.
}

// A removed stream is gone from the demuxer, the stats and the reports; its id stays unused and calls with it do
// nothing. The other participant's streams are unaffected.
SM_TEST(SessionMultiStream, RemovedStreamsLeaveTheDemuxerStatsAndReports) {
  Participant a(1, "0", "1"), b(2, "2", "3");
  CountingDecoder dec_a, dec_b;
  Receiver r;
  r.Add(a, "A", "0", "1", &dec_a);
  r.Add(b, "B", "2", "3", &dec_b);
  Exchange({&a, &b}, r, 0, 1200);
  SM_ASSERT_TRUE(r.session.RemoveVideoReceive(r.video[1]));
  SM_ASSERT_TRUE(r.session.RemoveAudioReceive(r.audio[1]));
  SM_EXPECT_FALSE(r.session.RemoveVideoReceive(r.video[1]));  // Already gone.
  SM_EXPECT_FALSE(r.session.HasVideoReceive(r.video[1]));
  SM_EXPECT_TRUE(r.session.HasVideoReceive(r.video[0]));
  SM_EXPECT_TRUE(r.session.TakeVideoFrames(r.video[1]).empty());
  std::vector<float> pcm(480, 1.0f);
  SM_EXPECT_TRUE(r.session.GetAudio(r.audio[1], pcm, Timestamp::Millis(1200)) == timing::AudioOperation::kSilence);
  SM_EXPECT_EQ(pcm[0], 0.0f);
  const uint64_t unroutable = r.session.stats().unroutable;
  const std::vector<Bytes> rtcp = Exchange({&a, &b}, r, 1200, 2000);
  SM_EXPECT_GT(r.session.stats().unroutable, unroutable);  // B's packets now have no stream.
  SM_EXPECT_EQ(dec_b.frames.size(), 1u);
  SM_EXPECT_EQ(dec_a.frames.size(), 2u);
  StatsReport report;
  SM_ASSERT_TRUE(r.session.GetStats(Timestamp::Millis(3200), &report) == ObsStatus::kOk);
  SM_EXPECT_EQ(report.inbound.size(), 2u);
  // The reports after the removal carry no block about B's streams; A's still get theirs.
  const std::set<uint32_t> to_b = ReportedSsrcs(b, rtcp, 3200);
  SM_EXPECT_EQ(to_b.count(b.video.ssrc), 0u);
  SM_EXPECT_EQ(to_b.count(b.audio.ssrc), 0u);
  const std::set<uint32_t> to_a = ReportedSsrcs(a, rtcp, 3200);
  SM_EXPECT_EQ(to_a.count(a.video.ssrc), 1u);
  // A returning participant's stream gets a new id (ids are not reused) and receives again.
  CountingDecoder dec_c;
  AudioStreamConfig again = b.audio;
  again.mid = "2";
  again.sync_group = "B";
  const AudioRecvId c = r.session.AddAudioReceive(again, &dec_c);
  SM_EXPECT_GT(c.index, r.audio[1].index);
  Exchange({&b}, r, 3200, 300);
  SM_EXPECT_EQ(dec_c.frames.size(), 1u);
}
