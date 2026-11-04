// The native apps' resolver for remote hostname (mDNS `.local`) candidates (#1152): the library's
// AsyncDnsResolverFactoryInterface (sm/p2p/async_dns_resolver.h) over getaddrinfo(3), as libwebrtc's
// BasicAsyncDnsResolverFactory (rtc_base/async_dns_resolver.cc) runs getaddrinfo off the network thread.
//
// The library reads no clock and starts no thread, so the worker threads are the host's: the factory owns up to
// kMaxWorkers of them, started on demand and joined when it is destroyed. A resolution's callback runs on the host's
// thread, from Poll(now), which the host's loop calls with the `now` it read (beside OsSocketServer::Poll).
//
// getaddrinfo resolves `.local` names over multicast DNS on macOS (mDNSResponder) and on Linux with nss-mdns
// (libnss-mdns, "mdns4_minimal" in /etc/nsswitch.conf); elsewhere a `.local` name fails to resolve and the candidate
// is dropped, as libwebrtc's resolver fails it. Not on Windows or wasm (the apps are POSIX only); in a browser the
// browser itself resolves mDNS.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/async_dns_resolver.h"

namespace sm::interop {

class OsDnsResolverFactory final : public p2p::AsyncDnsResolverFactoryInterface {
 public:
  static constexpr int kMaxWorkers = 4;

  OsDnsResolverFactory() = default;
  // Joins the workers (a lookup in progress finishes first; getaddrinfo can't be cancelled).
  ~OsDnsResolverFactory() override;
  OsDnsResolverFactory(const OsDnsResolverFactory&) = delete;
  OsDnsResolverFactory& operator=(const OsDnsResolverFactory&) = delete;

  // Resolvers must not outlive the factory.
  std::unique_ptr<p2p::AsyncDnsResolverInterface> Create() override;

  // Runs the callbacks of the resolutions that finished, on the caller's thread, each with `now`.
  void Poll(Timestamp now);
  // Resolutions started and not yet delivered.
  int pending() const;

  // One blocking lookup, as a worker does it: the IPv4 and IPv6 addresses of `hostname` in getaddrinfo's order, or
  // its error (an EAI_* code). For tests and diagnostics.
  static int Lookup(const std::string& hostname, std::vector<p2p::IpAddress>* ips);

 private:
  class Resolver;
  struct Job {
    uint64_t id = 0;
    std::string hostname;
    std::vector<p2p::IpAddress> ips;
    int error = 0;
  };
  uint64_t Enqueue(Resolver* resolver, const std::string& hostname);
  void Cancel(uint64_t id);
  void WorkerLoop();

  mutable std::mutex mu_;
  std::condition_variable cv_;
  bool stop_ = false;
  int idle_workers_ = 0;
  std::deque<Job> todo_;  // Guarded by mu_.
  std::deque<Job> done_;  // Guarded by mu_.
  std::vector<std::thread> workers_;
  uint64_t next_id_ = 1;
  // The host thread's: the resolvers waiting for a result, by job id.
  std::map<uint64_t, Resolver*> waiting_;
};

}  // namespace sm::interop
