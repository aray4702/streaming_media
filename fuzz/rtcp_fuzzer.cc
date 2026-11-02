// Compound RTCP: the splitter, every block parser the library has, and the
// session's RtcpEngine::OnRtcp, which dispatches the blocks, as libwebrtc's
// test/fuzzers/rtcp_receiver_fuzzer.cc feeds RTCPReceiver::IncomingPacket.
#include <cstddef>
#include <cstdint>
#include <span>

#include "sm/cc/transport_feedback.h"
#include "sm/loss/rtcp_feedback.h"
#include "sm/rtp/rtcp_engine.h"
#include "sm/rtp/rtcp_sdes_bye.h"
#include "sm/timing/rtcp_stats.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::span<const uint8_t> in(data, size);
  if (auto packets = sm::loss::SplitCompoundRtcp(in)) {
    for (const auto& v : *packets) {
      // Every parser on every block, not only the one its type selects, so a
      // parser's own type and length checks are exercised too.
      (void)sm::timing::ParseSenderReport(v.data);
      (void)sm::timing::ParseReceiverReport(v.data);
      (void)sm::timing::ParseExtendedReports(v.data);
      (void)sm::loss::ParseNack(v.data);
      (void)sm::loss::ParsePli(v.data);
      (void)sm::loss::ParseFir(v.data);
      (void)sm::loss::ParseLossNotification(v.data);
      (void)sm::cc::ParseTransportFeedback(v.data);
      (void)sm::cc::ParseRemb(v.data);
      (void)sm::rtp::ParseSdes(v.data);
      (void)sm::rtp::ParseBye(v.data);
    }
  }
  sm::rtp::RtcpEngine engine;
  engine.AddLocalMediaSsrc(0x11111111);
  (void)engine.OnRtcp(in, sm::Timestamp::Millis(10'000));
  // A second datagram on the same engine reaches the state the first left
  // (stored RRTRs, FIR sequence numbers).
  (void)engine.OnRtcp(in, sm::Timestamp::Millis(11'000));
  return 0;
}
