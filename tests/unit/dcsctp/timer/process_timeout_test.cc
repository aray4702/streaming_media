// Tests for sm/dcsctp/timer/process_timeout.h (#1060), the library's
// stand-in for libwebrtc M157's net/dcsctp/timer/task_queue_timeout.{h,cc}.
// The first cases follow task_queue_timeout_test.cc's (start and expire,
// stop, restart with a shorter or longer duration, several timeouts) with
// Process(now) in place of the simulated task queue; the last connects two
// DcSctpSockets whose only clock and timers are two ProcessTimeoutFactory
// instances.
#include "sm/dcsctp/timer/process_timeout.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gmock_lite.h"
#include "sm/core/units.h"
#include "sm/dcsctp/public/dcsctp_message.h"
#include "sm/dcsctp/public/dcsctp_options.h"
#include "sm/dcsctp/public/dcsctp_socket.h"
#include "sm/dcsctp/public/types.h"
#include "sm/dcsctp/socket/dcsctp_socket.h"

namespace sm::dcsctp {
namespace {
using ::testing::ElementsAre;
using ::testing::IsEmpty;

constexpr Timestamp kStart = Timestamp::Seconds(1000);

struct Recorder {
  std::vector<TimeoutID> fired;
  ProcessTimeoutFactory factory{[this](TimeoutID id) { fired.push_back(id); }, kStart};
};

SM_TEST(DcsctpProcessTimeoutTest, StartAndExpire) {
  Recorder r;
  std::unique_ptr<Timeout> t = r.factory.CreateTimeout();
  SM_EXPECT_TRUE(r.factory.NextProcessTime().IsPlusInfinity());
  t->Start(DurationMs(1000), TimeoutID(1));
  SM_EXPECT_EQ(r.factory.NextProcessTime(), kStart + TimeDelta::Millis(1000));
  r.factory.Process(kStart + TimeDelta::Millis(999));
  SM_EXPECT_THAT(r.fired, IsEmpty());
  r.factory.Process(kStart + TimeDelta::Millis(1000));
  SM_EXPECT_THAT(r.fired, ElementsAre(TimeoutID(1)));
  SM_EXPECT_TRUE(r.factory.NextProcessTime().IsPlusInfinity());
  // It fires once.
  r.factory.Process(kStart + TimeDelta::Millis(5000));
  SM_EXPECT_THAT(r.fired, ElementsAre(TimeoutID(1)));
}

SM_TEST(DcsctpProcessTimeoutTest, StopBeforeExpiring) {
  Recorder r;
  std::unique_ptr<Timeout> t = r.factory.CreateTimeout();
  t->Start(DurationMs(1000), TimeoutID(1));
  r.factory.Process(kStart + TimeDelta::Millis(500));
  t->Stop();
  SM_EXPECT_TRUE(r.factory.NextProcessTime().IsPlusInfinity());
  r.factory.Process(kStart + TimeDelta::Millis(2000));
  SM_EXPECT_THAT(r.fired, IsEmpty());
}

SM_TEST(DcsctpProcessTimeoutTest, RestartWithShorterAndLongerDuration) {
  Recorder r;
  std::unique_ptr<Timeout> t = r.factory.CreateTimeout();
  t->Start(DurationMs(1000), TimeoutID(1));
  r.factory.Process(kStart + TimeDelta::Millis(100));
  // Restarted at +100 ms for 200 ms: due at +300 ms, with the new id.
  t->Restart(DurationMs(200), TimeoutID(2));
  SM_EXPECT_EQ(r.factory.NextProcessTime(), kStart + TimeDelta::Millis(300));
  r.factory.Process(kStart + TimeDelta::Millis(300));
  SM_EXPECT_THAT(r.fired, ElementsAre(TimeoutID(2)));

  // Restarted for longer than first started: the first expiry passes.
  t->Start(DurationMs(100), TimeoutID(3));
  t->Restart(DurationMs(1000), TimeoutID(4));
  r.factory.Process(kStart + TimeDelta::Millis(400));
  SM_EXPECT_THAT(r.fired, ElementsAre(TimeoutID(2)));
  r.factory.Process(kStart + TimeDelta::Millis(1300));
  SM_EXPECT_THAT(r.fired, ElementsAre(TimeoutID(2), TimeoutID(4)));
}

SM_TEST(DcsctpProcessTimeoutTest, SeveralTimeoutsFireInExpiryOrder) {
  Recorder r;
  std::unique_ptr<Timeout> a = r.factory.CreateTimeout();
  std::unique_ptr<Timeout> b = r.factory.CreateTimeout(DelayPrecision::kHigh);
  std::unique_ptr<Timeout> c = r.factory.CreateTimeout();
  std::unique_ptr<Timeout> d = r.factory.CreateTimeout();
  a->Start(DurationMs(300), TimeoutID(1));
  b->Start(DurationMs(100), TimeoutID(2));
  c->Start(DurationMs(200), TimeoutID(3));
  d->Start(DurationMs(100), TimeoutID(4));
  SM_EXPECT_EQ(r.factory.NextProcessTime(), kStart + TimeDelta::Millis(100));
  // One late Process call: earliest expiry first, ties in creation order.
  r.factory.Process(kStart + TimeDelta::Millis(1000));
  SM_EXPECT_THAT(r.fired, ElementsAre(TimeoutID(2), TimeoutID(4), TimeoutID(3), TimeoutID(1)));
}

SM_TEST(DcsctpProcessTimeoutTest, CallbackMayRearmAndDelete) {
  std::unique_ptr<Timeout> t;
  std::unique_ptr<Timeout> other;
  std::vector<TimeoutID> fired;
  ProcessTimeoutFactory factory(
      [&](TimeoutID id) {
        fired.push_back(id);
        if (id == TimeoutID(1)) {
          t->Start(DurationMs(100), TimeoutID(5));  // Re-armed: due later.
          other.reset();                            // Deleted while due.
        }
      },
      kStart);
  t = factory.CreateTimeout();
  other = factory.CreateTimeout();
  t->Start(DurationMs(100), TimeoutID(1));
  other->Start(DurationMs(100), TimeoutID(2));
  factory.Process(kStart + TimeDelta::Millis(150));
  SM_EXPECT_THAT(fired, ElementsAre(TimeoutID(1)));
  SM_EXPECT_EQ(factory.NextProcessTime(), kStart + TimeDelta::Millis(250));
  factory.Process(kStart + TimeDelta::Millis(250));
  SM_EXPECT_THAT(fired, ElementsAre(TimeoutID(1), TimeoutID(5)));
  t.reset();
}

// A host's DcSctpSocketCallbacks on the library's time model.
class ProcessCallbacks : public DcSctpSocketCallbacks {
 public:
  explicit ProcessCallbacks(uint32_t random) : random_(random) {}

  SendPacketStatus SendPacketWithStatus(std::span<const uint8_t> data) override {
    sent.emplace_back(data.begin(), data.end());
    return SendPacketStatus::kSuccess;
  }
  std::unique_ptr<Timeout> CreateTimeout(DelayPrecision precision) override {
    return timeouts.CreateTimeout(precision);
  }
  Timestamp Now() override { return timeouts.now(); }
  uint32_t GetRandomInt(uint32_t low, uint32_t high) override {
    return low + random_ % (high - low);
  }
  void OnMessageReceived(DcSctpMessage message) override { received.push_back(std::move(message)); }
  void OnError(ErrorKind, std::string_view) override {}
  void OnAborted(ErrorKind, std::string_view) override { aborted = true; }
  void OnConnected() override { connected = true; }
  void OnClosed() override {}
  void OnConnectionRestarted() override {}
  void OnStreamsResetFailed(std::span<const StreamID>, std::string_view) override {}
  void OnStreamsResetPerformed(std::span<const StreamID>) override {}
  void OnIncomingStreamsReset(std::span<const StreamID>) override {}

  DcSctpSocketInterface* socket = nullptr;
  ProcessTimeoutFactory timeouts{[this](TimeoutID id) { socket->HandleTimeout(id); }, kStart};
  std::deque<std::vector<uint8_t>> sent;
  std::vector<DcSctpMessage> received;
  bool connected = false;
  bool aborted = false;

 private:
  uint32_t random_;
};

SM_TEST(DcsctpProcessTimeoutTest, TwoSocketsConnectThroughLostInitAndExchangeAMessage) {
  ProcessCallbacks cb_a(1234);
  ProcessCallbacks cb_z(5678);
  DcSctpOptions options;
  DcSctpSocket a("A", cb_a, nullptr, options);
  DcSctpSocket z("Z", cb_z, nullptr, options);
  cb_a.socket = &a;
  cb_z.socket = &z;

  a.Connect();
  // The first INIT is lost.
  SM_ASSERT_EQ(cb_a.sent.size(), 1u);
  cb_a.sent.clear();
  // T1-init: t1_init_timeout (dcsctp_options.h, 1000 ms at M157).
  const Timestamp t1 = cb_a.timeouts.NextProcessTime();
  SM_EXPECT_EQ(t1, kStart + options.t1_init_timeout.ToTimeDelta());

  Timestamp now = kStart;
  for (int step = 0; step < 200 && !(cb_a.connected && cb_z.connected); ++step) {
    now = now + TimeDelta::Millis(10);
    cb_a.timeouts.Process(now);
    cb_z.timeouts.Process(now);
    while (!cb_a.sent.empty() || !cb_z.sent.empty()) {
      while (!cb_a.sent.empty()) {
        z.ReceivePacket(cb_a.sent.front());
        cb_a.sent.pop_front();
      }
      while (!cb_z.sent.empty()) {
        a.ReceivePacket(cb_z.sent.front());
        cb_z.sent.pop_front();
      }
    }
  }
  SM_ASSERT_TRUE(cb_a.connected);
  SM_ASSERT_TRUE(cb_z.connected);
  // Connected only after the INIT was resent at T1-init's expiry.
  SM_EXPECT_GE(now, t1);
  SM_EXPECT_LT(now, t1 + TimeDelta::Millis(20));

  a.Send(DcSctpMessage(StreamID(1), PPID(51), std::vector<uint8_t>{1, 2, 3}), SendOptions());
  for (int step = 0; step < 100 && cb_z.received.empty(); ++step) {
    while (!cb_a.sent.empty()) {
      z.ReceivePacket(cb_a.sent.front());
      cb_a.sent.pop_front();
    }
    while (!cb_z.sent.empty()) {
      a.ReceivePacket(cb_z.sent.front());
      cb_z.sent.pop_front();
    }
    now = now + TimeDelta::Millis(10);
    cb_a.timeouts.Process(now);
    cb_z.timeouts.Process(now);
  }
  SM_ASSERT_EQ(cb_z.received.size(), 1u);
  SM_EXPECT_EQ(cb_z.received[0].stream_id(), StreamID(1));
  SM_EXPECT_EQ(cb_z.received[0].ppid(), PPID(51));
  SM_EXPECT_THAT(cb_z.received[0].payload(), ElementsAre(1, 2, 3));
  SM_EXPECT_FALSE(cb_a.aborted || cb_z.aborted);
}

}  // namespace
}  // namespace sm::dcsctp
