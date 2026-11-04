// sm/p2p/async_dns_resolver.h: the plain result (rtc_base/async_dns_resolver.cc's AsyncDnsResolverResultImpl) and the
// deterministic resolver for tests and the simulator (#1152).
#include "sm/p2p/async_dns_resolver.h"

#include <algorithm>
#include <utility>

namespace sm::p2p {

bool BasicAsyncDnsResolverResult::GetResolvedAddress(IpFamily family, SocketAddress* addr) const {
  // AsyncDnsResolverResultImpl::GetResolvedAddress: the first IP of the family, set on a copy of the address asked
  // for (SetResolvedIP keeps its name).
  for (const IpAddress& ip : ips_) {
    if (ip.family() != family) continue;
    *addr = addr_;
    addr->SetResolvedIP(ip);
    return true;
  }
  return false;
}

std::string NormalizeHostname(std::string_view hostname) {
  if (!hostname.empty() && hostname.back() == '.') hostname.remove_suffix(1);
  std::string out(hostname);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

class FakeAsyncDnsResolverFactory::Resolver final : public AsyncDnsResolverInterface {
 public:
  explicit Resolver(FakeAsyncDnsResolverFactory* factory) : factory_(factory) {}
  ~Resolver() override { factory_->Cancel(this); }

  void Start(const SocketAddress& addr, std::function<void(Timestamp now)> callback) override {
    addr_ = addr;
    callback_ = std::move(callback);
    seq_ = factory_->next_seq_++;
    ++factory_->started_;
    factory_->pending_.push_back(this);
  }
  const AsyncDnsResolverResult& result() const override { return result_; }
  uint64_t seq() const { return seq_; }

  // Sets the result and runs the callback; `this` may be destroyed by it.
  void Answer(Timestamp now) {
    const auto it = factory_->hosts_.find(NormalizeHostname(addr_.hostname()));
    if (it == factory_->hosts_.end()) {
      result_.Set(addr_, {}, kErrorNotFound);
    } else {
      result_.Set(addr_, it->second, 0);
    }
    ++factory_->answered_;
    std::function<void(Timestamp)> callback = std::move(callback_);
    callback_ = nullptr;
    if (callback) callback(now);
  }

 private:
  FakeAsyncDnsResolverFactory* const factory_;
  SocketAddress addr_;
  std::function<void(Timestamp)> callback_;
  BasicAsyncDnsResolverResult result_;
  uint64_t seq_ = 0;
};

FakeAsyncDnsResolverFactory::~FakeAsyncDnsResolverFactory() = default;

void FakeAsyncDnsResolverFactory::SetAddresses(std::string_view hostname, std::vector<IpAddress> ips) {
  hosts_[NormalizeHostname(hostname)] = std::move(ips);
}

std::unique_ptr<AsyncDnsResolverInterface> FakeAsyncDnsResolverFactory::Create() {
  ++created_;
  return std::make_unique<Resolver>(this);
}

void FakeAsyncDnsResolverFactory::Cancel(Resolver* r) {
  pending_.erase(std::remove(pending_.begin(), pending_.end(), r), pending_.end());
}

void FakeAsyncDnsResolverFactory::Process(Timestamp now) {
  if (hold_) return;
  // Only the resolutions waiting now; a callback may start others (they wait) or destroy waiting ones (Cancel
  // removes them from the queue).
  const uint64_t limit = next_seq_;
  while (!pending_.empty() && pending_.front()->seq() < limit && !hold_) {
    Resolver* r = pending_.front();
    pending_.pop_front();
    r->Answer(now);
  }
}

Timestamp FakeAsyncDnsResolverFactory::NextProcessTime(Timestamp now) const {
  return !hold_ && !pending_.empty() ? now : Timestamp::PlusInfinity();
}

}  // namespace sm::p2p
