// ULPFEC over RED (RFC 5109): a stream of length-prefixed RTP packets of one
// SSRC goes through UlpfecReceiver, which unwraps RED and hands the FEC
// packets to ForwardErrorCorrection::DecodeFec, as libwebrtc's
// test/fuzzers/ulpfec_receiver_fuzzer.cc and ulpfec_decoder_fuzzer.cc do.
// Bytes of the SSRC and payload types are rewritten so most packets reach the
// decoder instead of being dropped by the SSRC check.
#include <cstddef>
#include <cstdint>
#include <span>

#include "fuzz_data_reader.h"
#include "sm/core/rtp_packet.h"
#include "sm/loss/ulpfec.h"

namespace {
constexpr uint32_t kSsrc = 0x22222222;
constexpr int kUlpfecPt = 119;  // sm::loss::ulpfec defaults (red 118, ulpfec 119), as the test bench sends.
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  sm::fuzz::FuzzDataReader r(std::span<const uint8_t>(data, size));
  sm::loss::ulpfec::UlpfecReceiver receiver(kSsrc, kUlpfecPt);
  size_t packets = 0;
  while (r.BytesLeft() > 0 && packets++ < 1024) {
    const uint8_t mode = r.ReadU8();
    auto p = sm::RtpPacket::Parse(r.ReadLengthPrefixed());
    if (!p) continue;
    if ((mode & 1) == 0) p->ssrc = kSsrc;
    (void)receiver.AddReceivedRedPacket(*p, (mode & 2) != 0);
    for (auto& out : receiver.ProcessReceivedFec()) (void)out.Serialize();
  }
  return 0;
}
