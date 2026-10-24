// 3.1 / 3.5 RTCP feedback wire formats: generic NACK (RFC 4585 6.2.1), PLI
// (RFC 4585 6.3.1), FIR (RFC 5104 4.3.1), loss notification (LNTF, the
// libwebrtc "goog-lntf" application-layer feedback), and compound RTCP
// packets (RFC 3550 6.1). Layouts follow libwebrtc's rtcp_packet/ nack, pli,
// fir and loss_notification so that libwebrtc parses what this library sends.
// Pure functions: no clock, no state.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sm::loss {

// RTCP packet types used by the library.
inline constexpr uint8_t kRtcpSr = 200;
inline constexpr uint8_t kRtcpRr = 201;
inline constexpr uint8_t kRtcpSdes = 202;
inline constexpr uint8_t kRtcpBye = 203;
inline constexpr uint8_t kRtcpXr = 207;  // Extended reports (RFC 3611).
inline constexpr uint8_t kRtcpRtpfb = 205;  // Transport-layer feedback (NACK, transport-cc).
inline constexpr uint8_t kRtcpPsfb = 206;   // Payload-specific feedback (PLI, FIR, LNTF, REMB).

// Generic NACK: lost RTP sequence numbers of one media SSRC.
struct Nack {
  uint32_t sender_ssrc = 0;
  uint32_t media_ssrc = 0;
  // In the order they were requested. Serialization packs each run of numbers
  // within 16 of the item's first number into one PID+BLP item.
  std::vector<uint16_t> seqs;
};

// Picture loss indication: the decoder lost reference pictures.
struct Pli {
  uint32_t sender_ssrc = 0;
  uint32_t media_ssrc = 0;
};

// Full intra request: one entry per requested SSRC. `seq_nr` increases by one
// for each new request and lets the sender ignore retransmitted requests.
struct FirEntry {
  uint32_t ssrc = 0;
  uint8_t seq_nr = 0;
};
struct Fir {
  uint32_t sender_ssrc = 0;
  std::vector<FirEntry> entries;
};

// Loss notification: tells the sender how far the receiver has decoded and
// received, so it can decide whether a keyframe is needed. Sequence numbers
// are those of the frames' generic sequence (16 bits, wrapping).
struct LossNotification {
  uint32_t sender_ssrc = 0;
  uint32_t media_ssrc = 0;
  uint16_t last_decoded = 0;
  uint16_t last_received = 0;  // Within 32767 after last_decoded (wrapping).
  // True when every frame that has been received since last_decoded can be
  // decoded once the missing ones arrive (no keyframe needed yet).
  bool decodability_flag = false;
};

// At most this many 32-bit words of NACK items fit one RTCP packet; further
// sequence numbers are dropped by SerializeNack (the session splits earlier).
inline constexpr size_t kMaxNackItems = 65535 - 3;

// Each Serialize returns an empty vector when the message cannot be encoded
// (a NACK with no sequence numbers, a FIR with no entries, a loss notification
// whose last_received is not within 32767 after last_decoded).
std::vector<uint8_t> SerializeNack(const Nack& n);
std::vector<uint8_t> SerializePli(const Pli& p);
std::vector<uint8_t> SerializeFir(const Fir& f);
std::vector<uint8_t> SerializeLossNotification(const LossNotification& l);

// Each Parse takes exactly one RTCP packet (header included) and returns
// nullopt for a wrong packet type or FMT, a bad version, a length that does
// not match the data, or an empty / truncated payload.
std::optional<Nack> ParseNack(std::span<const uint8_t> data);
std::optional<Pli> ParsePli(std::span<const uint8_t> data);
std::optional<Fir> ParseFir(std::span<const uint8_t> data);
std::optional<LossNotification> ParseLossNotification(std::span<const uint8_t> data);

// --- Compound RTCP -------------------------------------------------------------

enum class RtcpType {
  kSenderReport,
  kReceiverReport,
  kNack,
  kTransportCc,
  kPli,
  kFir,
  kLossNotification,
  kRemb,
  kSdes,
  kBye,
  kXr,
  kOther,  // Valid RTCP header, type the library does not interpret.
};

// One packet inside a compound RTCP datagram. `data` includes the header.
struct RtcpPacketView {
  uint8_t packet_type = 0;
  uint8_t fmt = 0;  // The count / FMT field of the first byte.
  RtcpType type = RtcpType::kOther;
  std::span<const uint8_t> data;
};

// Splits a compound RTCP datagram into its packets, validating each header
// (version 2, length inside the datagram, padding only on the last packet).
// Returns nullopt for an empty datagram, a bad header or trailing bytes.
std::optional<std::vector<RtcpPacketView>> SplitCompoundRtcp(std::span<const uint8_t> data);

// Concatenates packets into a compound datagram. As libwebrtc does, the first
// packet must be an SR or an RR (use an RR with no blocks if there is nothing
// to report), and only the last packet may carry the padding bit (RFC 3550
// 6.1); otherwise returns an empty vector. Reduced-size RTCP
// (RFC 5506) is sent without this wrapper.
std::vector<uint8_t> BuildCompoundRtcp(std::span<const std::vector<uint8_t>> packets);

}  // namespace sm::loss
