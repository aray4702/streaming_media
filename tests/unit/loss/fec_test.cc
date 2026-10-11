#include <bit>

#include "sm/core/random.h"
#include "sm/loss/fec.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::loss;

namespace {
std::vector<RtpPacket> MakeFrame(uint16_t first_seq, int k, uint32_t ts, Random& rng) {
  std::vector<RtpPacket> v;
  for (int i = 0; i < k; ++i) {
    RtpPacket p;
    p.seq = static_cast<uint16_t>(first_seq + i);
    p.timestamp = ts;
    p.ssrc = 1234;
    p.payload_type = 96;
    p.marker = i == k - 1;
    p.payload.resize(static_cast<size_t>(rng.UniformInt(50, 1200)));
    for (auto& b : p.payload) b = static_cast<uint8_t>(rng.Next());
    v.push_back(p);
  }
  return v;
}
}  // namespace

SM_TEST(Fec, MasksCoverEveryPacket) {
  for (auto type : {FecMaskType::kInterleaved, FecMaskType::kBlock}) {
    for (int k = 1; k <= 48; ++k) {
      for (int m = 1; m <= k; ++m) {
        uint64_t all = 0;
        for (int j = 0; j < m; ++j) all |= FecMask(k, m, j, type);
        SM_EXPECT_EQ(std::popcount(all), k);
      }
    }
  }
  SM_EXPECT_EQ(FecPacketsFor(10, 0.25), 3);
  SM_EXPECT_EQ(FecPacketsFor(10, 0.0), 0);
  SM_EXPECT_EQ(FecPacketsFor(1, 0.1), 1);
}

SM_TEST(Fec, RecoversSingleLossExactly) {
  Random rng(1);
  FecEncoder enc;
  enc.SetParams({0.3, FecMaskType::kBlock});
  FecDecoder dec;
  auto frame = MakeFrame(100, 10, 9000, rng);
  std::vector<RtpPacket> fec;
  for (size_t i = 0; i < frame.size(); ++i) {
    auto f = enc.AddMediaPacket(frame[i], i + 1 == frame.size());
    fec.insert(fec.end(), f.begin(), f.end());
  }
  SM_EXPECT_EQ(fec.size(), 3u);
  for (size_t i = 0; i < frame.size(); ++i) {
    if (i != 4) dec.OnMediaPacket(frame[i]);
  }
  std::vector<RtpPacket> rec;
  for (auto& f : fec) {
    auto r = dec.OnFecPacket(f);
    rec.insert(rec.end(), r.begin(), r.end());
  }
  SM_ASSERT_EQ(rec.size(), 1u);
  SM_EXPECT_EQ(rec[0].seq, frame[4].seq);
  SM_EXPECT_EQ(rec[0].payload, frame[4].payload);
  SM_EXPECT_EQ(rec[0].timestamp, frame[4].timestamp);
  SM_EXPECT_EQ(rec[0].payload_type, frame[4].payload_type);
  SM_EXPECT_EQ(rec[0].marker, frame[4].marker);
}

SM_TEST(Fec, InterleavedRecoversBurstUpToM) {
  Random rng(2);
  FecEncoder enc;
  enc.SetParams({0.4, FecMaskType::kInterleaved});
  FecDecoder dec;
  auto frame = MakeFrame(65530, 10, 1, rng);  // Crosses seq wrap.
  std::vector<RtpPacket> fec;
  for (size_t i = 0; i < frame.size(); ++i) {
    auto f = enc.AddMediaPacket(frame[i], i + 1 == frame.size());
    fec.insert(fec.end(), f.begin(), f.end());
  }
  SM_ASSERT_EQ(fec.size(), 4u);
  // Burst of 4 consecutive losses: 3,4,5,6.
  for (size_t i = 0; i < frame.size(); ++i) {
    if (i < 3 || i > 6) dec.OnMediaPacket(frame[i]);
  }
  size_t got = 0;
  for (auto& f : fec) got += dec.OnFecPacket(f).size();
  SM_EXPECT_EQ(got, 4u);
}

SM_TEST(Fec, BlockMaskCannotRecoverTwoInSameBlock) {
  Random rng(3);
  FecEncoder enc;
  enc.SetParams({0.2, FecMaskType::kBlock});
  FecDecoder dec;
  auto frame = MakeFrame(0, 10, 1, rng);
  std::vector<RtpPacket> fec;
  for (size_t i = 0; i < frame.size(); ++i) {
    auto f = enc.AddMediaPacket(frame[i], i + 1 == frame.size());
    fec.insert(fec.end(), f.begin(), f.end());
  }
  for (size_t i = 2; i < frame.size(); ++i) dec.OnMediaPacket(frame[i]);  // Lose 0 and 1.
  size_t got = 0;
  for (auto& f : fec) got += dec.OnFecPacket(f).size();
  SM_EXPECT_EQ(got, 0u);
}

SM_TEST(Fec, IterativeRecoveryAcrossParityPackets) {
  // Media arrive after FEC; recovery chains as more media come in.
  Random rng(4);
  FecEncoder enc;
  enc.SetParams({0.5, FecMaskType::kInterleaved});
  FecDecoder dec;
  auto frame = MakeFrame(10, 6, 1, rng);
  std::vector<RtpPacket> fec;
  for (size_t i = 0; i < frame.size(); ++i) {
    auto f = enc.AddMediaPacket(frame[i], i + 1 == frame.size());
    fec.insert(fec.end(), f.begin(), f.end());
  }
  for (auto& f : fec) SM_EXPECT_TRUE(dec.OnFecPacket(f).empty());
  size_t got = 0;
  for (size_t i : {0u, 1u, 2u, 5u}) got += dec.OnMediaPacket(frame[i]).size();
  // Lost 3 and 4 are recovered; 5 is also rebuilt (from parity {2,5}) before
  // its own late arrival, which then counts as a duplicate.
  SM_EXPECT_EQ(got, 3u);
  SM_EXPECT_EQ(dec.stats().recovered, 3u);
}

SM_TEST(Fec, ZeroProtectionEmitsNothing) {
  Random rng(5);
  FecEncoder enc;
  auto frame = MakeFrame(0, 5, 1, rng);
  for (size_t i = 0; i < frame.size(); ++i) SM_EXPECT_TRUE(enc.AddMediaPacket(frame[i], i == 4).empty());
  SM_EXPECT_EQ(CollectMetrics(enc)["overhead"], 0.0);
}
