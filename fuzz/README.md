# Fuzz targets

libFuzzer targets for the parsers that read network input (#989, requirement
C10 of milestone 3, #944). Each `<name>_fuzzer.cc` is one
`LLVMFuzzerTestOneInput`:

| Target | What it feeds | libwebrtc's counterpart (`test/fuzzers/`) |
|---|---|---|
| `rtp_packet` | `RtpPacket::Parse`, the RFC 8285 extensions, a serialize/parse round trip | `rtp_packet_fuzzer.cc` |
| `rtcp` | `SplitCompoundRtcp`, every RTCP block parser, `rtp::RtcpEngine::OnRtcp` | `rtcp_receiver_fuzzer.cc` |
| `transport_feedback` | `cc::ParseTransportFeedback`, its arrival times, a round trip | `transport_feedback_packet_loss_tracker_fuzzer.cc` |
| `h264_depacketizer` | `rtp::H264Depacketizer::Parse` and `Reassemble` | `h264_depacketizer_fuzzer.cc` |
| `packet_buffer` | `timing::PacketBuffer` with fuzzed sequence numbers, timestamps and bits | `packet_buffer_fuzzer.cc` |
| `red` | `loss::ParseRed`, `loss::SplitRed` | `neteq_rtp_fuzzer.cc` (RED through NetEq) |
| `ulpfec` | `loss::ulpfec::UlpfecReceiver`, `ForwardErrorCorrection::DecodeFec` | `ulpfec_receiver_fuzzer.cc`, `ulpfec_decoder_fuzzer.cc` |
| `stun` | `p2p::StunMessage::Read` as STUN, TURN and ICE messages, integrity and FINGERPRINT checks, a write/read round trip, `StunRequestManager::CheckResponse` | `stun_parser_fuzzer.cc`, `stun_validator_fuzzer.cc` |

Of libwebrtc's `test/fuzzers/`, only the STUN fuzzers are in
`compare/webrtc/reference/` (#1019; `net/dcsctp/fuzzers/` is too). `stun`
follows their code; the other targets follow libwebrtc's file names and what
each one feeds, not its code. The multi-packet input formats are this
library's own and are documented at the top of each target.

## The corpus replay test (every build)

With tests on (not on wasm), each target is also built with `replay_main.cc`
as `sm_fuzz_replay_<name>`, and ctest runs it over `corpus/<name>/` as
`fuzz.<name>`. The native and native-asan presets run them, so a crash or a
sanitizer report on a known input fails the ordinary gate without a fuzzing
job. `ctest --preset native -L fuzz` runs only these.

## Fuzzing (clang)

```sh
cmake -S . -B out/fuzz -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DSM_BUILD_FUZZERS=ON \
      -DSM_WITH_TLS=OFF -DSM_BUILD_BENCH=OFF -DSM_BUILD_TESTBENCH=OFF -DSM_BUILD_EXAMPLES=OFF
cmake --build out/fuzz -j 4
mkdir -p /tmp/rtcp && out/fuzz/fuzz/rtcp_fuzzer /tmp/rtcp fuzz/corpus/rtcp -max_total_time=600
```

`SM_BUILD_FUZZERS` (default OFF) builds `<name>_fuzzer` with
`-fsanitize=fuzzer,address,undefined` and instruments every library target
for coverage (`-fsanitize=fuzzer-no-link`). Write new inputs to a scratch
directory as above, not into `corpus/`.

A crash is filed as a `sev:1` issue with its input (`crash-<sha1>`, attached
base64 or as a scenario), and the fix adds the input to `corpus/<name>/` so
the replay test keeps it fixed.

## The corpus

`corpus/` is generated from test bench calls by `tools/fuzz_corpus.py`
(`sm_testbench --packet-dump`): RTP of every kind (audio with RED, H.264
video, RTX, padding, ULPFEC over RED), compound RTCP, and records built from
them in the multi-packet targets' formats. Regenerate it, don't edit it:

```sh
python3 tools/fuzz_corpus.py --testbench out/native/testbench/sm_testbench
```

`corpus/stun/` comes from `tools/stun_fuzz_corpus.py` instead (#1017): the
sample packets of `tests/unit/p2p/stun_test.cc` (libwebrtc's
`stun_unittest.cc` packets and the RFC 5769 vectors), the binding exchanges
of the test STUN server (`sim/stun_server.h`), signed ICE connectivity checks
and their responses, and a TURN Allocate with its 401:

```sh
python3 tools/stun_fuzz_corpus.py
```

The `stun` target links the STUN tests' HMAC-SHA1 and MD5
(`tests/unit/p2p/test_crypto.cc`) for STUN's crypto hooks, so integrity checks
run to the comparison and the RFC 5769 vectors check OK.

Later parts (#989) add DTLS records, SDP and SCTP as those parsers land.
