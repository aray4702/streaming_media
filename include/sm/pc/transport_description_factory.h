#pragma once
// Transport descriptions for offers and answers (C6 part 2, #1063): libwebrtc M157's
// p2p/base/transport_description_factory.{h,cc} (compare/webrtc/reference/p2p/base/).
//
// Differences from libwebrtc:
// - The certificate is an `sm::tls::SslIdentity` (shared), not an `RTCCertificate`.
// - No field trials: `WebRTC-IceHandshakeDtls` (the goog-sped-v1 ICE option) is off, as it is by default.
#include <memory>

#include "sm/p2p/ice_credentials_iterator.h"
#include "sm/p2p/transport_description.h"
#include "sm/tls/ssl_identity.h"

namespace sm::pc {

using p2p::ConnectionRole;
using p2p::IceCredentialsIterator;
using p2p::IceParameters;
using p2p::TransportDescription;

struct TransportOptions {
  bool ice_restart = false;
  bool prefer_passive_role = false;
  // If true, ICE renomination is supported and will be used if it is also supported by the remote side.
  bool enable_ice_renomination = false;
  // If true, SPED (STUN Protocol for Embedding DTLS) is supported and will be used if the remote side supports it.
  bool dtls_handshake_in_stun = false;
};

class TransportDescriptionFactory {
 public:
  TransportDescriptionFactory() = default;

  const std::shared_ptr<const tls::SslIdentity>& certificate() const { return certificate_; }
  void set_certificate(std::shared_ptr<const tls::SslIdentity> certificate) { certificate_ = std::move(certificate); }

  // A transport description for an offer: new ICE credentials unless `current_description` has them and no ICE
  // restart is asked for, the trickle option, and the DTLS fingerprint with setup actpass.
  std::unique_ptr<TransportDescription> CreateOffer(const TransportOptions& options,
                                                    const TransportDescription* current_description,
                                                    IceCredentialsIterator* ice_credentials) const;
  // A transport description that answers `offer`. With `require_transport_attributes` the offer must carry a
  // fingerprint (null otherwise); it is false for a bundled m= section after the first one. The DTLS role is
  // active for an actpass offer (passive with `prefer_passive_role`), and the opposite of an active or passive one.
  std::unique_ptr<TransportDescription> CreateAnswer(const TransportDescription* offer,
                                                     const TransportOptions& options,
                                                     bool require_transport_attributes,
                                                     const TransportDescription* current_description,
                                                     IceCredentialsIterator* ice_credentials) const;

  // Test only: accept descriptions without a fingerprint, and write them without one when there is no certificate.
  bool insecure() const { return insecure_; }
  void SetInsecureForTesting() { insecure_ = true; }

 private:
  bool SetSecurityInfo(TransportDescription* description, ConnectionRole role) const;

  bool insecure_ = false;
  std::shared_ptr<const tls::SslIdentity> certificate_;
};

}  // namespace sm::pc
