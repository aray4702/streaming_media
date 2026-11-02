// The STUN crypto hooks (sm/p2p/stun.h) on sm::tls's BoringSSL HMAC, MD5 and
// random bytes (#984): RFC 5769's integrity values check with the real crypto
// as with the test helpers.
#include <cstring>
#include <span>
#include <string>

#include "sm/p2p/stun.h"
#include "sm/tls/crypto_random.h"
#include "sm/tls/message_digest.h"
#include "sm_test.h"

using namespace sm::p2p;

namespace {

bool TlsHmacSha1(std::span<const uint8_t> key, std::span<const uint8_t> data, uint8_t out[20]) {
  return sm::tls::ComputeHmac(sm::tls::kDigestSha1, key, data, std::span<uint8_t>(out, 20)) == 20;
}
bool TlsMd5(std::span<const uint8_t> data, uint8_t out[16]) {
  return sm::tls::ComputeDigest(sm::tls::kDigestMd5, data, std::span<uint8_t>(out, 16)) == 16;
}
bool TlsRandom(std::span<uint8_t> out) { return sm::tls::CreateRandomBytes(out); }

// RFC 5769 2.1 sample request and 2.4 long-term-auth request (as stun_unittest.cc).
const uint8_t kRequest[] = {
    0x00, 0x01, 0x00, 0x58, 0x21, 0x12, 0xa4, 0x42, 0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86, 0xfa, 0x87,
    0xdf, 0xae, 0x80, 0x22, 0x00, 0x10, 0x53, 0x54, 0x55, 0x4e, 0x20, 0x74, 0x65, 0x73, 0x74, 0x20, 0x63, 0x6c,
    0x69, 0x65, 0x6e, 0x74, 0x00, 0x24, 0x00, 0x04, 0x6e, 0x00, 0x01, 0xff, 0x80, 0x29, 0x00, 0x08, 0x93, 0x2f,
    0xf9, 0xb1, 0x51, 0x26, 0x3b, 0x36, 0x00, 0x06, 0x00, 0x09, 0x65, 0x76, 0x74, 0x6a, 0x3a, 0x68, 0x36, 0x76,
    0x59, 0x20, 0x20, 0x20, 0x00, 0x08, 0x00, 0x14, 0x9a, 0xea, 0xa7, 0x0c, 0xbf, 0xd8, 0xcb, 0x56, 0x78, 0x1e,
    0xf2, 0xb5, 0xb2, 0xd3, 0xf2, 0x49, 0xc1, 0xb5, 0x71, 0xa2, 0x80, 0x28, 0x00, 0x04, 0xe5, 0x7a, 0x3b, 0xcf};
const uint8_t kLongTermAuthRequest[] = {
    0x00, 0x01, 0x00, 0x60, 0x21, 0x12, 0xa4, 0x42, 0x78, 0xad, 0x34, 0x33, 0xc6, 0xad, 0x72, 0xc0, 0x29, 0xda,
    0x41, 0x2e, 0x00, 0x06, 0x00, 0x12, 0xe3, 0x83, 0x9e, 0xe3, 0x83, 0x88, 0xe3, 0x83, 0xaa, 0xe3, 0x83, 0x83,
    0xe3, 0x82, 0xaf, 0xe3, 0x82, 0xb9, 0x00, 0x00, 0x00, 0x15, 0x00, 0x1c, 0x66, 0x2f, 0x2f, 0x34, 0x39, 0x39,
    0x6b, 0x39, 0x35, 0x34, 0x64, 0x36, 0x4f, 0x4c, 0x33, 0x34, 0x6f, 0x4c, 0x39, 0x46, 0x53, 0x54, 0x76, 0x79,
    0x36, 0x34, 0x73, 0x41, 0x00, 0x14, 0x00, 0x0b, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x6f, 0x72,
    0x67, 0x00, 0x00, 0x08, 0x00, 0x14, 0xf6, 0x70, 0x24, 0x65, 0x6d, 0xd6, 0x4a, 0x3e, 0x02, 0xb8, 0xe0, 0x71,
    0x2e, 0x85, 0xc9, 0xa2, 0x8c, 0xa8, 0x96, 0x66};

}  // namespace

SM_TEST(StunTls, Rfc5769IntegrityWithBoringSsl) {
  StunCrypto saved = GetStunCrypto();
  StunCrypto c;
  c.hmac_sha1 = &TlsHmacSha1;
  c.md5 = &TlsMd5;
  c.random_bytes = &TlsRandom;
  SetStunCrypto(c);

  SM_EXPECT_TRUE(StunMessage::ValidateMessageIntegrityForTesting("VOkJxbRl1RmTxUk/WvJxBt", kRequest));
  SM_EXPECT_FALSE(StunMessage::ValidateMessageIntegrityForTesting("InvalidPassword", kRequest));
  std::string key;
  SM_EXPECT_TRUE(ComputeStunCredentialHash(
      "\xe3\x83\x9e\xe3\x83\x88\xe3\x83\xaa\xe3\x83\x83\xe3\x82\xaf\xe3\x82\xb9", "example.org", "TheMatrIX", &key));
  SM_EXPECT_TRUE(StunMessage::ValidateMessageIntegrityForTesting(key, kLongTermAuthRequest));

  // A signed binding request round-trips.
  StunMessage req(STUN_BINDING_REQUEST);
  SM_EXPECT_EQ(req.transaction_id().size(), kStunTransactionIdLength);
  req.AddAttribute(std::make_unique<StunByteStringAttribute>(STUN_ATTR_USERNAME, std::string_view("a:b")));
  SM_EXPECT_TRUE(req.AddMessageIntegrity("pw"));
  SM_EXPECT_TRUE(req.AddFingerprint());
  ByteBufferWriter out;
  SM_ASSERT_TRUE(req.Write(&out));
  StunMessage back;
  ByteBufferReader r(out);
  SM_ASSERT_TRUE(back.Read(&r));
  SM_EXPECT_TRUE(back.ValidateMessageIntegrity("pw") == StunMessage::IntegrityStatus::kIntegrityOk);
  SM_EXPECT_TRUE(StunMessage::ValidateFingerprint(out.DataView()));

  SetStunCrypto(saved);
}

// InstallStunCryptoFromTls (#997): BoringSSL's hooks in one call; ids are
// random, and the transaction manager sends.
#include "sm/p2p/stun_crypto_tls.h"
#include "sm/p2p/stun_request.h"

SM_TEST(StunTls, InstallStunCryptoFromTls) {
  const StunCrypto saved = GetStunCrypto();
  SetStunCrypto(StunCrypto{});
  InstallStunCryptoFromTls();
  SM_EXPECT_TRUE(GetStunCrypto().hmac_sha1 != nullptr);
  SM_EXPECT_TRUE(GetStunCrypto().md5 != nullptr);
  SM_EXPECT_TRUE(GetStunCrypto().random_bytes != nullptr);
  SM_EXPECT_TRUE(StunMessage::ValidateMessageIntegrityForTesting("VOkJxbRl1RmTxUk/WvJxBt", kRequest));
  // Ids differ from one message to the next.
  SM_EXPECT_NE(StunMessage::GenerateTransactionId(), StunMessage::GenerateTransactionId());
  int sent = 0;
  StunRequestManager manager([&sent](sm::Timestamp, std::span<const uint8_t>, StunRequest*) { ++sent; });
  struct Plain : public StunRequest {
    explicit Plain(StunRequestManager& m) : StunRequest(m, std::make_unique<StunMessage>(STUN_BINDING_REQUEST)) {
      SetAuthenticationRequired(false);
    }
  };
  SM_EXPECT_TRUE(manager.Send(sm::Timestamp::Seconds(1), std::make_unique<Plain>(manager)));
  SM_EXPECT_EQ(sent, 1);
  SetStunCrypto(saved);
}
