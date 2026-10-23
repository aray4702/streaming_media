#include "sm/loss/ulpfec.h"

#include <algorithm>
#include <cstring>

#include "sm/core/seq_num.h"
#include "ulpfec_tables.h"

namespace sm::loss::ulpfec {
namespace {

// forward_error_correction.cc.
constexpr uint16_t kOldSequenceThreshold = 0x3fff;

// ulpfec_header_reader_writer.cc.
constexpr size_t kMaxTrackedMediaPackets = 4 * kUlpfecMaxMediaPackets;
constexpr size_t kMaxFecPackets = kUlpfecMaxMediaPackets;
constexpr size_t kFecLevel0HeaderSize = 10;
constexpr size_t kFecLevel1HeaderSizeLBitSet = 2 + kUlpfecPacketMaskSizeLBitSet;
constexpr size_t kFecLevel1HeaderSizeLBitClear = 2 + kUlpfecPacketMaskSizeLBitClear;
constexpr size_t kPacketMaskOffset = kFecLevel0HeaderSize + 2;

size_t UlpfecHeaderSize(size_t packet_mask_size) {
  return packet_mask_size <= kUlpfecPacketMaskSizeLBitClear
             ? kFecLevel0HeaderSize + kFecLevel1HeaderSizeLBitClear
             : kFecLevel0HeaderSize + kFecLevel1HeaderSizeLBitSet;
}

// ulpfec_generator.cc.
constexpr int kMaxExcessOverhead = 50;  // Q8.
constexpr int kMinMediaPackets = 4;
constexpr int kHighProtectionThreshold = 80;
constexpr float kMinMediaPacketsAdaptationThreshold = 2.0f;
constexpr uint32_t kUnknownSsrc = 0;

// mod_ops.h MinDiff for uint16_t.
uint16_t MinDiff(uint16_t a, uint16_t b) {
  return std::min(static_cast<uint16_t>(a - b), static_cast<uint16_t>(b - a));
}

// SortablePacket::LessThan: `first` is older than `second`.
template <class A, class B>
bool LessThan(const A& first, const B& second) {
  return AheadOf<uint16_t>(second->seq_num, first->seq_num);
}

uint16_t ReadBe16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
void WriteBe16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}
void WriteBe32(uint8_t* p, uint32_t v) {
  WriteBe16(p, static_cast<uint16_t>(v >> 16));
  WriteBe16(p + 2, static_cast<uint16_t>(v));
}

// Fits sub_mask (num_rows rows of num_sub_mask_bytes) into packet_mask rows
// of num_mask_bytes.
void FitSubMask(int num_mask_bytes, int num_sub_mask_bytes, int num_rows, const uint8_t* sub_mask,
                uint8_t* packet_mask) {
  if (num_mask_bytes == num_sub_mask_bytes) {
    std::memcpy(packet_mask, sub_mask, static_cast<size_t>(num_rows * num_sub_mask_bytes));
  } else {
    for (int i = 0; i < num_rows; ++i) {
      int pkt_mask_idx = i * num_mask_bytes;
      int pkt_mask_idx2 = i * num_sub_mask_bytes;
      for (int j = 0; j < num_sub_mask_bytes; ++j) packet_mask[pkt_mask_idx++] = sub_mask[pkt_mask_idx2++];
    }
  }
}

// Shifts a mask right by num_column_shift columns and fits it into rows
// num_column_shift..end_row of packet_mask.
void ShiftFitSubMask(int num_mask_bytes, int res_mask_bytes, int num_column_shift, int end_row,
                     const uint8_t* sub_mask, uint8_t* packet_mask) {
  const int num_bit_shifts = num_column_shift % 8;
  const int num_byte_shifts = num_column_shift >> 3;
  for (int i = num_column_shift; i < end_row; ++i) {
    int pkt_mask_idx = i * num_mask_bytes + res_mask_bytes - 1 + num_byte_shifts;
    int pkt_mask_idx2 = (i - num_column_shift) * res_mask_bytes + res_mask_bytes - 1;
    uint8_t shift_right_curr_byte = 0;
    uint8_t shift_left_prev_byte = 0;
    if (num_mask_bytes > res_mask_bytes) {
      shift_left_prev_byte = static_cast<uint8_t>(sub_mask[pkt_mask_idx2] << (8 - num_bit_shifts));
      packet_mask[pkt_mask_idx + 1] = shift_left_prev_byte;
    }
    for (int j = res_mask_bytes - 1; j > 0; j--) {
      shift_right_curr_byte = static_cast<uint8_t>(sub_mask[pkt_mask_idx2] >> num_bit_shifts);
      shift_left_prev_byte = static_cast<uint8_t>(sub_mask[pkt_mask_idx2 - 1] << (8 - num_bit_shifts));
      packet_mask[pkt_mask_idx] = shift_right_curr_byte | shift_left_prev_byte;
      pkt_mask_idx--;
      pkt_mask_idx2--;
    }
    packet_mask[pkt_mask_idx] = static_cast<uint8_t>(sub_mask[pkt_mask_idx2] >> num_bit_shifts);
  }
}

enum ProtectionMode { kModeNoOverlap, kModeOverlap, kModeBiasFirstPacket };

void RemainingPacketProtection(int num_media_packets, int num_fec_remaining,
                               int num_fec_for_imp_packets, int num_mask_bytes, ProtectionMode mode,
                               uint8_t* packet_mask, internal::PacketMaskTable* mask_table) {
  if (mode == kModeNoOverlap) {
    const int res_mask_bytes =
        static_cast<int>(internal::PacketMaskSize(static_cast<size_t>(num_media_packets - num_fec_for_imp_packets)));
    const int end_row = num_fec_for_imp_packets + num_fec_remaining;
    auto sub_21 = mask_table->LookUp(num_media_packets - num_fec_for_imp_packets, num_fec_remaining);
    ShiftFitSubMask(num_mask_bytes, res_mask_bytes, num_fec_for_imp_packets, end_row, sub_21.data(),
                    packet_mask);
  } else {
    auto sub_22 = mask_table->LookUp(num_media_packets, num_fec_remaining);
    FitSubMask(num_mask_bytes, num_mask_bytes, num_fec_remaining, sub_22.data(),
               &packet_mask[num_fec_for_imp_packets * num_mask_bytes]);
    if (mode == kModeBiasFirstPacket) {
      for (int i = 0; i < num_fec_remaining; ++i) packet_mask[i * num_mask_bytes] |= (1 << 7);
    }
  }
}

void ImportantPacketProtection(int num_fec_for_imp_packets, int num_imp_packets, int num_mask_bytes,
                               uint8_t* packet_mask, internal::PacketMaskTable* mask_table) {
  const int num_imp_mask_bytes = static_cast<int>(internal::PacketMaskSize(static_cast<size_t>(num_imp_packets)));
  auto sub_1 = mask_table->LookUp(num_imp_packets, num_fec_for_imp_packets);
  FitSubMask(num_mask_bytes, num_imp_mask_bytes, num_fec_for_imp_packets, sub_1.data(), packet_mask);
}

int SetProtectionAllocation(int num_media_packets, int num_fec_packets, int num_imp_packets) {
  const float alloc_par = 0.5f;
  const int max_num_fec_for_imp = static_cast<int>(alloc_par * static_cast<float>(num_fec_packets));
  int num_fec_for_imp_packets = num_imp_packets < max_num_fec_for_imp ? num_imp_packets : max_num_fec_for_imp;
  if (num_fec_packets == 1 && num_media_packets > 2 * num_imp_packets) num_fec_for_imp_packets = 0;
  return num_fec_for_imp_packets;
}

void UnequalProtectionMask(int num_media_packets, int num_fec_packets, int num_imp_packets,
                           int num_mask_bytes, uint8_t* packet_mask, internal::PacketMaskTable* mask_table) {
  const ProtectionMode mode = kModeOverlap;
  const int num_fec_for_imp_packets = SetProtectionAllocation(num_media_packets, num_fec_packets, num_imp_packets);
  const int num_fec_remaining = num_fec_packets - num_fec_for_imp_packets;
  if (num_fec_for_imp_packets > 0) {
    ImportantPacketProtection(num_fec_for_imp_packets, num_imp_packets, num_mask_bytes, packet_mask, mask_table);
  }
  if (num_fec_remaining > 0) {
    RemainingPacketProtection(num_media_packets, num_fec_remaining, num_fec_for_imp_packets, num_mask_bytes,
                              mode, packet_mask, mask_table);
  }
}

}  // namespace

namespace internal {

size_t PacketMaskSize(size_t num_sequence_numbers) {
  return num_sequence_numbers > 8 * kUlpfecPacketMaskSizeLBitClear ? kUlpfecPacketMaskSizeLBitSet
                                                                   : kUlpfecPacketMaskSizeLBitClear;
}

PacketMaskTable::PacketMaskTable(FecMaskType fec_mask_type, int num_media_packets)
    : table_(PickTable(fec_mask_type, num_media_packets)) {}

std::span<const uint8_t> PacketMaskTable::LookUp(int num_media_packets, int num_fec_packets) {
  if (num_media_packets <= 12) return LookUpInFecTable(table_, num_media_packets - 1, num_fec_packets - 1);
  const int mask_length = static_cast<int>(PacketMaskSize(static_cast<size_t>(num_media_packets)));
  // Interleaved: media packet X is protected by FEC packet X % num_fec_packets.
  for (int row = 0; row < num_fec_packets; row++) {
    for (int col = 0; col < mask_length; col++) {
      uint8_t byte = 0;
      for (int bit = 0; bit < 8; ++bit) {
        const int x = col * 8 + bit;
        if (x % num_fec_packets == row && x < num_media_packets) byte |= static_cast<uint8_t>(0x80 >> bit);
      }
      fec_packet_mask_[row * mask_length + col] = byte;
    }
  }
  return {&fec_packet_mask_[0], static_cast<size_t>(num_fec_packets * mask_length)};
}

const uint8_t* PacketMaskTable::PickTable(FecMaskType fec_mask_type, int num_media_packets) {
  if (fec_mask_type != FecMaskType::kRandom && num_media_packets <= static_cast<int>(kPacketMaskBurstyTbl[0])) {
    return &kPacketMaskBurstyTbl[0];
  }
  return &kPacketMaskRandomTbl[0];
}

std::span<const uint8_t> LookUpInFecTable(const uint8_t* table, int media_packet_index, int fec_index) {
  const uint8_t* entry = &table[1];  // Skip over the table size.
  int entry_size_increment = 2;      // 0-16 are 2 bytes wide, then 6.
  for (int i = 0; i < media_packet_index; ++i) {
    if (i == 16) entry_size_increment = 6;
    const uint8_t count = entry[0];
    ++entry;
    for (int j = 0; j < count; ++j) entry += entry_size_increment * (j + 1);
  }
  if (media_packet_index == 16) entry_size_increment = 6;
  ++entry;  // Skip over the size.
  for (int i = 0; i < fec_index; ++i) entry += entry_size_increment * (i + 1);
  return {&entry[0], static_cast<size_t>(entry_size_increment * (fec_index + 1))};
}

void GeneratePacketMasks(int num_media_packets, int num_fec_packets, int num_imp_packets,
                         bool use_unequal_protection, PacketMaskTable* mask_table, uint8_t* packet_mask) {
  const int num_mask_bytes = static_cast<int>(PacketMaskSize(static_cast<size_t>(num_media_packets)));
  if (!use_unequal_protection || num_imp_packets == 0) {
    auto mask = mask_table->LookUp(num_media_packets, num_fec_packets);
    std::memcpy(packet_mask, mask.data(), mask.size());
  } else {
    UnequalProtectionMask(num_media_packets, num_fec_packets, num_imp_packets, num_mask_bytes, packet_mask,
                          mask_table);
  }
}

void InsertZeroColumns(int num_zeros, uint8_t* new_mask, int new_mask_bytes, int num_fec_packets,
                       int new_bit_index) {
  for (int row = 0; row < num_fec_packets; ++row) {
    const int new_byte_index = row * new_mask_bytes + new_bit_index / 8;
    const int max_shifts = 7 - (new_bit_index % 8);
    new_mask[new_byte_index] = static_cast<uint8_t>(new_mask[new_byte_index] << std::min(num_zeros, max_shifts));
  }
}

void CopyColumn(uint8_t* new_mask, int new_mask_bytes, uint8_t* old_mask, int old_mask_bytes,
                int num_fec_packets, int new_bit_index, int old_bit_index) {
  for (int row = 0; row < num_fec_packets; ++row) {
    const int new_byte_index = row * new_mask_bytes + new_bit_index / 8;
    const int old_byte_index = row * old_mask_bytes + old_bit_index / 8;
    new_mask[new_byte_index] |= static_cast<uint8_t>((old_mask[old_byte_index] & 0x80) >> 7);
    if (new_bit_index % 8 != 7) new_mask[new_byte_index] = static_cast<uint8_t>(new_mask[new_byte_index] << 1);
    old_mask[old_byte_index] = static_cast<uint8_t>(old_mask[old_byte_index] << 1);
  }
}

}  // namespace internal

// ---------------------------------------------------------------------------
// ForwardErrorCorrection

ForwardErrorCorrection::ForwardErrorCorrection(uint32_t ssrc) : ssrc_(ssrc) {}

size_t ForwardErrorCorrection::MaxPacketOverhead() { return kFecLevel0HeaderSize + kFecLevel1HeaderSizeLBitSet; }

uint16_t ForwardErrorCorrection::ParseSequenceNumber(const uint8_t* packet) { return ReadBe16(&packet[2]); }

uint32_t ForwardErrorCorrection::ParseSsrc(const uint8_t* packet) {
  return (static_cast<uint32_t>(packet[8]) << 24) | (static_cast<uint32_t>(packet[9]) << 16) |
         (static_cast<uint32_t>(packet[10]) << 8) | packet[11];
}

int ForwardErrorCorrection::NumFecPackets(int num_media_packets, int protection_factor) {
  // Q0 with an unsigned round; at least one FEC packet if protection is asked for.
  int num_fec_packets = (num_media_packets * protection_factor + (1 << 7)) >> 8;
  if (protection_factor > 0 && num_fec_packets == 0) num_fec_packets = 1;
  return num_fec_packets;
}

int ForwardErrorCorrection::EncodeFec(const std::vector<Buffer>& media_packets, uint8_t protection_factor,
                                      int num_important_packets, bool use_unequal_protection,
                                      FecMaskType fec_mask_type, std::vector<Buffer>* fec_packets) {
  fec_packets->clear();
  const size_t num_media_packets = media_packets.size();
  if (num_media_packets == 0 || num_media_packets > kUlpfecMaxMediaPackets) return -1;
  if (num_important_packets < 0 || static_cast<size_t>(num_important_packets) > num_media_packets) return -1;
  // (libwebrtc only logs a warning when a packet plus overhead exceeds an MTU.)
  for (const auto& media_packet : media_packets) {
    if (media_packet.size() < kRtpHeaderSize) return -1;
  }
  const int num_fec_packets = NumFecPackets(static_cast<int>(num_media_packets), protection_factor);
  if (num_fec_packets == 0) return 0;

  internal::PacketMaskTable mask_table(fec_mask_type, static_cast<int>(num_media_packets));
  packet_mask_size_ = internal::PacketMaskSize(num_media_packets);
  std::memset(packet_masks_, 0, static_cast<size_t>(num_fec_packets) * packet_mask_size_);
  internal::GeneratePacketMasks(static_cast<int>(num_media_packets), num_fec_packets, num_important_packets,
                                use_unequal_protection, &mask_table, packet_masks_);

  // Adapt packet masks to missing media packets.
  const int num_mask_bits = InsertZerosInPacketMasks(media_packets, static_cast<size_t>(num_fec_packets));
  if (num_mask_bits < 0) return -1;  // Gaps too wide for one block.
  packet_mask_size_ = internal::PacketMaskSize(static_cast<size_t>(num_mask_bits));

  fec_packets->assign(static_cast<size_t>(num_fec_packets), Buffer());
  GenerateFecPayloads(media_packets, static_cast<size_t>(num_fec_packets), fec_packets);
  const uint16_t seq_num_base = ParseSequenceNumber(media_packets.front().data());
  for (int i = 0; i < num_fec_packets; ++i) {
    FinalizeFecHeader(seq_num_base, &packet_masks_[static_cast<size_t>(i) * packet_mask_size_], packet_mask_size_,
                      (*fec_packets)[static_cast<size_t>(i)]);
  }
  return 0;
}

void ForwardErrorCorrection::GenerateFecPayloads(const std::vector<Buffer>& media_packets, size_t num_fec_packets,
                                                 std::vector<Buffer>* fec_packets) {
  for (size_t i = 0; i < num_fec_packets; ++i) {
    Buffer* const fec_packet = &(*fec_packets)[i];
    size_t pkt_mask_idx = i * packet_mask_size_;
    const size_t fec_header_size = UlpfecHeaderSize(packet_mask_size_);  // MinPacketMaskSize is the size.
    size_t media_pkt_idx = 0;
    auto it = media_packets.cbegin();
    uint16_t prev_seq_num = ParseSequenceNumber(it->data());
    while (it != media_packets.cend()) {
      const Buffer& media_packet = *it;
      if (packet_masks_[pkt_mask_idx] & (1 << (7 - media_pkt_idx))) {
        const size_t media_payload_length = media_packet.size() - kRtpHeaderSize;
        const size_t fec_packet_length = fec_header_size + media_payload_length;
        if (fec_packet_length > fec_packet->size()) fec_packet->resize(fec_packet_length, 0);
        XorHeaders(media_packet, fec_packet);
        XorPayloads(media_packet, media_payload_length, fec_header_size, fec_packet);
      }
      ++it;
      if (it != media_packets.cend()) {
        const uint16_t seq_num = ParseSequenceNumber(it->data());
        media_pkt_idx += static_cast<uint16_t>(seq_num - prev_seq_num);
        prev_seq_num = seq_num;
      }
      pkt_mask_idx += media_pkt_idx / 8;
      media_pkt_idx %= 8;
    }
  }
}

int ForwardErrorCorrection::InsertZerosInPacketMasks(const std::vector<Buffer>& media_packets,
                                                     size_t num_fec_packets) {
  const size_t num_media_packets = media_packets.size();
  if (num_media_packets <= 1) return static_cast<int>(num_media_packets);
  const uint16_t last_seq_num = ParseSequenceNumber(media_packets.back().data());
  const uint16_t first_seq_num = ParseSequenceNumber(media_packets.front().data());
  const size_t total_missing_seq_nums =
      static_cast<uint16_t>(last_seq_num - first_seq_num) - num_media_packets + 1;
  if (total_missing_seq_nums == 0) return static_cast<int>(num_media_packets);
  if (total_missing_seq_nums + num_media_packets > kUlpfecMaxMediaPackets) return -1;

  const size_t tmp_packet_mask_size = internal::PacketMaskSize(total_missing_seq_nums + num_media_packets);
  std::memset(tmp_packet_masks_, 0, num_fec_packets * tmp_packet_mask_size);
  auto it = media_packets.cbegin();
  uint16_t prev_seq_num = first_seq_num;
  ++it;
  const int fec_rows = static_cast<int>(num_fec_packets);
  const int tmp_bytes = static_cast<int>(tmp_packet_mask_size);
  const int old_bytes = static_cast<int>(packet_mask_size_);
  internal::CopyColumn(tmp_packet_masks_, tmp_bytes, packet_masks_, old_bytes, fec_rows, 0, 0);
  size_t new_bit_index = 1;
  size_t old_bit_index = 1;
  while (it != media_packets.cend()) {
    if (new_bit_index == kUlpfecMaxMediaPackets) break;  // Only 48 packets fit.
    const uint16_t seq_num = ParseSequenceNumber(it->data());
    const int num_zeros_to_insert = static_cast<uint16_t>(seq_num - prev_seq_num - 1);
    if (num_zeros_to_insert > 0) {
      internal::InsertZeroColumns(num_zeros_to_insert, tmp_packet_masks_, tmp_bytes, fec_rows,
                                  static_cast<int>(new_bit_index));
    }
    new_bit_index += static_cast<size_t>(num_zeros_to_insert);
    internal::CopyColumn(tmp_packet_masks_, tmp_bytes, packet_masks_, old_bytes, fec_rows,
                         static_cast<int>(new_bit_index), static_cast<int>(old_bit_index));
    ++new_bit_index;
    ++old_bit_index;
    prev_seq_num = seq_num;
    ++it;
  }
  if (new_bit_index % 8 != 0) {
    // The last byte isn't full: shift its bits into place.
    for (size_t row = 0; row < num_fec_packets; ++row) {
      const size_t new_byte_index = row * tmp_packet_mask_size + new_bit_index / 8;
      tmp_packet_masks_[new_byte_index] =
          static_cast<uint8_t>(tmp_packet_masks_[new_byte_index] << (7 - (new_bit_index % 8)));
    }
  }
  std::memcpy(packet_masks_, tmp_packet_masks_, num_fec_packets * tmp_packet_mask_size);
  return static_cast<int>(new_bit_index);
}

void ForwardErrorCorrection::FinalizeFecHeader(uint16_t seq_num_base, const uint8_t* packet_mask,
                                               size_t packet_mask_size, Buffer& fec_packet) {
  const size_t fec_header_size = UlpfecHeaderSize(packet_mask_size);
  // Every mask row protects a packet, so this only guards a bad mask (libwebrtc DCHECKs).
  if (fec_packet.size() < fec_header_size) fec_packet.resize(fec_header_size, 0);
  uint8_t* data = fec_packet.data();
  data[0] &= 0x7f;  // E bit 0.
  if (packet_mask_size == kUlpfecPacketMaskSizeLBitSet) {
    data[0] |= 0x40;  // L bit.
  } else {
    data[0] &= 0xbf;
  }
  std::memcpy(&data[8], &data[2], 2);  // Length recovery, from its temporary location.
  WriteBe16(&data[2], seq_num_base);
  // Protection length covers the whole packet (not required in general).
  WriteBe16(&data[10], static_cast<uint16_t>(fec_packet.size() - fec_header_size));
  std::memcpy(&data[12], packet_mask, packet_mask_size);
}

bool ForwardErrorCorrection::ReadFecHeader(ReceivedFecPacket* fec_packet) {
  Buffer& buf = *fec_packet->pkt;
  if (buf.size() < kPacketMaskOffset) return false;  // Truncated.
  uint8_t* data = buf.data();
  const bool l_bit = (data[0] & 0x40) != 0;
  const size_t packet_mask_size = l_bit ? kUlpfecPacketMaskSizeLBitSet : kUlpfecPacketMaskSizeLBitClear;
  fec_packet->fec_header_size = UlpfecHeaderSize(packet_mask_size);
  fec_packet->protected_ssrc = fec_packet->ssrc;  // Due to RED.
  fec_packet->seq_num_base = ReadBe16(&data[2]);
  fec_packet->packet_mask_offset = kPacketMaskOffset;
  fec_packet->packet_mask_size = packet_mask_size;
  fec_packet->protection_length = ReadBe16(&data[10]);
  // Keep the length recovery field where FlexFEC has it, so the XOR is the same.
  std::memcpy(&data[2], &data[8], 2);
  return true;
}

void ForwardErrorCorrection::ResetState(RecoveredPacketList* recovered_packets) {
  recovered_packets->clear();
  received_fec_packets_.clear();
}

void ForwardErrorCorrection::InsertMediaPacket(RecoveredPacketList* recovered_packets,
                                               const ReceivedPacket& received_packet) {
  auto insert_pos = recovered_packets->begin();
  for (auto it = recovered_packets->rbegin(); it != recovered_packets->rend(); ++it) {
    if (LessThan(*it, &received_packet)) {
      insert_pos = it.base();
      break;
    }
    if ((*it)->seq_num == received_packet.seq_num) return;  // Duplicate.
  }
  auto recovered_packet = std::make_unique<RecoveredPacket>();
  recovered_packet->was_recovered = false;
  recovered_packet->returned = true;  // Already passed on.
  recovered_packet->ssrc = received_packet.ssrc;
  recovered_packet->seq_num = received_packet.seq_num;
  recovered_packet->pkt = received_packet.pkt;
  RecoveredPacket* ptr = recovered_packet.get();
  recovered_packets->insert(insert_pos, std::move(recovered_packet));
  UpdateCoveringFecPackets(*ptr);
}

void ForwardErrorCorrection::UpdateCoveringFecPackets(const RecoveredPacket& packet) {
  for (auto& fec_packet : received_fec_packets_) {
    auto& pp = fec_packet->protected_packets;
    auto protected_it = std::lower_bound(pp.begin(), pp.end(), &packet,
                                         [](const auto& a, const auto& b) { return LessThan(a, b); });
    if (protected_it != pp.end() && (*protected_it)->seq_num == packet.seq_num) (*protected_it)->pkt = packet.pkt;
  }
}

void ForwardErrorCorrection::InsertFecPacket(const RecoveredPacketList& recovered_packets,
                                             const ReceivedPacket& received_packet) {
  auto insert_pos = received_fec_packets_.begin();
  for (auto it = received_fec_packets_.rbegin(); it != received_fec_packets_.rend(); ++it) {
    if (LessThan(*it, &received_packet)) {
      insert_pos = it.base();
      break;
    }
    if ((*it)->seq_num == received_packet.seq_num) return;  // Duplicate.
  }
  auto fec_packet = std::make_unique<ReceivedFecPacket>();
  // ReadFecHeader rewrites bytes 2-3, so the FEC packet keeps its own copy.
  fec_packet->pkt = std::make_shared<Buffer>(*received_packet.pkt);
  fec_packet->ssrc = received_packet.ssrc;
  fec_packet->seq_num = received_packet.seq_num;
  if (!ReadFecHeader(fec_packet.get())) return;
  if (fec_packet->protected_ssrc != ssrc_) return;  // Unknown media SSRC.
  if (fec_packet->packet_mask_offset + fec_packet->packet_mask_size > fec_packet->pkt->size()) return;  // Corrupt.
  for (size_t byte_idx = 0; byte_idx < fec_packet->packet_mask_size; ++byte_idx) {
    const uint8_t packet_mask = (*fec_packet->pkt)[fec_packet->packet_mask_offset + byte_idx];
    for (size_t bit_idx = 0; bit_idx < 8; ++bit_idx) {
      if (packet_mask & (1 << (7 - bit_idx))) {
        auto protected_packet = std::make_unique<ProtectedPacket>();
        protected_packet->ssrc = ssrc_;
        protected_packet->seq_num = static_cast<uint16_t>(fec_packet->seq_num_base + (byte_idx << 3) + bit_idx);
        fec_packet->protected_packets.push_back(std::move(protected_packet));
      }
    }
  }
  if (fec_packet->protected_packets.empty()) return;  // All-zero mask: discard.
  AssignRecoveredPackets(recovered_packets, fec_packet.get());
  received_fec_packets_.insert(insert_pos, std::move(fec_packet));
  if (received_fec_packets_.size() > kMaxFecPackets) received_fec_packets_.pop_front();
}

void ForwardErrorCorrection::AssignRecoveredPackets(const RecoveredPacketList& recovered_packets,
                                                    ReceivedFecPacket* fec_packet) {
  auto& protected_packets = fec_packet->protected_packets;
  auto it_p = protected_packets.begin();
  auto it_r = recovered_packets.cbegin();
  while (it_p != protected_packets.end() && it_r != recovered_packets.cend()) {
    if (LessThan(*it_p, *it_r)) {
      ++it_p;
    } else if (LessThan(*it_r, *it_p)) {
      ++it_r;
    } else {
      (*it_p)->pkt = (*it_r)->pkt;  // Already recovered.
      ++it_p;
      ++it_r;
    }
  }
}

void ForwardErrorCorrection::InsertPacket(const ReceivedPacket& received_packet,
                                          RecoveredPacketList* recovered_packets) {
  // Keep the FEC packets within half the sequence number space, so the list
  // stays sorted and wrap-around can't decode the wrong packets.
  if (!received_fec_packets_.empty() && received_packet.ssrc == received_fec_packets_.front()->ssrc) {
    auto it = received_fec_packets_.begin();
    while (it != received_fec_packets_.end()) {
      if (MinDiff(received_packet.seq_num, (*it)->seq_num) > kOldSequenceThreshold) {
        it = received_fec_packets_.erase(it);
      } else {
        break;
      }
    }
  }
  if (received_packet.is_fec) {
    InsertFecPacket(*recovered_packets, received_packet);
  } else {
    InsertMediaPacket(recovered_packets, received_packet);
  }
  DiscardOldRecoveredPackets(recovered_packets);
}

bool ForwardErrorCorrection::StartPacketRecovery(const ReceivedFecPacket& fec_packet,
                                                 RecoveredPacket* recovered_packet) {
  const Buffer& fec = *fec_packet.pkt;
  if (fec.size() < fec_packet.fec_header_size + fec_packet.protection_length) return false;  // Truncated.
  if (fec_packet.protection_length >
      std::min(kIpPacketSize - kRtpHeaderSize, kIpPacketSize - fec_packet.fec_header_size)) {
    return false;  // Incorrect protection length.
  }
  recovered_packet->pkt = std::make_shared<Buffer>(fec_packet.protection_length + kRtpHeaderSize, 0);
  recovered_packet->returned = false;
  recovered_packet->was_recovered = true;
  // The minimum RTP header; sequence number and SSRC are set at the end.
  std::memcpy(recovered_packet->pkt->data(), fec.data(), kRtpHeaderSize);
  if (fec_packet.protection_length > 0) {
    std::memcpy(recovered_packet->pkt->data() + kRtpHeaderSize, fec.data() + fec_packet.fec_header_size,
                fec_packet.protection_length);
  }
  return true;
}

bool ForwardErrorCorrection::FinishPacketRecovery(RecoveredPacket* recovered_packet) {
  Buffer& pkt = *recovered_packet->pkt;
  pkt[0] |= 0x80;  // RTP version 2.
  pkt[0] &= 0xbf;
  const size_t new_size = static_cast<size_t>(ReadBe16(&pkt[2])) + kRtpHeaderSize;
  if (new_size > kIpPacketSize - kRtpHeaderSize) return false;  // Larger than an IP packet.
  pkt.resize(new_size, 0);
  WriteBe16(&pkt[2], recovered_packet->seq_num);
  WriteBe32(&pkt[8], recovered_packet->ssrc);
  return true;
}

void ForwardErrorCorrection::XorHeaders(const Buffer& src, Buffer* dst) {
  uint8_t* d = dst->data();
  const uint8_t* s = src.data();
  d[0] ^= s[0];  // V, P, X, CC.
  d[1] ^= s[1];  // M, PT.
  uint8_t len[2];
  WriteBe16(len, static_cast<uint16_t>(src.size() - kRtpHeaderSize));
  d[2] ^= len[0];  // Length recovery.
  d[3] ^= len[1];
  for (int i = 4; i < 8; ++i) d[i] ^= s[i];  // Timestamp.
  // Bytes 8-11 (SSRC) are skipped.
}

void ForwardErrorCorrection::XorPayloads(const Buffer& src, size_t payload_length, size_t dst_offset, Buffer* dst) {
  if (dst_offset + payload_length > dst->size()) dst->resize(dst_offset + payload_length, 0);
  uint8_t* d = dst->data();
  const uint8_t* s = src.data();
  for (size_t i = 0; i < payload_length; ++i) d[dst_offset + i] ^= s[kRtpHeaderSize + i];
}

bool ForwardErrorCorrection::RecoverPacket(const ReceivedFecPacket& fec_packet, RecoveredPacket* recovered_packet) {
  if (!StartPacketRecovery(fec_packet, recovered_packet)) return false;
  for (const auto& protected_packet : fec_packet.protected_packets) {
    if (protected_packet->pkt == nullptr) {
      recovered_packet->seq_num = protected_packet->seq_num;  // The one being recovered.
      recovered_packet->ssrc = protected_packet->ssrc;
    } else {
      const Buffer& src = *protected_packet->pkt;
      XorHeaders(src, recovered_packet->pkt.get());
      XorPayloads(src, src.size() - kRtpHeaderSize, kRtpHeaderSize, recovered_packet->pkt.get());
    }
  }
  return FinishPacketRecovery(recovered_packet);
}

size_t ForwardErrorCorrection::AttemptRecovery(RecoveredPacketList* recovered_packets) {
  size_t num_recovered_packets = 0;
  auto fec_packet_it = received_fec_packets_.begin();
  while (fec_packet_it != received_fec_packets_.end()) {
    const int packets_missing = NumCoveredPacketsMissing(**fec_packet_it);
    if (packets_missing == 1) {
      auto recovered_packet = std::make_unique<RecoveredPacket>();
      if (!RecoverPacket(**fec_packet_it, recovered_packet.get())) {
        fec_packet_it = received_fec_packets_.erase(fec_packet_it);
        continue;
      }
      ++num_recovered_packets;
      RecoveredPacket* ptr = recovered_packet.get();
      auto insert_pos = recovered_packets->begin();
      for (auto it = recovered_packets->rbegin(); it != recovered_packets->rend(); ++it) {
        if (!LessThan(recovered_packet, *it)) {
          insert_pos = it.base();
          break;
        }
      }
      recovered_packets->insert(insert_pos, std::move(recovered_packet));
      UpdateCoveringFecPackets(*ptr);
      DiscardOldRecoveredPackets(recovered_packets);
      received_fec_packets_.erase(fec_packet_it);
      // A recovery may unlock others: start over from the first FEC packet.
      fec_packet_it = received_fec_packets_.begin();
    } else if (packets_missing == 0 || IsOldFecPacket(**fec_packet_it, recovered_packets)) {
      fec_packet_it = received_fec_packets_.erase(fec_packet_it);
    } else {
      ++fec_packet_it;
    }
  }
  return num_recovered_packets;
}

int ForwardErrorCorrection::NumCoveredPacketsMissing(const ReceivedFecPacket& fec_packet) {
  int packets_missing = 0;
  for (const auto& protected_packet : fec_packet.protected_packets) {
    if (protected_packet->pkt == nullptr && ++packets_missing > 1) break;  // Can't recover more than one.
  }
  return packets_missing;
}

void ForwardErrorCorrection::DiscardOldRecoveredPackets(RecoveredPacketList* recovered_packets) {
  while (recovered_packets->size() > kMaxTrackedMediaPackets) recovered_packets->pop_front();
}

bool ForwardErrorCorrection::IsOldFecPacket(const ReceivedFecPacket& fec_packet,
                                            const RecoveredPacketList* recovered_packets) {
  if (recovered_packets->empty()) return false;
  const uint16_t back_recovered_seq_num = recovered_packets->back()->seq_num;
  const uint16_t last_protected_seq_num = fec_packet.protected_packets.back()->seq_num;
  return MinDiff(back_recovered_seq_num, last_protected_seq_num) > kOldSequenceThreshold;
}

size_t ForwardErrorCorrection::DecodeFec(const ReceivedPacket& received_packet,
                                         RecoveredPacketList* recovered_packets) {
  if (recovered_packets->size() == kMaxTrackedMediaPackets) {
    const RecoveredPacket* back = recovered_packets->back().get();
    if (received_packet.ssrc == back->ssrc &&
        MinDiff(received_packet.seq_num, back->seq_num) > kMaxTrackedMediaPackets) {
      // A big gap: the old recovered packets are useless.
      ResetState(recovered_packets);
    }
  }
  InsertPacket(received_packet, recovered_packets);
  return AttemptRecovery(recovered_packets);
}

// ---------------------------------------------------------------------------
// UlpfecGenerator

UlpfecGenerator::UlpfecGenerator(int red_payload_type, int ulpfec_payload_type)
    : red_payload_type_(red_payload_type), ulpfec_payload_type_(ulpfec_payload_type), fec_(kUnknownSsrc) {}

void UlpfecGenerator::SetProtectionParameters(const FecProtectionParams& delta_params,
                                              const FecProtectionParams& key_params) {
  pending_params_ = {delta_params, key_params};
  pending_params_.delta_params.fec_rate = std::clamp(delta_params.fec_rate, 0, 255);
  pending_params_.keyframe_params.fec_rate = std::clamp(key_params.fec_rate, 0, 255);
  has_pending_params_ = true;
}

const FecProtectionParams& UlpfecGenerator::CurrentParams() const {
  return media_contains_keyframe_ ? current_params_.keyframe_params : current_params_.delta_params;
}

void UlpfecGenerator::AddPacketAndGenerateFec(const RtpPacket& packet, bool is_key_frame) {
  generated_fec_packets_.clear();  // GetFecPackets wasn't called; libwebrtc DCHECKs this.
  if (has_pending_params_) {
    current_params_ = pending_params_;
    has_pending_params_ = false;
    min_num_media_packets_ = CurrentParams().fec_rate > kHighProtectionThreshold ? kMinMediaPackets : 1;
  }
  if (is_key_frame) media_contains_keyframe_ = true;
  const bool complete_frame = packet.marker;
  if (media_packets_.size() < kUlpfecMaxMediaPackets) {
    media_packets_.push_back(packet.Serialize());
    last_media_packet_ = packet;  // Its header is copied into the RED packets.
    has_last_media_packet_ = true;
    ++stats_.media_packets;
  }
  if (complete_frame) ++num_protected_frames_;
  const FecProtectionParams params = CurrentParams();
  // FEC over at most max_fec_frames frames, or as soon as the excess overhead
  // is below kMaxExcessOverhead and min_num_media_packets_ is reached.
  if (complete_frame && (num_protected_frames_ >= params.max_fec_frames ||
                         (ExcessOverheadBelowMax() && MinimumMediaPacketsReached()))) {
    constexpr int kNumImportantPackets = 0;
    constexpr bool kUseUnequalProtection = false;
    fec_.EncodeFec(media_packets_, static_cast<uint8_t>(params.fec_rate), kNumImportantPackets,
                   kUseUnequalProtection, params.fec_mask_type, &generated_fec_packets_);
    if (generated_fec_packets_.empty()) ResetState();
  }
}

bool UlpfecGenerator::ExcessOverheadBelowMax() const {
  return (Overhead() - CurrentParams().fec_rate) < kMaxExcessOverhead;
}

bool UlpfecGenerator::MinimumMediaPacketsReached() const {
  const float average_num_packets_per_frame =
      static_cast<float>(media_packets_.size()) / static_cast<float>(num_protected_frames_);
  const int num_media_packets = static_cast<int>(media_packets_.size());
  if (average_num_packets_per_frame < kMinMediaPacketsAdaptationThreshold) {
    return num_media_packets >= min_num_media_packets_;
  }
  return num_media_packets >= min_num_media_packets_ + 1;  // More packets per frame: one more.
}

int UlpfecGenerator::Overhead() const {
  const int n = static_cast<int>(media_packets_.size());
  const int num_fec_packets = ForwardErrorCorrection::NumFecPackets(n, CurrentParams().fec_rate);
  return (num_fec_packets << 8) / n;  // Q8.
}

std::vector<RtpPacket> UlpfecGenerator::GetFecPackets() {
  std::vector<RtpPacket> fec_packets;
  if (generated_fec_packets_.empty()) return fec_packets;
  // FEC packets have no RTP header of their own: reuse the last media packet's.
  RtpPacket header = last_media_packet_;
  header.payload.clear();
  header.padding_size = 0;
  header.payload_type = static_cast<uint8_t>(red_payload_type_);
  header.marker = false;
  fec_packets.reserve(generated_fec_packets_.size());
  for (const auto& fec_packet : generated_fec_packets_) {
    RtpPacket red_packet = header;
    red_packet.payload.reserve(kRedForFecHeaderLength + fec_packet.size());
    red_packet.payload.push_back(static_cast<uint8_t>(ulpfec_payload_type_));  // RED header, F bit 0.
    red_packet.payload.insert(red_packet.payload.end(), fec_packet.begin(), fec_packet.end());
    stats_.fec_bytes += static_cast<int64_t>(red_packet.size());
    fec_packets.push_back(std::move(red_packet));
  }
  ++stats_.fec_blocks;
  stats_.fec_packets += fec_packets.size();
  ResetState();
  return fec_packets;
}

void UlpfecGenerator::ResetState() {
  media_packets_.clear();
  has_last_media_packet_ = false;
  generated_fec_packets_.clear();
  num_protected_frames_ = 0;
  media_contains_keyframe_ = false;
}

void UlpfecGenerator::ExportMetrics(MetricsWriter& w) const {
  w.Counter("media_packets", stats_.media_packets);
  w.Counter("fec_blocks", stats_.fec_blocks);
  w.Counter("fec_packets", stats_.fec_packets);
  w.Counter("fec_bytes", static_cast<uint64_t>(stats_.fec_bytes));
  w.Gauge("fec_rate_delta_q8", current_params_.delta_params.fec_rate);
  w.Gauge("fec_rate_key_q8", current_params_.keyframe_params.fec_rate);
}

// ---------------------------------------------------------------------------
// UlpfecReceiver

UlpfecReceiver::UlpfecReceiver(uint32_t ssrc, int ulpfec_payload_type)
    : ssrc_(ssrc), ulpfec_payload_type_(ulpfec_payload_type), fec_(ssrc) {}

//     0                   1                    2                   3
//     0 1 2 3 4 5 6 7 8 9 0 1 2 3  4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |F|   block PT  |  timestamp offset         |   block length    |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// With F = 0 only the one-byte header (block PT) is present (RFC 2198).
bool UlpfecReceiver::AddReceivedRedPacket(const RtpPacket& rtp_packet, bool recovered) {
  if (rtp_packet.ssrc != ssrc_) return false;
  if (rtp_packet.size() > kIpPacketSize) return false;
  constexpr size_t kRedHeaderLength = 1;
  if (rtp_packet.payload.empty()) return false;  // Corrupt or truncated.
  const uint8_t red_header = rtp_packet.payload[0];
  if (red_header & 0x80) return false;  // libwebrtc never sends more than one block with FEC.
  const uint8_t payload_type = red_header & 0x7f;

  ForwardErrorCorrection::ReceivedPacket received_packet;
  received_packet.is_fec = payload_type == ulpfec_payload_type_;
  received_packet.is_recovered = recovered;
  received_packet.ssrc = rtp_packet.ssrc;
  received_packet.seq_num = rtp_packet.seq;
  ++packet_counter_.num_packets;
  packet_counter_.num_bytes += static_cast<int64_t>(rtp_packet.size());
  if (received_packet.is_fec) {
    ++packet_counter_.num_fec_packets;
    // Everything after the RED header. libwebrtc slices the payload, so
    // padding of the RED packet is not part of the FEC data.
    received_packet.pkt = std::make_shared<ForwardErrorCorrection::Buffer>(
        rtp_packet.payload.begin() + kRedHeaderLength, rtp_packet.payload.end());
  } else {
    // The media packet as it was before RED: media PT, payload after the RED header.
    RtpPacket media = rtp_packet;
    media.payload_type = payload_type;
    media.payload.erase(media.payload.begin());
    received_packet.pkt = std::make_shared<ForwardErrorCorrection::Buffer>(media.Serialize());
  }
  if (!received_packet.pkt->empty()) received_packets_.push_back(std::move(received_packet));
  return true;
}

std::vector<RtpPacket> UlpfecReceiver::ProcessReceivedFec() {
  std::vector<RtpPacket> out;
  std::vector<ForwardErrorCorrection::ReceivedPacket> received_packets;
  received_packets.swap(received_packets_);
  size_t num_recovered_packets = 0;
  for (const auto& received_packet : received_packets) {
    if (!received_packet.is_fec) {
      auto media = RtpPacket::Parse(*received_packet.pkt);
      if (!media) continue;  // Corrupted media packet.
      out.push_back(std::move(*media));
    }
    // Recovered packets are not fed back: their bytes may differ from the
    // original's and would corrupt the FEC calculation.
    if (!received_packet.is_recovered) {
      num_recovered_packets += fec_.DecodeFec(received_packet, &recovered_packets_);
    }
  }
  if (num_recovered_packets == 0) return out;
  for (const auto& recovered_packet : recovered_packets_) {
    if (recovered_packet->returned) continue;
    ++packet_counter_.num_recovered_packets;
    recovered_packet->returned = true;
    auto parsed = RtpPacket::Parse(*recovered_packet->pkt);
    if (!parsed) continue;
    out.push_back(std::move(*parsed));
  }
  return out;
}

void UlpfecReceiver::ExportMetrics(MetricsWriter& w) const {
  w.Counter("red_packets", packet_counter_.num_packets);
  w.Counter("red_bytes", static_cast<uint64_t>(packet_counter_.num_bytes));
  w.Counter("fec_packets", packet_counter_.num_fec_packets);
  w.Counter("recovered", packet_counter_.num_recovered_packets);
}

}  // namespace sm::loss::ulpfec
