// loss::FecControllerDefault against libwebrtc M157's FecControllerDefault and
// media_opt_util.cc (compare/webrtc/reference/modules/video_coding/). The
// expected rates were worked out from kFecRateTable (fec_rate_table.h) by hand
// through VCMFecMethod::ProtectionFactor and ConvertFECRate; each test names
// the steps.
#include "sm/loss/fec_controller.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::loss;

namespace {

Timestamp Ms(int64_t ms) { return Timestamp::Millis(ms); }
DataRate Kbps(int64_t k) { return DataRate::KilobitsPerSec(k); }

}  // namespace

SM_TEST(FecController, MethodFollowsWhatIsEnabled) {
  FecControllerDefault c;
  SM_EXPECT_TRUE(c.method() == ProtectionMethod::kNone);
  // kNone makes no protection request at all.
  SM_EXPECT_FALSE(c.UpdateFecRates(Kbps(1000), 30, 40, 100, Ms(0)).has_value());
  c.SetProtectionMethod(true, true);
  SM_EXPECT_TRUE(c.method() == ProtectionMethod::kNackFec);
  c.SetProtectionMethod(true, false);
  SM_EXPECT_TRUE(c.method() == ProtectionMethod::kFec);
  c.SetProtectionMethod(false, true);
  SM_EXPECT_TRUE(c.method() == ProtectionMethod::kNack);
  // VCMNackMethod asks for no FEC.
  auto r = c.UpdateFecRates(Kbps(1000), 30, 40, 100, Ms(1000));
  SM_EXPECT_TRUE(r.has_value());
  SM_EXPECT_EQ(r->delta.fec_rate, 0);
  SM_EXPECT_EQ(r->key.fec_rate, 0);
}

SM_TEST(FecController, NoLossNoFec) {
  FecControllerDefault c;
  c.SetProtectionMethod(true, false);
  auto r = c.UpdateFecRates(Kbps(1000), 30, 0, 100, Ms(0));
  SM_EXPECT_EQ(r->delta.fec_rate, 0);
  SM_EXPECT_EQ(r->key.fec_rate, 0);
  SM_EXPECT_EQ(r->delta.max_fec_frames, 1);
  SM_EXPECT_TRUE(r->delta.fec_mask_type == ulpfec::FecMaskType::kRandom);
}

// 1000 kbps at 30 fps is 33 kbit per frame; at 704x576 the resolution factor is 1, so the rate
// index is (33 - 5) / 5 = 5. Loss 40/255: kFecRateTable[5 * 129 + 40] = 79, converted to a share
// of the media packets 0.5 + 255 * 79 / 176 = 114. Key frames: twice 79 is capped at 128, which
// converts to 255.
SM_TEST(FecController, DeltaRateFromTheTable) {
  FecControllerDefault c;
  c.SetProtectionMethod(true, false);
  auto r = c.UpdateFecRates(Kbps(1000), 30, 40, 100, Ms(0));
  SM_EXPECT_EQ(r->delta.fec_rate, 114);
  SM_EXPECT_EQ(r->key.fec_rate, 255);
  // A larger picture lowers the effective rate (resolution factor (1280 * 720 / (704 * 576))^-0.3):
  // index 4, kFecRateTable[4 * 129 + 40] = 77, converted 110.
  c.SetEncodingData(1280, 720, 1, 1200);
  r = c.UpdateFecRates(Kbps(1000), 30, 40, 100, Ms(1000));
  SM_EXPECT_EQ(r->delta.fec_rate, 110);
}

// Low loss: the table gives less than the first-partition minimum, 255 * 0.20 = 51, which converts
// to 0.5 + 255 * 51 / 204 = 64. Key frames get twice 51, 102, converted 170.
SM_TEST(FecController, FirstPartitionMinimum) {
  FecControllerDefault c;
  c.SetProtectionMethod(true, false);
  auto r = c.UpdateFecRates(Kbps(1000), 30, 13, 100, Ms(0));
  SM_EXPECT_EQ(r->delta.fec_rate, 64);
  SM_EXPECT_EQ(r->key.fec_rate, 170);
}

// At most 128/255 of all packets (kPacketLossMax - 1), which converts to 255.
SM_TEST(FecController, CappedAtHalfOfAllPackets) {
  FecControllerDefault c;
  c.SetProtectionMethod(true, false);
  auto r = c.UpdateFecRates(Kbps(1000), 30, 200, 100, Ms(0));
  SM_EXPECT_EQ(r->delta.fec_rate, 253);
  SM_EXPECT_EQ(r->key.fec_rate, 255);
}

// kMaxFilter: the largest loss of the last 10 one-second windows. A window
// opens with the first report after the previous one is 1 s old, and the
// history keeps 10 of them.
SM_TEST(FecController, LossIsAMaxOverTenSeconds) {
  FecControllerDefault c;
  c.SetProtectionMethod(true, false);
  c.UpdateFecRates(Kbps(1000), 30, 40, 100, Ms(0));
  SM_EXPECT_EQ(c.filtered_loss(), 40);
  for (int64_t t = 500; t < 10000; t += 500) {
    c.UpdateFecRates(Kbps(1000), 30, 0, 100, Ms(t));
    SM_EXPECT_EQ(c.filtered_loss(), 40);
  }
  // The window opened at 10 s pushes the one that held 40 out of the history.
  c.UpdateFecRates(Kbps(1000), 30, 0, 100, Ms(10000));
  SM_EXPECT_EQ(c.filtered_loss(), 0);
  // Within one window the largest report counts.
  c.UpdateFecRates(Kbps(1000), 30, 20, 100, Ms(10100));
  c.UpdateFecRates(Kbps(1000), 30, 5, 100, Ms(10200));
  SM_EXPECT_EQ(c.filtered_loss(), 20);
}

// VCMNackFecMethod: NACK alone for delta frames below kLowRttNackMs (20 ms), FEC kept from 20 ms on.
SM_TEST(FecController, NackFecLowRttIsNackOnlyForDeltaFrames) {
  FecControllerDefault c;
  c.SetProtectionMethod(true, true);
  auto r = c.UpdateFecRates(Kbps(1000), 30, 13, 19, Ms(0));
  SM_EXPECT_EQ(r->delta.fec_rate, 0);
  SM_EXPECT_EQ(r->key.fec_rate, 170);
  r = c.UpdateFecRates(Kbps(1000), 30, 13, 20, Ms(1000));
  SM_EXPECT_EQ(r->delta.fec_rate, 64);
}

// max(2 * fps * RTT + 0.5, 1) frames, at most kUpperLimitFramesFec (6); kFec uses one frame.
SM_TEST(FecController, NackFecMaxFramesFromRtt) {
  FecControllerDefault c;
  c.SetProtectionMethod(true, true);
  SM_EXPECT_EQ(c.UpdateFecRates(Kbps(1000), 30, 13, 50, Ms(0))->delta.max_fec_frames, 3);
  SM_EXPECT_EQ(c.UpdateFecRates(Kbps(1000), 30, 13, 150, Ms(1000))->key.max_fec_frames, 6);
  c.SetProtectionMethod(true, false);
  SM_EXPECT_EQ(c.UpdateFecRates(Kbps(1000), 30, 13, 150, Ms(2000))->delta.max_fec_frames, 1);
}

// BitRateTooLowForFec: at CIF, 90 kbps at 30 fps is 375 bytes per frame, under 400, so no FEC
// while the RTT is under 200 ms. At 200 ms the table applies: index 0, no first-partition minimum
// (1.76 packets per frame round to 1), so delta frames get kFecRateTable[13] = 0 and key frames
// the loss itself, 13, converted 14.
SM_TEST(FecController, NackFecOffWhenFramesAreSmall) {
  FecControllerDefault c;
  c.SetProtectionMethod(true, true);
  c.SetEncodingData(352, 288, 1, 1200);
  auto r = c.UpdateFecRates(Kbps(90), 30, 13, 100, Ms(0));
  SM_EXPECT_EQ(r->delta.fec_rate, 0);
  SM_EXPECT_EQ(r->key.fec_rate, 0);
  r = c.UpdateFecRates(Kbps(90), 30, 13, 200, Ms(1000));
  SM_EXPECT_EQ(r->delta.fec_rate, 0);
  SM_EXPECT_EQ(r->key.fec_rate, 14);
}

// UpdateWithEncodedData: size / max payload, ExpFilter(0.9999) over the ms between frames.
SM_TEST(FecController, PacketsPerFrameFiltered) {
  FecControllerDefault c;
  c.SetProtectionMethod(true, false);
  c.SetEncodingData(704, 576, 1, 1000);
  c.UpdateWithEncodedData(3000, false, Ms(0));
  SM_EXPECT_NEAR(c.packets_per_frame(), 3.0, 1e-6);
  c.UpdateWithEncodedData(6000, false, Ms(1000));
  // 0.9999^1000 = 0.904837: 0.904837 * 3 + 0.095163 * 6.
  SM_EXPECT_NEAR(c.packets_per_frame(), 3.285489, 1e-4);
  c.UpdateWithEncodedData(12000, true, Ms(1000));
  SM_EXPECT_NEAR(c.packets_per_frame_key(), 12.0, 1e-6);
  // Nothing for an empty frame.
  c.UpdateWithEncodedData(0, false, Ms(2000));
  SM_EXPECT_NEAR(c.packets_per_frame(), 3.285489, 1e-4);
}
