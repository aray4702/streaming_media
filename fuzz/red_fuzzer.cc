// RED payloads (RFC 2198): the parser and NetEq's split, as libwebrtc's
// test/fuzzers/neteq_rtp_fuzzer.cc reaches RedPayloadSplitter::SplitRed
// through NetEq::InsertPacket. The first byte picks the RED payload type and
// the rest is the payload.
#include <cstddef>
#include <cstdint>
#include <span>

#include "fuzz_data_reader.h"
#include "sm/loss/red.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  sm::fuzz::FuzzDataReader r(std::span<const uint8_t>(data, size));
  const uint8_t red_pt = r.ReadU8() & 0x7f;
  const uint32_t ts = r.ReadU32();
  const auto payload = r.ReadBytes(r.BytesLeft());
  (void)sm::loss::ParseRed(payload, ts);
  (void)sm::loss::SplitRed(payload, ts, red_pt);
  return 0;
}
