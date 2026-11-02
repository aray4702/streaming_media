// A fake ICE transport for the DTLS transport's tests (#1006): libwebrtc's p2p/test/fake_ice_transport.h (M157) on
// a simulated clock. Transports are wired to a destination (SetDestination, which makes them writable); packets
// travel through a FakeNetwork with a one-way delay, and optional loss, reordering, send filters and record
// combining, as the libwebrtc fake's filters and combine_outgoing_packets do.
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sm/p2p/ice_transport_internal.h"

namespace sm::p2p::test {

class FakeIceTransport;

// The simulated link and clock shared by the transports of one test.
class FakeNetwork {
 public:
  Timestamp now = Timestamp::Seconds(1000);
  TimeDelta delay = TimeDelta::Millis(10);  // One way.
  double loss = 0.0;                        // Random loss, both ways.
  double reorder = 0.0;                     // Chance a packet is held back by 1-3 extra ms.
  uint64_t seed = 1;

  struct InFlight {
    Timestamp at;
    uint64_t order;
    FakeIceTransport* to;
    std::vector<uint8_t> data;
  };
  std::vector<InFlight> flight;
  uint64_t next_order = 0;
  int dropped = 0;

  double Uniform() {  // xorshift64*, deterministic per seed.
    seed ^= seed >> 12;
    seed ^= seed << 25;
    seed ^= seed >> 27;
    return static_cast<double>((seed * 0x2545F4914F6CDD1DULL) >> 11) / 9007199254740992.0;
  }
  void Send(FakeIceTransport* to, std::vector<uint8_t> data) {
    if (loss > 0 && Uniform() < loss) {
      ++dropped;
      return;
    }
    Timestamp at = now + delay;
    if (reorder > 0 && Uniform() < reorder) at = at + TimeDelta::Millis(1 + static_cast<int64_t>(Uniform() * 3));
    flight.push_back({at, next_order++, to, std::move(data)});
  }
  // Delivers what is due at `now` (in send order among equal times).
  void DeliverDue();
};

class FakeIceTransport : public IceTransportInternal {
 public:
  // Returns true to drop the packet (set_packet_send_filter).
  using SendFilter = std::function<bool(std::span<const uint8_t>)>;

  FakeIceTransport(std::string name, int component, FakeNetwork* net)
      : name_(std::move(name)), component_(component), net_(net) {}

  // Connects to `dest` and becomes writable; unless `asymmetric`, `dest` connects back (SetDestination).
  void SetDestination(FakeIceTransport* dest, bool asymmetric = false) {
    if (dest_ == dest) return;
    dest_ = dest;
    if (dest && !asymmetric) dest->SetDestination(this, true);
    SetWritable(dest != nullptr);
  }
  void SetWritable(bool writable) {
    if (writable_ == writable) return;
    writable_ = writable;
    if (writable_) NotifyReadyToSend(this, net_->now);
    NotifyWritableState(this, net_->now);
  }
  void SetReceiving(bool receiving) {
    if (receiving_ == receiving) return;
    receiving_ = receiving;
    NotifyReceivingState(this, net_->now);
  }
  void set_rtt_estimate(std::optional<int> rtt_ms) { rtt_estimate_ = rtt_ms; }
  void set_packet_send_filter(SendFilter f) { send_filter_ = std::move(f); }
  // Combine every two consecutive outgoing packets into one (an endpoint that sends several records per packet).
  void combine_outgoing_packets(bool combine) { combine_ = combine; }

  // Packets this transport received, by kind (the libwebrtc test's received_dtls_* counters).
  int received_client_hellos = 0;
  int received_server_hellos = 0;
  int received_packets = 0;

  void Receive(std::span<const uint8_t> data) {
    ++received_packets;
    if (data.size() > 13 && data[0] == 22) {
      if (data[13] == 1) ++received_client_hellos;
      if (data[13] == 2) ++received_server_hellos;
    }
    NotifyPacketReceived(ReceivedPacket{data, SocketAddress(), net_->now, ReceivedPacket::kNotDecrypted});
  }

  // IceTransportInternal.
  const std::string& transport_name() const override { return name_; }
  bool writable() const override { return writable_; }
  bool receiving() const override { return receiving_; }
  int SendPacket(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options, int flags) override {
    (void)flags;
    if (!dest_) return -1;
    NotifySentPacket(this, SentPacketInfo{options.packet_id, now, data.size(), 0});
    if (send_filter_ && send_filter_(data)) return static_cast<int>(data.size());
    if (combine_) {
      pending_.insert(pending_.end(), data.begin(), data.end());
      if (++pending_count_ < 2) return static_cast<int>(data.size());
      pending_count_ = 0;
      net_->Send(dest_, std::exchange(pending_, {}));
      return static_cast<int>(data.size());
    }
    net_->Send(dest_, std::vector<uint8_t>(data.begin(), data.end()));
    return static_cast<int>(data.size());
  }
  int SetOption(SocketOption, int) override { return 0; }
  int GetError() override { return 0; }
  IceTransportStateInternal GetState() const override {
    return writable_ ? IceTransportStateInternal::kCompleted : IceTransportStateInternal::kConnecting;
  }
  IceTransportState GetIceTransportState() const override {
    return writable_ ? IceTransportState::kConnected : IceTransportState::kChecking;
  }
  int component() const override { return component_; }
  IceRole GetIceRole() const override { return role_; }
  void SetIceRole(IceRole role) override { role_ = role; }
  void SetIceParameters(const IceParameters&) override {}
  void SetRemoteIceParameters(const IceParameters&) override {}
  void SetRemoteIceMode(IceMode) override {}
  void SetIceConfig(const IceConfig& config) override { config_ = config; }
  const IceConfig& config() const override { return config_; }
  void MaybeStartGathering(Timestamp) override {}
  std::optional<int> GetRttEstimate() override { return rtt_estimate_; }

 private:
  std::string name_;
  int component_;
  FakeNetwork* net_;
  FakeIceTransport* dest_ = nullptr;
  bool writable_ = false;
  bool receiving_ = false;
  IceRole role_ = IceRole::kUnknown;
  IceConfig config_;
  std::optional<int> rtt_estimate_;
  SendFilter send_filter_;
  bool combine_ = false;
  int pending_count_ = 0;
  std::vector<uint8_t> pending_;
};

inline void FakeNetwork::DeliverDue() {
  for (;;) {
    auto best = flight.end();
    for (auto it = flight.begin(); it != flight.end(); ++it) {
      if (it->at <= now && (best == flight.end() || it->at < best->at ||
                            (it->at == best->at && it->order < best->order))) {
        best = it;
      }
    }
    if (best == flight.end()) return;
    FakeIceTransport* to = best->to;
    std::vector<uint8_t> data = std::move(best->data);
    flight.erase(best);
    to->Receive(data);
  }
}

}  // namespace sm::p2p::test
