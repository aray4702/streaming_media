// The dcSCTP socket on network input and API calls (#1060, requirements C7
// part 2 and C10): libwebrtc M157's test/fuzzers/dcsctp_socket_fuzzer.cc
// (compare/webrtc/reference/test/fuzzers/dcsctp_socket_fuzzer.cc), which runs
// net/dcsctp/fuzzers/dcsctp_fuzzers.cc's FuzzSocket, ported as
// fuzz/dcsctp_fuzzers.{h,cc}.
//
// As libwebrtc: a DcSctpSocket "A" with checksum verification off, and the
// input read by FuzzSocket: the first byte picks the socket's starting state
// (closed, connecting, connected, shutting down), the rest is a sequence of
// actions (receive a generated or raw chunk, send a message, reset streams,
// expire a timeout, close, ...). FuzzDataHelper::ReadRemaining is the whole
// input.
//
// The corpus (fuzz/corpus/dcsctp_socket/) holds an input for each starting
// state with a run of every action, and the dcsctp_packet corpus's packets.
#include <cstddef>
#include <cstdint>
#include <span>

#include "dcsctp_fuzzers.h"
#include "sm/dcsctp/public/dcsctp_options.h"
#include "sm/dcsctp/socket/dcsctp_socket.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  sm::dcsctp::dcsctp_fuzzers::FuzzerCallbacks cb;
  sm::dcsctp::DcSctpOptions options;
  options.disable_checksum_verification = true;
  sm::dcsctp::DcSctpSocket socket("A", cb, nullptr, options);

  sm::dcsctp::dcsctp_fuzzers::FuzzSocket(socket, cb, std::span<const uint8_t>(data, size));
  return 0;
}
