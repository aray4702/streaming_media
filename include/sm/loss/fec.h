// 3.2 XOR forward error correction in the style of ULPFEC/FlexFEC.
//
// Media packets are grouped (typically one video frame per group). For a
// group of k packets the encoder emits m = ceil(k * protection) parity
// packets; each one is the XOR of a subset of the group chosen by a mask.
//   kInterleaved: parity j covers packets i with i % m == j. Any burst of
//                 up to m consecutive losses is recoverable.
//   kBlock:       parity j covers a contiguous block of about k/m packets.
// The decoder recovers iteratively: any parity packet with exactly one
// missing protected packet reconstructs it, which may unlock others.
//
// Wire format of a FEC packet payload (big endian):
//   base_seq:16  mask:64  len_xor:16  ts_xor:32  pt_marker_xor:8  payload_xor
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/rtp_packet.h"
#include "sm/core/seq_num.h"

namespace sm::loss {

enum class FecMaskType { kInterleaved, kBlock };

struct FecParams {
  double protection = 0.0;  // Parity packets per media packet, 0..1.
  FecMaskType mask = FecMaskType::kInterleaved;
};

struct FecConfig {
  uint32_t fec_ssrc = 0;
  uint8_t fec_payload_type = 118;
  int max_group_size = 48;  // Mask is 64 bits; keep headroom.
  EventSink* events = nullptr;
};

struct FecStats {
  uint64_t media_packets = 0;
  uint64_t fec_packets = 0;
  int64_t media_bytes = 0;
  int64_t fec_bytes = 0;
  uint64_t recovered = 0;       // Decoder side.
  uint64_t fec_received = 0;
  uint64_t fec_useless = 0;     // Dropped with nothing to recover.
};

// Mask helper exposed for tests: bit i set = media packet i protected.
uint64_t FecMask(int k, int m, int j, FecMaskType type);
int FecPacketsFor(int k, double protection);

class FecEncoder : public MetricsSource {
 public:
  explicit FecEncoder(FecConfig cfg = {});
  void SetParams(const FecParams& p) { params_ = p; }
  const FecParams& params() const { return params_; }
  // Adds a media packet. When end_of_group is true (e.g. RTP marker at the
  // end of a frame) or the group is full, returns the parity packets.
  std::vector<RtpPacket> AddMediaPacket(const RtpPacket& p, bool end_of_group);
  const FecStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  std::vector<RtpPacket> Flush();
  FecConfig cfg_;
  FecParams params_;
  std::vector<RtpPacket> group_;
  uint16_t fec_seq_ = 0;
  FecStats stats_;
};

class FecDecoder : public MetricsSource {
 public:
  explicit FecDecoder(FecConfig cfg = {}, size_t window = 512);
  // Feed every received media packet; returns packets recovered as a result.
  std::vector<RtpPacket> OnMediaPacket(const RtpPacket& p);
  std::vector<RtpPacket> OnFecPacket(const RtpPacket& fec);
  const FecStats& stats() const { return stats_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  struct Parity {
    int64_t base;
    uint64_t mask;
    uint16_t len_xor;
    uint32_t ts_xor;
    uint8_t pt_marker_xor;
    std::vector<uint8_t> payload_xor;
    uint32_t ssrc;
  };
  std::vector<RtpPacket> TryRecover();
  void Store(const RtpPacket& p, int64_t useq);
  FecConfig cfg_;
  size_t window_;
  SeqNumUnwrapper<uint16_t> unwrapper_;
  std::map<int64_t, RtpPacket> media_;
  std::deque<Parity> parity_;
  FecStats stats_;
};

}  // namespace sm::loss
