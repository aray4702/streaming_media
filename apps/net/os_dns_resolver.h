// The native apps' resolver for remote hostname (mDNS `.local`) candidates (#1152): the library's
// AsyncDnsResolverFactoryInterface (sm/p2p/async_dns_resolver.h) over getaddrinfo(3), as libwebrtc's
// BasicAsyncDnsResolverFactory (rtc_base/async_dns_resolver.cc) runs getaddrinfo off the network thread.
//
// The library reads no clock and starts no thread, so the worker threads are the host's: the factory starts up to
// kMaxWorkers of them on demand. A resolution's callback runs on the host's thread, from Poll(now), which the host's
// loop calls with the `now` it read (beside OsSocketServer::Poll). getaddrinfo can't be cancelled and a `.local`
// name that nobody answers takes about 5 s to fail, so destroying the factory doesn't wait for the workers: they
// share the queues with it (as libwebrtc's resolver shares its state with its detached thread) and exit once their
// lookup returns.
//
// getaddrinfo resolves `.local` names over multicast DNS on macOS and iOS (mDNSResponder) and on Linux with nss-mdns
// (libnss-mdns, "mdns4_minimal" in /etc/nsswitch.conf); elsewhere a `.local` name fails to resolve and the candidate
// is dropped, as libwebrtc's resolver fails it. Not on Windows or wasm (the apps are POSIX only); in a browser the
// browser itself resolves mDNS.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_dns_resolver.h"

namespace sm::interop {

class OsDnsResolverFactory final : public p2p::AsyncDnsResolverFactoryInterface {
 public:
  static constexpr int kMaxWorkers = 4;

  OsDnsResolverFactory();
  // Returns at once; workers still in getaddrinfo exit when it returns, and their results are dropped.
  ~OsDnsResolverFactory() override;
  OsDnsResolverFactory(const OsDnsResolverFactory&) = delete;
  OsDnsResolverFactory& operator=(const OsDnsResolverFactory&) = delete;

  // Resolvers must not outlive the factory.
  std::unique_ptr<p2p::AsyncDnsResolverInterface> Create() override;

  // Runs the callbacks of the resolutions that finished, on the caller's thread, each with `now`.
  void Poll(Timestamp now);
  // Resolutions started and not yet delivered.
  int pending() const { return static_cast<int>(waiting_.size()); }

  // One blocking lookup, as a worker does it: the IPv4 and IPv6 addresses of `hostname` in getaddrinfo's order, or
  // its error (an EAI_* code). For tests and diagnostics.
  static int Lookup(const std::string& hostname, std::vector<p2p::IpAddress>* ips);

 private:
  class Resolver;
  struct Shared;
  uint64_t Enqueue(Resolver* resolver, const std::string& hostname);
  void Cancel(uint64_t id);

  std::shared_ptr<Shared> shared_;
  uint64_t next_id_ = 1;
  // The host thread's: the resolvers waiting for a result, by job id.
  std::map<uint64_t, Resolver*> waiting_;
};

}  // namespace sm::interop
