// 3.2 ULPFEC (RFC 5109) over RED (RFC 2198), as libwebrtc M157.
//
// A port of libwebrtc's ULPFEC path, file by file:
//   ForwardErrorCorrection   modules/rtp_rtcp/source/forward_error_correction.cc
//   packet masks             forward_error_correction_internal.cc and the
//                            fec_private_tables_{random,bursty} masks
//   FEC header               ulpfec_header_reader_writer.cc
//   UlpfecGenerator          ulpfec_generator.cc (sender, RED wrapping)
//   UlpfecReceiver           ulpfec_receiver.cc (RED demux, recovery)
// with libwebrtc's constants: at most 48 media packets per FEC block, 2- or
// 6-byte masks (L bit), protection factor in Q8 (0..255 = 0..100% of the
// media packets), kMinMediaPackets 4 above protection 80, kMaxExcessOverhead
// 50 (Q8), and the random and bursty mask tables for up to 12 media packets
// (interleaved masks are generated for 13 or more).
//
// Packets are raw RTP bytes, as in libwebrtc: the FEC XOR covers the first
// two header bytes, the timestamp, the length after the 12-byte fixed header
// and everything after it (CSRCs, extensions, payload, padding).
//
// Wire format of one ULPFEC packet, carried as the single block of a RED
// packet whose block PT is the ULPFEC payload type (RFC 5109 7.3 and 7.4):
//   E L P X CC M PT-recovery | SN base | TS recovery | length recovery
//   protection length | mask (16 bits, or 48 bits when L is set)
//   XOR of the protected payloads
//
// Not ported (libwebrtc doesn't use it on this path): FlexFEC. Unequal
// protection is ported (GeneratePacketMasks) but, as in UlpfecGenerator, the
// generator never asks for it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <span>
#include <vector>

#include "sm/core/metrics.h"
#include "sm/core/rtp_packet.h"

namespace sm::loss::ulpfec {

// forward_error_correction_internal.h, module_fec_types.h.
inline constexpr size_t kUlpfecMaxMediaPackets = 48;
inline constexpr size_t kUlpfecPacketMaskSizeLBitClear = 2;
inline constexpr size_t kUlpfecPacketMaskSizeLBitSet = 6;
inline constexpr size_t kUlpfecMaxPacketMaskSize = kUlpfecPacketMaskSizeLBitSet;
// rtp_rtcp_defines.h: IP_PACKET_SIZE, kRtpHeaderSize; rtp_sender_video.h:
// kRedForFecHeaderLength.
inline constexpr size_t kIpPacketSize = 1500;
inline constexpr size_t kRtpHeaderSize = 12;
inline constexpr size_t kRedForFecHeaderLength = 1;

enum class FecMaskType { kRandom, kBursty };  // kFecMaskRandom, kFecMaskBursty

// module_fec_types.h FecProtectionParams.
struct FecProtectionParams {
  int fec_rate = 0;  // Q8: 0..255 FEC packets per 256 media packets.
  int max_fec_frames = 0;
  FecMaskType fec_mask_type = FecMaskType::kRandom;
};

namespace internal {

// Bytes of packet mask needed for `num_sequence_numbers` (2 or 6).
size_t PacketMaskSize(size_t num_sequence_numbers);

// PacketMaskTable: table masks for up to 12 media packets, interleaved masks
// (media packet X protected by FEC packet X % m) generated for more.
class PacketMaskTable {
 public:
  PacketMaskTable(FecMaskType fec_mask_type, int num_media_packets);
  std::span<const uint8_t> LookUp(int num_media_packets, int num_fec_packets);

 private:
  static const uint8_t* PickTable(FecMaskType fec_mask_type, int num_media_packets);
  const uint8_t* table_;
  uint8_t fec_packet_mask_[kUlpfecMaxMediaPackets * kUlpfecMaxPacketMaskSize] = {};
};

// Writes num_fec_packets rows of PacketMaskSize(num_media_packets) bytes.
void GeneratePacketMasks(int num_media_packets, int num_fec_packets, int num_imp_packets,
                         bool use_unequal_protection, PacketMaskTable* mask_table,
                         uint8_t* packet_mask);

std::span<const uint8_t> LookUpInFecTable(const uint8_t* table, int media_packet_index,
                                          int fec_index);
void InsertZeroColumns(int num_zeros, uint8_t* new_mask, int new_mask_bytes, int num_fec_packets,
                       int new_bit_index);
void CopyColumn(uint8_t* new_mask, int new_mask_bytes, uint8_t* old_mask, int old_mask_bytes,
                int num_fec_packets, int new_bit_index, int old_bit_index);

}  // namespace internal

// ForwardErrorCorrection, the ULPFEC variant (CreateUlpfec): encoder and
// decoder of one media SSRC. In ULPFEC over RED the FEC packets share the
// media SSRC and sequence number space.
class ForwardErrorCorrection {
 public:
  using Buffer = std::vector<uint8_t>;
  using PacketPtr = std::shared_ptr<Buffer>;  // scoped_refptr<Packet>

  struct ReceivedPacket {
    uint32_t ssrc = 0;
    uint16_t seq_num = 0;
    bool is_fec = false;
    bool is_recovered = false;
    PacketPtr pkt;
  };
  struct RecoveredPacket {
    uint32_t ssrc = 0;
    uint16_t seq_num = 0;
    bool was_recovered = false;  // Recovered from FEC, not received.
    bool returned = false;       // Already handed to the caller.
    PacketPtr pkt;
  };
  using RecoveredPacketList = std::list<std::unique_ptr<RecoveredPacket>>;

  explicit ForwardErrorCorrection(uint32_t ssrc);

  // Fills `fec_packets` with FEC payloads (FEC header + XOR, no RTP header)
  // protecting `media_packets` (consecutive or with gaps, at most 48 sequence
  // numbers). Returns 0, or -1 if the packets can't be protected.
  int EncodeFec(const std::vector<Buffer>& media_packets, uint8_t protection_factor,
                int num_important_packets, bool use_unequal_protection, FecMaskType fec_mask_type,
                std::vector<Buffer>* fec_packets);

  // Adds one received packet (media or FEC payload) and recovers what it can
  // into `recovered_packets`. Returns the number of packets recovered.
  size_t DecodeFec(const ReceivedPacket& received_packet, RecoveredPacketList* recovered_packets);

  static int NumFecPackets(int num_media_packets, int protection_factor);
  // FEC header with a 6-byte mask (kFecLevel0HeaderSize + kFecLevel1HeaderSizeLBitSet).
  static size_t MaxPacketOverhead();
  void ResetState(RecoveredPacketList* recovered_packets);

  static uint16_t ParseSequenceNumber(const uint8_t* packet);
  static uint32_t ParseSsrc(const uint8_t* packet);

 private:
  struct ProtectedPacket {
    uint32_t ssrc = 0;
    uint16_t seq_num = 0;
    PacketPtr pkt;
  };
  struct ReceivedFecPacket {
    uint32_t ssrc = 0;
    uint16_t seq_num = 0;
    std::list<std::unique_ptr<ProtectedPacket>> protected_packets;
    size_t fec_header_size = 0;
    uint32_t protected_ssrc = 0;
    uint16_t seq_num_base = 0;
    size_t packet_mask_offset = 0;
    size_t packet_mask_size = 0;
    size_t protection_length = 0;
    PacketPtr pkt;
  };

  int InsertZerosInPacketMasks(const std::vector<Buffer>& media_packets, size_t num_fec_packets);
  void GenerateFecPayloads(const std::vector<Buffer>& media_packets, size_t num_fec_packets,
                           std::vector<Buffer>* fec_packets);
  void InsertPacket(const ReceivedPacket& received_packet, RecoveredPacketList* recovered_packets);
  void InsertMediaPacket(RecoveredPacketList* recovered_packets,
                         const ReceivedPacket& received_packet);
  void UpdateCoveringFecPackets(const RecoveredPacket& packet);
  void InsertFecPacket(const RecoveredPacketList& recovered_packets,
                       const ReceivedPacket& received_packet);
  static void AssignRecoveredPackets(const RecoveredPacketList& recovered_packets,
                                     ReceivedFecPacket* fec_packet);
  size_t AttemptRecovery(RecoveredPacketList* recovered_packets);
  static bool StartPacketRecovery(const ReceivedFecPacket& fec_packet,
                                  RecoveredPacket* recovered_packet);
  static void XorHeaders(const Buffer& src, Buffer* dst);
  static void XorPayloads(const Buffer& src, size_t payload_length, size_t dst_offset, Buffer* dst);
  static bool FinishPacketRecovery(RecoveredPacket* recovered_packet);
  static bool RecoverPacket(const ReceivedFecPacket& fec_packet, RecoveredPacket* recovered_packet);
  static int NumCoveredPacketsMissing(const ReceivedFecPacket& fec_packet);
  static void DiscardOldRecoveredPackets(RecoveredPacketList* recovered_packets);
  static bool IsOldFecPacket(const ReceivedFecPacket& fec_packet,
                             const RecoveredPacketList* recovered_packets);
  // UlpfecHeaderReader::ReadFecHeader, UlpfecHeaderWriter::FinalizeFecHeader.
  static bool ReadFecHeader(ReceivedFecPacket* fec_packet);
  static void FinalizeFecHeader(uint16_t seq_num_base, const uint8_t* packet_mask,
                                size_t packet_mask_size, Buffer& fec_packet);

  const uint32_t ssrc_;
  std::list<std::unique_ptr<ReceivedFecPacket>> received_fec_packets_;
  uint8_t packet_masks_[kUlpfecMaxMediaPackets * kUlpfecMaxPacketMaskSize] = {};
  uint8_t tmp_packet_masks_[kUlpfecMaxMediaPackets * kUlpfecMaxPacketMaskSize] = {};
  size_t packet_mask_size_ = 0;
};

struct UlpfecGeneratorStats {
  uint64_t media_packets = 0;  // Media packets added.
  uint64_t fec_blocks = 0;     // EncodeFec calls that produced FEC.
  uint64_t fec_packets = 0;
  int64_t fec_bytes = 0;       // RED-wrapped FEC packets, RTP headers included.
};

// UlpfecGenerator: groups media packets over frames and emits RED-wrapped
// ULPFEC packets once a frame completes the block.
class UlpfecGenerator : public MetricsSource {
 public:
  UlpfecGenerator(int red_payload_type, int ulpfec_payload_type);

  // Applied when the next media packet is added.
  void SetProtectionParameters(const FecProtectionParams& delta_params,
                               const FecProtectionParams& key_params);
  // `packet` is the media packet as sent, without RED (libwebrtc's
  // RtpSenderEgress unpacks a RED packet before adding it). `is_key_frame`
  // selects the key frame parameters for the block.
  void AddPacketAndGenerateFec(const RtpPacket& packet, bool is_key_frame);
  // The FEC packets generated by the last AddPacketAndGenerateFec, as RED
  // packets with the last media packet's header (marker cleared, RED PT).
  // Sequence numbers are the caller's to assign. Not retransmittable.
  std::vector<RtpPacket> GetFecPackets();
  size_t MaxPacketOverhead() const { return ForwardErrorCorrection::MaxPacketOverhead(); }
  const FecProtectionParams& CurrentParams() const;
  const UlpfecGeneratorStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct Params {
    FecProtectionParams delta_params;
    FecProtectionParams keyframe_params;
  };
  int Overhead() const;
  bool ExcessOverheadBelowMax() const;
  bool MinimumMediaPacketsReached() const;
  void ResetState();

  const int red_payload_type_;
  const int ulpfec_payload_type_;
  ForwardErrorCorrection fec_;
  std::vector<ForwardErrorCorrection::Buffer> media_packets_;
  bool has_last_media_packet_ = false;
  RtpPacket last_media_packet_;
  std::vector<ForwardErrorCorrection::Buffer> generated_fec_packets_;
  int num_protected_frames_ = 0;
  int min_num_media_packets_ = 1;
  Params current_params_;
  bool media_contains_keyframe_ = false;
  bool has_pending_params_ = false;
  Params pending_params_;
  UlpfecGeneratorStats stats_;
};

// ulpfec_receiver.h FecPacketCounter.
struct FecPacketCounter {
  uint64_t num_packets = 0;      // RED packets received.
  int64_t num_bytes = 0;
  uint64_t num_fec_packets = 0;
  uint64_t num_recovered_packets = 0;
};

// UlpfecReceiver: takes the RED packets of one SSRC, hands back the media
// packets inside them and the packets ULPFEC recovers.
class UlpfecReceiver : public MetricsSource {
 public:
  UlpfecReceiver(uint32_t ssrc, int ulpfec_payload_type);

  // Returns false (and drops it) for a packet of another SSRC, an empty or
  // oversized payload, or a RED packet with more than one block.
  // `recovered` marks a packet this receiver recovered earlier and that came
  // back through RED; it is delivered but not fed to the decoder.
  bool AddReceivedRedPacket(const RtpPacket& rtp_packet, bool recovered = false);
  // Media packets added since the last call (RED header removed, media PT
  // restored), then any newly recovered packets, in that order.
  std::vector<RtpPacket> ProcessReceivedFec();
  const FecPacketCounter& GetPacketCounter() const { return packet_counter_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  const uint32_t ssrc_;
  const int ulpfec_payload_type_;
  ForwardErrorCorrection fec_;
  std::vector<ForwardErrorCorrection::ReceivedPacket> received_packets_;
  ForwardErrorCorrection::RecoveredPacketList recovered_packets_;
  FecPacketCounter packet_counter_;
};

}  // namespace sm::loss::ulpfec
