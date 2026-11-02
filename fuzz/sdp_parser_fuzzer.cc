// SDP (#1062, requirements C6 and C10): what libwebrtc's test/fuzzers/sdp_parser_fuzzer.cc feeds
// (compare/webrtc/reference/test/fuzzers/), CreateSessionDescription(kOffer, input) on inputs up to 16384 bytes, plus
// a round trip that libwebrtc's target doesn't check: a description that parsed is written (SdpSerialize), and what
// is written must parse again and be written the same way, or the target aborts. The first write may differ from
// the input (ignored lines, defaults filled in, candidates moving the c= line); from then on it must be stable.
// libwebrtc takes the string from the input with FuzzDataHelper::ReadString; here the whole input is the SDP, so the
// corpus is plain SDP files (tools/sdp_fuzz_corpus.py).
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

#include "sm/pc/jsep.h"
#include "sm/pc/webrtc_sdp.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > 16384) return 0;
  const std::string_view message(reinterpret_cast<const char*>(data), size);
  sm::pc::SdpParseError error;
  std::unique_ptr<sm::pc::SessionDescriptionInterface> sdp =
      sm::pc::CreateSessionDescription(sm::pc::SdpType::kOffer, message, &error);
  if (!sdp) return 0;
  const std::string written = sm::pc::SdpSerialize(*sdp);
  std::unique_ptr<sm::pc::SessionDescriptionInterface> again =
      sm::pc::CreateSessionDescription(sm::pc::SdpType::kOffer, written, &error);
  if (!again) std::abort();
  if (sm::pc::SdpSerialize(*again) != written) std::abort();
  return 0;
}
