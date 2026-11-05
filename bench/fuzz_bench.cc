// Fuzzing of the network parsers (10.4): each fuzz target's throughput over its committed seed corpus
// (fuzz/corpus/<target>/). bench/CMakeLists.txt compiles the targets' own sources (fuzz/<target>_fuzzer.cc) into
// sm_bench with LLVMFuzzerTestOneInput renamed to SmFuzz_<target>, and defines SM_BENCH_FUZZ_<target> for each target
// this build has (the set fuzz/CMakeLists.txt builds: data channel and TLS targets only with their modules). Not for
// wasm, where the fuzz targets are not built.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "sm_bench.h"

namespace {

using FuzzFn = int (*)(const uint8_t*, size_t);

// The DTLS targets' budgets depend on the TLS library: mbedTLS's handshakes take about ten times BoringSSL's.
#if SM_TLS_BACKEND_MBEDTLS
[[maybe_unused]] constexpr bool kMbedTls = true;
#else
[[maybe_unused]] constexpr bool kMbedTls = false;
#endif

// The corpus files in name order, and the empty input, as fuzz/replay_main.cc runs them.
std::vector<std::vector<uint8_t>> LoadCorpus(const char* name) {
  std::vector<std::filesystem::path> files;
  std::error_code ec;
  const std::filesystem::path dir = std::filesystem::path(SM_FUZZ_CORPUS_DIR) / name;
  for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
    if (e.is_regular_file()) files.push_back(e.path());
  }
  std::sort(files.begin(), files.end());
  std::vector<std::vector<uint8_t>> inputs;
  for (const auto& f : files) {
    std::ifstream in(f, std::ios::binary);
    inputs.emplace_back(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  if (!inputs.empty()) inputs.emplace_back();
  return inputs;
}

// One iteration runs the target over the whole corpus; the reported unit is one input (ns per input).
void RunCorpus(sm::bench::State& st, const char* corpus, FuzzFn fn) {
  const auto inputs = LoadCorpus(corpus);
  st.Counter("corpus_inputs", static_cast<double>(inputs.size()));
  if (inputs.empty()) {
    st.Require("corpus_inputs", 0, ">=", 1);
    return;
  }
  st.SetItemsPerIteration(static_cast<double>(inputs.size()));
  int sum = 0;
  for (auto _ : st) {
    for (const auto& in : inputs) sum += fn(in.data(), in.size());
  }
  sm::bench::DoNotOptimize(sum);
}

}  // namespace

// SM_FUZZ_BENCH(target, corpus, budget): obs/Fuzz_<target>, the target over corpus/<corpus>/; budget in ns per input.
#define SM_FUZZ_BENCH(target, corpus, budget)                    \
  extern "C" int SmFuzz_##target(const uint8_t* data, size_t size); \
  SM_BENCH(obs, Fuzz_##target) {                                 \
    RunCorpus(st, corpus, &SmFuzz_##target);                     \
    st.BudgetNsPerItem(budget);                                  \
  }

// Unit (each): one corpus input through the target's LLVMFuzzerTestOneInput.
#if SM_BENCH_FUZZ_h264_depacketizer
SM_FUZZ_BENCH(h264_depacketizer, "h264_depacketizer", 5000)
#endif
#if SM_BENCH_FUZZ_packet_buffer
SM_FUZZ_BENCH(packet_buffer, "packet_buffer", 100000)
#endif
#if SM_BENCH_FUZZ_red
SM_FUZZ_BENCH(red, "red", 2500)
#endif
#if SM_BENCH_FUZZ_rtcp
SM_FUZZ_BENCH(rtcp, "rtcp", 6000)
#endif
#if SM_BENCH_FUZZ_rtp_packet
SM_FUZZ_BENCH(rtp_packet, "rtp_packet", 2000)
#endif
#if SM_BENCH_FUZZ_stun
SM_FUZZ_BENCH(stun, "stun", 80000)
#endif
#if SM_BENCH_FUZZ_tcp_framing
SM_FUZZ_BENCH(tcp_framing, "tcp_framing", 40000)
#endif
#if SM_BENCH_FUZZ_transport_feedback
SM_FUZZ_BENCH(transport_feedback, "transport_feedback", 5000)
#endif
#if SM_BENCH_FUZZ_turn
SM_FUZZ_BENCH(turn, "turn", 600000)
#endif
#if SM_BENCH_FUZZ_ulpfec
SM_FUZZ_BENCH(ulpfec, "ulpfec", 60000)
#endif
#if SM_BENCH_FUZZ_dcsctp_packet
SM_FUZZ_BENCH(dcsctp_packet, "dcsctp_packet", 3000)
#endif
#if SM_BENCH_FUZZ_dcsctp_socket
SM_FUZZ_BENCH(dcsctp_socket, "dcsctp_socket", 40000)
#endif
#if SM_BENCH_FUZZ_dtls_record
SM_FUZZ_BENCH(dtls_record, "dtls_record", kMbedTls ? 8000000 : 800000)
#endif
#if SM_BENCH_FUZZ_dtls_transport
SM_FUZZ_BENCH(dtls_transport, "dtls_record", kMbedTls ? 8000000 : 800000)
#endif
#if SM_BENCH_FUZZ_sdp_parser
SM_FUZZ_BENCH(sdp_parser, "sdp_parser", 150000)
#endif
