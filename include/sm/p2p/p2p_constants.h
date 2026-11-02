// ICE constants (issue #999, part of ICE #990, milestone 3): libwebrtc M157's
// p2p/base/p2p_constants.{h,cc} (compare/webrtc/reference/p2p/base/), the
// ICE parts (the plan-B content names and SDP setup strings are left out).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "sm/core/units.h"

namespace sm::p2p {

inline constexpr int ICE_UFRAG_LENGTH = 4;   // RFC 5245 minimum.
inline constexpr int ICE_PWD_LENGTH = 24;    // RFC 5245 minimum is 22; libwebrtc uses a multiple of 4.
inline constexpr size_t ICE_UFRAG_MIN_LENGTH = 4;
inline constexpr size_t ICE_PWD_MIN_LENGTH = 22;
inline constexpr size_t ICE_UFRAG_MAX_LENGTH = 256;
inline constexpr size_t ICE_PWD_MAX_LENGTH = 256;

inline constexpr int ICE_CANDIDATE_COMPONENT_RTP = 1;
inline constexpr int ICE_CANDIDATE_COMPONENT_RTCP = 2;
inline constexpr int ICE_CANDIDATE_COMPONENT_DEFAULT = 1;

inline constexpr char UDP_PROTOCOL_NAME[] = "udp";
inline constexpr char TCP_PROTOCOL_NAME[] = "tcp";
inline constexpr char SSLTCP_PROTOCOL_NAME[] = "ssltcp";
inline constexpr char TLS_PROTOCOL_NAME[] = "tls";

// True for a name in the mDNS .local domain (case-insensitive, an optional
// trailing dot).
bool IsMdnsHostname(std::string_view hostname);

// IceConfig defaults.
inline constexpr TimeDelta kMinCheckReceivingInterval = TimeDelta::Millis(50);
inline constexpr TimeDelta kReceivingTimeout = TimeDelta::Millis(50 * 50);
inline constexpr TimeDelta kStrongPingInterval = TimeDelta::Millis(480);
inline constexpr TimeDelta kWeakPingInterval = TimeDelta::Millis(48);
inline constexpr TimeDelta kStrongAndStableWritableConnectionPingInterval = TimeDelta::Millis(2'500);
inline constexpr TimeDelta kWeakOrStabilizingWritableConnectionPingInterval = TimeDelta::Millis(900);
inline constexpr TimeDelta kBackupConnectionPingInterval = TimeDelta::Seconds(25);
inline constexpr TimeDelta kReceivingSwitchingDelay = TimeDelta::Seconds(1);
inline constexpr TimeDelta kRegatherOnFailedNetworksInterval = TimeDelta::Seconds(5 * 60);
inline constexpr TimeDelta kConnectionWriteConnectTimeout = TimeDelta::Seconds(5);
inline constexpr int kConnectionWriteConnectFailures = 5;
inline constexpr TimeDelta kConnectionWriteTimeout = TimeDelta::Seconds(15);
inline constexpr TimeDelta kStunKeepaliveInterval = TimeDelta::Seconds(10);
inline constexpr int kMinPingsAtWeakPingInterval = 3;
inline constexpr TimeDelta kWeakConnectionReceiveTimeout = TimeDelta::Millis(2'500);
inline constexpr TimeDelta kDeadConnectionReceiveTimeout = TimeDelta::Seconds(30);
inline constexpr TimeDelta kConnectionResponseTimeout = TimeDelta::Seconds(60);
inline constexpr TimeDelta kMinConnectionLifetime = TimeDelta::Seconds(10);

// RFC 5245 4.1.2.1 type preferences (0-126).
enum IcePriorityValue : uint8_t {
  ICE_TYPE_PREFERENCE_RELAY_TLS = 0,
  ICE_TYPE_PREFERENCE_RELAY_TCP = 1,
  ICE_TYPE_PREFERENCE_RELAY_DTLS = 2,
  ICE_TYPE_PREFERENCE_RELAY_UDP = 3,
  ICE_TYPE_PREFERENCE_PRFLX_TCP = 80,
  ICE_TYPE_PREFERENCE_HOST_TCP = 90,
  ICE_TYPE_PREFERENCE_SRFLX = 100,
  ICE_TYPE_PREFERENCE_PRFLX = 110,
  ICE_TYPE_PREFERENCE_HOST = 126
};

inline constexpr int kMaxTurnUsernameLength = 509;  // RFC 8489 section 14.3.

}  // namespace sm::p2p
