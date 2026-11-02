// libwebrtc's rtc_base/message_digest_unittest.cc and crypto_random_unittest.cc cases, plus RFC 2202 / RFC 4231
// HMAC vectors (#984).
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "sm/tls/crypto_random.h"
#include "sm/tls/message_digest.h"
#include "sm_test.h"

using namespace sm::tls;

SM_TEST(TlsMessageDigest, KnownDigests) {
  SM_EXPECT_EQ(ComputeDigest(kDigestMd5, "abc"), std::string("900150983cd24fb0d6963f7d28e17f72"));
  SM_EXPECT_EQ(ComputeDigest(kDigestSha1, "abc"), std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
  SM_EXPECT_EQ(ComputeDigest(kDigestSha224, "abc"), std::string("23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7"));
  SM_EXPECT_EQ(ComputeDigest(kDigestSha256, "abc"),
               std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  SM_EXPECT_EQ(ComputeDigest(kDigestSha1, ""), std::string("da39a3ee5e6b4b0d3255bfef95601890afd80709"));
  SM_EXPECT_TRUE(ComputeDigest("sha-3", "abc").empty());
  SM_EXPECT_TRUE(CreateMessageDigest("bogus") == nullptr);
}

SM_TEST(TlsMessageDigest, DigestIsReusableAfterFinish) {
  auto d = CreateMessageDigest(kDigestSha1);
  SM_ASSERT_TRUE(d != nullptr);
  uint8_t a[20], b[20];
  d->Update("abc", 3);
  SM_EXPECT_EQ(d->Finish(a, sizeof(a)), 20u);
  d->Update("abc", 3);
  SM_EXPECT_EQ(d->Finish(b, sizeof(b)), 20u);
  SM_EXPECT_TRUE(std::memcmp(a, b, 20) == 0);
  SM_EXPECT_EQ(d->Finish(a, 10), 0u);  // Too short.
}

SM_TEST(TlsMessageDigest, Fips180) {
  SM_EXPECT_TRUE(IsFips180DigestAlgorithm(kDigestSha256));
  SM_EXPECT_TRUE(IsFips180DigestAlgorithm(kDigestSha1));
  SM_EXPECT_FALSE(IsFips180DigestAlgorithm(kDigestMd5));
}

// RFC 2202 (HMAC-MD5, HMAC-SHA1) and RFC 4231 (HMAC-SHA256) test cases.
SM_TEST(TlsMessageDigest, HmacVectors) {
  const std::string key20(20, '\x0b');
  SM_EXPECT_EQ(ComputeHmac(kDigestSha1, key20, "Hi There"), std::string("b617318655057264e28bc0b6fb378c8ef146be00"));
  SM_EXPECT_EQ(ComputeHmac(kDigestSha1, "Jefe", "what do ya want for nothing?"),
               std::string("effcdf6ae5eb2fa2d27416d5f184df9c259a7c79"));
  SM_EXPECT_EQ(ComputeHmac(kDigestMd5, std::string(16, '\x0b'), "Hi There"), std::string("9294727a3638bb1c13f48ef8158bfc9d"));
  SM_EXPECT_EQ(ComputeHmac(kDigestSha256, key20, "Hi There"),
               std::string("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
  // A key longer than the block is hashed first (RFC 2202 test case 6).
  SM_EXPECT_EQ(ComputeHmac(kDigestSha1, std::string(80, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First"),
               std::string("aa4ae5e15272d00e95705637ce8a3b55ed402112"));
  // libwebrtc's HMAC is for 64-byte blocks only.
  SM_EXPECT_TRUE(ComputeHmac(kDigestSha512, "k", "x").empty());
}

SM_TEST(TlsCryptoRandom, Strings) {
  SM_EXPECT_EQ(CreateRandomString(16).size(), 16u);
  SM_EXPECT_EQ(CreateRandomString(10, "abcd").size(), 10u);
  SM_EXPECT_TRUE(CreateRandomString(10, "abc").empty());  // 3 doesn't divide 256.
  const std::string uuid = CreateRandomUuid();
  SM_EXPECT_EQ(uuid.size(), 36u);
  SM_EXPECT_EQ(uuid[14], '4');
  SM_EXPECT_TRUE(std::string("89ab").find(uuid[19]) != std::string::npos);
  std::set<std::string> seen;
  for (int i = 0; i < 100; ++i) seen.insert(CreateRandomString(22));
  SM_EXPECT_EQ(seen.size(), 100u);
}

SM_TEST(TlsCryptoRandom, TestModeIsRepeatable) {
  SetRandomTestMode(true);
  const std::string a = CreateRandomString(32);
  const uint32_t id = CreateRandomId();
  SetRandomTestMode(true);
  SM_EXPECT_EQ(CreateRandomString(32), a);
  SM_EXPECT_EQ(CreateRandomId(), id);
  SetRandomTestMode(false);
  SM_EXPECT_TRUE(CreateRandomNonZeroId() != 0);
}
