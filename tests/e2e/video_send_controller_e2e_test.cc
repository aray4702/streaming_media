// VideoSendController over sim::NetworkLink (issue #37): an encoder that sizes
// its frames from the controller's target, through a bottleneck that drops 5x.
// The app holds no frame dropper, keyframe or pause state of its own.
#include <algorithm>
#include <vector>

#include "rtp_test_payload.h"
#include "sm/rtp/session.h"
#include "sm/rtp/video_send_controller.h"
#include "sm/sim/network_link.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;

namespace {
SessionConfig MakeConfig(uint32_t ssrc) {
  SessionConfig c;
  c.local_ssrc = ssrc;
  c.cname = "t";
  c.extensions.Register(1, sync::uri::kTransportCc);
  c.extensions.Register(2, sync::uri::kAbsSendTime);
  c.send_side.gcc.start_rate = DataRate::KilobitsPerSec(2000);
  return c;
}
}  // namespace

SM_TEST(E2E_VideoSendController, EncoderFollowsTheTargetAfterACapacityDrop) {
  const test::TestVideoParser parser;
  Session a(MakeConfig(10)), b(MakeConfig(20));
  VideoStreamConfig v;
  v.ssrc = 2;
  v.rtx_ssrc = 3;
  v.parser = &parser;
  auto send = a.AddVideoSend(v);
  auto recv = b.AddVideoReceive(v);
  (void)b.AddVideoSend(v);
  VideoSendController ctl({}, a);

  sim::LinkConfig lc;
  lc.base_delay = TimeDelta::Millis(30);
  lc.capacity = DataRate::KilobitsPerSec(2500);
  lc.queue_limit = DataSize::Bytes(25000);
  sim::NetworkLink<std::vector<uint8_t>> ab(lc, 1), ba(lc, 2);

  int64_t encoded = 0, dropped = 0, keyframes = 0, frames_received = 0;
  int64_t bytes_before = 0, bytes_after = 0;
  DataRate last_target;
  for (int64_t t = 0; t < 14000; ++t) {
    const Timestamp now = Timestamp::Millis(t);
    if (t == 8000) {
      lc.capacity = DataRate::KilobitsPerSec(500);
      ab.SetConfig(lc);
    }
    if (t % 33 == 0) {
      const EncodeDecision d = ctl.OnCapturedFrame({640, 360}, now);
      last_target = d.target;
      if (d.encode) {
        const double per_frame = static_cast<double>(d.target.bps()) / 8 / 30;
        const size_t bytes = static_cast<size_t>(d.keyframe ? per_frame * 4 : per_frame);
        auto chunks = test::MakeTestFrameChunks(d.keyframe, bytes, 1188);
        a.SendVideoChunks(send, chunks, static_cast<uint32_t>(t * 90), now);
        ctl.OnEncoded({DataSize::Bytes(static_cast<int64_t>(bytes)), d.keyframe}, now);
        ++encoded;
        keyframes += d.keyframe ? 1 : 0;
        (t < 8000 ? bytes_before : bytes_after) += static_cast<int64_t>(bytes);
      } else {
        ++dropped;
      }
    }
    a.Process(now);
    b.Process(now);
    for (auto& p : a.TakeOutgoing()) ab.Send(now, std::move(p.bytes), p.wire_size);
    for (auto& p : b.TakeOutgoing()) ba.Send(now, std::move(p.bytes), p.wire_size);
    ab.ReceiveEach(now, [&](std::vector<uint8_t> bytes, Timestamp at) { b.OnPacket(bytes, at); });
    ba.ReceiveEach(now, [&](std::vector<uint8_t> bytes, Timestamp at) { a.OnPacket(bytes, at); });
    frames_received += static_cast<int64_t>(b.TakeVideoFrames(recv).size());
    for (const auto& e : a.TakeEvents()) ctl.OnSessionEvent(e);
    b.TakeEvents();
  }
  const double kbps_before = static_cast<double>(bytes_before) * 8 / 8000.0;
  const double kbps_after = static_cast<double>(bytes_after) * 8 / 6000.0;
  SM_RECORD("encoded", static_cast<double>(encoded));
  SM_RECORD("dropped", static_cast<double>(dropped));
  SM_RECORD("keyframes", static_cast<double>(keyframes));
  SM_RECORD("kbps_before", kbps_before);
  SM_RECORD("kbps_after", kbps_after);
  SM_RECORD("last_target_kbps", last_target.kbps());
  SM_EXPECT_GE(keyframes, 1);
  SM_EXPECT_GT(frames_received, 0);
  SM_EXPECT_LT(kbps_after, kbps_before);
  // The encoder ends near the new capacity, not the old one.
  SM_EXPECT_LT(last_target.kbps(), 1000.0);
}
