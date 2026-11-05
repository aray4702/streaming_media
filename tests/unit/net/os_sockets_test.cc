// The apps' OS sockets (apps/net/os_sockets.h): the arrival time of a datagram (#1168). A datagram that arrives
// while Poll waits is stamped with the time Poll reads it, as libwebrtc's AsyncUDPSocket stamps it with the clock
// at the read, not with the `now` Poll was given before the wait. Chrome's congestion control reads these times
// back from our transport-wide feedback; stamped with the time before the wait (up to 10 ms early on the browser
// interop peer), its 15-20 ms probe clusters measured too low a rate and its estimate toward our endpoint stalled.
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "os_sockets.h"
#include "sm/core/units.h"
#include "sm/p2p/async_packet_socket.h"
#include "sm/p2p/socket_address.h"
#include "sm_test.h"

using namespace sm;

namespace {

int64_t SteadyMicros() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Sends one datagram to `to` (127.0.0.1) from a plain socket after `delay`, and returns the steady time just
// before the send.
int64_t SendAfter(uint16_t to, std::chrono::milliseconds delay) {
  std::this_thread::sleep_for(delay);
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in sin;
  interop::ToSockaddr(p2p::SocketAddress("127.0.0.1", to), &sin);
  const uint8_t data[100] = {};
  const int64_t sent_at = SteadyMicros();
  ::sendto(fd, data, sizeof(data), 0, reinterpret_cast<sockaddr*>(&sin), sizeof(sin));
  ::close(fd);
  return sent_at;
}

}  // namespace

// A datagram that arrives 30 ms into Poll's wait is stamped no earlier than it was sent, and no later than Poll
// returned: Poll's `now` plus the time Poll ran until it read it. Before #1168 it carried `now` itself, 30 ms early.
SM_TEST(OsSockets, DatagramArrivingDuringTheWaitIsStampedWhenRead) {
  interop::OsSocketServer server;
  std::unique_ptr<p2p::AsyncPacketSocket> rx = server.CreateUdpSocket(p2p::SocketAddress("127.0.0.1", 0), 0, 0);
  SM_ASSERT_TRUE(rx != nullptr);
  std::vector<Timestamp> arrivals;
  rx->RegisterReceivedPacketCallback(
      [&](p2p::AsyncPacketSocket*, const p2p::ReceivedPacket& p) { arrivals.push_back(p.arrival_time); });

  // The loop's clock: `base` at steady time t0.
  const Timestamp base = Timestamp::Seconds(100);
  const int64_t t0 = SteadyMicros();
  int64_t sent_at = 0;
  std::thread sender([&] { sent_at = SendAfter(rx->GetLocalAddress().port(), std::chrono::milliseconds(30)); });
  int64_t polled_from = 0;
  while (arrivals.empty() && SteadyMicros() - t0 < 5'000'000) {
    polled_from = SteadyMicros();
    server.Poll(base + TimeDelta::Micros(polled_from - t0), TimeDelta::Seconds(1));
  }
  const int64_t returned_at = SteadyMicros();
  sender.join();
  SM_ASSERT_EQ(arrivals.size(), size_t{1});
  // Poll's wait began before the send (the sender sleeps 30 ms first), so the old stamp, `now`, precedes it.
  SM_EXPECT_LT(polled_from, sent_at);
  SM_EXPECT_GE((arrivals[0] - base).us(), sent_at - t0);
  SM_EXPECT_LE((arrivals[0] - base).us(), returned_at - t0);
}

// Two datagrams 20 ms apart, each read by its own Poll: each is stamped between its send and Poll's return, so
// their spacing follows the sender's (a probe cluster's receive interval), not when each Poll was called.
SM_TEST(OsSockets, ArrivalSpacingFollowsTheSender) {
  interop::OsSocketServer server;
  std::unique_ptr<p2p::AsyncPacketSocket> rx = server.CreateUdpSocket(p2p::SocketAddress("127.0.0.1", 0), 0, 0);
  SM_ASSERT_TRUE(rx != nullptr);
  std::vector<Timestamp> arrivals;
  rx->RegisterReceivedPacketCallback(
      [&](p2p::AsyncPacketSocket*, const p2p::ReceivedPacket& p) { arrivals.push_back(p.arrival_time); });

  const Timestamp base = Timestamp::Seconds(100);
  const int64_t t0 = SteadyMicros();
  int64_t sent[2] = {0, 0};
  std::thread sender([&] {
    const uint16_t port = rx->GetLocalAddress().port();
    sent[0] = SendAfter(port, std::chrono::milliseconds(15));
    sent[1] = SendAfter(port, std::chrono::milliseconds(20));
  });
  // Each Poll is given the clock at the call, then waits (up to 1 s) for the next datagram.
  while (arrivals.size() < 2 && SteadyMicros() - t0 < 5'000'000) {
    server.Poll(base + TimeDelta::Micros(SteadyMicros() - t0), TimeDelta::Seconds(1));
  }
  const int64_t returned_at = SteadyMicros();
  sender.join();
  SM_ASSERT_EQ(arrivals.size(), size_t{2});
  for (int i = 0; i < 2; ++i) {
    SM_EXPECT_GE((arrivals[i] - base).us(), sent[i] - t0);
    SM_EXPECT_LE((arrivals[i] - base).us(), returned_at - t0);
  }
  SM_EXPECT_GE((arrivals[1] - arrivals[0]).us(), 0);
}
