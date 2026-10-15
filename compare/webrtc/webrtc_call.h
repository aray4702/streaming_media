// Runs a test bench scenario through libwebrtc instead of this library.
//
// Two webrtc::Call instances (the layer under PeerConnection) send audio
// and video one way over the test bench's own emulated links, in simulated
// time. The video encoder is the test bench's encoder model, so video
// differences come from transport, repair and buffering rather than codecs.
// The result uses the test bench's metric names and definitions; metrics
// libwebrtc does not expose are left out.
#pragma once

#include <string>

#include "call.h"

namespace sm::compare {

testbench::Report RunWebrtcCall(const testbench::Scenario& s, const testbench::RunOptions& opt = {});

// "M157.8103@{#0}" etc., from the binary's VERSIONS file at build time.
std::string WebrtcVersion();

// Metrics RunWebrtcCall reports with a different (approximate) definition
// than RunCall, with the reason. See compare/README.md.
const std::vector<std::pair<std::string, std::string>>& WebrtcMetricNotes();

}  // namespace sm::compare
