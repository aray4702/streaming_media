// Transport-wide congestion control feedback (one RTCP packet): the parser,
// the arrival times it derives, and a round trip through the serializer, as
// libwebrtc's test/fuzzers/transport_feedback_packet_loss_tracker_fuzzer.cc
// and rtcp_receiver_fuzzer.cc reach rtcp::TransportFeedback::Parse.
#include <cstddef>
#include <cstdint>
#include <span>

#include "sm/cc/transport_feedback.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  auto fb = sm::cc::ParseTransportFeedback(std::span<const uint8_t>(data, size));
  if (!fb) return 0;
  (void)fb->ArrivalTimes();
  const auto bytes = sm::cc::SerializeTransportFeedback(*fb);
  if (!bytes.empty()) (void)sm::cc::ParseTransportFeedback(bytes);
  return 0;
}
