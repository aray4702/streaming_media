// `av_sync off` only takes the streams out of the receiver's sync group
// (issue #524), as sm_compare's libwebrtc side leaves `sync_group` empty
// (compare/webrtc/webrtc_call.cc) while libwebrtc's audio send channel keeps
// RtcpMode::kCompound (compare/webrtc/reference/audio/channel_send.cc:551)
// and sends its SR. Our sender keeps its audio RTCP too, so the sender's
// RTCP on the forward link is the same with sync on and off.
#include <cstdint>
#include <string>
#include <vector>

#include "call.h"
#include "scenario.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::testbench;

namespace {

// Send times of the sender's RTCP packets on the forward link of an audio +
// video call on a clean link (no cross traffic, so every untyped packet is RTCP).
std::vector<int64_t> SenderRtcpTimes(bool av_sync) {
  auto parsed = ParseScenario(std::string("name av_sync_rtcp\nduration 12s\nav_sync ") + (av_sync ? "on" : "off") +
                              "\nat 0s capacity=2500kbps delay=25ms queue=300ms\n");
  SM_EXPECT_TRUE(parsed.scenario.has_value());
  std::vector<int64_t> t;
  if (!parsed.scenario) return t;
  RunOptions opt;
  opt.link_trace = true;
  const Report rep = RunCall(*parsed.scenario, opt);
  for (const auto& r : rep.link_trace)
    if (r.kind == sim::LinkKind::kOther) t.push_back(r.time_us);
  return t;
}

}  // namespace

SM_TEST(TestbenchAvSyncRtcp, SyncOffKeepsTheSendersAudioRtcp) {
  const auto on = SenderRtcpTimes(true);
  const auto off = SenderRtcpTimes(false);
  // Video reports about every second and the audio SR about every 5 s over 12 s.
  SM_EXPECT_TRUE(on.size() >= 12);
  SM_EXPECT_EQ(off.size(), on.size());
  SM_EXPECT_TRUE(off == on);
}
