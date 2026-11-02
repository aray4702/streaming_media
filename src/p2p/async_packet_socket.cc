// libwebrtc's rtc_base/async_packet_socket.cc at M157 (see the header).
#include "sm/p2p/async_packet_socket.h"

#include <cassert>

namespace sm::p2p {

void AsyncPacketSocket::RegisterReceivedPacketCallback(ReceivedCallback cb) {
  assert(!received_ && "one received-packet callback per socket");  // RTC_CHECK(!received_packet_callback_).
  received_ = std::move(cb);
}

// Callbacks may unsubscribe (or subscribe) while they run, so each loop
// walks a copy.
void AsyncPacketSocket::NotifySentPacket(const SentPacketInfo& info) {
  auto subscribers = sent_;
  for (auto& [tag, cb] : subscribers) cb(this, info);
}

void AsyncPacketSocket::NotifyReadyToSend() {
  auto subscribers = ready_;
  for (auto& [tag, cb] : subscribers) cb(this);
}

void AsyncPacketSocket::NotifyClosed(int err) {
  auto subscribers = closed_;
  for (auto& [tag, cb] : subscribers) cb(this, err);
}

}  // namespace sm::p2p
