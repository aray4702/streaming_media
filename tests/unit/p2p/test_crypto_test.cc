// Self-tests of test_crypto.{h,cc} against the standards' own vectors.
#include <cstring>
#include <set>
#include <span>
#include <string>
#include <string_view>

#include "sm_test.h"
#include "sm/p2p/stun.h"
#include "test_crypto.h"

namespace {

using sm::p2p::testing::HmacSha1;
using sm::p2p::testing::Md5;
using sm::p2p::testing::Sha1;
using sm::p2p::testing::Sha256;
using sm::p2p::testing::HmacSha256;

std::span<const uint8_t> Bytes(std::string_view s) {
  return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

std::string Hex(const uint8_t* p, size_t n) {
  static const char* kHex = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; ++i) {
    s += kHex[p[i] >> 4];
    s += kHex[p[i] & 15];
  }
  return s;
}

}  // namespace

// The helpers themselves, against the standards' own test vectors, so the STUN
// tests that use them pin the port and not the helper.
SM_TEST(StunTestCrypto, Sha1Fips180Vectors) {
  uint8_t d[20];
  Sha1(Bytes("abc"), d);
  SM_EXPECT_EQ(Hex(d, 20), std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
  Sha1(Bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), d);
  SM_EXPECT_EQ(Hex(d, 20), std::string("84983e441c3bd26ebaae4aa1f95129e5e54670f1"));
  Sha1(Bytes(""), d);
  SM_EXPECT_EQ(Hex(d, 20), std::string("da39a3ee5e6b4b0d3255bfef95601890afd80709"));
}

SM_TEST(StunTestCrypto, HmacSha1Rfc2202Vectors) {
  uint8_t d[20];
  uint8_t key1[20];
  std::memset(key1, 0x0b, sizeof(key1));
  HmacSha1(key1, Bytes("Hi There"), d);
  SM_EXPECT_EQ(Hex(d, 20), std::string("b617318655057264e28bc0b6fb378c8ef146be00"));
  HmacSha1(Bytes("Jefe"), Bytes("what do ya want for nothing?"), d);
  SM_EXPECT_EQ(Hex(d, 20), std::string("effcdf6ae5eb2fa2d27416d5f184df9c259a7c79"));
  uint8_t key6[80];
  std::memset(key6, 0xaa, sizeof(key6));
  HmacSha1(key6, Bytes("Test Using Larger Than Block-Size Key - Hash Key First"), d);
  SM_EXPECT_EQ(Hex(d, 20), std::string("aa4ae5e15272d00e95705637ce8a3b55ed402112"));
}

SM_TEST(StunTestCrypto, Md5Rfc1321Vectors) {
  uint8_t d[16];
  Md5(Bytes(""), d);
  SM_EXPECT_EQ(Hex(d, 16), std::string("d41d8cd98f00b204e9800998ecf8427e"));
  Md5(Bytes("abc"), d);
  SM_EXPECT_EQ(Hex(d, 16), std::string("900150983cd24fb0d6963f7d28e17f72"));
  Md5(Bytes("12345678901234567890123456789012345678901234567890123456789012345678901234567890"), d);
  SM_EXPECT_EQ(Hex(d, 16), std::string("57edf4a22be3c955ac49da2e2107b67a"));
}

SM_TEST(StunTestCrypto, Sha256AndHmacVectors) {
  uint8_t d[32];
  Sha256(Bytes("abc"), d);
  SM_EXPECT_EQ(Hex(d, 32), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  Sha256(Bytes(""), d);
  SM_EXPECT_EQ(Hex(d, 32), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  // RFC 4231 test cases 1, 2 and 6.
  uint8_t key1[20];
  std::memset(key1, 0x0b, sizeof(key1));
  HmacSha256(key1, Bytes("Hi There"), d);
  SM_EXPECT_EQ(Hex(d, 32), std::string("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
  HmacSha256(Bytes("Jefe"), Bytes("what do ya want for nothing?"), d);
  SM_EXPECT_EQ(Hex(d, 32), std::string("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
  uint8_t key6[131];
  std::memset(key6, 0xaa, sizeof(key6));
  HmacSha256(key6, Bytes("Test Using Larger Than Block-Size Key - Hash Key First"), d);
  SM_EXPECT_EQ(Hex(d, 32), std::string("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));
}

// Issue #1048: STUN transaction ids (12 bytes, one of 64 characters per byte, stun.cc CreateRandomString) from
// CounterBytes repeat after 64 ids, which drops checks in long ICE runs; UniqueBytes' ids don't repeat.
SM_TEST(StunTestCrypto, UniqueIdsDontRepeat) {
  sm::p2p::testing::InstallTestCrypto();
  std::set<std::string> counter;
  for (int i = 0; i < 65; ++i) counter.insert(sm::p2p::StunMessage::GenerateTransactionId());
  SM_EXPECT_TRUE(counter.size() < 65u);
  sm::p2p::testing::InstallTestCryptoWithUniqueIds();
  std::set<std::string> unique;
  for (int i = 0; i < 100000; ++i) unique.insert(sm::p2p::StunMessage::GenerateTransactionId());
  SM_EXPECT_EQ(unique.size(), 100000u);
  sm::p2p::testing::InstallTestCrypto();
}
