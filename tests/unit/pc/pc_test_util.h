// Test helpers for the RTP/SRTP/DTLS-SRTP transports (#1007): libwebrtc's p2p/test/fake_packet_transport.h,
// pc/test/rtp_transport_test_util.h (TransportObserver) and pc/test/srtp_test_util.h at M157.
//
// media/base/fake_rtp.h (kPcmuFrame, kPcmuFrameWithExtensions, kFakeRtcpReport, kHeaderExtensionIDs) is not in
// compare/webrtc/reference/; the packets below have what the cases depend on: PCMU (payload type 0) from SSRC 1,
// the same frame with four one-byte header extensions (ids 1-4), and an RTCP receiver report.
#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sm/pc/rtp_transport.h"
#include "sm/tls/dtls_transport.h"

namespace sm::pc::test {

// PT=0 (PCMU), SN=1, TS=0, SSRC=1, then 160 bytes of payload (one 20 ms PCMU frame).
inline std::vector<uint8_t> PcmuFrame() {
  std::vector<uint8_t> p = {0x80, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
  p.resize(12 + 160, 0xFF);
  return p;
}
// The header extension ids fake_rtp.h's kHeaderExtensionIDs names (1 and 4); the frame below carries 1-4.
inline constexpr int kHeaderExtensionIDs[] = {1, 4};
// The same frame with the X bit and a one-byte-form extension block: ids 1-4, each 4 bytes of value.
inline std::vector<uint8_t> PcmuFrameWithExtensions() {
  std::vector<uint8_t> p = {0x90, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                            0xBE, 0xDE, 0x00, 0x05};  // 5 words of elements.
  for (uint8_t id = 1; id <= 4; ++id) {
    p.push_back(static_cast<uint8_t>((id << 4) | 3));  // L = 3: 4 bytes.
    for (int i = 0; i < 4; ++i) p.push_back(static_cast<uint8_t>(0x10 * id + i));
  }
  p.insert(p.end(), {0x00, 0x00, 0x00, 0x00});  // Padding to 20 bytes.
  p.resize(p.size() + 160, 0xFF);
  return p;
}
// The value bytes of extension `id` in a frame laid out as PcmuFrameWithExtensions.
inline std::vector<uint8_t> ExtensionValue(const std::vector<uint8_t>& p, int id) {
  const size_t at = 16 + 5 * static_cast<size_t>(id - 1) + 1;
  if (p.size() < at + 4) return {};
  return std::vector<uint8_t>(p.begin() + static_cast<long>(at), p.begin() + static_cast<long>(at + 4));
}
// RR from SSRC 1 with one report block about SSRC 2.
inline const std::vector<uint8_t> kFakeRtcpReport = {
    0x81, 0xC9, 0x00, 0x07, 0x00, 0x00, 0x00, 0x01,  // V=2 RC=1 PT=RR len=7, SSRC 1
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,  // report block: SSRC 2, loss 0
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,  // highest seq 1, jitter 0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // LSR, DLSR
};

inline std::vector<uint8_t> Bytes(const char* s, size_t n) {
  return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(s), reinterpret_cast<const uint8_t*>(s) + n);
}
// pc/test/srtp_test_util.h and srtp_transport_unittest.cc's keys (master key then salt).
inline const std::vector<uint8_t> kTestKey1 = Bytes("ABCDEFGHIJKLMNOPQRSTUVWXYZ1234", 30);
inline const std::vector<uint8_t> kTestKey2 = Bytes("4321ZYXWVUTSRQPONMLKJIHGFEDCBA", 30);
inline const std::vector<uint8_t> kTestKeyGcm128_1 = Bytes("ABCDEFGHIJKLMNOPQRSTUVWXYZ12", 28);
inline const std::vector<uint8_t> kTestKeyGcm128_2 = Bytes("21ZYXWVUTSRQPONMLKJIHGFEDCBA", 28);
inline const std::vector<uint8_t> kTestKeyGcm256_1 = Bytes("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqr", 44);
inline const std::vector<uint8_t> kTestKeyGcm256_2 = Bytes("rqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA", 44);
inline size_t rtp_auth_tag_len(int suite) {
  return suite == tls::kSrtpAes128CmSha1_32 ? 4 : suite == tls::kSrtpAes128CmSha1_80 ? 10 : 16;
}
inline size_t rtcp_auth_tag_len(int suite) {
  return suite == tls::kSrtpAes128CmSha1_32 || suite == tls::kSrtpAes128CmSha1_80 ? 10 : 16;
}

// p2p/test/fake_packet_transport.h: delivers to its destination at once, and remembers the last packet sent.
class FakePacketTransport : public p2p::PacketTransportInternal {
 public:
  explicit FakePacketTransport(std::string name) : name_(std::move(name)) {}
  // Connects to `dest` (both ways unless `asymmetric`), which makes it writable and ready to send.
  void SetDestination(FakePacketTransport* dest, bool asymmetric = false) {
    dest_ = dest;
    if (dest && !asymmetric && dest != this) dest->SetDestination(this, true);
    SetWritable(dest != nullptr);
  }
  void SetWritable(bool writable) {
    if (writable_ == writable) return;
    writable_ = writable;
    if (writable_) NotifyReadyToSend(this, now);
    NotifyWritableState(this, now);
  }
  const std::optional<std::vector<uint8_t>>& last_sent_packet() const { return last_sent_packet_; }
  int sent = 0;
  Timestamp now = Timestamp::Seconds(1000);

  const std::string& transport_name() const override { return name_; }
  bool writable() const override { return writable_; }
  bool receiving() const override { return true; }
  int SendPacket(Timestamp t, std::span<const uint8_t> data, const p2p::PacketOptions& options, int) override {
    if (!dest_) return -1;
    ++sent;
    last_sent_packet_ = std::vector<uint8_t>(data.begin(), data.end());
    NotifySentPacket(this, p2p::SentPacketInfo{options.packet_id, t, data.size(), 0});
    const std::vector<uint8_t> copy = *last_sent_packet_;
    dest_->NotifyPacketReceived(p2p::ReceivedPacket{copy, p2p::SocketAddress(), t, p2p::ReceivedPacket::kNotDecrypted});
    return static_cast<int>(data.size());
  }
  int SetOption(p2p::SocketOption, int) override { return 0; }
  int GetError() override { return 0; }

 private:
  std::string name_;
  FakePacketTransport* dest_ = nullptr;
  bool writable_ = false;
  std::optional<std::vector<uint8_t>> last_sent_packet_;
};

// pc/test/rtp_transport_test_util.h's TransportObserver.
class TransportObserver : public RtpPacketSinkInterface {
 public:
  TransportObserver() = default;
  explicit TransportObserver(RtpTransportInternal* t) { Attach(t); }
  void Attach(RtpTransportInternal* t) {
    t->SubscribeRtcpPacketReceived(this, [this](const std::vector<uint8_t>& p, std::optional<Timestamp>) {
      OnRtcpPacketReceived(p);
    });
    t->SubscribeReadyToSend(this, [this](bool ready) { OnReadyToSend(ready); });
    t->SetUnDemuxableRtpPacketReceivedHandler([this](const RtpPacketReceived&) { ++un_demuxable_rtp_count_; });
    t->SubscribeSentPacket(this, [this](const p2p::SentPacketInfo&) { ++sent_packet_count_; });
  }
  void OnRtpPacket(const RtpPacketReceived& packet) override {
    ++rtp_count_;
    last_recv_rtp_packet_ = packet.buffer;
    last_rtp_ = packet.packet;
  }
  void OnRtcpPacketReceived(const std::vector<uint8_t>& packet) {
    ++rtcp_count_;
    last_recv_rtcp_packet_ = packet;
  }
  void OnReadyToSend(bool ready) {
    ++ready_to_send_signal_count_;
    ready_to_send_ = ready;
  }
  int rtp_count() const { return rtp_count_; }
  int un_demuxable_rtp_count() const { return un_demuxable_rtp_count_; }
  int rtcp_count() const { return rtcp_count_; }
  int sent_packet_count() const { return sent_packet_count_; }
  bool ready_to_send() const { return ready_to_send_; }
  int ready_to_send_signal_count() const { return ready_to_send_signal_count_; }
  const std::vector<uint8_t>& last_recv_rtp_packet() const { return last_recv_rtp_packet_; }
  const RtpPacket& last_rtp() const { return last_rtp_; }
  const std::vector<uint8_t>& last_recv_rtcp_packet() const { return last_recv_rtcp_packet_; }

 private:
  bool ready_to_send_ = false;
  int rtp_count_ = 0, un_demuxable_rtp_count_ = 0, rtcp_count_ = 0, sent_packet_count_ = 0;
  int ready_to_send_signal_count_ = 0;
  std::vector<uint8_t> last_recv_rtp_packet_;
  RtpPacket last_rtp_;
  std::vector<uint8_t> last_recv_rtcp_packet_;
};

inline RtpDemuxerCriteria PayloadTypeCriteria(uint8_t pt) {
  RtpDemuxerCriteria c;
  c.payload_types = {pt};
  return c;
}

inline void SetSeq(std::vector<uint8_t>& p, uint16_t seq) {
  p[2] = static_cast<uint8_t>(seq >> 8);
  p[3] = static_cast<uint8_t>(seq);
}

}  // namespace sm::pc::test
