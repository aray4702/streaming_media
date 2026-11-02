// A packet transport: what ICE gives the DTLS layer and DTLS gives the RTP/SCTP layers (#1006, part of DTLS-SRTP
// #991, milestone 3). libwebrtc's p2p/base/packet_transport_internal.{h,cc} at M157 (compare/webrtc/reference/),
// with the library's time model: nothing reads a clock or starts a thread, so SendPacket and the state signals carry
// `now`, and callbacks run on the caller's thread inside the call that caused them.
//
// Differences from libwebrtc, none of them on the wire:
// - Callback lists are std::function lists removed by tag (CallbackList), as in sm/p2p/async_packet_socket.h.
// - The network route (network_route(), SignalNetworkRouteChanged) is left out until ICE has candidate pairs to
//   report (#1002).
// - No SequenceChecker: like every module in the library, a transport is used from one thread at a time.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"

namespace sm::p2p {

// PacketFlags (p2p/dtls/dtls_transport_internal.h).
enum PacketFlags {
  kPfNormal = 0x00,      // A normal packet.
  kPfSrtpBypass = 0x01,  // An encrypted SRTP packet; DTLS passes it through untouched.
};

// rtc_base/callback_list.h's CallbackList with tags: receivers may be added or removed while it sends, and a
// receiver removed during a send is not called after that.
template <typename... Args>
class TaggedCallbackList {
 public:
  using Callback = std::function<void(Args...)>;
  void Add(const void* tag, Callback cb) { receivers_.push_back({tag, std::make_shared<Callback>(std::move(cb))}); }
  void Remove(const void* tag) {
    std::erase_if(receivers_, [tag](const Entry& e) {
      if (e.tag != tag) return false;
      *e.cb = nullptr;
      return true;
    });
  }
  void Send(Args... args) {
    const std::vector<Entry> snapshot = receivers_;
    for (const Entry& e : snapshot) {
      if (*e.cb) (*e.cb)(args...);
    }
  }
  bool empty() const { return receivers_.empty(); }

 private:
  struct Entry {
    const void* tag;
    std::shared_ptr<Callback> cb;
  };
  std::vector<Entry> receivers_;
};

class PacketTransportInternal {
 public:
  using StateCallback = std::function<void(PacketTransportInternal*, Timestamp now)>;
  using ReceivedCallback = std::function<void(PacketTransportInternal*, const ReceivedPacket&)>;
  using SentCallback = std::function<void(PacketTransportInternal*, const SentPacketInfo&)>;

  PacketTransportInternal(const PacketTransportInternal&) = delete;
  PacketTransportInternal& operator=(const PacketTransportInternal&) = delete;
  virtual ~PacketTransportInternal() = default;

  virtual const std::string& transport_name() const = 0;
  // The transport has been established.
  virtual bool writable() const = 0;
  // The transport has received a packet recently (each implementation says how recently).
  virtual bool receiving() const = 0;
  // Sends a packet. The bytes sent, or < 0 with GetError() set, as a POSIX send() would.
  virtual int SendPacket(Timestamp now, std::span<const uint8_t> data, const PacketOptions& options,
                         int flags = kPfNormal) = 0;
  virtual int SetOption(SocketOption opt, int value) = 0;
  virtual bool GetOption(SocketOption /*opt*/, int* /*value*/) { return false; }
  // The most recent error on this transport.
  virtual int GetError() = 0;

  // The writable state changed.
  void SubscribeWritableState(const void* tag, StateCallback cb) { writable_state_.Add(tag, std::move(cb)); }
  void UnsubscribeWritableState(const void* tag) { writable_state_.Remove(tag); }
  void NotifyWritableState(PacketTransportInternal* t, Timestamp now) { writable_state_.Send(t, now); }
  // Ready to send: more sensitive than writable (a writable transport's socket buffer may be full).
  void SubscribeReadyToSend(const void* tag, StateCallback cb) { ready_to_send_.Add(tag, std::move(cb)); }
  void UnsubscribeReadyToSend(const void* tag) { ready_to_send_.Remove(tag); }
  void NotifyReadyToSend(PacketTransportInternal* t, Timestamp now) { ready_to_send_.Send(t, now); }
  // The receiving state changed.
  void SubscribeReceivingState(const void* tag, StateCallback cb) { receiving_state_.Add(tag, std::move(cb)); }
  void UnsubscribeReceivingState(const void* tag) { receiving_state_.Remove(tag); }
  void NotifyReceivingState(PacketTransportInternal* t, Timestamp now) { receiving_state_.Send(t, now); }
  // Each received packet.
  void RegisterReceivedPacketCallback(const void* tag, ReceivedCallback cb) { received_.Add(tag, std::move(cb)); }
  void DeregisterReceivedPacketCallback(const void* tag) { received_.Remove(tag); }
  // Each packet sent.
  void SubscribeSentPacket(const void* tag, SentCallback cb) { sent_.Add(tag, std::move(cb)); }
  void UnsubscribeSentPacket(const void* tag) { sent_.Remove(tag); }
  void NotifySentPacket(PacketTransportInternal* t, const SentPacketInfo& info) { sent_.Send(t, info); }
  // The transport closed (one receiver, called once).
  void SetOnCloseCallback(std::function<void()> cb) { on_close_ = std::move(cb); }

 protected:
  PacketTransportInternal() = default;
  void NotifyPacketReceived(const ReceivedPacket& packet) { received_.Send(this, packet); }
  void NotifyOnClose() {
    if (auto cb = std::exchange(on_close_, nullptr)) cb();
  }

 private:
  TaggedCallbackList<PacketTransportInternal*, Timestamp> writable_state_, ready_to_send_, receiving_state_;
  TaggedCallbackList<PacketTransportInternal*, const ReceivedPacket&> received_;
  TaggedCallbackList<PacketTransportInternal*, const SentPacketInfo&> sent_;
  std::function<void()> on_close_;
};

}  // namespace sm::p2p
