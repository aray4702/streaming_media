#include <algorithm>
#include <bit>

#include "sm/core/random.h"
#include "sm/loss/ulpfec.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::loss::ulpfec;

namespace {

using Buffer = ForwardErrorCorrection::Buffer;

constexpr uint32_t kSsrc = 0x11223344;
constexpr int kRedPt = 127;
constexpr int kUlpfecPt = 125;

RtpPacket MakeMedia(uint16_t seq, uint32_t ts, size_t payload, Random& rng, bool marker = false) {
  RtpPacket p;
  p.payload_type = 96;
  p.seq = seq;
  p.timestamp = ts;
  p.ssrc = kSsrc;
  p.marker = marker;
  p.payload.resize(payload);
  for (auto& b : p.payload) b = static_cast<uint8_t>(rng.Next());
  return p;
}

// Bit `col` of row `row` in a mask with `bytes` bytes per row.
bool MaskBit(std::span<const uint8_t> mask, size_t bytes, int row, int col) {
  return (mask[static_cast<size_t>(row) * bytes + static_cast<size_t>(col / 8)] & (0x80 >> (col % 8))) != 0;
}

// Feeds the media packets (except `lost`) and the FEC payloads (except
// `lost_fec`) to a decoder; returns the recovered packets by sequence number.
std::vector<Buffer> Decode(const std::vector<Buffer>& media, const std::vector<Buffer>& fec,
                           const std::vector<bool>& lost, const std::vector<bool>& lost_fec,
                           uint16_t first_fec_seq) {
  ForwardErrorCorrection dec(kSsrc);
  ForwardErrorCorrection::RecoveredPacketList recovered;
  for (size_t i = 0; i < media.size(); ++i) {
    if (lost[i]) continue;
    ForwardErrorCorrection::ReceivedPacket r;
    r.ssrc = kSsrc;
    r.seq_num = ForwardErrorCorrection::ParseSequenceNumber(media[i].data());
    r.pkt = std::make_shared<Buffer>(media[i]);
    dec.DecodeFec(r, &recovered);
  }
  for (size_t j = 0; j < fec.size(); ++j) {
    if (lost_fec[j]) continue;
    ForwardErrorCorrection::ReceivedPacket r;
    r.ssrc = kSsrc;
    r.seq_num = static_cast<uint16_t>(first_fec_seq + j);
    r.is_fec = true;
    r.pkt = std::make_shared<Buffer>(fec[j]);
    dec.DecodeFec(r, &recovered);
  }
  std::vector<Buffer> out;
  for (const auto& p : recovered) {
    if (p->was_recovered) out.push_back(*p->pkt);
  }
  return out;
}

}  // namespace

// The tables read from libwebrtc M157: 12 entries, each with k masks of k
// rows; every row protects something and every media packet is covered.
SM_TEST(Ulpfec, MaskTablesMatchLibwebrtcLayout) {
  for (auto type : {FecMaskType::kRandom, FecMaskType::kBursty}) {
    for (int k = 1; k <= 48; ++k) {
      internal::PacketMaskTable table(type, k);
      const size_t bytes = internal::PacketMaskSize(static_cast<size_t>(k));
      SM_EXPECT_EQ(bytes, k > 16 ? 6u : 2u);
      for (int m = 1; m <= k; ++m) {
        auto mask = table.LookUp(k, m);
        SM_ASSERT_EQ(mask.size(), static_cast<size_t>(m) * bytes);
        std::vector<bool> covered(static_cast<size_t>(k), false);
        for (int row = 0; row < m; ++row) {
          bool any = false;
          for (int col = 0; col < 8 * static_cast<int>(bytes); ++col) {
            if (!MaskBit(mask, bytes, row, col)) continue;
            SM_EXPECT_LT(col, k);  // No bits past the last media packet.
            any = true;
            covered[static_cast<size_t>(std::min(col, k - 1))] = true;
          }
          SM_EXPECT_TRUE(any);
        }
        SM_EXPECT_EQ(std::count(covered.begin(), covered.end(), true), k);
        if (k > 12) {
          // Generated interleaved: media packet X in row X % m.
          for (int col = 0; col < k; ++col) {
            for (int row = 0; row < m; ++row) SM_EXPECT_EQ(MaskBit(mask, bytes, row, col), col % m == row);
          }
        }
      }
    }
  }
  // Spot values of kPacketMaskRandomTbl (kMaskRandom1_1, 2_2, 3_2, 12_1).
  internal::PacketMaskTable random(FecMaskType::kRandom, 12);
  auto m11 = random.LookUp(1, 1);
  SM_EXPECT_EQ(m11[0], 0x80);
  auto m22 = random.LookUp(2, 2);
  SM_EXPECT_EQ(m22[0], 0xc0);
  SM_EXPECT_EQ(m22[2], 0x80);
  auto m32 = random.LookUp(3, 2);
  SM_EXPECT_EQ(m32[0], 0xc0);
  SM_EXPECT_EQ(m32[2], 0xa0);
  auto m121 = random.LookUp(12, 1);
  SM_EXPECT_EQ(m121[0], 0xff);
  SM_EXPECT_EQ(m121[1], 0xf0);
}

// fec_private_tables_bursty.h: for every (k, m) mask, all runs of up to m
// consecutive losses (media then FEC, in sequence order) are recoverable.
SM_TEST(Ulpfec, BurstyMasksRecoverConsecutiveLosses) {
  Random rng(7);
  for (int k = 1; k <= 12; ++k) {
    std::vector<Buffer> media;
    for (int i = 0; i < k; ++i) media.push_back(MakeMedia(static_cast<uint16_t>(1000 + i), 90, 40, rng).Serialize());
    for (int m = 1; m <= k; ++m) {
      ForwardErrorCorrection enc(kSsrc);
      std::vector<Buffer> fec;
      const uint8_t factor = static_cast<uint8_t>(std::min(255, (m * 256 + k / 2) / k));
      SM_ASSERT_EQ(enc.EncodeFec(media, factor, 0, false, FecMaskType::kBursty, &fec), 0);
      if (static_cast<int>(fec.size()) != m) continue;  // Q8 rounding gave another m.
      const int n = k + m;
      for (int len = 1; len <= m; ++len) {
        for (int start = 0; start + len <= n; ++start) {
          std::vector<bool> lost(static_cast<size_t>(k), false), lost_fec(static_cast<size_t>(m), false);
          int lost_media = 0;
          for (int i = start; i < start + len; ++i) {
            if (i < k) {
              lost[static_cast<size_t>(i)] = true;
              ++lost_media;
            } else {
              lost_fec[static_cast<size_t>(i - k)] = true;
            }
          }
          auto rec = Decode(media, fec, lost, lost_fec, static_cast<uint16_t>(1000 + k));
          SM_EXPECT_EQ(static_cast<int>(rec.size()), lost_media);
        }
      }
    }
  }
}

SM_TEST(Ulpfec, NumFecPacketsIsQ8RoundedWithAtLeastOne) {
  SM_EXPECT_EQ(ForwardErrorCorrection::NumFecPackets(10, 0), 0);
  SM_EXPECT_EQ(ForwardErrorCorrection::NumFecPackets(10, 1), 1);
  SM_EXPECT_EQ(ForwardErrorCorrection::NumFecPackets(10, 64), 3);   // 2.5 rounds up.
  SM_EXPECT_EQ(ForwardErrorCorrection::NumFecPackets(10, 255), 10);
  SM_EXPECT_EQ(ForwardErrorCorrection::NumFecPackets(4, 51), 1);    // 0.8 rounds up.
  SM_EXPECT_EQ(ForwardErrorCorrection::MaxPacketOverhead(), 18u);
}

// RFC 5109 section 7.3 header and section 7.4 level-0 header, as libwebrtc
// writes them for ULPFEC.
SM_TEST(Ulpfec, HeaderByteLayoutFollowsRfc5109) {
  Random rng(1);
  RtpPacket a = MakeMedia(0x1234, 0x01020304, 10, rng, false);
  RtpPacket b = MakeMedia(0x1235, 0x0a0b0c0d, 6, rng, true);
  b.payload_type = 100;
  b.csrcs = {0xdeadbeef};
  const Buffer ba = a.Serialize(), bb = b.Serialize();
  ForwardErrorCorrection enc(kSsrc);
  std::vector<Buffer> fec;
  SM_ASSERT_EQ(enc.EncodeFec({ba, bb}, 255, 0, false, FecMaskType::kRandom, &fec), 0);
  SM_ASSERT_EQ(fec.size(), 2u);  // kMaskRandom2_2: {a, b}, {a}.
  const Buffer& f = fec[0];
  const size_t len_a = ba.size() - 12, len_b = bb.size() - 12;
  SM_ASSERT_EQ(f.size(), 14 + std::max(len_a, len_b));
  SM_EXPECT_EQ(f[0] & 0x80, 0);                            // E = 0.
  SM_EXPECT_EQ(f[0] & 0x40, 0);                            // L = 0: 16-bit mask.
  SM_EXPECT_EQ(f[0] & 0x3f, (ba[0] ^ bb[0]) & 0x3f);       // P, X, CC recovery.
  SM_EXPECT_EQ(f[1], ba[1] ^ bb[1]);                       // M, PT recovery.
  SM_EXPECT_EQ((f[2] << 8) | f[3], 0x1234);                // SN base.
  for (int i = 4; i < 8; ++i) SM_EXPECT_EQ(f[i], ba[i] ^ bb[i]);  // TS recovery.
  SM_EXPECT_EQ(static_cast<size_t>((f[8] << 8) | f[9]), len_a ^ len_b);  // Length recovery.
  SM_EXPECT_EQ(static_cast<size_t>((f[10] << 8) | f[11]), f.size() - 14);  // Protection length.
  SM_EXPECT_EQ(f[12], 0xc0);                               // Mask: both packets.
  SM_EXPECT_EQ(f[13], 0x00);
  for (size_t i = 0; i < f.size() - 14; ++i) {
    const uint8_t xa = i < len_a ? ba[12 + i] : 0;
    const uint8_t xb = i < len_b ? bb[12 + i] : 0;
    SM_EXPECT_EQ(f[14 + i], xa ^ xb);
  }
  SM_EXPECT_EQ(fec[1][12], 0x80);  // Second row: packet a only.
  SM_EXPECT_EQ(fec[1].size(), 14 + len_a);
}

SM_TEST(Ulpfec, LongMaskSetsLBit) {
  Random rng(2);
  std::vector<Buffer> media;
  for (int i = 0; i < 20; ++i) media.push_back(MakeMedia(static_cast<uint16_t>(i), 1, 30, rng).Serialize());
  ForwardErrorCorrection enc(kSsrc);
  std::vector<Buffer> fec;
  SM_ASSERT_EQ(enc.EncodeFec(media, 26, 0, false, FecMaskType::kRandom, &fec), 0);  // 20 * 26 / 256 = 2.
  SM_ASSERT_EQ(fec.size(), 2u);
  SM_EXPECT_EQ(fec[0][0] & 0x40, 0x40);
  SM_EXPECT_EQ(fec[0].size(), 18u + 30u);
  SM_EXPECT_EQ(static_cast<size_t>((fec[0][10] << 8) | fec[0][11]), 30u);
  // Interleaved: row 0 protects the even packets 0..18.
  SM_EXPECT_EQ(fec[0][12], 0xaa);
  SM_EXPECT_EQ(fec[0][14], 0xa0);
  SM_EXPECT_EQ(fec[0][15], 0x00);
}

// Every field of the RTP header that ULPFEC carries comes back intact:
// padding, extension, CSRC count, marker, PT, timestamp, length.
SM_TEST(Ulpfec, RecoversEveryRtpHeaderField) {
  Random rng(3);
  std::vector<RtpPacket> pkts;
  for (int i = 0; i < 6; ++i) pkts.push_back(MakeMedia(static_cast<uint16_t>(65533 + i), 3000u * i + 7, 20 + 13 * i, rng));
  pkts[1].padding_size = 7;
  pkts[2].extension_profile = 0xBEDE;
  pkts[2].extension = {0x10, 0xab, 0x00, 0x00};
  pkts[3].csrcs = {1, 2, 3};
  pkts[4].marker = true;
  pkts[4].payload_type = 111;
  pkts[5].payload.clear();  // Header only.
  std::vector<Buffer> media;
  for (auto& p : pkts) media.push_back(p.Serialize());
  for (size_t lose = 0; lose < media.size(); ++lose) {
    ForwardErrorCorrection enc(kSsrc);
    std::vector<Buffer> fec;
    SM_ASSERT_EQ(enc.EncodeFec(media, 255, 0, false, FecMaskType::kRandom, &fec), 0);
    std::vector<bool> lost(media.size(), false);
    lost[lose] = true;
    auto rec = Decode(media, fec, lost, std::vector<bool>(fec.size(), false), 10);
    SM_ASSERT_EQ(rec.size(), 1u);
    SM_EXPECT_TRUE(rec[0] == media[lose]);
    auto parsed = RtpPacket::Parse(rec[0]);
    SM_ASSERT_TRUE(parsed.has_value());
    SM_EXPECT_EQ(parsed->seq, pkts[lose].seq);  // Across the 16-bit wrap.
    SM_EXPECT_EQ(parsed->padding_size, pkts[lose].padding_size);
    SM_EXPECT_EQ(parsed->marker, pkts[lose].marker);
  }
}

// InsertZerosInPacketMasks: packets with sequence gaps (other streams or
// packets not protected) are covered by zero columns.
SM_TEST(Ulpfec, SequenceGapsAreZeroColumns) {
  Random rng(4);
  const uint16_t seqs[] = {100, 101, 104, 105, 109, 110, 111, 120};
  std::vector<Buffer> media;
  for (uint16_t s : seqs) media.push_back(MakeMedia(s, 5, 50, rng).Serialize());
  ForwardErrorCorrection enc(kSsrc);
  std::vector<Buffer> fec;
  SM_ASSERT_EQ(enc.EncodeFec(media, 128, 0, false, FecMaskType::kRandom, &fec), 0);
  SM_ASSERT_EQ(fec.size(), 4u);
  // The mask has 21 columns (100..120): 6-byte mask, bits only on sent packets.
  for (const auto& f : fec) {
    SM_EXPECT_EQ(f[0] & 0x40, 0x40);
    for (int col = 0; col < 48; ++col) {
      if (!MaskBit(std::span<const uint8_t>(f).subspan(12, 6), 6, 0, col)) continue;
      SM_EXPECT_TRUE(std::find(std::begin(seqs), std::end(seqs), 100 + col) != std::end(seqs));
    }
  }
  for (size_t lose = 0; lose < media.size(); ++lose) {
    std::vector<bool> lost(media.size(), false);
    lost[lose] = true;
    auto rec = Decode(media, fec, lost, std::vector<bool>(fec.size(), false), 200);
    SM_ASSERT_EQ(rec.size(), 1u);
    SM_EXPECT_TRUE(rec[0] == media[lose]);
  }
  // More than 48 sequence numbers can't be one block.
  std::vector<Buffer> wide = {MakeMedia(0, 0, 10, rng).Serialize(), MakeMedia(60, 0, 10, rng).Serialize()};
  SM_EXPECT_EQ(enc.EncodeFec(wide, 255, 0, false, FecMaskType::kRandom, &fec), -1);
  SM_EXPECT_TRUE(fec.empty());
}

SM_TEST(Ulpfec, RejectsBadInput) {
  Random rng(5);
  ForwardErrorCorrection enc(kSsrc);
  std::vector<Buffer> fec;
  SM_EXPECT_EQ(enc.EncodeFec({}, 255, 0, false, FecMaskType::kRandom, &fec), -1);
  SM_EXPECT_EQ(enc.EncodeFec({Buffer(11, 0)}, 255, 0, false, FecMaskType::kRandom, &fec), -1);
  std::vector<Buffer> too_many;
  for (int i = 0; i < 49; ++i) too_many.push_back(MakeMedia(static_cast<uint16_t>(i), 0, 4, rng).Serialize());
  SM_EXPECT_EQ(enc.EncodeFec(too_many, 255, 0, false, FecMaskType::kRandom, &fec), -1);
  SM_EXPECT_EQ(enc.EncodeFec({too_many[0]}, 0, 0, false, FecMaskType::kRandom, &fec), 0);
  SM_EXPECT_TRUE(fec.empty());

  // Decoder: truncated header, oversized protection length, all-zero mask,
  // a foreign SSRC, and random bytes are dropped without recovering anything.
  std::vector<Buffer> media = {MakeMedia(1, 0, 30, rng).Serialize(), MakeMedia(2, 0, 30, rng).Serialize()};
  SM_ASSERT_EQ(enc.EncodeFec(media, 128, 0, false, FecMaskType::kRandom, &fec), 0);
  SM_ASSERT_EQ(fec.size(), 1u);
  auto feed_fec = [&](const Buffer& payload, uint32_t ssrc) {
    ForwardErrorCorrection dec(kSsrc);
    ForwardErrorCorrection::RecoveredPacketList recovered;
    ForwardErrorCorrection::ReceivedPacket m;
    m.ssrc = kSsrc;
    m.seq_num = 1;
    m.pkt = std::make_shared<Buffer>(media[0]);
    dec.DecodeFec(m, &recovered);
    ForwardErrorCorrection::ReceivedPacket f;
    f.ssrc = ssrc;
    f.seq_num = 3;
    f.is_fec = true;
    f.pkt = std::make_shared<Buffer>(payload);
    return dec.DecodeFec(f, &recovered);
  };
  SM_EXPECT_EQ(feed_fec(fec[0], kSsrc), 1u);  // The intact packet recovers.
  SM_EXPECT_EQ(feed_fec(Buffer(fec[0].begin(), fec[0].begin() + 11), kSsrc), 0u);
  Buffer long_prot = fec[0];
  long_prot[10] = 0xff;
  SM_EXPECT_EQ(feed_fec(long_prot, kSsrc), 0u);
  Buffer zero_mask = fec[0];
  zero_mask[12] = zero_mask[13] = 0;
  SM_EXPECT_EQ(feed_fec(zero_mask, kSsrc), 0u);
  SM_EXPECT_EQ(feed_fec(fec[0], kSsrc + 1), 0u);
  for (int i = 0; i < 2000; ++i) {
    Buffer junk(static_cast<size_t>(rng.UniformInt(0, 80)));
    for (auto& b : junk) b = static_cast<uint8_t>(rng.Next());
    feed_fec(junk, kSsrc);  // Must not crash (ASan).
  }
}

// UlpfecGenerator: when a block is closed, and how the RED packets look.
SM_TEST(Ulpfec, GeneratorGroupsFramesAsLibwebrtc) {
  Random rng(6);
  // Rate 50 (Q8) on one-packet frames: 1 and 2 packets give overhead 256 and
  // 128, above 50 + kMaxExcessOverhead; 3 packets give 85, so the block
  // closes on the third frame.
  {
    UlpfecGenerator gen(kRedPt, kUlpfecPt);
    gen.SetProtectionParameters({50, 30, FecMaskType::kRandom}, {50, 30, FecMaskType::kRandom});
    for (int f = 0; f < 3; ++f) {
      gen.AddPacketAndGenerateFec(MakeMedia(static_cast<uint16_t>(f), 90u * f, 100, rng, true), false);
      SM_EXPECT_EQ(gen.GetFecPackets().size(), f == 2 ? 1u : 0u);
    }
  }
  // Rate above kHighProtectionThreshold (80): at least kMinMediaPackets (4).
  {
    UlpfecGenerator gen(kRedPt, kUlpfecPt);
    gen.SetProtectionParameters({128, 30, FecMaskType::kRandom}, {128, 30, FecMaskType::kRandom});
    for (int f = 0; f < 4; ++f) {
      gen.AddPacketAndGenerateFec(MakeMedia(static_cast<uint16_t>(f), 90u * f, 100, rng, true), false);
      SM_EXPECT_EQ(gen.GetFecPackets().size(), f == 3 ? 2u : 0u);
    }
  }
  // max_fec_frames closes the block regardless; two packets per frame need
  // kMinMediaPackets + 1 otherwise.
  {
    UlpfecGenerator gen(kRedPt, kUlpfecPt);
    gen.SetProtectionParameters({255, 1, FecMaskType::kRandom}, {255, 1, FecMaskType::kRandom});
    gen.AddPacketAndGenerateFec(MakeMedia(10, 1, 50, rng, false), false);
    SM_EXPECT_EQ(gen.GetFecPackets().size(), 0u);
    gen.AddPacketAndGenerateFec(MakeMedia(11, 1, 50, rng, true), false);
    auto red = gen.GetFecPackets();
    SM_ASSERT_EQ(red.size(), 2u);
    for (const auto& p : red) {
      SM_EXPECT_EQ(p.payload_type, kRedPt);
      SM_EXPECT_FALSE(p.marker);
      SM_EXPECT_EQ(p.ssrc, kSsrc);
      SM_EXPECT_EQ(p.timestamp, 1u);
      SM_EXPECT_EQ(p.seq, 11);  // The last media packet's header; the caller numbers it.
      SM_EXPECT_EQ(p.payload[0], kUlpfecPt);  // RED header, F = 0.
    }
    SM_EXPECT_EQ(gen.stats().fec_packets, 2u);
  }
  // Key frame parameters apply to a block with a key frame packet.
  {
    UlpfecGenerator gen(kRedPt, kUlpfecPt);
    gen.SetProtectionParameters({0, 1, FecMaskType::kRandom}, {255, 1, FecMaskType::kRandom});
    gen.AddPacketAndGenerateFec(MakeMedia(1, 1, 50, rng, true), false);
    SM_EXPECT_EQ(gen.GetFecPackets().size(), 0u);
    gen.AddPacketAndGenerateFec(MakeMedia(2, 2, 50, rng, true), true);
    SM_EXPECT_EQ(gen.GetFecPackets().size(), 1u);
  }
}

namespace {
RtpPacket WrapInRed(const RtpPacket& media) {
  RtpPacket red = media;
  red.payload_type = kRedPt;
  red.payload.insert(red.payload.begin(), media.payload_type);
  return red;
}
}  // namespace

// Sender to receiver over RED: lost media packets come back byte for byte.
SM_TEST(Ulpfec, GeneratorToReceiverRecoversLosses) {
  Random rng(8);
  UlpfecGenerator gen(kRedPt, kUlpfecPt);
  gen.SetProtectionParameters({80, 3, FecMaskType::kBursty}, {120, 3, FecMaskType::kBursty});
  UlpfecReceiver rx(kSsrc, kUlpfecPt);
  uint16_t seq = 65000;
  int lost = 0, delivered_lost = 0;
  std::vector<RtpPacket> lost_packets;
  for (int frame = 0; frame < 300; ++frame) {
    const int n = 1 + static_cast<int>(rng.UniformInt(0, 5));
    for (int i = 0; i < n; ++i) {
      RtpPacket media = MakeMedia(seq++, 3000u * static_cast<uint32_t>(frame), static_cast<size_t>(rng.UniformInt(10, 1100)),
                                  rng, i == n - 1);
      gen.AddPacketAndGenerateFec(media, frame % 50 == 0);
      std::vector<RtpPacket> wire = {WrapInRed(media)};
      for (auto& f : gen.GetFecPackets()) {
        f.seq = seq++;
        wire.push_back(std::move(f));
      }
      for (auto& p : wire) {
        if (rng.UniformInt(0, 99) < 5) {
          if (p.payload[0] != kUlpfecPt) {
            ++lost;
            lost_packets.push_back(media);
          }
          continue;
        }
        SM_EXPECT_TRUE(rx.AddReceivedRedPacket(p));
        for (auto& out : rx.ProcessReceivedFec()) {
          auto it = std::find_if(lost_packets.begin(), lost_packets.end(),
                                 [&](const RtpPacket& l) { return l.seq == out.seq; });
          if (it != lost_packets.end()) {
            SM_EXPECT_TRUE(out.Serialize() == it->Serialize());
            ++delivered_lost;
            lost_packets.erase(it);
          }
        }
      }
    }
  }
  SM_EXPECT_GT(lost, 20);
  SM_EXPECT_GT(delivered_lost, lost / 3);
  SM_EXPECT_EQ(rx.GetPacketCounter().num_recovered_packets, static_cast<uint64_t>(delivered_lost));
  SM_EXPECT_GT(rx.GetPacketCounter().num_fec_packets, 0u);
}

SM_TEST(Ulpfec, ReceiverDropsWhatLibwebrtcDrops) {
  Random rng(9);
  UlpfecReceiver rx(kSsrc, kUlpfecPt);
  RtpPacket media = MakeMedia(1, 1, 20, rng);
  RtpPacket other = WrapInRed(media);
  other.ssrc = kSsrc + 1;
  SM_EXPECT_FALSE(rx.AddReceivedRedPacket(other));
  RtpPacket empty = media;
  empty.payload.clear();
  SM_EXPECT_FALSE(rx.AddReceivedRedPacket(empty));
  RtpPacket multi = WrapInRed(media);
  multi.payload[0] |= 0x80;  // F bit: more than one block.
  SM_EXPECT_FALSE(rx.AddReceivedRedPacket(multi));
  RtpPacket huge = WrapInRed(media);
  huge.payload.resize(1600);
  SM_EXPECT_FALSE(rx.AddReceivedRedPacket(huge));
  SM_EXPECT_EQ(rx.GetPacketCounter().num_packets, 0u);
  SM_EXPECT_TRUE(rx.AddReceivedRedPacket(WrapInRed(media)));
  auto out = rx.ProcessReceivedFec();
  SM_ASSERT_EQ(out.size(), 1u);
  SM_EXPECT_TRUE(out[0].Serialize() == media.Serialize());  // RED header gone, media PT back.
}
