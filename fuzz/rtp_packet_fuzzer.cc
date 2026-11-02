// RTP packet parsing: the fixed header, CSRCs, padding and the RFC 8285
// extension block, as libwebrtc's test/fuzzers/rtp_packet_fuzzer.cc feeds
// RtpPacketReceived::Parse and then reads every extension. A packet that
// parses must serialize and parse again to the same fields.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

#include "sm/core/rtp_packet.h"
#include "sm/sync/header_extensions.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  auto p = sm::RtpPacket::Parse(std::span<const uint8_t>(data, size));
  if (!p) return 0;
  if (!p->extension.empty() || p->extension_profile != 0) {
    (void)sm::sync::ParseHeaderExtensions(p->extension_profile, p->extension);
  }
  const auto bytes = p->Serialize();
  auto q = sm::RtpPacket::Parse(bytes);
  if (!q || q->seq != p->seq || q->timestamp != p->timestamp || q->ssrc != p->ssrc ||
      q->payload_type != p->payload_type || q->marker != p->marker || q->csrcs != p->csrcs ||
      q->payload != p->payload || q->padding_size != p->padding_size) {
    std::abort();
  }
  return 0;
}
