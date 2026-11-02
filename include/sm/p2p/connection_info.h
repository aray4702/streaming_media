// libwebrtc M157's p2p/base/connection_info.{h,cc} (compare/webrtc/reference/p2p/base/): a candidate pair's state
// and statistics (RTCIceCandidatePairStats).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/candidate.h"

namespace sm::p2p {

// https://w3c.github.io/webrtc-stats/#dom-rtcstatsicecandidatepairstate
enum class IceCandidatePairState {
  WAITING = 0,  // Check has not been performed, Waiting pair on CL.
  IN_PROGRESS,  // Check has been sent, transaction is in progress.
  SUCCEEDED,    // Check already done, produced a successful result.
  FAILED,       // Check for this connection failed.
  // According to spec there should also be a frozen state, but nothing is ever frozen.
};

struct ConnectionInfo {
  bool best_connection = false;            // Is this the best connection we have?
  bool writable = false;                   // Has this connection received a STUN response?
  bool receiving = false;                  // Has this connection received anything?
  bool timeout = false;                    // Has this connection timed out?
  size_t rtt = 0;                          // The STUN RTT for this connection.
  size_t sent_discarded_bytes = 0;         // Outgoing bytes discarded on socket errors.
  size_t sent_total_bytes = 0;             // Bytes sent, not counting discarded ones.
  size_t sent_bytes_second = 0;            // Bps over the last measurement interval.
  size_t sent_discarded_packets = 0;       // Outgoing packets discarded on socket errors.
  size_t sent_total_packets = 0;           // Outgoing packets attempted, discarded ones included.
  size_t sent_ping_requests_total = 0;     // STUN ping requests sent.
  size_t sent_ping_requests_before_first_response = 0;
  size_t sent_ping_responses = 0;          // STUN ping responses sent.
  size_t recv_total_bytes = 0;             // Bytes received.
  size_t recv_bytes_second = 0;            // Bps over the last measurement interval.
  size_t packets_received = 0;             // Packets received.
  size_t recv_ping_requests = 0;           // STUN ping requests received.
  size_t recv_ping_responses = 0;          // STUN ping responses received.
  Candidate local_candidate;
  Candidate remote_candidate;
  const void* key = nullptr;  // Identifies the connection.
  IceCandidatePairState state = IceCandidatePairState::WAITING;
  uint64_t priority = 0;
  bool nominated = false;
  uint64_t total_round_trip_time_ms = 0;
  std::optional<uint32_t> current_round_trip_time_ms;
  // In the caller's time base (libwebrtc converts them to the Unix epoch).
  std::optional<Timestamp> last_data_received;
  std::optional<Timestamp> last_data_sent;
};

using ConnectionInfos = std::vector<ConnectionInfo>;

}  // namespace sm::p2p
