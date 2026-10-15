#include <vector>

#include "sm/rtp/rtcp_sdes_bye.h"
#include "sm_test.h"

using namespace sm::rtp;
using Bytes = std::vector<uint8_t>;

SM_TEST(RtcpSdesBye, SdesCnameByteLayout) {
  // RFC 3550 6.5: SSRC, CNAME item, terminator, padded to a word boundary.
  std::vector<SdesChunk> chunks = {{1, {{kSdesCname, "ab"}}}};
  Bytes expect = {0x81, 202, 0x00, 0x03, 0, 0, 0, 1, 1, 2, 'a', 'b', 0, 0, 0, 0};
  SM_EXPECT_TRUE(SerializeSdes(chunks) == expect);
  auto back = ParseSdes(expect);
  SM_ASSERT_TRUE(back.has_value());
  SM_ASSERT_EQ(back->size(), 1u);
  SM_EXPECT_EQ((*back)[0].ssrc, 1u);
  SM_ASSERT_EQ((*back)[0].items.size(), 1u);
  SM_EXPECT_EQ((*back)[0].items[0].type, kSdesCname);
  SM_EXPECT_TRUE((*back)[0].items[0].text == "ab");
}

SM_TEST(RtcpSdesBye, SdesAlignmentAndMultipleChunks) {
  // Text lengths 0..5 put the terminator at every offset in a word.
  for (size_t n = 0; n <= 5; ++n) {
    std::vector<SdesChunk> chunks = {{7, {{kSdesCname, std::string(n, 'x')}, {4, "p"}}}, {9, {}}};
    Bytes b = SerializeSdes(chunks);
    SM_ASSERT_TRUE(!b.empty());
    SM_EXPECT_EQ(b.size() % 4, 0u);
    SM_EXPECT_EQ(b.size(), (size_t{b[2]} * 256 + b[3] + 1) * 4);
    auto back = ParseSdes(b);
    SM_ASSERT_TRUE(back.has_value());
    SM_ASSERT_EQ(back->size(), 2u);
    SM_EXPECT_EQ((*back)[0].items.size(), 2u);
    SM_EXPECT_EQ((*back)[0].items[0].text.size(), n);
    SM_EXPECT_EQ((*back)[1].ssrc, 9u);
    SM_EXPECT_TRUE((*back)[1].items.empty());
  }
}

SM_TEST(RtcpSdesBye, SdesRejectsUnencodableAndMalformed) {
  SM_EXPECT_TRUE(SerializeSdes({}).empty());
  std::vector<SdesChunk> too_many(32, SdesChunk{1, {}});
  SM_EXPECT_TRUE(SerializeSdes(too_many).empty());
  SM_EXPECT_TRUE(SerializeSdes(std::vector<SdesChunk>{{1, {{0, "x"}}}}).empty());
  SM_EXPECT_TRUE(SerializeSdes(std::vector<SdesChunk>{{1, {{1, std::string(256, 'x')}}}}).empty());
  SM_EXPECT_FALSE(SerializeSdes(std::vector<SdesChunk>{{1, {{1, std::string(255, 'x')}}}}).empty());

  Bytes good = SerializeSdes(std::vector<SdesChunk>{{1, {{kSdesCname, "ab"}}}});
  Bytes wrong_type = good;
  wrong_type[1] = 203;
  SM_EXPECT_FALSE(ParseSdes(wrong_type).has_value());
  Bytes bad_len = good;
  bad_len[3] = 5;
  SM_EXPECT_FALSE(ParseSdes(bad_len).has_value());
  // Item length runs past the packet.
  Bytes overrun = good;
  overrun[9] = 200;
  SM_EXPECT_FALSE(ParseSdes(overrun).has_value());
  // Count says two chunks but only one is present.
  Bytes count = good;
  count[0] = 0x82;
  SM_EXPECT_FALSE(ParseSdes(count).has_value());
  // No terminator before the end of the packet.
  Bytes no_term = {0x81, 202, 0x00, 0x02, 0, 0, 0, 1, 1, 2, 'a', 'b'};
  SM_EXPECT_FALSE(ParseSdes(no_term).has_value());
}

SM_TEST(RtcpSdesBye, ByeByteLayout) {
  Bye b{{0x11223344}, "bye"};
  Bytes expect = {0x81, 203, 0x00, 0x02, 0x11, 0x22, 0x33, 0x44, 3, 'b', 'y', 'e'};
  SM_EXPECT_TRUE(SerializeBye(b) == expect);
  auto back = ParseBye(expect);
  SM_ASSERT_TRUE(back.has_value());
  SM_ASSERT_EQ(back->ssrcs.size(), 1u);
  SM_EXPECT_EQ(back->ssrcs[0], 0x11223344u);
  SM_EXPECT_TRUE(back->reason == "bye");
}

SM_TEST(RtcpSdesBye, ByeWithoutReasonAndPadded) {
  Bytes b = SerializeBye(Bye{{1, 2}, ""});
  Bytes expect = {0x82, 203, 0x00, 0x02, 0, 0, 0, 1, 0, 0, 0, 2};
  SM_EXPECT_TRUE(b == expect);
  auto back = ParseBye(b);
  SM_ASSERT_TRUE(back.has_value());
  SM_EXPECT_EQ(back->ssrcs.size(), 2u);
  SM_EXPECT_TRUE(back->reason.empty());
  // Reason of every length 1..8 pads to a word and round-trips.
  for (size_t n = 1; n <= 8; ++n) {
    Bytes r = SerializeBye(Bye{{5}, std::string(n, 'r')});
    SM_ASSERT_TRUE(!r.empty());
    SM_EXPECT_EQ(r.size() % 4, 0u);
    auto rb = ParseBye(r);
    SM_ASSERT_TRUE(rb.has_value());
    SM_EXPECT_EQ(rb->reason.size(), n);
  }
  // A BYE with no sources is valid (count 0).
  SM_EXPECT_TRUE(ParseBye(SerializeBye(Bye{})).has_value());
  // Padded packet: P bit, last byte counts the pad bytes.
  Bytes padded = {0xA1, 203, 0x00, 0x02, 0, 0, 0, 1, 0, 0, 0, 4};
  auto pb = ParseBye(padded);
  SM_ASSERT_TRUE(pb.has_value());
  SM_EXPECT_EQ(pb->ssrcs.size(), 1u);
  padded.back() = 100;
  SM_EXPECT_FALSE(ParseBye(padded).has_value());
}

SM_TEST(RtcpSdesBye, ByeRejectsUnencodableAndMalformed) {
  SM_EXPECT_TRUE(SerializeBye(Bye{std::vector<uint32_t>(32, 1), ""}).empty());
  SM_EXPECT_TRUE(SerializeBye(Bye{{1}, std::string(256, 'x')}).empty());
  Bytes good = SerializeBye(Bye{{1}, "ab"});
  Bytes count = good;
  count[0] = 0x83;  // Three sources, one present.
  SM_EXPECT_FALSE(ParseBye(count).has_value());
  Bytes reason_overrun = good;
  reason_overrun[8] = 200;
  SM_EXPECT_FALSE(ParseBye(reason_overrun).has_value());
  Bytes wrong_type = good;
  wrong_type[1] = 202;
  SM_EXPECT_FALSE(ParseBye(wrong_type).has_value());
}

SM_TEST(RtcpSdesBye, FuzzedBytesNeverCrash) {
  std::vector<Bytes> seeds = {SerializeSdes(std::vector<SdesChunk>{{1, {{kSdesCname, "cname"}}}, {2, {{4, "x"}}}}),
                              SerializeBye(Bye{{1, 2}, "going away"})};
  for (const Bytes& s : seeds) {
    for (size_t len = 0; len <= s.size(); ++len) {
      Bytes p(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(len));
      (void)ParseSdes(p);
      (void)ParseBye(p);
    }
    for (size_t i = 0; i < s.size(); ++i) {
      for (int v : {0, 1, 0x20, 0x80, 0xFF}) {
        Bytes p = s;
        p[i] = static_cast<uint8_t>(v);
        (void)ParseSdes(p);
        (void)ParseBye(p);
      }
    }
  }
}
