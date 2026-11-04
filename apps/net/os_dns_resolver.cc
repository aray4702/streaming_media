// apps/net/os_dns_resolver.h: getaddrinfo on host-owned worker threads (#1152).
#include "os_dns_resolver.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace sm::interop {

class OsDnsResolverFactory::Resolver final : public p2p::AsyncDnsResolverInterface {
 public:
  explicit Resolver(OsDnsResolverFactory* factory) : factory_(factory) {}
  ~Resolver() override {
    if (id_ != 0) factory_->Cancel(id_);
  }

  void Start(const p2p::SocketAddress& addr, std::function<void(Timestamp now)> callback) override {
    addr_ = addr;
    callback_ = std::move(callback);
    id_ = factory_->Enqueue(this, addr.hostname());
  }
  const p2p::AsyncDnsResolverResult& result() const override { return result_; }

  // On the host's thread, from Poll: `this` may be destroyed by the callback.
  void Deliver(Timestamp now, std::vector<p2p::IpAddress> ips, int error) {
    id_ = 0;
    result_.Set(addr_, std::move(ips), error);
    std::function<void(Timestamp)> callback = std::move(callback_);
    callback_ = nullptr;
    if (callback) callback(now);
  }

 private:
  OsDnsResolverFactory* const factory_;
  p2p::SocketAddress addr_;
  std::function<void(Timestamp)> callback_;
  p2p::BasicAsyncDnsResolverResult result_;
  uint64_t id_ = 0;
};

OsDnsResolverFactory::~OsDnsResolverFactory() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    stop_ = true;
    todo_.clear();
  }
  cv_.notify_all();
  for (std::thread& t : workers_) t.join();
}

std::unique_ptr<p2p::AsyncDnsResolverInterface> OsDnsResolverFactory::Create() {
  return std::make_unique<Resolver>(this);
}

uint64_t OsDnsResolverFactory::Enqueue(Resolver* resolver, const std::string& hostname) {
  const uint64_t id = next_id_++;
  waiting_[id] = resolver;
  {
    std::lock_guard<std::mutex> lock(mu_);
    todo_.push_back(Job{id, hostname, {}, 0});
    if (idle_workers_ == 0 && static_cast<int>(workers_.size()) < kMaxWorkers) {
      workers_.emplace_back([this] { WorkerLoop(); });
    }
  }
  cv_.notify_one();
  return id;
}

void OsDnsResolverFactory::Cancel(uint64_t id) {
  waiting_.erase(id);
  std::lock_guard<std::mutex> lock(mu_);
  todo_.erase(std::remove_if(todo_.begin(), todo_.end(), [id](const Job& j) { return j.id == id; }), todo_.end());
  done_.erase(std::remove_if(done_.begin(), done_.end(), [id](const Job& j) { return j.id == id; }), done_.end());
}

int OsDnsResolverFactory::pending() const { return static_cast<int>(waiting_.size()); }

void OsDnsResolverFactory::Poll(Timestamp now) {
  std::deque<Job> done;
  {
    std::lock_guard<std::mutex> lock(mu_);
    done.swap(done_);
  }
  for (Job& job : done) {
    // A resolver destroyed by an earlier callback has left `waiting_`.
    auto it = waiting_.find(job.id);
    if (it == waiting_.end()) continue;
    Resolver* r = it->second;
    waiting_.erase(it);
    r->Deliver(now, std::move(job.ips), job.error);
  }
}

void OsDnsResolverFactory::WorkerLoop() {
  std::unique_lock<std::mutex> lock(mu_);
  for (;;) {
    ++idle_workers_;
    cv_.wait(lock, [this] { return stop_ || !todo_.empty(); });
    --idle_workers_;
    if (stop_) return;
    Job job = std::move(todo_.front());
    todo_.pop_front();
    lock.unlock();
    job.error = Lookup(job.hostname, &job.ips);
    lock.lock();
    if (stop_) return;
    done_.push_back(std::move(job));
  }
}

int OsDnsResolverFactory::Lookup(const std::string& hostname, std::vector<p2p::IpAddress>* ips) {
  // rtc_base/async_dns_resolver.cc's ResolveHostname: getaddrinfo with AI_ADDRCONFIG, every IPv4 and IPv6 result in
  // order.
  ips->clear();
  addrinfo hints;
  std::memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_flags = AI_ADDRCONFIG;
  addrinfo* result = nullptr;
  const int ret = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
  if (ret != 0) return ret;
  for (addrinfo* cursor = result; cursor; cursor = cursor->ai_next) {
    if (cursor->ai_family == AF_INET) {
      const auto* sin = reinterpret_cast<const sockaddr_in*>(cursor->ai_addr);
      ips->push_back(p2p::IpAddress(ntohl(sin->sin_addr.s_addr)));
    } else if (cursor->ai_family == AF_INET6) {
      const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(cursor->ai_addr);
      std::array<uint8_t, 16> bytes;
      std::memcpy(bytes.data(), &sin6->sin6_addr, 16);
      ips->push_back(p2p::IpAddress::V6(bytes));
    }
  }
  freeaddrinfo(result);
  // getaddrinfo lists one entry per socket type: keep each address once.
  std::vector<p2p::IpAddress> unique;
  for (const p2p::IpAddress& ip : *ips) {
    if (std::find(unique.begin(), unique.end(), ip) == unique.end()) unique.push_back(ip);
  }
  ips->swap(unique);
  return 0;
}

}  // namespace sm::interop
