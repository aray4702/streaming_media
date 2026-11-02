// A certificate fingerprint, as SDP's a=fingerprint carries it: libwebrtc's rtc_base/ssl_fingerprint.h (M157),
// ported for milestone 3 (#984 part 1b).
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sm/tls/ssl_identity.h"

namespace sm::tls {

struct SslFingerprint {
  SslFingerprint(std::string_view alg, std::span<const uint8_t> d) : algorithm(alg), digest(d.begin(), d.end()) {}

  // The digest of `cert` with `algorithm`; null for an unknown algorithm.
  static std::unique_ptr<SslFingerprint> Create(std::string_view algorithm, const SslCertificate& cert);
  static std::unique_ptr<SslFingerprint> Create(std::string_view algorithm, const SslIdentity& identity);
  // From SDP: a FIPS 180 algorithm and "AB:CD:..." hex (CreateFromRfc4572); null if either is malformed.
  static std::unique_ptr<SslFingerprint> CreateFromRfc4572(std::string_view algorithm, std::string_view fingerprint);
  // With the certificate's own signature digest algorithm (CreateFromCertificate).
  static std::unique_ptr<SslFingerprint> CreateFromCertificate(const SslIdentity& identity);

  // "AB:CD:..." upper case, as SDP writes it.
  std::string GetRfc4572Fingerprint() const;
  // "sha-256 AB:CD:...".
  std::string ToString() const;
  bool operator==(const SslFingerprint& o) const { return algorithm == o.algorithm && digest == o.digest; }

  std::string algorithm;
  std::vector<uint8_t> digest;
};

}  // namespace sm::tls
