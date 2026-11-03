// The transports a call's session descriptions set up (C6 part 3, #1064): libwebrtc M157's pc/jsep_transport.{h,cc},
// pc/jsep_transport_controller.{h,cc} and the BUNDLE bookkeeping of pc/jsep_transport_collection.h
// (compare/webrtc/reference/pc/), for C6's subset.
//
// What it does, as libwebrtc does:
// - Applying a local or remote description creates, for each m= section that is not rejected and is the first of
//   its BUNDLE group (or in none), a JsepTransport: a P2PTransportChannel (ICE, component 1), a DtlsTransport with
//   the local certificate over it, and a DtlsSrtpTransport over that. rtcp-mux is required (kRtcpMuxPolicyRequire,
//   PeerConnection's default), so there is no RTCP component; a section without a=rtcp-mux is refused.
// - BUNDLE (RFC 8843) as BundleManager does: an answer (or any description with max-bundle) replaces the
//   established groups, an offer only updates groups already established. The other mids of an established group
//   use the first mid's transport, and when an answer is applied the transports no mid uses any more are destroyed
//   (JsepTransportCollection::CommitTransports). The offer/answer checks of ValidateAndMaybeUpdateBundleGroups are
//   kept (a mid in two groups, a group added or changed in an answer, an answer that drops a bundled section).
// - The ICE role is set by the first local description (controlling for an offer, controlled for an answer), with
//   DetermineIceRole's ICE-lite exceptions. ICE credentials go to the channel from each description; trickled
//   candidates (AddRemoteCandidates) need the remote description and go to the transport of their mid.
// - On an answer (local or remote), NegotiateAndSetDtlsParameters: the DTLS role from both a=setup values
//   (NegotiateDtlsRole, RFC 5763 section 5) and the remote fingerprint go to the DTLS transport. A local fingerprint
//   must be the local certificate's (VerifyCertificateFingerprint). Encrypted header extension ids (RFC 6904) go to
//   the DTLS-SRTP transport, merged over a BUNDLE group.
// - Gathered candidates are reported with the transport's name (its first mid).
//
// The library's time model: nothing reads a clock or starts a thread. Calls that can send take `now`; the owner runs
// Process(now) at NextProcessTime() (ICE checks, DTLS retransmissions), and delivers the sockets' packets itself.
//
// Differences from libwebrtc, none of them on the wire:
// - One thread: no network-thread hops, and callbacks run inside the call that caused them. RTCError is bool plus a
//   message.
// - Left out until a part needs them: kRtcpMuxPolicyNegotiate (non-muxed RTCP), rollback and pranswer commits,
//   ICE restarts' role re-evaluation (redetermine_role_on_ice_restart), DTLS-in-STUN (SPED), cryptex, the
//   aggregate ICE/DTLS/connection states (the transports' own states are read instead), and the SCTP transport per
//   JsepTransport (PeerEndpoint creates the one it uses on the data section's DTLS transport when SCTP starts, #1091).
// - pc/jsep_transport_collection.cc is not in compare/webrtc/reference/; BundleManager::Update and the commit are
//   ported from M157's behaviour as its header describes it.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sm/core/units.h"
#include "sm/p2p/candidate.h"
#include "sm/p2p/dtls_transport.h"
#include "sm/p2p/p2p_transport_channel.h"
#include "sm/p2p/port_allocator.h"
#include "sm/p2p/transport_description.h"
#include "sm/pc/dtls_srtp_transport.h"
#include "sm/pc/session_description.h"
#include "sm/tls/ssl_identity.h"

namespace sm::pc {

// pc/jsep_transport.h's JsepTransportDescription.
struct JsepTransportDescription {
  bool rtcp_mux_enabled = true;
  std::vector<int> encrypted_header_extension_ids;
  p2p::TransportDescription transport_desc;
};

// pc/jsep_transport.h: one ICE + DTLS + DTLS-SRTP transport and the descriptions applied to it.
class JsepTransport {
 public:
  JsepTransport(std::string mid, std::shared_ptr<const tls::SslIdentity> certificate,
                std::unique_ptr<p2p::P2PTransportChannel> ice, std::unique_ptr<p2p::DtlsTransport> dtls,
                std::unique_ptr<DtlsSrtpTransport> rtp_transport);
  ~JsepTransport();
  JsepTransport(const JsepTransport&) = delete;
  JsepTransport& operator=(const JsepTransport&) = delete;

  const std::string& name() const { return mid_; }
  bool SetLocalJsepTransportDescription(const JsepTransportDescription& d, p2p::SdpType type, Timestamp now,
                                        std::string* error);
  bool SetRemoteJsepTransportDescription(const JsepTransportDescription& d, p2p::SdpType type, Timestamp now,
                                         std::string* error);
  bool AddRemoteCandidates(const std::vector<p2p::Candidate>& candidates, Timestamp now, std::string* error);
  std::optional<p2p::SslRole> GetDtlsRole() const;

  const JsepTransportDescription* local_description() const { return local_description_.get(); }
  const JsepTransportDescription* remote_description() const { return remote_description_.get(); }
  p2p::P2PTransportChannel* ice_transport() const { return ice_.get(); }
  p2p::DtlsTransport* rtp_dtls_transport() const { return dtls_.get(); }
  DtlsSrtpTransport* rtp_transport() const { return rtp_transport_.get(); }

 private:
  bool NegotiateAndSetDtlsParameters(p2p::SdpType local_description_type, Timestamp now, std::string* error);

  const std::string mid_;
  std::shared_ptr<const tls::SslIdentity> local_certificate_;
  // Destroyed in reverse: the DTLS-SRTP transport before the DTLS transport before the ICE channel.
  std::unique_ptr<p2p::P2PTransportChannel> ice_;
  std::unique_ptr<p2p::DtlsTransport> dtls_;
  std::unique_ptr<DtlsSrtpTransport> rtp_transport_;
  std::unique_ptr<JsepTransportDescription> local_description_;
  std::unique_ptr<JsepTransportDescription> remote_description_;
};

// PeerConnectionInterface::BundlePolicy.
enum class BundlePolicy { kBalanced, kMaxBundle, kMaxCompat };

class JsepTransportController {
 public:
  struct Config {
    // kBundlePolicyBalanced, PeerConnection's default.
    BundlePolicy bundle_policy = BundlePolicy::kBalanced;
    // CryptoOptions::Srtp::enable_encrypted_rtp_header_extensions: off by default.
    bool enable_encrypted_rtp_header_extensions = false;
    std::optional<p2p::IceConfig> ice_config;
  };
  // (transport name, candidate): a candidate the transport gathered.
  using CandidateCallback = std::function<void(const std::string& transport_name, const p2p::Candidate&)>;

  // `allocator` is not owned and must outlive the controller.
  JsepTransportController(p2p::PortAllocator* allocator, Config config);
  ~JsepTransportController();
  JsepTransportController(const JsepTransportController&) = delete;
  JsepTransportController& operator=(const JsepTransportController&) = delete;

  // The certificate every DTLS transport uses; set once, before the first description.
  bool SetLocalCertificate(std::shared_ptr<const tls::SslIdentity> certificate);
  const std::shared_ptr<const tls::SslIdentity>& local_certificate() const { return certificate_; }

  bool SetLocalDescription(p2p::SdpType type, const SessionDescription* local_desc,
                           const SessionDescription* remote_desc, Timestamp now, std::string* error = nullptr);
  bool SetRemoteDescription(p2p::SdpType type, const SessionDescription* local_desc,
                            const SessionDescription* remote_desc, Timestamp now, std::string* error = nullptr);
  bool AddRemoteCandidates(std::string_view mid, const std::vector<p2p::Candidate>& candidates, Timestamp now,
                           std::string* error = nullptr);
  void MaybeStartGathering(Timestamp now);

  // The transports of a mid; null if it has none (rejected, or unknown).
  JsepTransport* GetJsepTransportForMid(std::string_view mid) const;
  DtlsSrtpTransport* GetRtpTransport(std::string_view mid) const;
  p2p::DtlsTransport* GetDtlsTransport(std::string_view mid) const;
  std::optional<p2p::SslRole> GetDtlsRole(std::string_view mid) const;
  p2p::IceRole ice_role() const { return ice_role_; }
  // The established BUNDLE groups.
  const std::vector<ContentGroup>& bundle_groups() const { return bundle_groups_; }
  size_t transport_count() const { return transports_.size(); }

  void SubscribeIceCandidateGathered(CandidateCallback cb) { on_candidate_ = std::move(cb); }

  // ICE checks and DTLS handshake retransmissions due by `now`.
  void Process(Timestamp now);
  Timestamp NextProcessTime() const;

 private:
  bool ApplyDescription(bool local, p2p::SdpType type, const SessionDescription* local_desc,
                        const SessionDescription* remote_desc, Timestamp now, std::string* error);
  bool ValidateAndMaybeUpdateBundleGroups(bool local, p2p::SdpType type, const SessionDescription* local_desc,
                                          const SessionDescription* remote_desc, std::string* error);
  void UpdateBundles(const SessionDescription* description, p2p::SdpType type);
  const ContentGroup* LookupGroupByMid(std::string_view mid) const;
  bool IsFirstMidInGroup(std::string_view mid) const;
  bool ValidateContent(const ContentInfo& content, std::string* error) const;
  void HandleRejectedContent(const ContentInfo& content);
  JsepTransport* GetJsepTransportByName(std::string_view name) const;
  void MaybeCreateJsepTransport(const ContentInfo& content, Timestamp now);
  void DestroyUnusedTransports();
  void SetIceRole(p2p::IceRole role);
  p2p::IceRole DetermineIceRole(const JsepTransport* transport, const TransportInfo& transport_info,
                                p2p::SdpType type, bool local) const;
  std::vector<int> GetEncryptedHeaderExtensionIds(const ContentInfo& content) const;

  p2p::PortAllocator* const allocator_;
  const Config config_;
  std::shared_ptr<const tls::SslIdentity> certificate_;
  p2p::IceRole ice_role_ = p2p::ICEROLE_CONTROLLING;
  bool ice_role_initialized_ = false;
  std::vector<ContentGroup> bundle_groups_;
  // Transports by name (their first mid), and the transport each mid uses.
  std::map<std::string, std::unique_ptr<JsepTransport>, std::less<>> transports_;
  std::map<std::string, JsepTransport*, std::less<>> mid_to_transport_;
  CandidateCallback on_candidate_;
};

}  // namespace sm::pc
