#include "sm/core/event_log.h"
#include "sm/loss/keyframe_request.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::loss;

namespace {
Timestamp Ms(int64_t ms) { return Timestamp::Millis(ms); }
FrameInfo Key(int64_t id) { return FrameInfo{id, true, {}}; }
FrameInfo Delta(int64_t id, int64_t ref) { return FrameInfo{id, false, {ref}}; }
size_t CountType(const std::vector<FeedbackMessage>& v, FeedbackType t) {
  size_t n = 0;
  for (auto& m : v) n += m.type == t;
  return n;
}
}  // namespace

SM_TEST(KeyframeRequest, InOrderFramesAreDecodable) {
  KeyframeRequester k;
  k.OnFrame(Key(0), Ms(0));
  k.OnFrame(Delta(1, 0), Ms(33));
  k.OnFrame(Delta(2, 1), Ms(66));
  auto d = k.TakeDecodableFrames();
  SM_EXPECT_EQ(d.size(), 3u);
  SM_EXPECT_TRUE(k.Process(Ms(100)).empty());
}

SM_TEST(KeyframeRequest, LateReferenceUnblocksWithoutPli) {
  KeyframeRequestConfig cfg;
  cfg.send_lntf = true;  // libwebrtc's `lntf.enabled` (off by default since #580).
  KeyframeRequester k(cfg);
  k.OnFrame(Key(0), Ms(0));
  k.OnFrame(Delta(2, 1), Ms(66));  // 1 is late (being NACKed).
  auto fb = k.Process(Ms(70));
  SM_EXPECT_EQ(CountType(fb, FeedbackType::kLntf), 1u);
  SM_EXPECT_EQ(CountType(fb, FeedbackType::kPli), 0u);
  k.OnFrame(Delta(1, 0), Ms(120));
  auto d = k.TakeDecodableFrames();
  SM_ASSERT_EQ(d.size(), 3u);
  SM_EXPECT_EQ(d[1], 1);
  SM_EXPECT_EQ(d[2], 2);
  SM_EXPECT_EQ(CountType(k.Process(Ms(500)), FeedbackType::kPli), 0u);
}

SM_TEST(KeyframeRequest, DecoderErrorAndStreamStart) {
  EventLog log;
  KeyframeRequestConfig cfg;
  cfg.events = &log;
  KeyframeRequester k(cfg);
  k.OnStreamStart(Ms(0));
  auto fb = k.Process(Ms(0));
  SM_ASSERT_EQ(fb.size(), 1u);
  SM_EXPECT_TRUE(fb[0].type == FeedbackType::kFir);
  SM_EXPECT_EQ(fb[0].fir_seq, 1);
  k.OnFrame(Key(0), Ms(50));
  k.OnDecoderError(Ms(1000));
  fb = k.Process(Ms(1000));
  SM_EXPECT_EQ(CountType(fb, FeedbackType::kPli), 1u);
  SM_EXPECT_EQ(log.Count("keyframe", "pli"), 1u);
  SM_EXPECT_EQ(log.Count("keyframe", "fir"), 1u);
}

SM_TEST(KeyframeRequest, BlockedFrameIsLeftToNackWhilePacketsKeepArriving) {
  // No repair window and no retry timer: a blocked frame alone asks for nothing.
  EventLog log;
  KeyframeRequestConfig cfg;
  cfg.events = &log;
  KeyframeRequester k(cfg);
  k.OnFrame(Key(0), Ms(0));
  k.OnFrameReleased(Ms(0));  // The frame wait (3 s) applies from here, not the 200 ms keyframe wait.
  k.OnFrame(Delta(2, 1), Ms(66));
  k.OnFrameLost(1, Ms(70));
  for (int64_t t = 100; t <= 2900; t += 100) {
    k.OnMediaPacket(Ms(t), false);
    SM_EXPECT_EQ(CountType(k.Process(Ms(t)), FeedbackType::kPli), 0u);
  }
  SM_EXPECT_EQ(k.stats().pli_sent, 0u);
}

SM_TEST(KeyframeRequest, DecodeTimeoutAsksOnceAndRearms) {
  EventLog log;
  KeyframeRequestConfig cfg;
  cfg.events = &log;
  KeyframeRequester k(cfg);
  k.OnFrame(Key(0), Ms(0));
  k.OnFrameReleased(Ms(0));
  k.OnFrame(Delta(2, 1), Ms(66));  // Blocked: frame 1 never comes.
  k.OnMediaPacket(Ms(66), false);
  SM_EXPECT_EQ(CountType(k.Process(Ms(10)), FeedbackType::kPli), 0u);  // Only the LNTF for the blocked frame.
  size_t asked = 0;
  for (int64_t t = 100; t < 3000; t += 100) {
    k.OnMediaPacket(Ms(t), false);
    asked += CountType(k.Process(Ms(t)), FeedbackType::kPli);
  }
  SM_EXPECT_EQ(asked, 0u);  // 3 s since the last released frame (t = 0) has not passed.
  k.OnMediaPacket(Ms(3000), false);
  SM_EXPECT_EQ(CountType(k.Process(Ms(3000)), FeedbackType::kPli), 1u);
  SM_EXPECT_EQ(log.Count("keyframe", "trigger_decode_timeout"), 1u);
  k.OnMediaPacket(Ms(3100), false);
  SM_EXPECT_EQ(CountType(k.Process(Ms(3100)), FeedbackType::kPli), 0u);
  k.OnMediaPacket(Ms(6000), false);
  SM_EXPECT_EQ(CountType(k.Process(Ms(6000)), FeedbackType::kPli), 1u);  // Re-armed.
}

SM_TEST(KeyframeRequest, DecodeTimeoutRunsFromReleaseNotFromDecodable) {
  // libwebrtc re-arms the time-out when the frame scheduler releases a frame for decoding
  // (VideoStreamBufferController::OnFrameReady -> VideoReceiveStreamTimeoutTracker::OnEncodedFrameReleased),
  // after its wait for the render time, not when the frame becomes decodable (issue #151).
  KeyframeRequester k;
  SM_EXPECT_TRUE(k.Process(Ms(0)).empty());
  k.OnFrame(Key(0), Ms(0));         // Decodable at 0 ...
  k.OnFrameReleased(Ms(60));         // ... released for decoding at 60.
  k.OnFrame(Delta(2, 1), Ms(66));    // Blocked: frame 1 never comes.
  k.OnMediaPacket(Ms(3000), false);
  SM_EXPECT_EQ(CountType(k.Process(Ms(3000)), FeedbackType::kPli), 0u);
  k.OnMediaPacket(Ms(3059), false);
  SM_EXPECT_EQ(CountType(k.Process(Ms(3059)), FeedbackType::kPli), 0u);
  k.OnMediaPacket(Ms(3060), false);
  SM_EXPECT_EQ(CountType(k.Process(Ms(3060)), FeedbackType::kPli), 1u);
}

SM_TEST(KeyframeRequest, KeyframeWaitUntilTheFirstRelease) {
  // OnEncodedFrameReleased ends the keyframe wait (max_wait_for_keyframe, 200 ms): until the first frame is
  // released the shorter wait applies, even if a frame is already decodable.
  KeyframeRequester k;
  SM_EXPECT_TRUE(k.Process(Ms(0)).empty());
  k.OnFrame(Key(0), Ms(10));
  k.TakeDecodableFrames();
  k.OnMediaPacket(Ms(300), false);
  SM_EXPECT_EQ(CountType(k.Process(Ms(300)), FeedbackType::kPli), 1u);
  KeyframeRequester k2;
  SM_EXPECT_TRUE(k2.Process(Ms(0)).empty());
  k2.OnFrame(Key(0), Ms(10));
  k2.OnFrameReleased(Ms(20));
  k2.OnMediaPacket(Ms(300), false);
  SM_EXPECT_EQ(CountType(k2.Process(Ms(300)), FeedbackType::kPli), 0u);
}

SM_TEST(KeyframeRequest, NoTimeoutRequestWhileAKeyframeArrivesOrWhenIdle) {
  KeyframeRequester k;
  k.OnFrame(Key(0), Ms(0));
  k.OnFrame(Delta(2, 1), Ms(66));
  k.OnMediaPacket(Ms(2900), true);  // A keyframe is on its way.
  SM_EXPECT_EQ(CountType(k.Process(Ms(3000)), FeedbackType::kPli), 0u);
  // No packet for 5 s: the stream is idle, no request spam.
  SM_EXPECT_EQ(CountType(k.Process(Ms(9000)), FeedbackType::kPli), 0u);
}

SM_TEST(KeyframeRequest, FirstFrameNotKeyframeAsksAndNoRequestAtStart) {
  EventLog log;
  KeyframeRequestConfig cfg;
  cfg.events = &log;
  KeyframeRequester k(cfg);
  k.OnFrame(Delta(5, 4), Ms(0));
  SM_EXPECT_EQ(CountType(k.Process(Ms(10)), FeedbackType::kPli), 1u);
  SM_EXPECT_EQ(log.Count("keyframe", "trigger_first_frame_delta"), 1u);
  KeyframeRequester k2;
  k2.OnFrame(Key(0), Ms(0));
  SM_EXPECT_TRUE(k2.Process(Ms(10)).empty());
}

SM_TEST(KeyframeRequest, ExternalAndDecoderTriggersMakeOneRequest) {
  KeyframeRequester k;
  k.OnFrame(Key(0), Ms(0));
  k.OnExternalRequest(Ms(10), "trigger_nack_list_full");
  k.OnDecoderError(Ms(10));
  SM_EXPECT_EQ(CountType(k.Process(Ms(11)), FeedbackType::kPli), 1u);
  SM_EXPECT_EQ(CountType(k.Process(Ms(12)), FeedbackType::kPli), 0u);
}

SM_TEST(KeyframeRequest, NoRequestWhileALongKeyframeIsArriving) {
  // A keyframe that takes longer than max_wait_for_keyframe (200 ms) to arrive
  // is not asked for again while its packets keep coming: every packet of it
  // counts as a keyframe packet, by its frame type or by the keyframe's RTP
  // timestamp (RtpVideoStreamReceiver2::UpdatePacketReceiveTimestamps). In
  // issue #335 a 1 fps call asked again at 400 ms, when only the first packet
  // of the first keyframe counted.
  KeyframeRequester k;
  SM_EXPECT_TRUE(k.Process(Ms(0)).empty());
  k.OnMediaPacket(Ms(10), true, 1000u);
  size_t asked = 0;
  for (int64_t t = 50; t <= 900; t += 50) {
    // Fragments of the same keyframe: key by frame type, then only by RTP timestamp.
    k.OnMediaPacket(Ms(t), t < 300, 1000u);
    asked += CountType(k.Process(Ms(t)), FeedbackType::kPli);
  }
  SM_EXPECT_EQ(asked, 0u);
  // Delta packets of another frame don't count: 200 ms later the wait fires.
  k.OnMediaPacket(Ms(950), false, 4000u);
  k.OnMediaPacket(Ms(1150), false, 4000u);
  SM_EXPECT_EQ(CountType(k.Process(Ms(1150)), FeedbackType::kPli), 1u);
}
