// The native apps' resolver for remote hostname candidates (#1152, apps/net/os_dns_resolver.h): getaddrinfo on the
// factory's worker threads, delivered on the caller's thread by Poll(now). Uses only names every host resolves
// without a network ("localhost") or never resolves (RFC 6761's ".invalid"); a `.local` name is resolved by the
// browser interop test (tests/interop/browser, --mdns).
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "os_dns_resolver.h"
#include "sm/core/units.h"
#include "sm/p2p/async_dns_resolver.h"
#include "sm/p2p/socket_address.h"
#include "sm_test.h"

using namespace sm;

namespace {

// Polls until `done` or about 10 s.
template <class F>
bool PollUntil(interop::OsDnsResolverFactory& factory, F done) {
  for (int i = 0; i < 1000 && !done(); ++i) {
    factory.Poll(Timestamp::Millis(1000 + i));
    if (!done()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return done();
}

}  // namespace

SM_TEST(OsDnsResolver, ResolvesLocalhost) {
  interop::OsDnsResolverFactory factory;
  std::unique_ptr<p2p::AsyncDnsResolverInterface> resolver = factory.Create();
  bool called = false;
  Timestamp called_at = Timestamp::MinusInfinity();
  resolver->Start(p2p::SocketAddress("localhost", 1234), [&](Timestamp now) {
    called = true;
    called_at = now;
  });
  // Never from within Start.
  SM_EXPECT_FALSE(called);
  SM_EXPECT_EQ(factory.pending(), 1);
  SM_ASSERT_TRUE(PollUntil(factory, [&] { return called; }));
  SM_EXPECT_TRUE(called_at.IsFinite());
  SM_EXPECT_EQ(resolver->result().GetError(), 0);
  p2p::SocketAddress v4, v6;
  const bool has_v4 = resolver->result().GetResolvedAddress(p2p::IpFamily::kV4, &v4);
  const bool has_v6 = resolver->result().GetResolvedAddress(p2p::IpFamily::kV6, &v6);
  SM_ASSERT_TRUE(has_v4 || has_v6);
  const p2p::SocketAddress& a = has_v4 ? v4 : v6;
  SM_EXPECT_TRUE(a.IsLoopbackIp());
  // The resolved address keeps the name and the port.
  SM_EXPECT_EQ(a.hostname(), std::string("localhost"));
  SM_EXPECT_EQ(a.port(), 1234);
  SM_EXPECT_FALSE(a.IsUnresolvedIP());
  SM_EXPECT_EQ(factory.pending(), 0);
}

SM_TEST(OsDnsResolver, UnknownNameFails) {
  interop::OsDnsResolverFactory factory;
  std::unique_ptr<p2p::AsyncDnsResolverInterface> resolver = factory.Create();
  bool called = false;
  resolver->Start(p2p::SocketAddress("no-such-host.invalid", 9), [&](Timestamp) { called = true; });
  SM_ASSERT_TRUE(PollUntil(factory, [&] { return called; }));
  SM_EXPECT_NE(resolver->result().GetError(), 0);
  p2p::SocketAddress a;
  SM_EXPECT_FALSE(resolver->result().GetResolvedAddress(p2p::IpFamily::kV4, &a));
}

// Destroying a resolver cancels its callback; the others still run, in the host's Poll.
SM_TEST(OsDnsResolver, DestroyedResolverIsNotCalled) {
  interop::OsDnsResolverFactory factory;
  auto r1 = factory.Create();
  auto r2 = factory.Create();
  int calls1 = 0, calls2 = 0;
  r1->Start(p2p::SocketAddress("localhost", 1), [&](Timestamp) { ++calls1; });
  r2->Start(p2p::SocketAddress("localhost", 2), [&](Timestamp) { ++calls2; });
  r1.reset();
  SM_EXPECT_EQ(factory.pending(), 1);
  SM_ASSERT_TRUE(PollUntil(factory, [&] { return calls2 == 1; }));
  for (int i = 0; i < 5; ++i) factory.Poll(Timestamp::Seconds(100 + i));
  SM_EXPECT_EQ(calls1, 0);
  SM_EXPECT_EQ(calls2, 1);
}

// The factory can go with lookups still running (its destructor joins the workers).
SM_TEST(OsDnsResolver, FactoryDestroyedWithLookupsInFlight) {
  auto factory = std::make_unique<interop::OsDnsResolverFactory>();
  std::vector<std::unique_ptr<p2p::AsyncDnsResolverInterface>> resolvers;
  for (int i = 0; i < 6; ++i) {
    resolvers.push_back(factory->Create());
    resolvers.back()->Start(p2p::SocketAddress("localhost", static_cast<uint16_t>(i + 1)), [](Timestamp) {});
  }
  resolvers.clear();
  factory.reset();
  SM_EXPECT_TRUE(factory == nullptr);
}

SM_TEST(OsDnsResolver, Lookup) {
  std::vector<p2p::IpAddress> ips;
  SM_EXPECT_EQ(interop::OsDnsResolverFactory::Lookup("127.0.0.1", &ips), 0);
  SM_ASSERT_EQ(ips.size(), 1u);
  SM_EXPECT_TRUE(ips[0] == *p2p::IpAddress::FromString("127.0.0.1"));
}
