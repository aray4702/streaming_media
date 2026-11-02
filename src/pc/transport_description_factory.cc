// libwebrtc M157 p2p/base/transport_description_factory.cc.
#include "sm/pc/transport_description_factory.h"

#include "sm/tls/ssl_fingerprint.h"

namespace sm::pc {

std::unique_ptr<TransportDescription> TransportDescriptionFactory::CreateOffer(
    const TransportOptions& options, const TransportDescription* current_description,
    IceCredentialsIterator* ice_credentials) const {
  auto desc = std::make_unique<TransportDescription>();
  // Generate the ICE credentials if we don't already have them.
  if (!current_description || options.ice_restart) {
    IceParameters credentials = ice_credentials->GetIceCredentials();
    desc->ice_ufrag = credentials.ufrag;
    desc->ice_pwd = credentials.pwd;
  } else {
    desc->ice_ufrag = current_description->ice_ufrag;
    desc->ice_pwd = current_description->ice_pwd;
  }
  desc->AddOption(p2p::ICE_OPTION_TRICKLE);
  if (options.enable_ice_renomination) desc->AddOption(p2p::ICE_OPTION_RENOMINATION);
  if (options.dtls_handshake_in_stun) desc->AddOption(p2p::ICE_OPTION_SPED);
  // If we are not trying to establish a secure transport, don't add a fingerprint.
  if (insecure_ && !certificate_) return desc;
  // Fail if we can't create the fingerprint. If we are the initiator set role to "actpass".
  if (!SetSecurityInfo(desc.get(), ConnectionRole::kActpass)) return nullptr;
  return desc;
}

std::unique_ptr<TransportDescription> TransportDescriptionFactory::CreateAnswer(
    const TransportDescription* offer, const TransportOptions& options, bool require_transport_attributes,
    const TransportDescription* current_description, IceCredentialsIterator* ice_credentials) const {
  if (!offer) return nullptr;
  auto desc = std::make_unique<TransportDescription>();
  // Generate the ICE credentials if we don't already have them or ICE is being restarted.
  if (!current_description || options.ice_restart) {
    IceParameters credentials = ice_credentials->GetIceCredentials();
    desc->ice_ufrag = credentials.ufrag;
    desc->ice_pwd = credentials.pwd;
  } else {
    desc->ice_ufrag = current_description->ice_ufrag;
    desc->ice_pwd = current_description->ice_pwd;
  }
  desc->AddOption(p2p::ICE_OPTION_TRICKLE);
  if (options.enable_ice_renomination) desc->AddOption(p2p::ICE_OPTION_RENOMINATION);
  if (options.dtls_handshake_in_stun && offer->HasOption(p2p::ICE_OPTION_SPED)) desc->AddOption(p2p::ICE_OPTION_SPED);

  // Answer without DTLS parameters if insecure without a certificate, or insecure with a non-DTLS offer.
  if ((!certificate_ || !offer->identity_fingerprint) && insecure()) return desc;
  if (!offer->identity_fingerprint) {
    // We require DTLS, but the other side didn't offer it. Fail.
    if (require_transport_attributes) return nullptr;
    // This may be a bundled section, the fingerprint may legitimately be missing.
    return desc;
  }
  // The offer supports DTLS, so answer with DTLS.
  if (!certificate_) return nullptr;  // RTC_CHECK in libwebrtc.
  ConnectionRole role = ConnectionRole::kNone;
  switch (offer->connection_role) {
    case ConnectionRole::kActpass:
      role = options.prefer_passive_role ? ConnectionRole::kPassive : ConnectionRole::kActive;
      break;
    case ConnectionRole::kActive:
      role = ConnectionRole::kPassive;
      break;
    case ConnectionRole::kPassive:
      role = ConnectionRole::kActive;
      break;
    case ConnectionRole::kNone:
      // Reached if a=setup is not in the SDP, which is a protocol violation.
      role = options.prefer_passive_role ? ConnectionRole::kPassive : ConnectionRole::kActive;
      break;
    default:
      return nullptr;
  }
  if (!SetSecurityInfo(desc.get(), role)) return nullptr;
  return desc;
}

bool TransportDescriptionFactory::SetSecurityInfo(TransportDescription* desc, ConnectionRole role) const {
  if (!certificate_) return false;
  // RFC 4572 Section 5: the a=fingerprint lines use the hash function of the certificate's signature.
  desc->identity_fingerprint = tls::SslFingerprint::CreateFromCertificate(*certificate_);
  if (!desc->identity_fingerprint) return false;
  desc->connection_role = role;
  return true;
}

}  // namespace sm::pc
