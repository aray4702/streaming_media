// apps/net/os_dns_resolver.h: getaddrinfo on host-owned worker threads (#1152).
#include "os_dns_resolver.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace sm::interop {

namespace {

struct Job {
  uint64_t id = 0;
  std::string hostname;
  std::vector<p2p::IpAddress> ips;
  int error = 0;
};

}  // namespace

// The queues the factory shares with its workers; a worker holds it until it exits, so a factory destroyed while a
// lookup runs leaves the worker nothing dangling.
struct OsDnsResolverFactory::Shared {
  std::mutex mu;
  std::condition_variable cv;
  bool stop = false;
  int workers = 0;
  int idle = 0;
  std::deque<Job> todo;
  std::deque<Job> done;

  static void WorkerLoop(std::shared_ptr<Shared> s) {
    std::unique_lock<std::mutex> lock(s->mu);
    for (;;) {
      ++s->idle;
      s->cv.wait(lock, [&] { return s->stop || !s->todo.empty(); });
      --s->idle;
      if (s->stop) break;
      Job job = std::move(s->todo.front());
      s->todo.pop_front();
      lock.unlock();
      job.error = OsDnsResolverFactory::Lookup(job.hostname, &job.ips);
      lock.lock();
      if (s->stop) break;
      s->done.push_back(std::move(job));
    }
    --s->workers;
  }
};

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

OsDnsResolverFactory::OsDnsResolverFactory() : shared_(std::make_shared<Shared>()) {}

OsDnsResolverFactory::~OsDnsResolverFactory() {
  {
    std::lock_guard<std::mutex> lock(shared_->mu);
    shared_->stop = true;
    shared_->todo.clear();
    shared_->done.clear();
  }
  shared_->cv.notify_all();
}

std::unique_ptr<p2p::AsyncDnsResolverInterface> OsDnsResolverFactory::Create() {
  return std::make_unique<Resolver>(this);
}

uint64_t OsDnsResolverFactory::Enqueue(Resolver* resolver, const std::string& hostname) {
  const uint64_t id = next_id_++;
  waiting_[id] = resolver;
  {
    std::lock_guard<std::mutex> lock(shared_->mu);
    shared_->todo.push_back(Job{id, hostname, {}, 0});
    if (shared_->idle == 0 && shared_->workers < kMaxWorkers) {
      ++shared_->workers;
      std::thread(Shared::WorkerLoop, shared_).detach();
    }
  }
  shared_->cv.notify_one();
  return id;
}

void OsDnsResolverFactory::Cancel(uint64_t id) {
  waiting_.erase(id);
  std::lock_guard<std::mutex> lock(shared_->mu);
  auto& todo = shared_->todo;
  auto& done = shared_->done;
  todo.erase(std::remove_if(todo.begin(), todo.end(), [id](const Job& j) { return j.id == id; }), todo.end());
  done.erase(std::remove_if(done.begin(), done.end(), [id](const Job& j) { return j.id == id; }), done.end());
}

void OsDnsResolverFactory::Poll(Timestamp now) {
  std::deque<Job> done;
  {
    std::lock_guard<std::mutex> lock(shared_->mu);
    done.swap(shared_->done);
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
    p2p::IpAddress ip;
    if (cursor->ai_family == AF_INET) {
      const auto* sin = reinterpret_cast<const sockaddr_in*>(cursor->ai_addr);
      ip = p2p::IpAddress(ntohl(sin->sin_addr.s_addr));
    } else if (cursor->ai_family == AF_INET6) {
      const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(cursor->ai_addr);
      std::array<uint8_t, 16> bytes;
      std::memcpy(bytes.data(), &sin6->sin6_addr, 16);
      ip = p2p::IpAddress::V6(bytes);
    } else {
      continue;
    }
    // getaddrinfo lists one entry per socket type: keep each address once.
    if (std::find(ips->begin(), ips->end(), ip) == ips->end()) ips->push_back(ip);
  }
  freeaddrinfo(result);
  return 0;
}

}  // namespace sm::interop
