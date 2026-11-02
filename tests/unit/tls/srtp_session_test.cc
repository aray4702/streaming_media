// SrtpSession (#995): libwebrtc M157's pc/srtp_session_unittest.cc, case for case, plus DTLS-SRTP end to end (two
// DtlsTransports handshake, export the keys as DtlsSrtpTransport::ExtractParams does, and SRTP flows both ways).
//
// media/base/fake_rtp.h (kPcmuFrame, kFakeRtcpReport) is not in compare/webrtc/reference/; the packets below have
// what the cases depend on: an RTP packet with sequence number 1 and SSRC 1, and an RTCP receiver report.
#include <cstring>
#include <functional>
#include <limits>
#include <vector>

#include "sm/tls/dtls_transport.h"
#include "sm/tls/srtp_session.h"
#include "sm/tls/ssl_fingerprint.h"
#include "sm/tls/ssl_identity.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::tls;

namespace {

// PT=0 (PCMU), SN=1, TS=0, SSRC=1, then 160 bytes of payload (one 20 ms PCMU frame).
std::vector<uint8_t> PcmuFrame() {
  std::vector<uint8_t> p = {0x80, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
  p.resize(12 + 160, 0xFF);
  return p;
}
// RR from SSRC 1 with one report block about SSRC 2.
const std::vector<uint8_t> kFakeRtcpReport = {
    0x81, 0xC9, 0x00, 0x07, 0x00, 0x00, 0x00, 0x01,  // V=2 RC=1 PT=RR len=7, SSRC 1
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,  // report block: SSRC 2, loss 0
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,  // highest seq 1, jitter 0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // LSR, DLSR
};

// pc/test/srtp_test_util.h
const std::vector<uint8_t> kTestKey1(reinterpret_cast<const uint8_t*>("ABCDEFGHIJKLMNOPQRSTUVWXYZ1234"),
                                     reinterpret_cast<const uint8_t*>("ABCDEFGHIJKLMNOPQRSTUVWXYZ1234") + 30);
const std::vector<uint8_t> kTestKey2(reinterpret_cast<const uint8_t*>("4321ZYXWVUTSRQPONMLKJIHGFEDCBA"),
                                     reinterpret_cast<const uint8_t*>("4321ZYXWVUTSRQPONMLKJIHGFEDCBA") + 30);
size_t rtp_auth_tag_len(int suite) {
  return suite == kSrtpAes128CmSha1_32 ? 4 : suite == kSrtpAes128CmSha1_80 ? 10 : 16;
}
size_t rtcp_auth_tag_len(int suite) {
  return suite == kSrtpAes128CmSha1_32 || suite == kSrtpAes128CmSha1_80 ? 10 : 16;
}

struct Fixture {
  SrtpSession s1, s2;
  std::vector<uint8_t> rtp = PcmuFrame();
  std::vector<uint8_t> rtcp = kFakeRtcpReport;

  void TestProtectRtp(int suite) {
    const size_t len = rtp.size();
    SM_EXPECT_TRUE(s1.ProtectRtp(rtp));
    SM_EXPECT_EQ(rtp.size(), len + rtp_auth_tag_len(suite));
    SM_EXPECT_NE(0, std::memcmp(PcmuFrame().data(), rtp.data(), len));
  }
  void TestProtectRtcp(int suite) {
    const size_t len = rtcp.size();
    SM_EXPECT_TRUE(s1.ProtectRtcp(rtcp));
    SM_EXPECT_EQ(rtcp.size(), len + 4 + rtcp_auth_tag_len(suite));
    SM_EXPECT_NE(0, std::memcmp(kFakeRtcpReport.data(), rtcp.data(), len));
  }
  void TestUnprotectRtp() {
    SM_EXPECT_TRUE(s2.UnprotectRtp(rtp));
    SM_EXPECT_TRUE(rtp == PcmuFrame());
  }
  void TestUnprotectRtcp() {
    SM_EXPECT_TRUE(s2.UnprotectRtcp(rtcp));
    SM_EXPECT_TRUE(rtcp == kFakeRtcpReport);
  }
  void SetSeq(uint16_t seq) {
    rtp = PcmuFrame();
    rtp[2] = static_cast<uint8_t>(seq >> 8);
    rtp[3] = static_cast<uint8_t>(seq);
  }
};

void ProtectUnprotect(int suite) {
  Fixture f;
  SM_EXPECT_TRUE(f.s1.SetSend(suite, kTestKey1));
  SM_EXPECT_TRUE(f.s2.SetReceive(suite, kTestKey1));
  f.TestProtectRtp(suite);
  f.TestProtectRtcp(suite);
  f.TestUnprotectRtp();
  f.TestUnprotectRtcp();
}

}  // namespace

SM_TEST(TlsSrtpSession, TestGoodSetup) {
  Fixture f;
  SM_EXPECT_TRUE(f.s1.SetSend(kSrtpAes128CmSha1_80, kTestKey1));
  SM_EXPECT_TRUE(f.s2.SetReceive(kSrtpAes128CmSha1_80, kTestKey1));
}

SM_TEST(TlsSrtpSession, TestBadSetup) {
  // The keys can't change once set (Update* does that).
  Fixture f;
  SM_EXPECT_TRUE(f.s1.SetSend(kSrtpAes128CmSha1_80, kTestKey1));
  SM_EXPECT_TRUE(f.s2.SetReceive(kSrtpAes128CmSha1_80, kTestKey1));
  SM_EXPECT_FALSE(f.s1.SetSend(kSrtpAes128CmSha1_80, kTestKey2));
  SM_EXPECT_FALSE(f.s2.SetReceive(kSrtpAes128CmSha1_80, kTestKey2));
}

SM_TEST(TlsSrtpSession, TestKeysTooShort) {
  Fixture f;
  SM_EXPECT_FALSE(f.s1.SetSend(kSrtpAes128CmSha1_80, std::span(kTestKey1).first(1)));
  SM_EXPECT_FALSE(f.s2.SetReceive(kSrtpAes128CmSha1_80, std::span(kTestKey1).first(1)));
}

SM_TEST(TlsSrtpSession, TestProtect_AES_CM_128_HMAC_SHA1_80) { ProtectUnprotect(kSrtpAes128CmSha1_80); }
SM_TEST(TlsSrtpSession, TestProtect_AES_CM_128_HMAC_SHA1_32) { ProtectUnprotect(kSrtpAes128CmSha1_32); }

// Not in libwebrtc's srtp_session_unittest.cc (its GCM cases are in srtp_transport_unittest.cc): the GCM suites
// libwebrtc M157 offers (DtlsConfig's defaults), with their 16-byte tags and 28/44-byte keys.
SM_TEST(TlsSrtpSession, TestProtect_AEAD_AES_GCM) {
  for (int suite : {kSrtpAeadAes128Gcm, kSrtpAeadAes256Gcm}) {
    int k = 0, s = 0;
    SM_ASSERT_TRUE(SrtpKeyAndSaltLength(suite, k, s));
    std::vector<uint8_t> key(static_cast<size_t>(k + s));
    for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i * 7 + 3);
    Fixture f;
    SM_EXPECT_FALSE(f.s1.SetSend(suite, kTestKey1));  // A 30-byte key is the wrong length for GCM.
    SM_EXPECT_TRUE(f.s1.SetSend(suite, key));
    SM_EXPECT_TRUE(f.s2.SetReceive(suite, key));
    SM_EXPECT_EQ(f.s1.GetSrtpOverhead(), 16);
    f.TestProtectRtp(suite);
    f.TestProtectRtcp(suite);
    f.TestUnprotectRtp();
    f.TestUnprotectRtcp();
  }
}

SM_TEST(TlsSrtpSession, TestGetSendStreamPacketIndex) {
  Fixture f;
  SM_EXPECT_TRUE(f.s1.SetSend(kSrtpAes128CmSha1_32, kTestKey1));
  int64_t index = 0;
  SM_EXPECT_TRUE(f.s1.ProtectRtp(f.rtp, &index));
  // Shifted by 16, in network byte order: NetworkToHost64(1 << 16) on a little-endian host.
  SM_EXPECT_EQ(index, int64_t{0x0000010000000000});
}

SM_TEST(TlsSrtpSession, TestTamperReject) {
  Fixture f;
  SM_EXPECT_TRUE(f.s1.SetSend(kSrtpAes128CmSha1_80, kTestKey1));
  SM_EXPECT_TRUE(f.s2.SetReceive(kSrtpAes128CmSha1_80, kTestKey1));
  f.TestProtectRtp(kSrtpAes128CmSha1_80);
  f.rtp[0] = 0x12;
  SM_EXPECT_FALSE(f.s2.UnprotectRtp(f.rtp));
  SM_EXPECT_EQ(f.s2.rtp_unprotect_failures(), 1);
  SM_EXPECT_EQ(f.s2.last_rtp_unprotect_error(), kSrtpErrBadParam);
  f.TestProtectRtcp(kSrtpAes128CmSha1_80);
  f.rtcp[1] = 0x34;
  SM_EXPECT_FALSE(f.s2.UnprotectRtcp(f.rtcp));
  SM_EXPECT_EQ(f.s2.rtcp_unprotect_failures(), 1);
  SM_EXPECT_EQ(f.s2.last_rtcp_unprotect_error(), kSrtpErrAuthFail);
}

SM_TEST(TlsSrtpSession, TestUnencryptReject) {
  Fixture f;
  SM_EXPECT_TRUE(f.s1.SetSend(kSrtpAes128CmSha1_80, kTestKey1));
  SM_EXPECT_TRUE(f.s2.SetReceive(kSrtpAes128CmSha1_80, kTestKey1));
  SM_EXPECT_FALSE(f.s2.UnprotectRtp(f.rtp));
  SM_EXPECT_EQ(f.s2.last_rtp_unprotect_error(), kSrtpErrAuthFail);
  SM_EXPECT_FALSE(f.s2.UnprotectRtcp(f.rtcp));
  SM_EXPECT_EQ(f.s2.last_rtcp_unprotect_error(), kSrtpErrCantCheck);
}

SM_TEST(TlsSrtpSession, TestBuffersTooSmall) {
  // A buffer without room for the trailer is an error (the span form; a vector grows).
  Fixture f;
  SM_EXPECT_TRUE(f.s1.SetSend(kSrtpAes128CmSha1_80, kTestKey1));
  size_t len = f.rtp.size();
  SM_EXPECT_FALSE(f.s1.ProtectRtp(std::span<uint8_t>(f.rtp), len));
  SM_EXPECT_EQ(len, f.rtp.size());
  len = f.rtcp.size();
  SM_EXPECT_FALSE(f.s1.ProtectRtcp(std::span<uint8_t>(f.rtcp), len));
  // RTCP needs the 4-byte index as well as the tag.
  std::vector<uint8_t> rtcp = kFakeRtcpReport;
  rtcp.resize(rtcp.size() + 10);
  len = kFakeRtcpReport.size();
  SM_EXPECT_FALSE(f.s1.ProtectRtcp(std::span<uint8_t>(rtcp), len));
  rtcp.resize(kFakeRtcpReport.size() + 14);
  SM_EXPECT_TRUE(f.s1.ProtectRtcp(std::span<uint8_t>(rtcp), len));
  SM_EXPECT_EQ(len, rtcp.size());
}

SM_TEST(TlsSrtpSession, TestReplay) {
  const uint16_t kMaxSeqnum = std::numeric_limits<uint16_t>::max() - 1;
  const uint16_t seqnum_big = 62275;
  const uint16_t seqnum_small = 10;
  const uint16_t replay_window = 1024;
  Fixture f;
  SM_EXPECT_TRUE(f.s1.SetSend(kSrtpAes128CmSha1_80, kTestKey1));
  SM_EXPECT_TRUE(f.s2.SetReceive(kSrtpAes128CmSha1_80, kTestKey1));
  f.SetSeq(seqnum_big);
  SM_EXPECT_TRUE(f.s1.ProtectRtp(f.rtp));
  // Within the 1024 window: protected again.
  f.SetSeq(seqnum_big - replay_window + 1);
  SM_EXPECT_TRUE(f.s1.ProtectRtp(f.rtp));
  // Outside it: refused.
  f.SetSeq(seqnum_big - replay_window - 1);
  SM_EXPECT_FALSE(f.s1.ProtectRtp(f.rtp));
  // Forward to a small number (a wrap).
  f.SetSeq(seqnum_small);
  SM_EXPECT_TRUE(f.s1.ProtectRtp(f.rtp));
  // Around 0, outside the window: refused.
  f.SetSeq(static_cast<uint16_t>(kMaxSeqnum + seqnum_small - replay_window - 1));
  SM_EXPECT_FALSE(f.s1.ProtectRtp(f.rtp));
  // Around 0, within the window: protected.
  for (uint16_t seqnum = 65000; seqnum < 65003; ++seqnum) {
    f.SetSeq(seqnum);
    SM_EXPECT_TRUE(f.s1.ProtectRtp(f.rtp));
  }
  // Back to the normal sequence: the loop above must not have moved libsrtp's local index.
  f.SetSeq(seqnum_small + 1);
  SM_EXPECT_TRUE(f.s1.ProtectRtp(f.rtp));
}

SM_TEST(TlsSrtpSession, RemoveSsrc) {
  Fixture f;
  SM_EXPECT_TRUE(f.s1.SetSend(kSrtpAes128CmSha1_80, kTestKey1));
  SM_EXPECT_TRUE(f.s2.SetReceive(kSrtpAes128CmSha1_80, kTestKey1));
  SM_EXPECT_TRUE(f.s1.ProtectRtp(f.rtp));
  SM_EXPECT_TRUE(f.s2.UnprotectRtp(f.rtp));
  SM_EXPECT_TRUE(f.rtp == PcmuFrame());
  // The same packet again is a replay.
  f.rtp = PcmuFrame();
  SM_EXPECT_TRUE(f.s1.ProtectRtp(f.rtp));
  SM_EXPECT_FALSE(f.s2.UnprotectRtp(f.rtp));
  SM_EXPECT_EQ(f.s2.last_rtp_unprotect_error(), kSrtpErrReplayFail);
  // Removing SSRC 1 drops its state, so it is not a replay any more.
  SM_EXPECT_TRUE(f.s2.RemoveSsrcFromSession(1));
  SM_EXPECT_FALSE(f.s2.RemoveSsrcFromSession(1));
  SM_EXPECT_TRUE(f.s2.UnprotectRtp(f.rtp));
  SM_EXPECT_TRUE(f.rtp == PcmuFrame());
  SM_EXPECT_TRUE(f.s2.RemoveSsrcFromSession(1));
}

namespace {
// PT=0, SN=65535 then SN=1 (skipping 0), TS=0, SSRC=1, two payload bytes.
std::vector<uint8_t> WrapFrame(uint16_t seq) {
  return {0x80, 0x00, static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq), 0, 0, 0, 0, 0, 0, 0, 1, 0xBE, 0xEF};
}
}  // namespace

SM_TEST(TlsSrtpSession, ProtectUnprotectWrapAroundRocMismatch) {
  // Why the initial sequence number belongs in the lower half: a wrap with loss or reordering can fail to decrypt.
  Fixture f;
  SM_EXPECT_TRUE(f.s1.SetSend(kSrtpAes128CmSha1_80, kTestKey1));
  SM_EXPECT_TRUE(f.s2.SetReceive(kSrtpAes128CmSha1_80, kTestKey1));
  auto p1 = WrapFrame(65535), p2 = WrapFrame(1);
  SM_EXPECT_TRUE(f.s1.ProtectRtp(p1));
  SM_EXPECT_EQ(p1.size(), 24u);
  SM_EXPECT_TRUE(f.s1.ProtectRtp(p2));  // ROC 1 (RFC 3711 3.3.1).
  SM_EXPECT_EQ(p2.size(), 24u);
  // Frame 2 first: the receiver tries ROC 0 and fails (https://issues.webrtc.org/353565743).
  auto p2_copy = p2;
  SM_EXPECT_FALSE(f.s2.UnprotectRtp(p2_copy));
  SM_EXPECT_TRUE(f.s2.UnprotectRtp(p1));
  SM_ASSERT_EQ(p1.size(), 14u);
  SM_EXPECT_TRUE(p1[12] == 0xBE && p1[13] == 0xEF);
  // Now the rollover is seen, and frame 2 decrypts with ROC 1.
  SM_EXPECT_TRUE(f.s2.UnprotectRtp(p2));
  SM_ASSERT_EQ(p2.size(), 14u);
  SM_EXPECT_TRUE(p2[12] == 0xBE && p2[13] == 0xEF);
}

SM_TEST(TlsSrtpSession, ProtectGetPacketIndex) {
  Fixture f;
  SM_EXPECT_TRUE(f.s1.SetSend(kSrtpAes128CmSha1_80, kTestKey1));
  auto p1 = WrapFrame(65535), p2 = WrapFrame(1);
  int64_t index = 0;
  SM_EXPECT_TRUE(f.s1.ProtectRtp(p1, &index));
  SM_EXPECT_EQ(p1.size(), 24u);
  SM_EXPECT_EQ(index, int64_t{0xffff00000000});  // ntohl(65535 << 16)
  SM_EXPECT_TRUE(f.s1.ProtectRtp(p2, &index));
  SM_EXPECT_EQ(index, int64_t{0x10001000000});  // ntohl(65537 << 16)
}

namespace {
// RFC 9335 Appendix A.1 test vectors, as libwebrtc's SrtpSessionTestCryptex uses them.
const std::vector<uint8_t> kCryptexKey = {0xe1, 0xf9, 0x7a, 0x0d, 0x3e, 0x01, 0x8b, 0xe0, 0xd6, 0x4f,
                                          0xa3, 0x2c, 0x06, 0xde, 0x41, 0x39, 0x0e, 0xc6, 0x75, 0xad,
                                          0x49, 0x8a, 0xfe, 0xeb, 0xb6, 0x96, 0x0b, 0x3a, 0xab, 0xe6};
struct CryptexVector {
  std::vector<uint8_t> plain, encrypted;
};
const CryptexVector kCryptexVectors[] = {
    // A.1.1: one-byte header extension.
    {{0x90, 0x0f, 0x12, 0x35, 0xde, 0xca, 0xfb, 0xad, 0xca, 0xfe, 0xba, 0xbe, 0xbe, 0xde, 0x00, 0x01, 0x51, 0x00,
      0x02, 0x00, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab},
     {0x90, 0x0f, 0x12, 0x35, 0xde, 0xca, 0xfb, 0xad, 0xca, 0xfe, 0xba, 0xbe, 0xc0, 0xde, 0x00, 0x01,
      0xeb, 0x92, 0x36, 0x52, 0x51, 0xc3, 0xe0, 0x36, 0xf8, 0xde, 0x27, 0xe9, 0xc2, 0x7e, 0xe3, 0xe0,
      0xb4, 0x65, 0x1d, 0x9f, 0xbc, 0x42, 0x18, 0xa7, 0x02, 0x44, 0x52, 0x2f, 0x34, 0xa5}},
    // A.1.2: two-byte header extension.
    {{0x90, 0x0f, 0x12, 0x36, 0xde, 0xca, 0xfb, 0xad, 0xca, 0xfe, 0xba, 0xbe, 0x10, 0x00, 0x00, 0x01, 0x05, 0x02,
      0x00, 0x02, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab},
     {0x90, 0x0f, 0x12, 0x36, 0xde, 0xca, 0xfb, 0xad, 0xca, 0xfe, 0xba, 0xbe, 0xc2, 0xde, 0x00, 0x01,
      0x4e, 0xd9, 0xcc, 0x4e, 0x6a, 0x71, 0x2b, 0x30, 0x96, 0xc5, 0xca, 0x77, 0x33, 0x9d, 0x42, 0x04,
      0xce, 0x0d, 0x77, 0x39, 0x6c, 0xab, 0x69, 0x58, 0x5f, 0xbc, 0xe3, 0x81, 0x94, 0xa5}},
    // A.1.3: one-byte header extension and CSRCs.
    {{0x92, 0x0f, 0x12, 0x38, 0xde, 0xca, 0xfb, 0xad, 0xca, 0xfe, 0xba, 0xbe, 0x00, 0x01, 0xe2,
      0x40, 0x00, 0x00, 0xb2, 0x6e, 0xbe, 0xde, 0x00, 0x01, 0x51, 0x00, 0x02, 0x00, 0xab, 0xab,
      0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab},
     {0x92, 0x0f, 0x12, 0x38, 0xde, 0xca, 0xfb, 0xad, 0xca, 0xfe, 0xba, 0xbe, 0x8b, 0xb6, 0xe1, 0x2b, 0x5c, 0xff,
      0x16, 0xdd, 0xc0, 0xde, 0x00, 0x01, 0x92, 0x83, 0x8c, 0x8c, 0x09, 0xe5, 0x83, 0x93, 0xe1, 0xde, 0x3a, 0x9a,
      0x74, 0x73, 0x4d, 0x67, 0x45, 0x67, 0x13, 0x38, 0xc3, 0xac, 0xf1, 0x1d, 0xa2, 0xdf, 0x84, 0x23, 0xbe, 0xe0}},
    // A.1.4: two-byte header extension and CSRCs.
    {{0x92, 0x0f, 0x12, 0x39, 0xde, 0xca, 0xfb, 0xad, 0xca, 0xfe, 0xba, 0xbe, 0x00, 0x01, 0xe2,
      0x40, 0x00, 0x00, 0xb2, 0x6e, 0x10, 0x00, 0x00, 0x01, 0x05, 0x02, 0x00, 0x02, 0xab, 0xab,
      0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab},
     {0x92, 0x0f, 0x12, 0x39, 0xde, 0xca, 0xfb, 0xad, 0xca, 0xfe, 0xba, 0xbe, 0xf7, 0x0e, 0x51, 0x3e, 0xb9, 0x0b,
      0x9b, 0x25, 0xc2, 0xde, 0x00, 0x01, 0xbb, 0xed, 0x48, 0x48, 0xfa, 0xa6, 0x44, 0x66, 0x5f, 0x3d, 0x7f, 0x34,
      0x12, 0x59, 0x14, 0xe9, 0xf4, 0xd0, 0xae, 0x92, 0x3c, 0x6f, 0x47, 0x9b, 0x95, 0xa0, 0xf7, 0xb5, 0x31, 0x33}},
    // A.1.5: empty one-byte header extension and CSRCs.
    {{0x92, 0x0f, 0x12, 0x3a, 0xde, 0xca, 0xfb, 0xad, 0xca, 0xfe, 0xba, 0xbe, 0x00, 0x01,
      0xe2, 0x40, 0x00, 0x00, 0xb2, 0x6e, 0xbe, 0xde, 0x00, 0x00, 0xab, 0xab, 0xab, 0xab,
      0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab},
     {0x92, 0x0f, 0x12, 0x3a, 0xde, 0xca, 0xfb, 0xad, 0xca, 0xfe, 0xba, 0xbe, 0x71, 0x30, 0xb6, 0xab, 0xfe,
      0x2a, 0xb0, 0xe3, 0xc0, 0xde, 0x00, 0x00, 0xe3, 0xd9, 0xf6, 0x4b, 0x25, 0xc9, 0xe7, 0x4c, 0xb4, 0xcf,
      0x8e, 0x43, 0xfb, 0x92, 0xe3, 0x78, 0x1c, 0x2c, 0x0c, 0xea, 0xb6, 0xb3, 0xa4, 0x99, 0xa1, 0x4c}},
    // A.1.6: empty two-byte header extension and CSRCs.
    {{0x92, 0x0f, 0x12, 0x3b, 0xde, 0xca, 0xfb, 0xad, 0xca, 0xfe, 0xba, 0xbe, 0x00, 0x01,
      0xe2, 0x40, 0x00, 0x00, 0xb2, 0x6e, 0x10, 0x00, 0x00, 0x00, 0xab, 0xab, 0xab, 0xab,
      0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab},
     {0x92, 0x0f, 0x12, 0x3b, 0xde, 0xca, 0xfb, 0xad, 0xca, 0xfe, 0xba, 0xbe, 0xcb, 0xf2, 0x4c, 0x12, 0x43,
      0x30, 0xe1, 0xc8, 0xc2, 0xde, 0x00, 0x00, 0x59, 0x9d, 0xd4, 0x5b, 0xc9, 0xd6, 0x87, 0xb6, 0x03, 0xe8,
      0xb5, 0x9d, 0x77, 0x1f, 0xd3, 0x8e, 0x88, 0xb1, 0x70, 0xe0, 0xcd, 0x31, 0xe1, 0x25, 0xea, 0xbe}},
};
}  // namespace

SM_TEST(TlsSrtpSession, TestCryptexRfc9335_A_1) {
  for (const auto& v : kCryptexVectors) {
    Fixture f;
    SM_EXPECT_TRUE(f.s1.UseCryptex(true, true, true));
    SM_EXPECT_TRUE(f.s1.SetSend(kSrtpAes128CmSha1_80, kCryptexKey));
    std::vector<uint8_t> packet = v.plain;
    SM_EXPECT_TRUE(f.s1.ProtectRtp(packet));
    SM_EXPECT_TRUE(packet == v.encrypted);
    SM_EXPECT_TRUE(f.s2.UseCryptex(true, true, false));
    SM_EXPECT_TRUE(f.s2.SetReceive(kSrtpAes128CmSha1_80, kCryptexKey));
    SM_EXPECT_TRUE(f.s2.UnprotectRtp(packet));
    SM_EXPECT_TRUE(packet == v.plain);
  }
}

SM_TEST(TlsSrtpSession, TestCryptexRequire) {
  // The sender doesn't use cryptex; the receiver requires it, so a packet with clear extensions is refused.
  Fixture f;
  SM_EXPECT_TRUE(f.s1.SetSend(kSrtpAes128CmSha1_80, kCryptexKey));
  std::vector<uint8_t> packet = kCryptexVectors[0].plain;
  SM_EXPECT_TRUE(f.s1.ProtectRtp(packet));
  SM_EXPECT_TRUE(f.s2.UseCryptex(true, true, false));
  SM_EXPECT_TRUE(f.s2.SetReceive(kSrtpAes128CmSha1_80, kCryptexKey));
  SM_EXPECT_FALSE(f.s2.UnprotectRtp(packet));
  SM_EXPECT_EQ(f.s2.last_rtp_unprotect_error(), kSrtpErrCryptex);
}

namespace {

// Two DtlsTransports over a 10 ms pipe until both connect (as dtls_transport_test.cc's Pair).
struct DtlsPair {
  std::unique_ptr<DtlsTransport> client, server;
  Timestamp now = Timestamp::Seconds(1000);
  explicit DtlsPair(DtlsConfig cfg = {}) {
    auto cid = SslIdentity::Create("client");
    auto sid = SslIdentity::Create("server");
    auto cfp = SslFingerprint::CreateFromCertificate(*cid);
    auto sfp = SslFingerprint::CreateFromCertificate(*sid);
    client = std::make_unique<DtlsTransport>(std::move(cid), DtlsRole::kClient, cfg);
    server = std::make_unique<DtlsTransport>(std::move(sid), DtlsRole::kServer, cfg);
    client->SetPeerFingerprint(*sfp, now);
    server->SetPeerFingerprint(*cfp, now);
  }
  void Connect() {
    server->Start(now);
    client->Start(now);
    struct InFlight { Timestamp at; bool to_server; std::vector<uint8_t> d; };
    std::vector<InFlight> flight;
    for (int t = 0; t < 1000; ++t, now = now + TimeDelta::Millis(1)) {
      for (size_t i = 0; i < flight.size();) {
        if (flight[i].at <= now) {
          (flight[i].to_server ? *server : *client).OnPacket(flight[i].d, now);
          flight.erase(flight.begin() + static_cast<long>(i));
        } else {
          ++i;
        }
      }
      for (DtlsTransport* tr : {client.get(), server.get()}) {
        if (auto due = tr->NextTimeout(); due && *due <= now) tr->OnTimeout(now);
      }
      for (auto& d : client->TakeOutgoing()) flight.push_back({now + TimeDelta::Millis(10), true, std::move(d)});
      for (auto& d : server->TakeOutgoing()) flight.push_back({now + TimeDelta::Millis(10), false, std::move(d)});
    }
  }
};

}  // namespace

SM_TEST(TlsDtlsSrtp, ExtractedKeysPairUpByRole) {
  // DtlsSrtpTransport::ExtractParams: the client sends with the client key+salt and the server receives with it.
  DtlsPair p;
  SrtpKeys none;
  SM_EXPECT_FALSE(ExtractSrtpKeys(*p.client, none));  // Not connected yet.
  p.Connect();
  SM_ASSERT_TRUE(p.client->state() == DtlsState::kConnected && p.server->state() == DtlsState::kConnected);
  SrtpKeys c, s;
  SM_ASSERT_TRUE(ExtractSrtpKeys(*p.client, c));
  SM_ASSERT_TRUE(ExtractSrtpKeys(*p.server, s));
  SM_EXPECT_EQ(c.crypto_suite, kSrtpAes128CmSha1_80);
  SM_EXPECT_EQ(s.crypto_suite, kSrtpAes128CmSha1_80);
  SM_EXPECT_EQ(c.send_key.size(), 30u);  // 16-byte key + 14-byte salt.
  SM_EXPECT_TRUE(c.send_key == s.recv_key);
  SM_EXPECT_TRUE(c.recv_key == s.send_key);
  SM_EXPECT_TRUE(c.send_key != c.recv_key);
  // The split itself: client key = material[0,16) + material[32,46).
  std::vector<uint8_t> m(60);
  SM_ASSERT_TRUE(p.client->ExportSrtpKeyingMaterial(m));
  std::vector<uint8_t> client_key(m.begin(), m.begin() + 16);
  client_key.insert(client_key.end(), m.begin() + 32, m.begin() + 46);
  SM_EXPECT_TRUE(c.send_key == client_key);
}

SM_TEST(TlsDtlsSrtp, RtpAndRtcpBothWaysAfterTheHandshake) {
  for (int suite : {kSrtpAes128CmSha1_80, kSrtpAeadAes128Gcm, kSrtpAeadAes256Gcm}) {
    DtlsConfig cfg;
    cfg.srtp_crypto_suites = {suite};
    DtlsPair p(cfg);
    p.Connect();
    SrtpKeys c, s;
    SM_ASSERT_TRUE(ExtractSrtpKeys(*p.client, c));
    SM_ASSERT_TRUE(ExtractSrtpKeys(*p.server, s));
    SM_EXPECT_EQ(c.crypto_suite, suite);
    SrtpSession c_send, c_recv, s_send, s_recv;
    SM_ASSERT_TRUE(c_send.SetSend(c.crypto_suite, c.send_key));
    SM_ASSERT_TRUE(c_recv.SetReceive(c.crypto_suite, c.recv_key));
    SM_ASSERT_TRUE(s_send.SetSend(s.crypto_suite, s.send_key));
    SM_ASSERT_TRUE(s_recv.SetReceive(s.crypto_suite, s.recv_key));
    for (int i = 0; i < 3; ++i) {
      auto up = PcmuFrame(), down = PcmuFrame();
      up[3] = down[3] = static_cast<uint8_t>(1 + i);
      const auto up_plain = up, down_plain = down;
      SM_ASSERT_TRUE(c_send.ProtectRtp(up));
      SM_ASSERT_TRUE(s_send.ProtectRtp(down));
      SM_EXPECT_TRUE(up != down);  // Different keys each way.
      SM_EXPECT_TRUE(s_recv.UnprotectRtp(up));
      SM_EXPECT_TRUE(c_recv.UnprotectRtp(down));
      SM_EXPECT_TRUE(up == up_plain);
      SM_EXPECT_TRUE(down == down_plain);
    }
    auto rtcp = kFakeRtcpReport;
    SM_ASSERT_TRUE(s_send.ProtectRtcp(rtcp));
    SM_EXPECT_TRUE(c_recv.UnprotectRtcp(rtcp));
    SM_EXPECT_TRUE(rtcp == kFakeRtcpReport);
    // The wrong direction's key fails authentication.
    auto wrong = PcmuFrame();
    wrong[3] = 100;  // A sequence number c_recv hasn't seen, so the check is authentication, not replay.
    SM_ASSERT_TRUE(c_send.ProtectRtp(wrong));
    SM_EXPECT_FALSE(c_recv.UnprotectRtp(wrong));
    SM_EXPECT_EQ(c_recv.last_rtp_unprotect_error(), kSrtpErrAuthFail);
  }
}
