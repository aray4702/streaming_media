// Asynchronous name resolution for remote hostname (mDNS) candidates (#1152): libwebrtc M157's
// api/async_dns_resolver.h, the interface P2PTransportChannel resolves a remote `.local` candidate through
// (ResolveHostnameCandidate / OnCandidateResolved, compare/webrtc/reference/p2p/base/p2p_transport_channel.cc).
//
// The library's time model: nothing here reads a clock or starts a thread. A resolver's callback runs on the
// owner's thread, from the owner's loop, and carries the `now` it runs at (libwebrtc's callback takes none: it reads
// the clock). Destroying a resolver cancels its callback, as libwebrtc's does.
//
// Implementations:
// - FakeAsyncDnsResolverFactory (below): names set by the test, answered when the owner calls Process(now) (or held
//   until released), so tests and the simulator stay deterministic;
// - the native apps' OsDnsResolverFactory (apps/net/os_dns_resolver.h): getaddrinfo on worker threads the host owns
//   (on macOS and on Linux with nss-mdns, getaddrinfo resolves `.local` names over mDNS), delivered by its Poll(now).
// libwebrtc's PeerConnection always has one (BasicAsyncDnsResolverFactory); here the host passes one in
// (JsepTransportController::Config, P2PTransportChannel::Options). Without one, a hostname candidate is dropped, as
// libwebrtc's channel drops it without a factory.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/socket_address.h"

namespace sm::p2p {

class AsyncDnsResolverResult {
 public:
  virtual ~AsyncDnsResolverResult() = default;
  // True iff the address from Start was resolved to an address of `family`; then `addr` is a copy of that address
  // with the first such IP set (SetResolvedIP: it keeps the name).
  virtual bool GetResolvedAddress(IpFamily family, SocketAddress* addr) const = 0;
  // The resolver's error, 0 on success.
  virtual int GetError() const = 0;
};

class AsyncDnsResolverInterface {
 public:
  virtual ~AsyncDnsResolverInterface() = default;
  // Starts resolving the hostname of `addr`. `callback` runs once, later, on the owner's thread, with the time it
  // runs at; never from within Start.
  virtual void Start(const SocketAddress& addr, std::function<void(Timestamp now)> callback) = 0;
  // Valid once the callback has run.
  virtual const AsyncDnsResolverResult& result() const = 0;
};

class AsyncDnsResolverFactoryInterface {
 public:
  virtual ~AsyncDnsResolverFactoryInterface() = default;
  virtual std::unique_ptr<AsyncDnsResolverInterface> Create() = 0;
};

// A plain result: the address asked for and the IPs it resolved to (in the resolver's order), or an error.
class BasicAsyncDnsResolverResult final : public AsyncDnsResolverResult {
 public:
  void Set(const SocketAddress& addr, std::vector<IpAddress> ips, int error) {
    addr_ = addr;
    ips_ = std::move(ips);
    error_ = error;
  }
  bool GetResolvedAddress(IpFamily family, SocketAddress* addr) const override;
  int GetError() const override { return error_; }

 private:
  SocketAddress addr_;
  std::vector<IpAddress> ips_;
  int error_ = 0;
};

// Lower case, without a trailing dot: DNS names compare case-insensitively, and "a.local." is "a.local".
std::string NormalizeHostname(std::string_view hostname);

// The deterministic resolver (libwebrtc's tests use MockAsyncDnsResolver the same way, ResolverFactoryFixture).
class FakeAsyncDnsResolverFactory final : public AsyncDnsResolverFactoryInterface {
 public:
  // EAI_NONAME-like: the name is unknown.
  static constexpr int kErrorNotFound = 8;

  FakeAsyncDnsResolverFactory() = default;
  ~FakeAsyncDnsResolverFactory() override;
  FakeAsyncDnsResolverFactory(const FakeAsyncDnsResolverFactory&) = delete;
  FakeAsyncDnsResolverFactory& operator=(const FakeAsyncDnsResolverFactory&) = delete;

  // What `hostname` resolves to (case-insensitive, a trailing dot ignored). An empty list resolves with no address
  // (no error); an unknown name fails with kErrorNotFound.
  void SetAddresses(std::string_view hostname, std::vector<IpAddress> ips);
  // DelayResolution: while held, Process answers nothing (FireDelayedResolution is set_hold(false) + Process).
  void set_hold(bool hold) { hold_ = hold; }

  std::unique_ptr<AsyncDnsResolverInterface> Create() override;

  // Answers every resolution started before this call (unless held), in the order they started, each callback with
  // `now`. Resolutions started from a callback wait for the next call.
  void Process(Timestamp now);
  // `now` while something is waiting and not held; +infinity otherwise.
  Timestamp NextProcessTime(Timestamp now) const;

  size_t pending() const { return pending_.size(); }
  int created() const { return created_; }
  int started() const { return started_; }
  int answered() const { return answered_; }

 private:
  class Resolver;
  void Cancel(Resolver* r);

  std::map<std::string, std::vector<IpAddress>> hosts_;
  std::deque<Resolver*> pending_;
  bool hold_ = false;
  uint64_t next_seq_ = 0;
  int created_ = 0;
  int started_ = 0;
  int answered_ = 0;
};

}  // namespace sm::p2p
