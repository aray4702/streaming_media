/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/packet/sctp_packet.cc (compare/webrtc/reference/net/dcsctp/packet/sctp_packet.cc) for #1032 (C7
// part 1): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#include "sm/dcsctp/packet/sctp_packet.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/common/math.h"
#include "sm/dcsctp/packet/bounded_byte_reader.h"
#include "sm/dcsctp/packet/bounded_byte_writer.h"
#include "sm/dcsctp/packet/chunk/chunk.h"
#include "sm/dcsctp/packet/crc32c.h"
#include "sm/dcsctp/public/dcsctp_options.h"
#include "sm/dcsctp/public/types.h"
#include "sm/dcsctp/rtc_compat.h"

namespace sm::dcsctp {
namespace {
constexpr size_t kMaxUdpPacketSize = 65535;
constexpr size_t kChunkTlvHeaderSize = 4;
constexpr size_t kExpectedDescriptorCount = 4;
}  // namespace

/*
  0                   1                   2                   3
  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  |     Source Port Number        |     Destination Port Number   |
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  |                      Verification Tag                         |
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  |                           Checksum                            |
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

SctpPacket::Builder::Builder(VerificationTag verification_tag,
                             const DcSctpOptions& options)
    : verification_tag_(verification_tag),
      source_port_(options.local_port),
      dest_port_(options.remote_port),
      max_packet_size_(RoundDownTo4(options.mtu)) {}

SctpPacket::Builder& SctpPacket::Builder::Add(const Chunk& chunk) {
  if (out_.empty()) {
    out_.reserve(max_packet_size_);
    out_.resize(SctpPacket::kHeaderSize);
    BoundedByteWriter<kHeaderSize> buffer(out_);
    buffer.Store16<0>(source_port_);
    buffer.Store16<2>(dest_port_);
    buffer.Store32<4>(*verification_tag_);
    // Checksum is at offset 8 - written when calling Build(), if configured.
  }
  SM_DCSCTP_DCHECK(IsDivisibleBy4(out_.size()));

  chunk.SerializeTo(out_);
  if (out_.size() % 4 != 0) {
    out_.resize(RoundUpTo4(out_.size()));
  }

  SM_DCSCTP_DCHECK(out_.size() <= max_packet_size_)
      << "Exceeded max size, data=" << out_.size()
      << ", max_size=" << max_packet_size_;
  return *this;
}

size_t SctpPacket::Builder::bytes_remaining() const {
  if (out_.empty()) {
    // The packet header (CommonHeader) hasn't been written yet:
    return max_packet_size_ - kHeaderSize;
  } else if (out_.size() > max_packet_size_) {
    SM_DCSCTP_DCHECK_NOTREACHED() << "Exceeded max size, data=" << out_.size()
                            << ", max_size=" << max_packet_size_;
    return 0;
  }
  return max_packet_size_ - out_.size();
}

std::vector<uint8_t> SctpPacket::Builder::Build(bool write_checksum) {
  std::vector<uint8_t> out;
  out_.swap(out);

  if (!out.empty() && write_checksum) {
    uint32_t crc = GenerateCrc32C(out);
    BoundedByteWriter<kHeaderSize>(out).Store32<8>(crc);
  }

  SM_DCSCTP_DCHECK(out.size() <= max_packet_size_)
      << "Exceeded max size, data=" << out.size()
      << ", max_size=" << max_packet_size_;

  return out;
}

std::optional<SctpPacket> SctpPacket::Parse(std::span<const uint8_t> data,
                                            const DcSctpOptions& options) {
  if (data.size() < kHeaderSize + kChunkTlvHeaderSize ||
      data.size() > kMaxUdpPacketSize) {
    SM_DCSCTP_DLOG << "Invalid packet size";
    return std::nullopt;
  }

  BoundedByteReader<kHeaderSize> reader(data);

  CommonHeader common_header;
  common_header.source_port = reader.Load16<0>();
  common_header.destination_port = reader.Load16<2>();
  common_header.verification_tag = VerificationTag(reader.Load32<4>());
  common_header.checksum = reader.Load32<8>();

  // Create a copy of the packet, which will be held by this object.
  std::vector<uint8_t> data_copy =
      std::vector<uint8_t>(data.begin(), data.end());

  if (options.disable_checksum_verification ||
      (options.zero_checksum_alternate_error_detection_method !=
           ZeroChecksumAlternateErrorDetectionMethod::None() &&
       common_header.checksum == 0u)) {
    // https://www.ietf.org/archive/id/draft-tuexen-tsvwg-sctp-zero-checksum-01.html#section-4.3:
    // If an end point has sent the Zero Checksum Acceptable Chunk Parameter in
    // an INIT or INIT ACK chunk, it MUST accept SCTP packets using an incorrect
    // checksum value of zero in addition to SCTP packets containing the correct
    // CRC32c checksum value for this association.
  } else {
    // Verify the checksum. The checksum field must be zero when that's done.
    BoundedByteWriter<kHeaderSize>(data_copy).Store32<8>(0);
    uint32_t calculated_checksum = GenerateCrc32C(data_copy);
    if (calculated_checksum != common_header.checksum) {
      SM_DCSCTP_DLOG << StringFormat(
          "Invalid packet checksum, packet_checksum=0x%08x, "
          "calculated_checksum=0x%08x",
          common_header.checksum, calculated_checksum);
      return std::nullopt;
    }
    // Restore the checksum in the header.
    BoundedByteWriter<kHeaderSize>(data_copy).Store32<8>(
        common_header.checksum);
  }

  // Validate and parse the chunk headers in the message.
  /*
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |   Chunk Type  | Chunk  Flags  |        Chunk Length           |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  */

  std::vector<ChunkDescriptor> descriptors;
  descriptors.reserve(kExpectedDescriptorCount);
  std::span<const uint8_t> descriptor_data =
      std::span<const uint8_t>(data_copy).subspan(kHeaderSize);
  while (!descriptor_data.empty()) {
    if (descriptor_data.size() < kChunkTlvHeaderSize) {
      SM_DCSCTP_DLOG << "Too small chunk";
      return std::nullopt;
    }
    BoundedByteReader<kChunkTlvHeaderSize> chunk_header(descriptor_data);
    uint8_t type = chunk_header.Load8<0>();
    uint8_t flags = chunk_header.Load8<1>();
    uint16_t length = chunk_header.Load16<2>();
    uint16_t padded_length = RoundUpTo4(length);
    if (padded_length > descriptor_data.size()) {
      SM_DCSCTP_DLOG << "Too large chunk. length=" << length
                           << ", remaining=" << descriptor_data.size();
      return std::nullopt;
    } else if (padded_length < kChunkTlvHeaderSize) {
      SM_DCSCTP_DLOG << "Too small chunk. length=" << length;
      return std::nullopt;
    }
    descriptors.emplace_back(type, flags,
                             descriptor_data.subspan(0, padded_length));
    descriptor_data = descriptor_data.subspan(padded_length);
  }

  // Note that iterators (and pointer) are guaranteed to be stable when moving a
  // std::vector, and `descriptors` have pointers to within `data_copy`.
  return SctpPacket(common_header, std::move(data_copy),
                    std::move(descriptors));
}
}  // namespace sm::dcsctp
