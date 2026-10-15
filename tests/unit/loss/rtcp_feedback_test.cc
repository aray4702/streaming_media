#include <vector>

#include "sm/loss/rtcp_feedback.h"
#include "sm_test.h"

using namespace sm::loss;
using Bytes = std::vector<uint8_t>;

SM_TEST(RtcpFeedback, NackByteLayout) {
  // RFC 4585 6.2.1: PID=100, BLP bits 0 and 2 -> 101 and 103; then 300 alone.
  Nack n{0x11223344, 0x55667788, {100, 101, 103, 300}};
  Bytes expect = {0x81, 205, 0x00, 0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                  0x00, 0x64, 0x00, 0x05, 0x01, 0x2C, 0x00, 0x00};
  SM_EXPECT_TRUE(SerializeNack(n) == expect);
  auto back = ParseNack(expect);
  SM_ASSERT_TRUE(back.has_value());
  SM_EXPECT_EQ(back->sender_ssrc, 0x11223344u);
  SM_EXPECT_EQ(back->media_ssrc, 0x55667788u);
  SM_EXPECT_TRUE(back->seqs == n.seqs);
}

SM_TEST(RtcpFeedback, NackPacksRunsAndWraps) {
  // 17 numbers after the first do not fit one BLP: PID+16 is the last bit.
  Nack n;
  n.sender_ssrc = 1;
  n.media_ssrc = 2;
  for (int i = 0; i <= 17; ++i) n.seqs.push_back(static_cast<uint16_t>(65530 + i));  // Wraps at 65536.
  Bytes b = SerializeNack(n);
  SM_EXPECT_EQ(b.size(), 12u + 8u);  // Two items.
  SM_EXPECT_EQ(b[3], 4);
  auto back = ParseNack(b);
  SM_ASSERT_TRUE(back.has_value());
  SM_EXPECT_TRUE(back->seqs == n.seqs);
  // First item: PID 65530, BLP all ones.
  SM_EXPECT_EQ(b[14], 0xFF);
  SM_EXPECT_EQ(b[15], 0xFF);
}

SM_TEST(RtcpFeedback, NackDuplicatesAreDropped) {
  Nack n{1, 2, {10, 10, 11, 11}};
  auto back = ParseNack(SerializeNack(n));
  SM_ASSERT_TRUE(back.has_value());
  SM_EXPECT_EQ(back->seqs.size(), 2u);
  SM_EXPECT_EQ(back->seqs[0], 10);
  SM_EXPECT_EQ(back->seqs[1], 11);
}

SM_TEST(RtcpFeedback, NackRejectsMalformed) {
  SM_EXPECT_TRUE(SerializeNack(Nack{1, 2, {}}).empty());
  Bytes good = SerializeNack(Nack{1, 2, {5}});
  SM_ASSERT_TRUE(ParseNack(good).has_value());
  Bytes no_items(good.begin(), good.begin() + 12);
  no_items[3] = 2;
  SM_EXPECT_FALSE(ParseNack(no_items).has_value());
  Bytes truncated(good.begin(), good.end() - 2);
  SM_EXPECT_FALSE(ParseNack(truncated).has_value());
  Bytes bad_len = good;
  bad_len[3] = 5;
  SM_EXPECT_FALSE(ParseNack(bad_len).has_value());
  Bytes bad_version = good;
  bad_version[0] = 0x41;
  SM_EXPECT_FALSE(ParseNack(bad_version).has_value());
  Bytes bad_pt = good;
  bad_pt[1] = 206;
  SM_EXPECT_FALSE(ParseNack(bad_pt).has_value());
  Bytes bad_fmt = good;
  bad_fmt[0] = 0x82;
  SM_EXPECT_FALSE(ParseNack(bad_fmt).has_value());
  SM_EXPECT_FALSE(ParseNack(Bytes{}).has_value());
}

SM_TEST(RtcpFeedback, PliByteLayout) {
  Bytes expect = {0x81, 206, 0x00, 0x02, 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
  SM_EXPECT_TRUE(SerializePli(Pli{0xDEADBEEF, 0x01020304}) == expect);
  auto back = ParsePli(expect);
  SM_ASSERT_TRUE(back.has_value());
  SM_EXPECT_EQ(back->sender_ssrc, 0xDEADBEEFu);
  SM_EXPECT_EQ(back->media_ssrc, 0x01020304u);
  // A PLI with a payload is not a PLI.
  Bytes longer = expect;
  longer.insert(longer.end(), {0, 0, 0, 0});
  longer[3] = 3;
  SM_EXPECT_FALSE(ParsePli(longer).has_value());
  Bytes shorter(expect.begin(), expect.begin() + 8);
  shorter[3] = 1;
  SM_EXPECT_FALSE(ParsePli(shorter).has_value());
}

SM_TEST(RtcpFeedback, FirByteLayout) {
  // RFC 5104 4.3.1: media SSRC in the header is 0; the FCI carries SSRC + seq nr.
  Fir f{0x0A0B0C0D, {{0x01020304, 7}, {0x05060708, 255}}};
  Bytes expect = {0x84, 206, 0x00, 0x06, 0x0A, 0x0B, 0x0C, 0x0D, 0, 0, 0, 0,
                  0x01, 0x02, 0x03, 0x04, 7, 0, 0, 0, 0x05, 0x06, 0x07, 0x08, 255, 0, 0, 0};
  SM_EXPECT_TRUE(SerializeFir(f) == expect);
  auto back = ParseFir(expect);
  SM_ASSERT_TRUE(back.has_value());
  SM_EXPECT_EQ(back->sender_ssrc, 0x0A0B0C0Du);
  SM_ASSERT_EQ(back->entries.size(), 2u);
  SM_EXPECT_EQ(back->entries[1].ssrc, 0x05060708u);
  SM_EXPECT_EQ(back->entries[1].seq_nr, 255);
  SM_EXPECT_TRUE(SerializeFir(Fir{1, {}}).empty());
  Bytes partial(expect.begin(), expect.end() - 4);
  partial[3] = 5;
  SM_EXPECT_FALSE(ParseFir(partial).has_value());
  Bytes none(expect.begin(), expect.begin() + 12);
  none[3] = 2;
  SM_EXPECT_FALSE(ParseFir(none).has_value());
}

SM_TEST(RtcpFeedback, LossNotificationByteLayout) {
  // libwebrtc loss_notification.cc: "LNTF", last decoded, then (delta << 1 | D).
  LossNotification l{0x01010101, 0x02020202, 0x1000, 0x1005, true};
  Bytes expect = {0x8F, 206, 0x00, 0x04, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02,
                  'L', 'N', 'T', 'F', 0x10, 0x00, 0x00, 0x0B};
  SM_EXPECT_TRUE(SerializeLossNotification(l) == expect);
  auto back = ParseLossNotification(expect);
  SM_ASSERT_TRUE(back.has_value());
  SM_EXPECT_EQ(back->last_decoded, 0x1000);
  SM_EXPECT_EQ(back->last_received, 0x1005);
  SM_EXPECT_TRUE(back->decodability_flag);
  // Wrap-around and max delta.
  LossNotification w{1, 2, 65530, static_cast<uint16_t>(65530 + 32767), false};
  auto wb = ParseLossNotification(SerializeLossNotification(w));
  SM_ASSERT_TRUE(wb.has_value());
  SM_EXPECT_EQ(wb->last_received, w.last_received);
  SM_EXPECT_FALSE(wb->decodability_flag);
  // last_received more than 32767 ahead cannot be encoded.
  SM_EXPECT_TRUE(SerializeLossNotification(LossNotification{1, 2, 0, 40000, false}).empty());
}

SM_TEST(RtcpFeedback, LossNotificationRejectsMalformed) {
  Bytes good = SerializeLossNotification(LossNotification{1, 2, 3, 4, false});
  SM_ASSERT_TRUE(ParseLossNotification(good).has_value());
  Bytes bad_id = good;
  bad_id[12] = 'X';
  SM_EXPECT_FALSE(ParseLossNotification(bad_id).has_value());
  Bytes remb_id = good;
  remb_id[12] = 'R';
  remb_id[13] = 'E';
  remb_id[14] = 'M';
  remb_id[15] = 'B';
  SM_EXPECT_FALSE(ParseLossNotification(remb_id).has_value());
  Bytes truncated(good.begin(), good.end() - 4);
  truncated[3] = 4;
  SM_EXPECT_FALSE(ParseLossNotification(truncated).has_value());
  SM_EXPECT_FALSE(ParseLossNotification(Bytes(good.begin(), good.begin() + 6)).has_value());
}

SM_TEST(RtcpFeedback, PaddingIsStripped) {
  // A padded PLI-style packet: P bit set, last byte counts the pad bytes.
  Bytes nack = SerializeNack(Nack{1, 2, {9}});
  nack[0] |= 0x20;
  nack.insert(nack.end(), {0, 0, 0, 4});
  nack[3] = 4;
  auto back = ParseNack(nack);
  SM_ASSERT_TRUE(back.has_value());
  SM_EXPECT_EQ(back->seqs.size(), 1u);
  // Pad count larger than the payload is rejected.
  nack.back() = 200;
  SM_EXPECT_FALSE(ParseNack(nack).has_value());
}

SM_TEST(RtcpFeedback, CompoundBuildAndSplit) {
  Bytes rr_bytes = {0x80, 201, 0x00, 0x01, 0x00, 0x00, 0x00, 77};  // RR with no report blocks.
  std::vector<Bytes> pkts = {rr_bytes, SerializeNack(Nack{77, 5, {1, 2}}), SerializePli(Pli{77, 5}),
                             SerializeFir(Fir{77, {{5, 1}}}),
                             SerializeLossNotification(LossNotification{77, 5, 1, 3, true})};
  Bytes compound = BuildCompoundRtcp(pkts);
  size_t total = 0;
  for (const auto& p : pkts) total += p.size();
  SM_EXPECT_EQ(compound.size(), total);
  auto views = SplitCompoundRtcp(compound);
  SM_ASSERT_TRUE(views.has_value());
  SM_ASSERT_EQ(views->size(), 5u);
  SM_EXPECT_TRUE((*views)[0].type == RtcpType::kReceiverReport);
  SM_EXPECT_TRUE((*views)[1].type == RtcpType::kNack);
  SM_EXPECT_TRUE((*views)[2].type == RtcpType::kPli);
  SM_EXPECT_TRUE((*views)[3].type == RtcpType::kFir);
  SM_EXPECT_TRUE((*views)[4].type == RtcpType::kLossNotification);
  SM_EXPECT_TRUE(ParseNack((*views)[1].data).has_value());
  SM_EXPECT_TRUE(ParsePli((*views)[2].data).has_value());
  SM_EXPECT_TRUE(ParseFir((*views)[3].data).has_value());
  SM_EXPECT_TRUE(ParseLossNotification((*views)[4].data).has_value());
}

SM_TEST(RtcpFeedback, CompoundRequiresReportFirst) {
  std::vector<Bytes> pkts = {SerializePli(Pli{1, 2})};
  SM_EXPECT_TRUE(BuildCompoundRtcp(pkts).empty());
  SM_EXPECT_TRUE(BuildCompoundRtcp({}).empty());
  Bytes sr(28, 0);
  sr[0] = 0x80;
  sr[1] = 200;
  sr[3] = 6;
  std::vector<Bytes> ok = {sr, SerializePli(Pli{1, 2})};
  SM_EXPECT_FALSE(BuildCompoundRtcp(ok).empty());
}

SM_TEST(RtcpFeedback, SplitRejectsMalformedCompound) {
  Bytes pli = SerializePli(Pli{1, 2});
  SM_EXPECT_FALSE(SplitCompoundRtcp(Bytes{}).has_value());
  Bytes trailing = pli;
  trailing.push_back(0);
  SM_EXPECT_FALSE(SplitCompoundRtcp(trailing).has_value());
  Bytes too_long = pli;
  too_long[3] = 9;
  SM_EXPECT_FALSE(SplitCompoundRtcp(too_long).has_value());
  Bytes bad_version = pli;
  bad_version[0] = 0x01;
  SM_EXPECT_FALSE(SplitCompoundRtcp(bad_version).has_value());
  // Padding on a packet that is not the last one.
  Bytes padded_first = pli;
  padded_first[0] |= 0x20;
  padded_first.insert(padded_first.end(), pli.begin(), pli.end());
  SM_EXPECT_FALSE(SplitCompoundRtcp(padded_first).has_value());
}

SM_TEST(RtcpFeedback, FuzzedBytesNeverCrash) {
  // Every prefix and every single-byte mutation of valid packets is handled.
  std::vector<Bytes> seeds = {SerializeNack(Nack{1, 2, {5, 6, 90}}), SerializePli(Pli{1, 2}),
                              SerializeFir(Fir{1, {{2, 3}}}),
                              SerializeLossNotification(LossNotification{1, 2, 3, 9, true})};
  for (const Bytes& s : seeds) {
    for (size_t len = 0; len <= s.size(); ++len) {
      Bytes p(s.begin(), s.begin() + len);
      (void)ParseNack(p);
      (void)ParsePli(p);
      (void)ParseFir(p);
      (void)ParseLossNotification(p);
      (void)SplitCompoundRtcp(p);
    }
    for (size_t i = 0; i < s.size(); ++i) {
      for (int v : {0, 1, 0x20, 0x80, 0xFF}) {
        Bytes p = s;
        p[i] = static_cast<uint8_t>(v);
        (void)ParseNack(p);
        (void)ParsePli(p);
        (void)ParseFir(p);
        (void)ParseLossNotification(p);
        (void)SplitCompoundRtcp(p);
      }
    }
  }
}
