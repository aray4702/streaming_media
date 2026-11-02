// Cases from libwebrtc's rtc_base/ssl_identity_unittest.cc and ssl_fingerprint tests (#984 part 1b).
#include <ctime>
#include <string>

#include "sm/tls/message_digest.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"
#include "sm_test.h"

using namespace sm::tls;

SM_TEST(TlsSslIdentity, CreatesAnEcdsaIdentityWithASha256Certificate) {
  auto id = SslIdentity::Create("test", KeyType::kEcdsa);
  SM_ASSERT_TRUE(id != nullptr);
  std::string alg;
  SM_ASSERT_TRUE(id->certificate().GetSignatureDigestAlgorithm(alg));
  SM_EXPECT_EQ(alg, std::string(kDigestSha256));
  // Valid for 30 days from now (ssl_identity.h kDefaultCertificateLifetimeInSeconds).
  const int64_t expires = id->certificate().CertificateExpirationTime();
  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  SM_EXPECT_TRUE(expires > now + kDefaultCertificateLifetimeSeconds - 120 && expires <= now + kDefaultCertificateLifetimeSeconds + 5);
  // Two identities differ.
  auto other = SslIdentity::Create("test", KeyType::kEcdsa);
  SM_ASSERT_TRUE(other != nullptr);
  SM_EXPECT_FALSE(*id == *other);
}

SM_TEST(TlsSslIdentity, RsaIdentity) {
  auto id = SslIdentity::Create("rsa", KeyType::kRsa);
  SM_ASSERT_TRUE(id != nullptr);
  std::string alg;
  SM_ASSERT_TRUE(id->certificate().GetSignatureDigestAlgorithm(alg));
  SM_EXPECT_EQ(alg, std::string(kDigestSha256));
}

SM_TEST(TlsSslIdentity, PemRoundTrip) {
  auto id = SslIdentity::Create("pem", KeyType::kEcdsa);
  SM_ASSERT_TRUE(id != nullptr);
  const std::string key = id->PrivateKeyToPem(), cert = id->certificate().ToPem();
  SM_EXPECT_TRUE(key.find("-----BEGIN PRIVATE KEY-----") == 0);
  SM_EXPECT_TRUE(cert.find("-----BEGIN CERTIFICATE-----") == 0);
  SM_EXPECT_TRUE(id->PublicKeyToPem().find("-----BEGIN PUBLIC KEY-----") == 0);
  auto back = SslIdentity::CreateFromPem(key, cert);
  SM_ASSERT_TRUE(back != nullptr);
  SM_EXPECT_TRUE(*back == *id);
  // A key that doesn't match the certificate is refused.
  auto other = SslIdentity::Create("other", KeyType::kEcdsa);
  SM_EXPECT_TRUE(SslIdentity::CreateFromPem(other->PrivateKeyToPem(), cert) == nullptr);
  SM_EXPECT_TRUE(SslIdentity::CreateFromPem("garbage", cert) == nullptr);
  auto clone = id->Clone();
  SM_EXPECT_TRUE(*clone == *id);
}

SM_TEST(TlsSslIdentity, CreateForTestTakesItsValidity) {
  SslIdentityParams p;
  p.common_name = "fixed";
  p.not_before = 1000000000;  // 2001.
  p.not_after = 2000000000;   // 2033.
  auto id = SslIdentity::CreateForTest(p);
  SM_ASSERT_TRUE(id != nullptr);
  SM_EXPECT_EQ(id->certificate().CertificateExpirationTime(), int64_t{2000000000});
  p.common_name = "";
  SM_EXPECT_TRUE(SslIdentity::CreateForTest(p) == nullptr);  // A common name is required (AddCommonName).
}

SM_TEST(TlsSslFingerprint, FromIdentityAndRfc4572) {
  auto id = SslIdentity::Create("fp", KeyType::kEcdsa);
  SM_ASSERT_TRUE(id != nullptr);
  auto fp = SslFingerprint::CreateFromCertificate(*id);
  SM_ASSERT_TRUE(fp != nullptr);
  SM_EXPECT_EQ(fp->algorithm, std::string(kDigestSha256));
  SM_EXPECT_EQ(fp->digest.size(), 32u);
  const std::string sdp = fp->GetRfc4572Fingerprint();
  SM_EXPECT_EQ(sdp.size(), 32u * 3 - 1);  // "AB:" x 32 less the last colon.
  SM_EXPECT_TRUE(fp->ToString().find("sha-256 ") == 0);
  auto parsed = SslFingerprint::CreateFromRfc4572(kDigestSha256, sdp);
  SM_ASSERT_TRUE(parsed != nullptr);
  SM_EXPECT_TRUE(*parsed == *fp);
  // A PEM round trip keeps the fingerprint.
  auto back = SslIdentity::CreateFromPem(id->PrivateKeyToPem(), id->certificate().ToPem());
  SM_EXPECT_TRUE(*SslFingerprint::Create(kDigestSha256, *back) == *fp);
}

SM_TEST(TlsSslFingerprint, RejectsMalformedRfc4572) {
  SM_EXPECT_TRUE(SslFingerprint::CreateFromRfc4572(kDigestMd5, "AB:CD") == nullptr);  // Not FIPS 180.
  SM_EXPECT_TRUE(SslFingerprint::CreateFromRfc4572(kDigestSha256, "") == nullptr);
  SM_EXPECT_TRUE(SslFingerprint::CreateFromRfc4572(kDigestSha256, "AB:CD:") == nullptr);
  SM_EXPECT_TRUE(SslFingerprint::CreateFromRfc4572(kDigestSha256, "ABC:D") == nullptr);
  SM_EXPECT_TRUE(SslFingerprint::CreateFromRfc4572(kDigestSha256, "AB:ZZ") == nullptr);
  auto ok = SslFingerprint::CreateFromRfc4572(kDigestSha256, "ab:cd:01");
  SM_ASSERT_TRUE(ok != nullptr);
  SM_EXPECT_EQ(ok->GetRfc4572Fingerprint(), std::string("AB:CD:01"));
}
