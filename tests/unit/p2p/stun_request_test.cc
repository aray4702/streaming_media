// STUN transactions (issue #997): every case of libwebrtc M157's
// p2p/base/stun_request_unittest.cc, with the library's time model in place
// of the simulated task queue, plus the test STUN server (p2p/test/
// stun_server) and binding requests through each NAT type on the emulated
// network.
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/byte_buffer.h"
#include "sm/p2p/stun.h"
#include "sm/p2p/stun_request.h"
#include "sm/sim/emulated_sockets.h"
#include "sm/sim/stun_server.h"
#include "sm_test.h"
#include "test_crypto.h"

using namespace sm;
using namespace sm::p2p;

namespace {

std::unique_ptr<StunMessage> CreateStunMessage(uint16_t type, const StunMessage* req = nullptr) {
  return std::make_unique<StunMessage>(type, req ? req->transaction_id() : StunMessage::GenerateTransactionId());
}

std::unique_ptr<StunMessage> CreateParsedStunMessage(uint16_t type, const std::string& transaction_id,
                                                     std::string_view password) {
  StunMessage msg(type, transaction_id);
  msg.AddMessageIntegrity(password);
  ByteBufferWriter writer;
  msg.Write(&writer);
  ByteBufferReader reader(writer);
  auto parsed = std::make_unique<StunMessage>();
  parsed->Read(&reader);
  return parsed;
}

// StunRequestTest: counts sends and records the outcome.
struct Fixture {
  Timestamp now = Timestamp::Seconds(12345);
  StunRequestManager manager;
  int request_count = 0;
  std::vector<Timestamp> sent_at;
  StunMessage* response = nullptr;
  bool success = false;
  bool failure = false;
  bool timeout = false;
  bool clear_on_response = false;  // StunRequestReentranceTest.

  Fixture()
      : manager([this](Timestamp t, std::span<const uint8_t>, StunRequest*) {
          ++request_count;
          sent_at.push_back(t);
        }) {
    testing::InstallTestCrypto();
  }
  void OnResponse(StunMessage* res) {
    if (clear_on_response) manager.Clear();
    response = res;
    success = true;
  }
  void OnErrorResponse(StunMessage* res) {
    if (clear_on_response) manager.Clear();
    response = res;
    failure = true;
  }
  // AdvanceTime: runs everything due up to `now + d`.
  void Advance(TimeDelta d) {
    const Timestamp end = now + d;
    for (Timestamp t = manager.NextProcessTime(); t <= end; t = manager.NextProcessTime()) manager.Process(t);
    now = end;
  }
};

// StunRequestThunker: an unauthenticated binding request.
class Thunker : public StunRequest {
 public:
  Thunker(StunRequestManager& m, Fixture* f) : StunRequest(m, CreateStunMessage(STUN_BINDING_REQUEST)), f_(f) {
    SetAuthenticationRequired(false);
  }
  std::unique_ptr<StunMessage> CreateResponseMessage(uint16_t type) { return CreateStunMessage(type, msg()); }

 private:
  void OnResponse(Timestamp, StunMessage* res) override { f_->OnResponse(res); }
  void OnErrorResponse(Timestamp, StunMessage* res) override { f_->OnErrorResponse(res); }
  void OnTimeout(Timestamp) override { f_->timeout = true; }
  Fixture* f_;
};

// AuthenticatedStunRequestThunker.
class AuthThunker : public StunRequest {
 public:
  AuthThunker(StunRequestManager& m, Fixture* f, std::string_view password)
      : StunRequest(m, Create(password)), f_(f) {
    SetAuthenticationRequired(true);
  }
  static std::unique_ptr<StunMessage> Create(std::string_view password) {
    auto req = CreateStunMessage(STUN_BINDING_REQUEST);
    req->AddMessageIntegrity(password);
    return req;
  }

 private:
  void OnResponse(Timestamp, StunMessage* res) override { f_->OnResponse(res); }
  void OnErrorResponse(Timestamp, StunMessage* res) override { f_->OnErrorResponse(res); }
  void OnTimeout(Timestamp) override { f_->timeout = true; }
  Fixture* f_;
};

}  // namespace

SM_TEST(StunRequest, TestSuccess) {
  Fixture f;
  auto request = std::make_unique<Thunker>(f.manager, &f);
  auto res = request->CreateResponseMessage(STUN_BINDING_RESPONSE);
  SM_ASSERT_TRUE(f.manager.Send(f.now, std::move(request)));
  SM_EXPECT_TRUE(f.manager.CheckResponse(f.now, res.get()));
  SM_EXPECT_TRUE(f.response == res.get());
  SM_EXPECT_TRUE(f.success);
  SM_EXPECT_FALSE(f.failure);
  SM_EXPECT_FALSE(f.timeout);
  SM_EXPECT_TRUE(f.manager.empty());
}

SM_TEST(StunRequest, TestError) {
  Fixture f;
  auto request = std::make_unique<Thunker>(f.manager, &f);
  auto res = request->CreateResponseMessage(STUN_BINDING_ERROR_RESPONSE);
  f.manager.Send(f.now, std::move(request));
  SM_EXPECT_TRUE(f.manager.CheckResponse(f.now, res.get()));
  SM_EXPECT_TRUE(f.response == res.get());
  SM_EXPECT_FALSE(f.success);
  SM_EXPECT_TRUE(f.failure);
  SM_EXPECT_FALSE(f.timeout);
}

SM_TEST(StunRequest, TestUnexpected) {
  Fixture f;
  auto request = std::make_unique<Thunker>(f.manager, &f);
  // A response with another id.
  auto res = CreateStunMessage(STUN_BINDING_RESPONSE);
  f.manager.Send(f.now, std::move(request));
  SM_EXPECT_FALSE(f.manager.CheckResponse(f.now, res.get()));
  SM_EXPECT_TRUE(f.response == nullptr);
  SM_EXPECT_FALSE(f.success);
  SM_EXPECT_FALSE(f.failure);
  SM_EXPECT_FALSE(f.timeout);
}

SM_TEST(StunRequest, TestBackoff) {
  Fixture f;
  auto request = std::make_unique<Thunker>(f.manager, &f);
  auto res = request->CreateResponseMessage(STUN_BINDING_RESPONSE);
  constexpr std::array<int, 9> kTotalDelaysMs = {0, 250, 750, 1750, 3750, 7750, 15750, 23750, 31750};
  const Timestamp start = f.now;
  f.manager.Send(f.now, std::move(request));
  // Just before the timeout, which would come at 39750 ms.
  f.Advance(TimeDelta::Millis(STUN_TOTAL_TIMEOUT - 1));
  SM_ASSERT_EQ(f.request_count, 9);
  for (size_t i = 0; i < kTotalDelaysMs.size(); ++i) SM_EXPECT_EQ((f.sent_at[i] - start).ms(), kTotalDelaysMs[i]);
  SM_EXPECT_TRUE(f.manager.CheckResponse(f.now, res.get()));
  SM_EXPECT_TRUE(f.response == res.get());
  SM_EXPECT_TRUE(f.success);
  SM_EXPECT_FALSE(f.failure);
  SM_EXPECT_FALSE(f.timeout);
}

SM_TEST(StunRequest, TestTimeout) {
  Fixture f;
  auto request = std::make_unique<Thunker>(f.manager, &f);
  auto res = request->CreateResponseMessage(STUN_BINDING_RESPONSE);
  f.manager.Send(f.now, std::move(request));
  f.Advance(TimeDelta::Millis(STUN_TOTAL_TIMEOUT));
  SM_EXPECT_FALSE(f.manager.CheckResponse(f.now, res.get()));
  SM_EXPECT_TRUE(f.response == nullptr);
  SM_EXPECT_FALSE(f.success);
  SM_EXPECT_FALSE(f.failure);
  SM_EXPECT_TRUE(f.timeout);
  SM_EXPECT_TRUE(f.manager.empty());
  SM_EXPECT_TRUE(f.manager.NextProcessTime().IsPlusInfinity());
}

SM_TEST(StunRequest, TestNoEmptyRequest) {
  Fixture f;
  auto request = std::make_unique<Thunker>(f.manager, &f);
  const std::string request_id = request->id();
  f.manager.Send(f.now, std::move(request), TimeDelta::Millis(100));
  SM_EXPECT_EQ(f.request_count, 0);
  SM_EXPECT_EQ((f.manager.NextProcessTime() - f.now).ms(), 100);
  StunMessage dummy_req(0, request_id);
  auto res = CreateStunMessage(STUN_BINDING_RESPONSE, &dummy_req);
  SM_EXPECT_TRUE(f.manager.CheckResponse(f.now, res.get()));
  SM_EXPECT_TRUE(f.response == res.get());
  SM_EXPECT_TRUE(f.success);
  SM_EXPECT_FALSE(f.failure);
  SM_EXPECT_FALSE(f.timeout);
  // The delayed first send never happens.
  f.Advance(TimeDelta::Seconds(1));
  SM_EXPECT_EQ(f.request_count, 0);
}

SM_TEST(StunRequest, TestUnrecognizedComprehensionRequiredAttribute) {
  Fixture f;
  auto request = std::make_unique<Thunker>(f.manager, &f);
  auto res = request->CreateResponseMessage(STUN_BINDING_ERROR_RESPONSE);
  f.manager.Send(f.now, std::move(request));
  res->AddAttribute(StunAttribute::CreateUInt32(0x7777));
  SM_EXPECT_FALSE(f.manager.CheckResponse(f.now, res.get()));
  SM_EXPECT_TRUE(f.response == nullptr);
  SM_EXPECT_FALSE(f.success);
  SM_EXPECT_FALSE(f.failure);
  SM_EXPECT_FALSE(f.timeout);
  // The transaction failed and is gone.
  SM_EXPECT_TRUE(f.manager.empty());
}

SM_TEST(StunRequest, ResponseValidatedWithDifferentPasswordRevalidates) {
  Fixture f;
  auto request = std::make_unique<AuthThunker>(f.manager, &f, "old_password");
  const std::string id = request->id();
  f.manager.Send(f.now, std::move(request));
  auto res = CreateParsedStunMessage(STUN_BINDING_RESPONSE, id, "old_password");
  // Checked first with the new remote password (Connection::OnReadPacket).
  SM_EXPECT_TRUE(res->ValidateMessageIntegrity("new_password") == StunMessage::IntegrityStatus::kIntegrityBad);
  SM_EXPECT_EQ(res->password(), std::string("new_password"));
  // CheckResponse revalidates with the request's password.
  SM_EXPECT_TRUE(f.manager.CheckResponse(f.now, res.get()));
  SM_EXPECT_TRUE(f.response == res.get());
  SM_EXPECT_TRUE(f.success);
}

SM_TEST(StunRequest, ResponseWithNewPasswordDoesNotMatchOldRequest) {
  Fixture f;
  auto request = std::make_unique<AuthThunker>(f.manager, &f, "old_password");
  const std::string id = request->id();
  f.manager.Send(f.now, std::move(request));
  // Signed with the new password: valid for it, not for the request.
  auto res = CreateParsedStunMessage(STUN_BINDING_RESPONSE, id, "new_password");
  SM_EXPECT_TRUE(res->ValidateMessageIntegrity("new_password") == StunMessage::IntegrityStatus::kIntegrityOk);
  SM_EXPECT_EQ(res->password(), std::string("new_password"));
  SM_EXPECT_FALSE(f.manager.CheckResponse(f.now, res.get()));
  SM_EXPECT_TRUE(f.response == nullptr);
  SM_EXPECT_FALSE(f.success);
}

SM_TEST(StunRequest, ResponseValidatedWithMatchingPassword) {
  Fixture f;
  auto request = std::make_unique<AuthThunker>(f.manager, &f, "password");
  const std::string id = request->id();
  f.manager.Send(f.now, std::move(request));
  auto res = CreateParsedStunMessage(STUN_BINDING_RESPONSE, id, "password");
  SM_EXPECT_TRUE(res->ValidateMessageIntegrity("password") == StunMessage::IntegrityStatus::kIntegrityOk);
  SM_EXPECT_EQ(res->password(), std::string("password"));
  SM_EXPECT_TRUE(f.manager.CheckResponse(f.now, res.get()));
  SM_EXPECT_TRUE(f.response == res.get());
  SM_EXPECT_TRUE(f.success);
}

SM_TEST(StunRequest, UnsignedResponseToAuthenticatedRequestIsRefused) {
  Fixture f;
  auto request = std::make_unique<AuthThunker>(f.manager, &f, "password");
  const std::string id = request->id();
  f.manager.Send(f.now, std::move(request));
  StunMessage res(STUN_BINDING_RESPONSE, id);  // No MESSAGE-INTEGRITY.
  SM_EXPECT_FALSE(f.manager.CheckResponse(f.now, &res));
  SM_EXPECT_FALSE(f.success);
  SM_EXPECT_FALSE(f.manager.empty());  // Still waiting.
}

SM_TEST(StunRequest, UnauthenticatedTurnResponses) {
  // An unauthenticated TURN request accepts only 401 and 300 errors.
  struct TurnThunker : public StunRequest {
    Fixture* f;
    TurnThunker(StunRequestManager& m, Fixture* fx) : StunRequest(m, CreateStunMessage(STUN_ALLOCATE_REQUEST)), f(fx) {
      SetAuthenticationRequired(false);
    }
    void OnResponse(Timestamp, StunMessage* res) override { f->OnResponse(res); }
    void OnErrorResponse(Timestamp, StunMessage* res) override { f->OnErrorResponse(res); }
  };
  for (int code : {0, 401, 300, 438}) {
    Fixture f;
    auto request = std::make_unique<TurnThunker>(f.manager, &f);
    const std::string id = request->id();
    f.manager.Send(f.now, std::move(request));
    std::unique_ptr<StunMessage> res;
    if (code == 0) {
      res = std::make_unique<StunMessage>(STUN_ALLOCATE_RESPONSE, id);
    } else {
      res = std::make_unique<StunMessage>(STUN_ALLOCATE_ERROR_RESPONSE, id);
      auto ec = StunAttribute::CreateErrorCode();
      ec->SetCode(code);
      res->AddAttribute(std::move(ec));
    }
    const bool accepted = f.manager.CheckResponse(f.now, res.get());
    SM_EXPECT_EQ(accepted, code == 401 || code == 300);
  }
}

SM_TEST(StunRequest, CheckResponseFromBytes) {
  Fixture f;
  auto request = std::make_unique<Thunker>(f.manager, &f);
  auto res = request->CreateResponseMessage(STUN_BINDING_RESPONSE);
  f.manager.Send(f.now, std::move(request));
  ByteBufferWriter w;
  res->Write(&w);
  // Too short, then the real thing.
  SM_EXPECT_FALSE(f.manager.CheckResponse(f.now, w.DataView().subspan(0, 19)));
  SM_EXPECT_TRUE(f.manager.CheckResponse(f.now, w.DataView()));
  SM_EXPECT_TRUE(f.success);
}

SM_TEST(StunRequest, FlushRestartsTheSchedule) {
  Fixture f;
  f.manager.Send(f.now, std::make_unique<Thunker>(f.manager, &f));
  SM_EXPECT_TRUE(f.manager.HasRequestForTest(STUN_BINDING_REQUEST));
  SM_EXPECT_FALSE(f.manager.HasRequestForTest(STUN_BINDING_INDICATION));
  f.Advance(TimeDelta::Millis(800));  // Sends at 0, 250, 750.
  SM_EXPECT_EQ(f.request_count, 3);
  f.manager.FlushForTest(f.now, kAllRequestsForTest);
  SM_EXPECT_EQ(f.request_count, 4);
  // The schedule starts over: the next send is 250 ms later.
  SM_EXPECT_EQ((f.manager.NextProcessTime() - f.now).ms(), 250);
}

SM_TEST(StunRequestReentrance, TestSuccess) {
  Fixture f;
  f.clear_on_response = true;
  auto request = std::make_unique<Thunker>(f.manager, &f);
  auto res = request->CreateResponseMessage(STUN_BINDING_RESPONSE);
  f.manager.Send(f.now, std::move(request));
  SM_EXPECT_TRUE(f.manager.CheckResponse(f.now, res.get()));
  SM_EXPECT_TRUE(f.response == res.get());
  SM_EXPECT_TRUE(f.success);
  SM_EXPECT_FALSE(f.failure);
  SM_EXPECT_FALSE(f.timeout);
}

SM_TEST(StunRequestReentrance, TestError) {
  Fixture f;
  f.clear_on_response = true;
  auto request = std::make_unique<Thunker>(f.manager, &f);
  auto res = request->CreateResponseMessage(STUN_BINDING_ERROR_RESPONSE);
  f.manager.Send(f.now, std::move(request));
  SM_EXPECT_TRUE(f.manager.CheckResponse(f.now, res.get()));
  SM_EXPECT_TRUE(f.response == res.get());
  SM_EXPECT_FALSE(f.success);
  SM_EXPECT_TRUE(f.failure);
  SM_EXPECT_FALSE(f.timeout);
}

SM_TEST(StunRequest, RefusesPredictableIds) {
  const StunCrypto saved = GetStunCrypto();
  SetStunCrypto(StunCrypto{});  // Nothing installed.
  int sent = 0;
  StunRequestManager manager([&sent](Timestamp, std::span<const uint8_t>, StunRequest*) { ++sent; });
  struct Plain : public StunRequest {
    explicit Plain(StunRequestManager& m) : StunRequest(m, CreateStunMessage(STUN_BINDING_REQUEST)) {
      SetAuthenticationRequired(false);
    }
  };
  SM_EXPECT_FALSE(manager.Send(Timestamp::Seconds(1), std::make_unique<Plain>(manager)));
  SM_EXPECT_EQ(sent, 0);
  SM_EXPECT_TRUE(manager.empty());
  manager.set_allow_predictable_ids_for_testing(true);
  SM_EXPECT_TRUE(manager.Send(Timestamp::Seconds(1), std::make_unique<Plain>(manager)));
  SM_EXPECT_EQ(sent, 1);
  SetStunCrypto(saved);
}

namespace {

constexpr uint32_t kServerIp = sim::SocketAddress::Ip(8, 8, 8, 8);
constexpr uint32_t kNatIp = sim::SocketAddress::Ip(5, 5, 5, 5);

sim::LinkConfig Link(int delay_ms, double loss = 0) {
  sim::LinkConfig c;
  c.base_delay = TimeDelta::Millis(delay_ms);
  c.loss_rate = loss;
  return c;
}

// A client's binding request over a socket; records the mapped address.
class BindingRequest : public StunRequest {
 public:
  BindingRequest(StunRequestManager& m, std::optional<SocketAddress>* mapped, bool* timed_out)
      : StunRequest(m, CreateStunMessage(STUN_BINDING_REQUEST)), mapped_(mapped), timed_out_(timed_out) {
    SetAuthenticationRequired(false);  // A STUN server's binding needs no credentials.
  }

 private:
  void OnResponse(Timestamp, StunMessage* res) override {
    if (const StunAddressAttribute* a = res->GetAddress(STUN_ATTR_XOR_MAPPED_ADDRESS)) *mapped_ = a->GetAddress();
  }
  void OnTimeout(Timestamp) override { *timed_out_ = true; }
  std::optional<SocketAddress>* mapped_;
  bool* timed_out_;
};

struct BindResult {
  std::optional<SocketAddress> mapped;
  bool timed_out = false;
  int sends = 0;
  int server_requests = 0;
  TimeDelta took = TimeDelta::Zero();
};

// A client behind `nat` (none for a public client) sends a binding request
// to a STUN server on the internet and runs the network until it is done.
BindResult BindThrough(std::optional<sim::NatType> nat, double uplink_loss = 0, uint64_t seed = 1) {
  sim::ByteNetwork net(seed);
  int realm = sim::ByteNetwork::kPublicRealm;
  uint32_t client_ip = sim::SocketAddress::Ip(1, 2, 3, 4);
  if (nat) {
    sim::NatConfig cfg;
    cfg.type = *nat;
    cfg.external_ip = kNatIp;
    realm = net.NatRealm(net.AddNat(cfg));
    client_ip = sim::SocketAddress::Ip(192, 168, 1, 2);
  }
  sim::EmulatedSocketServer client(net, net.AddHost(client_ip, realm, Link(10, uplink_loss), Link(10)));
  sim::EmulatedSocketServer server_host(net, net.AddHost(kServerIp, sim::ByteNetwork::kPublicRealm, Link(5), Link(5)));
  sim::StunServer stun(server_host.CreateUdpSocket(SocketAddress(server_host.ip(), 3478), 0, 0));

  auto socket = client.CreateUdpSocket(SocketAddress(client.ip(), 0), 0, 0);
  BindResult out;
  StunRequestManager manager([&](Timestamp now, std::span<const uint8_t> data, StunRequest*) {
    ++out.sends;
    socket->SendTo(now, data, stun.address(), {});
  });
  socket->RegisterReceivedPacketCallback(
      [&](AsyncPacketSocket*, const ReceivedPacket& p) { manager.CheckResponse(p.arrival_time, p.payload); });

  const Timestamp start = Timestamp::Seconds(1);
  manager.Send(start, std::make_unique<BindingRequest>(manager, &out.mapped, &out.timed_out));
  for (;;) {
    const Timestamp t = std::min(manager.NextProcessTime(), client.NextEvent());
    if (t.IsPlusInfinity() || t > start + TimeDelta::Seconds(60)) break;
    manager.Process(t);
    client.Deliver(t);
    server_host.Deliver(t);
    if (out.mapped || out.timed_out) {
      out.took = t - start;
      break;
    }
  }
  out.server_requests = stun.binding_requests();
  return out;
}

}  // namespace

SM_TEST(StunServer, BindingThroughEachNatType) {
  testing::InstallTestCrypto();
  // A public client sees its own address.
  BindResult open = BindThrough(std::nullopt);
  SM_ASSERT_TRUE(open.mapped.has_value());
  SM_EXPECT_EQ(open.mapped->ToString(), std::string("1.2.3.4:49152"));
  // 10 + 5 ms there, 5 + 10 ms back.
  SM_EXPECT_EQ(open.took.ms(), 30);
  SM_EXPECT_EQ(open.sends, 1);
  for (sim::NatType type : {sim::NatType::kOpenCone, sim::NatType::kAddrRestricted, sim::NatType::kPortRestricted,
                            sim::NatType::kSymmetric}) {
    // Behind every type, the server reports the NAT's mapping, and its reply
    // gets back in: it comes from the address the client sent to.
    BindResult r = BindThrough(type);
    SM_ASSERT_TRUE(r.mapped.has_value());
    SM_EXPECT_EQ(r.mapped->ToString(), std::string("5.5.5.5:49152"));
    SM_EXPECT_FALSE(r.timed_out);
  }
}

SM_TEST(StunServer, RetransmissionRecoversFromLoss) {
  testing::InstallTestCrypto();
  // 50% loss on the client's uplink: some seeds need resends, all succeed
  // within the 9 sends, and every resend follows libwebrtc's schedule.
  int resent = 0;
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    BindResult r = BindThrough(sim::NatType::kPortRestricted, 0.5, seed);
    SM_EXPECT_TRUE(r.mapped.has_value());
    if (r.sends > 1) ++resent;
    SM_EXPECT_GE(r.server_requests, 1);
  }
  SM_EXPECT_GT(resent, 0);
  // With every datagram lost, the request times out after 39.75 s and 9 sends.
  BindResult lost = BindThrough(sim::NatType::kPortRestricted, 1.0);
  SM_EXPECT_TRUE(lost.timed_out);
  SM_EXPECT_FALSE(lost.mapped.has_value());
  SM_EXPECT_EQ(lost.sends, 9);
  SM_EXPECT_EQ(lost.took.ms(), STUN_TOTAL_TIMEOUT);
  SM_EXPECT_EQ(lost.server_requests, 0);
}

SM_TEST(StunServer, AnswersOtherRequestsWith600) {
  testing::InstallTestCrypto();
  sim::ByteNetwork net;
  sim::EmulatedSocketServer a(net, net.AddHost(sim::SocketAddress::Ip(1, 1, 1, 1), 0, Link(0), Link(0)));
  sim::EmulatedSocketServer b(net, net.AddHost(kServerIp, 0, Link(0), Link(0)));
  sim::StunServer stun(b.CreateUdpSocket(SocketAddress(b.ip(), 3478), 0, 0));
  auto sock = a.CreateUdpSocket(SocketAddress(a.ip(), 0), 0, 0);
  std::vector<std::vector<uint8_t>> got;
  sock->RegisterReceivedPacketCallback([&](AsyncPacketSocket*, const ReceivedPacket& p) {
    got.emplace_back(p.payload.begin(), p.payload.end());
  });
  StunMessage alloc(STUN_ALLOCATE_REQUEST);
  ByteBufferWriter w;
  alloc.Write(&w);
  sock->SendTo(Timestamp::Seconds(1), w.DataView(), stun.address(), {});
  const uint8_t junk[] = {1, 2, 3};
  sock->SendTo(Timestamp::Seconds(1), junk, stun.address(), {});
  for (int ms = 1000; ms <= 1100; ++ms) {
    a.Deliver(Timestamp::Millis(ms));
    b.Deliver(Timestamp::Millis(ms));
  }
  SM_ASSERT_EQ(got.size(), 1u);  // The junk is dropped.
  StunMessage err;
  ByteBufferReader r(got[0]);
  SM_ASSERT_TRUE(err.Read(&r));
  SM_EXPECT_EQ(err.type(), STUN_ALLOCATE_ERROR_RESPONSE);
  SM_EXPECT_EQ(err.GetErrorCodeValue(), 600);
  SM_EXPECT_EQ(err.transaction_id(), alloc.transaction_id());
}
