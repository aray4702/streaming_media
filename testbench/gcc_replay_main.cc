// sm_gcc_replay: run this library's GccController (cc v2) open loop over a forward-link
// trace (issue #150).
//
// The prebuilt libwebrtc exposes neither LossBasedBweV2's state nor its inputs, so
// the way to see what holds libwebrtc's target is to give our controller libwebrtc's
// packets: the per-packet forward-link trace (`time_us,size,kind,seq,queue_bytes,
// outcome,arrival_us`) of either stack, with the packets that carry a transport-wide
// sequence number (audio, video, rtx, padding, fec) numbered in send order. The
// controller sees the sends (ALR detector, send history) and a transport-cc feedback
// every --feedback-ms that covers every sequence number up to the highest one that
// has arrived, with the ones still missing reported lost, and a later report that
// carries the late arrivals again, as the v2 feedback generator does. The reverse
// path takes --reverse-ms. The output is the BWE trace CSV of tools/bwe_trace.py, so
// the rows can be laid next to libwebrtc's own (`delay_based`, `loss_based`, `target`).
//
// Open loop: the sender does not react to the target, so this answers what our
// estimators decide on libwebrtc's input, not what the call would do.
//
//   sm_gcc_replay [--feedback-ms=50] [--reverse-ms=30] [--start-kbps=300] [--min-kbps=30]
//                 [--max-kbps=10000] trace.csv > replay.bwe.csv
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "call.h"
#include "sm/cc/send_side_bwe.h"
#include "sm/cc/transport_feedback.h"

namespace {

struct Pkt {
  int64_t send_us = 0;
  int64_t arrival_us = -1;  // -1: never arrives.
  int size = 0;
  uint16_t seq = 0;
};

constexpr int64_t kTickUs = 250;
constexpr int64_t kRefUnitUs = 64000;

bool CarriesTransportSeq(const std::string& kind) {
  return kind == "audio" || kind == "video" || kind == "rtx" || kind == "padding" || kind == "fec";
}

std::vector<Pkt> Load(const char* path) {
  std::ifstream in(path);
  if (!in) {
    std::fprintf(stderr, "cannot open %s\n", path);
    std::exit(2);
  }
  std::string line;
  std::getline(in, line);
  if (line.find("arrival_us") == std::string::npos) {
    std::fprintf(stderr, "%s has no arrival_us column (traces from PR #171 on)\n", path);
    std::exit(2);
  }
  std::vector<Pkt> out;
  while (std::getline(in, line)) {
    std::vector<std::string> f;
    std::stringstream ss(line);
    for (std::string c; std::getline(ss, c, ',');) f.push_back(c);
    if (f.size() < 7 || !CarriesTransportSeq(f[2])) continue;
    Pkt p;
    p.send_us = std::atoll(f[0].c_str());
    p.size = std::atoi(f[1].c_str());
    p.arrival_us = f[5] == "delivered" ? std::atoll(f[6].c_str()) : -1;
    out.push_back(p);
  }
  std::stable_sort(out.begin(), out.end(), [](const Pkt& a, const Pkt& b) { return a.send_us < b.send_us; });
  for (size_t i = 0; i < out.size(); ++i) out[i].seq = static_cast<uint16_t>(i);
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  int64_t feedback_ms = 50, reverse_ms = 30;
  int start_kbps = 300, min_kbps = 30, max_kbps = 10000;
  const char* path = nullptr;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--feedback-ms=", 0) == 0) feedback_ms = std::atoll(a.c_str() + 14);
    else if (a.rfind("--reverse-ms=", 0) == 0) reverse_ms = std::atoll(a.c_str() + 13);
    else if (a.rfind("--start-kbps=", 0) == 0) start_kbps = std::atoi(a.c_str() + 13);
    else if (a.rfind("--min-kbps=", 0) == 0) min_kbps = std::atoi(a.c_str() + 11);
    else if (a.rfind("--max-kbps=", 0) == 0) max_kbps = std::atoi(a.c_str() + 11);
    else if (a == "-h" || a == "--help") {
      std::printf("usage: sm_gcc_replay [--feedback-ms=N] [--reverse-ms=N] [--start-kbps=N] [--min-kbps=N] [--max-kbps=N] trace.csv\n");
      return 0;
    } else path = argv[i];
  }
  if (!path) {
    std::fprintf(stderr, "usage: sm_gcc_replay trace.csv (see --help)\n");
    return 2;
  }
  const std::vector<Pkt> pkts = Load(path);
  if (pkts.empty()) return 0;

  using namespace sm;
  std::vector<cc::BweTraceRow> rows;
  cc::GccConfig cfg;
  cfg.version = 2;
  cfg.min_rate = DataRate::KilobitsPerSec(min_kbps);
  cfg.start_rate = DataRate::KilobitsPerSec(start_kbps);
  cfg.max_rate = DataRate::KilobitsPerSec(max_kbps);
  cfg.bwe_trace = &rows;
  cc::GccController gcc(cfg);
  gcc.Start(Timestamp::Micros(0));

  int64_t last_arrival = 0;
  for (const Pkt& p : pkts) last_arrival = std::max(last_arrival, p.arrival_us);
  const int64_t end_us = std::max(pkts.back().send_us, last_arrival) + 1000000;

  struct Delivery {
    int64_t at_us;
    cc::TransportFeedback fb;
  };
  std::vector<Delivery> pending;
  size_t next_sent = 0;
  int64_t next_fb_us = feedback_ms * 1000, next_process_us = 25000;
  size_t next_cover = 0;    // First sequence number no feedback has covered yet.
  size_t reported_to = 0;   // Late arrivals below this index are re-reported once.
  std::vector<bool> reported_received(pkts.size(), false);
  uint8_t fb_count = 0;
  const int64_t kOverheadBytes = 28;  // UDP/IP, as SendSideController adds to every packet.

  for (int64_t t = 0; t <= end_us; t += 1000) {
    // Sends.
    while (next_sent < pkts.size() && pkts[next_sent].send_us <= t) {
      const Pkt& p = pkts[next_sent++];
      gcc.OnPacketSent(p.seq, Timestamp::Micros(p.send_us), DataSize::Bytes(p.size + kOverheadBytes));
    }
    // Feedback generation at the receiver.
    while (t >= next_fb_us) {
      const int64_t fb_us = next_fb_us;
      next_fb_us += feedback_ms * 1000;
      size_t hi = next_cover;  // One past the highest sequence number that has arrived.
      for (size_t i = next_cover; i < next_sent; ++i) {
        if (pkts[i].arrival_us >= 0 && pkts[i].arrival_us <= fb_us) hi = i + 1;
      }
      size_t lo = next_cover;  // Re-report late arrivals: the lowest one not yet reported received.
      for (size_t i = reported_to; i < next_cover; ++i) {
        if (!reported_received[i] && pkts[i].arrival_us >= 0 && pkts[i].arrival_us <= fb_us) {
          lo = i;
          break;
        }
      }
      if (hi <= lo) continue;
      cc::TransportFeedback fb;
      fb.base_seq = static_cast<uint16_t>(lo);
      fb.feedback_count = fb_count++;
      int64_t first_arrival = -1;
      for (size_t i = lo; i < hi; ++i) {
        if (pkts[i].arrival_us >= 0 && pkts[i].arrival_us <= fb_us) {
          first_arrival = first_arrival < 0 ? pkts[i].arrival_us : std::min(first_arrival, pkts[i].arrival_us);
        }
      }
      int64_t ref = first_arrival < 0 ? 0 : (first_arrival / kRefUnitUs) * kRefUnitUs;
      fb.reference_time = static_cast<int32_t>(ref / kRefUnitUs);
      int64_t prev = ref;
      for (size_t i = lo; i < hi; ++i) {
        cc::TransportFeedback::Packet fp;
        fp.seq = static_cast<uint16_t>(i);
        fp.received = pkts[i].arrival_us >= 0 && pkts[i].arrival_us <= fb_us;
        if (fp.received) {
          const int64_t ticks = (pkts[i].arrival_us - prev) / kTickUs;
          fp.delta_ticks = ticks;
          prev += ticks * kTickUs;
          reported_received[i] = true;
        }
        fb.packets.push_back(fp);
      }
      next_cover = hi;
      while (reported_to < next_cover && reported_received[reported_to]) ++reported_to;
      pending.push_back({fb_us + reverse_ms * 1000, std::move(fb)});
    }
    // Feedback arrival at the sender.
    for (size_t i = 0; i < pending.size();) {
      if (pending[i].at_us <= t) {
        gcc.OnTransportFeedback(pending[i].fb, Timestamp::Micros(pending[i].at_us));
        pending.erase(pending.begin() + static_cast<long>(i));
      } else {
        ++i;
      }
    }
    while (t >= next_process_us) {
      gcc.OnProcessInterval(Timestamp::Micros(next_process_us), DataRate::Zero());
      next_process_us += 25000;
    }
  }
  std::fputs(sm::testbench::BweTraceCsv(rows).c_str(), stdout);
  return 0;
}
