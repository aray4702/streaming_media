// Observability (section 10) benchmarks: what a switched-off output costs (10.1), one connection stats collection
// (10.2, with sm::pc: SM_BENCH_WITH_PC), and a hot path with its event sink off and on (10.3, with section 3:
// SM_BENCH_WITH_LOSS). The fuzz targets' throughput (10.4) is in fuzz_bench.cc.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/logging.h"
#include "sm/core/metrics.h"
#include "sm/core/random.h"
#include "sm/core/units.h"
#include "sm_bench.h"

#if SM_BENCH_WITH_LOSS
#include "sm/loss/nack_tracker.h"
#endif

#if SM_BENCH_WITH_PC
#include <algorithm>
#include <deque>
#include <string_view>

#include "sm/p2p/basic_port_allocator.h"
#include "sm/p2p/dtls_transport.h"
#include "sm/p2p/network_manager.h"
#include "sm/p2p/p2p_transport_channel.h"
#include "sm/p2p/stun_crypto_tls.h"
#include "sm/pc/connection_stats.h"
#include "sm/sim/emulated_sockets.h"
#include "sm/tls/message_digest.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"
#endif

using namespace sm;

namespace {

// Read through a volatile so the compiler can't fold the null check away: the sink a component holds is only known
// at run time.
EventSink* volatile g_no_sink = nullptr;

#if SM_BENCH_WITH_PC
using namespace sm::p2p;

IpAddress Ip(std::string_view s) { return *IpAddress::FromString(s); }

// Two public hosts on the emulated network (10 ms access links), a P2PTransportChannel and a DtlsTransport each, as
// tests/unit/pc/connection_stats_test.cc sets them up (without the STUN server).
struct StatsFixture {
  struct Endpoint {
    std::unique_ptr<sim::EmulatedSocketServer> host;
    StaticNetworkManager network_manager;
    std::unique_ptr<BasicPortAllocator> allocator;
    std::unique_ptr<P2PTransportChannel> ch;
    std::unique_ptr<DtlsTransport> dtls;
    std::shared_ptr<const tls::SslIdentity> cert;
  };
  sim::ByteNetwork net;
  Endpoint ep[2];
  std::deque<Candidate> signaling[2];
  Timestamp now = Timestamp::Seconds(10);

  StatsFixture() {
    InstallStunCryptoFromTls();
    const IpAddress ips[2] = {Ip("11.11.11.11"), Ip("22.22.22.22")};
    const IceParameters ice[2] = {{"UF00", "TESTICEPWD00000000000000", false},
                                  {"UF01", "TESTICEPWD00000000000001", false}};
    sim::LinkConfig link;
    link.base_delay = TimeDelta::Millis(10);
    for (int i = 0; i < 2; ++i) {
      Endpoint& e = ep[i];
      e.host = std::make_unique<sim::EmulatedSocketServer>(
          net, net.AddHost(ips[i].v4(), sim::ByteNetwork::kPublicRealm, link, link));
      e.network_manager.AddInterface(ips[i]);
      e.allocator = std::make_unique<BasicPortAllocator>(&e.network_manager, e.host.get());
      e.allocator->set_flags(PORTALLOCATOR_DISABLE_RELAY | PORTALLOCATOR_DISABLE_TCP);
      e.allocator->set_step_delay(kMinimumStepDelay);
      e.ch = std::make_unique<P2PTransportChannel>(now, "0", ICE_CANDIDATE_COMPONENT_RTP, e.allocator.get());
      e.ch->SubscribeCandidateGathered(this, [this, i](IceTransportInternal*, const Candidate& c) {
        signaling[1 - i].push_back(c);
      });
      e.ch->SetIceParameters(ice[i]);
      e.ch->SetRemoteIceParameters(ice[1 - i]);
      e.ch->SetIceRole(i == 0 ? ICEROLE_CONTROLLING : ICEROLE_CONTROLLED);
      e.dtls = std::make_unique<DtlsTransport>(e.ch.get());
      e.cert = tls::SslIdentity::Create(i == 0 ? "stats-a" : "stats-b");
      e.dtls->SetLocalCertificate(e.cert);
      e.dtls->SetDtlsRole(i == 0 ? SslRole::kServer : SslRole::kClient);
    }
    for (int i = 0; i < 2; ++i) {
      auto fp = tls::SslFingerprint::Create(tls::kDigestSha256, *ep[1 - i].cert);
      ep[i].dtls->SetRemoteParameters(fp->algorithm, fp->digest, std::nullopt, now);
    }
    for (Endpoint& e : ep) e.ch->MaybeStartGathering(now);
  }
  ~StatsFixture() {
    for (Endpoint& e : ep) {
      e.dtls.reset();
      e.ch.reset();
    }
  }
  Timestamp NextEvent() const {
    Timestamp t = net.NextEvent();
    for (const Endpoint& e : ep) {
      t = std::min(t, e.ch->NextProcessTime());
      if (auto d = e.dtls->NextTimeout()) t = std::min(t, *d);
    }
    if (!signaling[0].empty() || !signaling[1].empty()) t = std::min(t, now);
    return t;
  }
  void Step(Timestamp t) {
    now = std::max(now, t);
    for (Endpoint& e : ep) {
      e.ch->Process(now);
      if (auto d = e.dtls->NextTimeout(); d && *d <= now) e.dtls->OnTimeout(now);
    }
    for (Endpoint& e : ep) e.host->Deliver(now);
    for (int i = 0; i < 2; ++i) {
      while (!signaling[i].empty()) {
        Candidate c = signaling[i].front();
        signaling[i].pop_front();
        ep[i].ch->AddRemoteCandidate(now, c);
      }
    }
  }
  template <class F>
  bool RunUntil(F stop, TimeDelta limit) {
    const Timestamp until = now + limit;
    for (int guard = 0; guard < 2000000; ++guard) {
      if (stop()) return true;
      const Timestamp t = NextEvent();
      if (t > until) {
        now = until;
        return stop();
      }
      Step(t);
    }
    return stop();
  }
  bool Connected() const {
    return ep[0].dtls->writable() && ep[1].dtls->writable() &&
           ep[0].ch->GetState() == IceTransportStateInternal::kCompleted &&
           ep[1].ch->GetState() == IceTransportStateInternal::kCompleted;
  }
};
#endif  // SM_BENCH_WITH_PC

#if SM_BENCH_WITH_LOSS
// The NACK tracker's receive path at 5% random loss (loss/NackTracker_PacketWith5PctLoss), with `events` as its
// event sink. Reports the events the sink got and a checksum of the NACK batches, which must not depend on the sink.
void NackTrackerHotPath(bench::State& st, EventSink* events) {
  loss::NackConfig cfg;
  cfg.events = events;
  loss::NackTracker n(cfg);
  Random rng(1);
  uint16_t seq = 0;
  int64_t t = 0;
  uint64_t checksum = 0;
  for (auto _ : st) {
    if (!rng.Bernoulli(0.05)) n.OnReceivedPacket(seq, false, Timestamp::Micros(t));
    ++seq;
    t += 1000;
    if ((seq & 7) == 0) {
      for (uint16_t s : n.GetNackBatch(Timestamp::Micros(t))) checksum = checksum * 31 + s;
    }
  }
  bench::DoNotOptimize(checksum);
}
#endif

}  // namespace

// ---- 10.1 Build and runtime switches -------------------------------------------------------------------------------

// Unit: one SM_LOG at info, compiled in but below the runtime severity (warnings, the default): the runtime check
// alone.
SM_BENCH(obs, Log_BelowRuntimeSeverity) {
  SetLogSeverity(LogSeverity::kWarning);
  int i = 0;
  for (auto _ : st) {
    SM_LOG(kInfo, "bench %d", i);
    ++i;
    bench::DoNotOptimize(i);
  }
  st.BudgetNsPerItem(5);
}

// Unit: one SM_EVENT with no sink (the runtime switch off): a null check.
SM_BENCH(obs, Event_NoSink) {
  int64_t i = 0;
  for (auto _ : st) {
    EventSink* sink = g_no_sink;
    SM_EVENT(sink, Timestamp::Micros(i), "bench", "event", i, i);
    ++i;
    bench::DoNotOptimize(i);
  }
  st.BudgetNsPerItem(3);
}

// Unit: one SM_EVENT into an EventLog ring (the runtime switch on), for comparison with Event_NoSink.
SM_BENCH(obs, Event_EventLog) {
  EventLog log(4096);
  EventSink* sink = &log;
  int64_t i = 0;
  for (auto _ : st) {
    SM_EVENT(sink, Timestamp::Micros(i), "bench", "event", i, i);
    ++i;
  }
  bench::DoNotOptimize(log.total());
  st.BudgetNsPerItem(20);
}

// Unit: one value added to a fixed-memory metrics Histogram (always on; read only when the app exports).
SM_BENCH(obs, Histogram_Add) {
  Histogram h;
  double v = 1.0;
  for (auto _ : st) {
    h.Add(v);
    v = v * 1.37 + 0.5;
    if (v > 1e6) v = 1.0;
  }
  bench::DoNotOptimize(h.count());
  st.BudgetNsPerItem(30);
}

// Unit: one export of a NACK tracker's metrics (about 20 counters and 2 histograms) through a MetricsRegistry as JSON,
// the cost an app pays only when it switches metrics on by exporting them.
#if SM_BENCH_WITH_LOSS
SM_BENCH(obs, MetricsRegistry_ExportJson) {
  loss::NackTracker n;
  for (uint16_t s = 0; s < 200; ++s) {
    if (s % 20 != 7) n.OnReceivedPacket(s, false, Timestamp::Millis(s));
  }
  MetricsRegistry reg;
  reg.Register("nack", &n);
  size_t bytes = 0;
  for (auto _ : st) bytes += reg.ToJson().size();
  bench::DoNotOptimize(bytes);
  st.BudgetNsPerItem(30000);
}
#endif

// ---- 10.2 ICE and DTLS connection stats ----------------------------------------------------------------------------

#if SM_BENCH_WITH_PC
// Unit: one pc::GetConnectionStats on a connected ICE + DTLS transport (transport, candidate pairs, candidates and
// both certificates).
SM_BENCH(obs, ConnectionStats_Collect) {
  StatsFixture f;
  if (!f.RunUntil([&] { return f.Connected(); }, TimeDelta::Seconds(10))) {
    st.Require("connected", 0, ">=", 1);
    return;
  }
  size_t pairs = 0;
  bool ok = true;
  for (auto _ : st) {
    rtp::StatsReport r;
    ok = pc::GetConnectionStats(f.now, *f.ep[0].dtls, &r) == ObsStatus::kOk && ok;
    pairs += r.candidate_pairs.size();
  }
  bench::DoNotOptimize(pairs);
#if SM_ENABLE_STATS
  st.Require("stats_ok", ok, ">=", 1);
#endif
  st.BudgetNsPerItem(40000);
}
#endif

// ---- 10.3 The observability-off check ------------------------------------------------------------------------------

#if SM_BENCH_WITH_LOSS
// Unit: one received packet through NackTracker at 5% loss with its event sink off (null); with
// NackTracker_EventsOn, the hot-path cost of switching events on.
SM_BENCH(obs, NackTracker_EventsOff) {
  NackTrackerHotPath(st, g_no_sink);
  st.BudgetNsPerItem(1500);
}

// Unit: as NackTracker_EventsOff, with an EventLog as the sink (gap, request and recovery events recorded).
SM_BENCH(obs, NackTracker_EventsOn) {
  EventLog log(4096);
  NackTrackerHotPath(st, &log);
  st.Counter("events_per_packet", st.iterations() ? double(log.total()) / double(st.iterations()) : 0);
  st.BudgetNsPerItem(1500);
}
#endif
