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
| `dcsctp_packet` | `dcsctp::SctpPacket::Parse` with and without the CRC-32C check, every chunk parser (`DebugConvertChunkToString`), SACK cleaning, parameter lists, a build/parse round trip; only with `SM_WITH_DATACHANNEL` (#1032) | `net/dcsctp/fuzzers/dcsctp_fuzzers.cc` (through the socket: `dcsctp_socket` below) |
| `dcsctp_socket` | `dcsctp::DcSctpSocket` from each of 10 starting states, driven by `FuzzSocket`'s actions (received chunks, sends, stream resets, expired timeouts, close); only with `SM_WITH_DATACHANNEL` (#1060) | `dcsctp_socket_fuzzer.cc` over `net/dcsctp/fuzzers/dcsctp_fuzzers.cc`, ported as `dcsctp_fuzzers.{h,cc}` |
| `stun` | `p2p::StunMessage::Read` as STUN, TURN and ICE messages, integrity and FINGERPRINT checks, a write/read round trip, `StunRequestManager::CheckResponse` | `stun_parser_fuzzer.cc`, `stun_validator_fuzzer.cc` |
| `turn` | A live TURN allocation (`TurnPort` on `sim::TurnServer` over the emulated network, a permission and channel 0x4000 bound): datagrams to `TurnPort::HandleIncomingPacket` (ChannelData with its framing and padding, Data indications, responses), to the server from the allocation's 5-tuple and from a new address (Allocate, Refresh, CreatePermission, ChannelBind, Send indications, ChannelData), and to the relayed address from the peer (#1058) | `turn_unwrap_fuzzer.cc` (its `UnwrapTurnPacket`, `media/base/turn_utils.cc`, is not ported; TURN's framing is unwrapped by the port and the server here) |
| `tcp_framing` | `AsyncTcpSocket` and `AsyncStunTcpSocket` (`ProcessInput`, `GetExpectedLength`) on the whole stream, then the stream over an emulated TCP connection in pieces into either socket or into a TURN server listening on TCP; the packets must tile the stream and match (#1058) | none (`rtc_base/async_tcp_socket.cc`, `p2p/base/async_stun_tcp_socket.cc`) |
| `dtls_record` | `tls::DtlsTransport::OnPacket` as client or server, before a handshake and after one with a real peer, at DTLS 1.3 and 1.2; built on both backends when `SM_TLS_CROSS_TEST` is on (`dtls_record`, `dtls_record_<other backend>`); only with `SM_WITH_TLS` (#1058) | none (what `OpenSSLStreamAdapter` reads records through) |
| `dtls_transport` | `p2p::DtlsTransport`'s receive path over the fake ICE transport, mid-handshake, after one, and with DTLS off, and `IsDtlsPacket`, `IsDtlsClientHelloPacket`, `IsDtlsHandshakePacket`, `ComputeDtlsPacketHash`, `PacketStash` on every datagram; replays `corpus/dtls_record/`; only with `SM_WITH_TLS` (#1058) | none (`p2p/dtls/dtls_transport.cc`, `p2p/dtls/dtls_utils.cc`) |

Of libwebrtc's `test/fuzzers/`, the STUN fuzzers (#1019) and the TURN, SDP
and SCTP ones (#1057) are in `compare/webrtc/reference/`
(`net/dcsctp/fuzzers/` is too). `stun` and `dcsctp_socket` follow their code;
the other targets follow libwebrtc's file names and what each one feeds, not
its code. libwebrtc M157 has no fuzzer for TCP framing or DTLS records, so
`tcp_framing`, `dtls_record` and `dtls_transport` name the functions they
feed instead. The multi-packet input formats are this
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

`corpus/turn/`, `corpus/tcp_framing/` and `corpus/dtls_record/` (#1058) are
written by their targets: with `SM_FUZZ_WRITE_SEEDS=<dir>` set, the first
input writes the seeds to `<dir>` (the target's header says which). `turn`'s
are the packets of its own setup (Allocate and its 401, CreatePermission,
ChannelBind, Send and Data indications, ChannelData) as records to each side;
`tcp_framing`'s are the messages of libwebrtc's TCP socket tests, whole and in
pieces; `dtls_record`'s are each side's flights of a real handshake at DTLS
1.3 and 1.2 and application data (random keys, so they are shapes for the
mutator rather than a replayable session):

```sh
touch /tmp/empty
for t in turn tcp_framing dtls_record; do
  rm -f fuzz/corpus/$t/*
  SM_FUZZ_WRITE_SEEDS=$PWD/fuzz/corpus/$t out/native/fuzz/sm_fuzz_replay_$t /tmp/empty
done
```

Later parts (#989) add SDP (with C6) and SCTP through the dcsctp socket (with
C7 part 2).
