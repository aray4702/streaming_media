// SCTP packets for data channels (#1032, requirements C7 part 1 and C10):
// dcsctp's packet layer (sm/dcsctp/packet/, libwebrtc M157's
// net/dcsctp/packet/) on network input.
//
// libwebrtc fuzzes dcsctp through the socket (net/dcsctp/fuzzers/
// dcsctp_fuzzers.cc's FuzzSocket, run by test/fuzzers/dcsctp_socket_fuzzer.cc),
// which needs DcSctpSocket (C7 part 2). That fuzzer's packets go through
// SctpPacket::Parse and then one chunk parser per descriptor, as the socket's
// dispatch does; this target feeds the same parsers without the socket:
//
// - SctpPacket::Parse on the input twice: with checksum verification off (so
//   mutated inputs reach the chunk loop, as the socket fuzzer sets
//   disable_checksum_verification), and with the default options, which
//   check the CRC-32C.
// - For every chunk descriptor, DebugConvertChunkToString (chunk.cc), which
//   runs the chunk's own Parse and ToString for each of the 17 chunk types
//   (and with them the parameter and error-cause parsers that INIT, INIT_ACK,
//   RE-CONFIG, ABORT and ERROR carry).
// - A SACK goes through SackChunk::Parse and ChunkValidators::Clean, as the
//   socket does with every SACK it receives, and the cleaned SACK must still
//   validate.
// - The whole input as a parameter list (Parameters::Parse) and, if it
//   parses, ErrorCausesToString, as an ABORT's or ERROR's causes.
// - Every chunk descriptor is added to a SctpPacket::Builder; the built
//   packet must parse again (with its checksum) into as many chunks.
//
// The corpus (fuzz/corpus/dcsctp_packet/) holds the packets of
// net/dcsctp/packet/sctp_packet_test.cc.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sm/dcsctp/packet/chunk/chunk.h"
#include "sm/dcsctp/packet/chunk/sack_chunk.h"
#include "sm/dcsctp/packet/chunk_validators.h"
#include "sm/dcsctp/packet/error_cause/error_cause.h"
#include "sm/dcsctp/packet/parameter/parameter.h"
#include "sm/dcsctp/packet/sctp_packet.h"
#include "sm/dcsctp/public/dcsctp_options.h"

namespace {

using sm::dcsctp::ChunkValidators;
using sm::dcsctp::DcSctpOptions;
using sm::dcsctp::SackChunk;
using sm::dcsctp::SctpPacket;

// A chunk the builder can add again: it serializes the raw bytes it parsed.
class RawChunk : public sm::dcsctp::Chunk {
 public:
  explicit RawChunk(std::span<const uint8_t> data) : data_(data.begin(), data.end()) {}
  void SerializeTo(std::vector<uint8_t>& out) const override {
    out.insert(out.end(), data_.begin(), data_.end());
  }
  size_t size() const { return data_.size(); }
  std::string ToString() const override { return "RAW"; }

 private:
  std::vector<uint8_t> data_;
};

void Check(bool condition) {
  if (!condition) std::abort();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::span<const uint8_t> input(data, size);

  DcSctpOptions unchecked;
  unchecked.disable_checksum_verification = true;
  std::optional<SctpPacket> packet = SctpPacket::Parse(input, unchecked);
  (void)SctpPacket::Parse(input, DcSctpOptions());

  if (packet.has_value()) {
    SctpPacket::Builder builder(packet->common_header().verification_tag, DcSctpOptions());
    size_t added = 0;
    std::vector<RawChunk> chunks;
    for (const SctpPacket::ChunkDescriptor& d : packet->descriptors()) {
      (void)sm::dcsctp::DebugConvertChunkToString(d.data);
      if (d.type == SackChunk::kType) {
        std::optional<SackChunk> sack = SackChunk::Parse(d.data);
        if (sack.has_value()) {
          SackChunk clean = ChunkValidators::Clean(*std::move(sack));
          Check(ChunkValidators::Validate(clean));
        }
      }
      chunks.emplace_back(d.data);
    }
    for (const RawChunk& c : chunks) {
      // A descriptor's bytes are already padded to a multiple of four.
      if (c.size() > builder.bytes_remaining()) break;
      builder.Add(c);
      ++added;
    }
    if (added > 0) {
      std::vector<uint8_t> built = builder.Build();
      std::optional<SctpPacket> again = SctpPacket::Parse(built, DcSctpOptions());
      Check(again.has_value());
      Check(again->descriptors().size() == added);
    }
  }

  std::optional<sm::dcsctp::Parameters> params = sm::dcsctp::Parameters::Parse(input);
  if (params.has_value()) (void)sm::dcsctp::ErrorCausesToString(*params);
  return 0;
}
