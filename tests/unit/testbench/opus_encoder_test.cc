// The Opus send path of `audio v2` (issues #126, #457): libopus 55513e81 set
// up as libwebrtc's AudioEncoderOpusImpl sets it up.
#include <algorithm>
#include <cmath>
#include <vector>

#include "opus_encoder.h"
#include "sm/sim/signals.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::testbench;

namespace {

std::vector<int16_t> Pcm48k(double seconds, uint64_t seed) {
  std::vector<int16_t> pcm;
  for (float v : sim::SpeechLike(48000, seconds, seed, true)) pcm.push_back(OpusSendEncoder::ToPcm(v));
  return pcm;
}

std::span<const int16_t> Frame(const std::vector<int16_t>& pcm, size_t k) {
  return std::span<const int16_t>(pcm).subspan(std::min(k * OpusSendEncoder::kFrameSamples, pcm.size()));
}

}  // namespace

SM_TEST(OpusEncoder, DefaultsAreLibwebrtcs) {
  // api/audio_codecs/opus/audio_encoder_opus_config.h
  SM_EXPECT_EQ(OpusDefaults::kFrameSizeMs, 20);
  SM_EXPECT_EQ(OpusDefaults::kSampleRateHz, 48000);
  SM_EXPECT_EQ(OpusDefaults::kBitrateBps, 32000);
  SM_EXPECT_EQ(OpusDefaults::kMinBitrateBps, 6000);
  SM_EXPECT_EQ(OpusDefaults::kMaxBitrateBps, 510000);
  SM_EXPECT_FALSE(OpusDefaults::kCbr);
  SM_EXPECT_FALSE(OpusDefaults::kDtx);
  SM_EXPECT_EQ(OpusDefaults::kComplexity, 9);
  SM_EXPECT_EQ(OpusDefaults::kMaxPlaybackRateHz, 48000);
}

SM_TEST(OpusEncoder, BitrateIsAllocationLessOverhead) {
  // OnReceivedUplinkAllocation: overhead_bps = bytes * 8 * 100 / Num10MsFramesInNextPacket.
  SM_EXPECT_EQ(OpusBitrateBps(32000 + 53 * 8 * 50, 53), 32000);
  SM_EXPECT_EQ(OpusBitrateBps(10000, 53), 6000);       // Clamped to the encoder's minimum.
  SM_EXPECT_EQ(OpusBitrateBps(900000, 0), 510000);     // And its maximum.
  SM_EXPECT_EQ(OpusBitrateBps(60000, 40, 40), 60000 - 40 * 8 * 25);
}

// RecreateEncoderInstance's settings, read back from libopus.
SM_TEST(OpusEncoder, LibopusHasRecreateEncoderInstancesSettings) {
  OpusSendEncoder e(32000, true);
  SM_EXPECT_TRUE(e.ok());
  opus_int32 v = 0;
  SM_EXPECT_EQ(opus_encoder_ctl(e.opus(), OPUS_GET_APPLICATION(&v)), OPUS_OK);
  SM_EXPECT_EQ(v, OPUS_APPLICATION_VOIP);
  opus_encoder_ctl(e.opus(), OPUS_GET_SAMPLE_RATE(&v));
  SM_EXPECT_EQ(v, 48000);
  opus_encoder_ctl(e.opus(), OPUS_GET_BITRATE(&v));
  SM_EXPECT_EQ(v, 32000);
  opus_encoder_ctl(e.opus(), OPUS_GET_INBAND_FEC(&v));
  SM_EXPECT_EQ(v, 1);
  opus_encoder_ctl(e.opus(), OPUS_GET_MAX_BANDWIDTH(&v));
  SM_EXPECT_EQ(v, OPUS_BANDWIDTH_FULLBAND);
  opus_encoder_ctl(e.opus(), OPUS_GET_COMPLEXITY(&v));
  SM_EXPECT_EQ(v, 9);
  opus_encoder_ctl(e.opus(), OPUS_GET_DTX(&v));
  SM_EXPECT_EQ(v, 0);
  opus_encoder_ctl(e.opus(), OPUS_GET_VBR(&v));
  SM_EXPECT_EQ(v, 1);
  opus_encoder_ctl(e.opus(), OPUS_GET_PACKET_LOSS_PERC(&v));
  SM_EXPECT_EQ(v, 0);
  e.SetPacketLossPercent(7);
  opus_encoder_ctl(e.opus(), OPUS_GET_PACKET_LOSS_PERC(&v));
  SM_EXPECT_EQ(v, 7);
}

// Acceptance (#457): the audio rate is libopus's on the bench's speech. #452
// measured libwebrtc's frames on parity-data's traces: 32 kbps VBR on speech
// with pauses averages below the nominal 80 B, and digital silence is small.
SM_TEST(OpusEncoder, SpeechIsVbrAroundTheTargetAndSilenceIsSmall) {
  OpusSendEncoder e(32000, true);
  const auto pcm = Pcm48k(20.0, 8);
  double bytes = 0;
  size_t frames = 0;
  for (size_t k = 0; k < 1000; ++k) {
    const auto p = e.Encode(Frame(pcm, k));
    SM_EXPECT_TRUE(p.size() >= 3);
    bytes += static_cast<double>(p.size());
    ++frames;
  }
  SM_EXPECT_TRUE(e.ok());
  const double kbps = bytes * 8 * 50 / static_cast<double>(frames) / 1000;
  SM_EXPECT_TRUE(kbps > 20 && kbps < 34);
  // Digital zero after the signal (once the encoder's look-ahead and the
  // signal's tail have passed): small packets, never empty with DTX off.
  const std::vector<int16_t> zeros(OpusSendEncoder::kFrameSamples, 0);
  for (int i = 0; i < 25; ++i) {
    const auto p = e.Encode(zeros);
    SM_EXPECT_FALSE(p.empty());
    if (i >= 5) SM_EXPECT_TRUE(p.size() < 40);
  }
}

SM_TEST(OpusEncoder, IsDeterministic) {
  OpusSendEncoder a(32000, true), b(32000, true);
  const auto pcm = Pcm48k(4.0, 9);
  for (size_t k = 0; k < 200; ++k) SM_EXPECT_TRUE(a.Encode(Frame(pcm, k)) == b.Encode(Frame(pcm, k)));
}

// In-band FEC (issue #62): libopus codes LBRR only with a projected loss, and
// WebRtcOpus_PacketHasFec finds it in the next packet.
SM_TEST(OpusEncoder, LbrrFollowsTheProjectedLoss) {
  const auto pcm = Pcm48k(6.0, 10);
  OpusSendEncoder clean(32000, true), lossy(32000, true), off(32000, false);
  lossy.SetPacketLossPercent(10);
  off.SetPacketLossPercent(10);
  int clean_fec = 0, lossy_fec = 0, off_fec = 0;
  for (size_t k = 0; k < 300; ++k) {
    clean_fec += OpusSendEncoder::PacketHasFec(clean.Encode(Frame(pcm, k)));
    lossy_fec += OpusSendEncoder::PacketHasFec(lossy.Encode(Frame(pcm, k)));
    off_fec += OpusSendEncoder::PacketHasFec(off.Encode(Frame(pcm, k)));
  }
  SM_EXPECT_EQ(clean_fec, 0);
  SM_EXPECT_EQ(off_fec, 0);
  SM_EXPECT_TRUE(lossy_fec > 100);
}

SM_TEST(OpusEncoder, PacketHasFecReadsTheLbrrFlag) {
  SM_EXPECT_FALSE(OpusSendEncoder::PacketHasFec({}));
  const uint8_t celt[] = {0x80 | (31 << 3), 0xff, 0xff};  // CELT-only: never FEC.
  SM_EXPECT_FALSE(OpusSendEncoder::PacketHasFec(celt));
  // SILK-only wideband 20 ms (config 9), mono, one frame: VAD bit, then the LBRR bit.
  const uint8_t lbrr[] = {9 << 3, 0x40, 0x00};
  const uint8_t no_lbrr[] = {9 << 3, 0x80, 0x00};
  SM_EXPECT_TRUE(OpusSendEncoder::PacketHasFec(lbrr));
  SM_EXPECT_FALSE(OpusSendEncoder::PacketHasFec(no_lbrr));
}

// AudioEncoderOpusImpl's loss input (audio_encoder_opus.cc):
// PacketLossFractionSmoother, then SetProjectedPacketLossRate.
SM_TEST(OpusEncoder, PacketLossRateIsSmoothedClampedAndRounded) {
  OpusPacketLossRate r(Timestamp::Millis(0));
  SM_EXPECT_EQ(r.percent(), 0);
  r.OnReceivedUplinkPacketLossFraction(0.1f, Timestamp::Millis(1000));  // The first sample is taken as is.
  SM_EXPECT_EQ(r.percent(), 10);
  // 10 s later a 0 report: 0.9999^10000 = 0.368 of the old value is kept.
  r.OnReceivedUplinkPacketLossFraction(0.0f, Timestamp::Millis(11000));
  SM_EXPECT_EQ(r.percent(), 4);
  OpusPacketLossRate high(Timestamp::Millis(0));
  high.OnReceivedUplinkPacketLossFraction(0.5f, Timestamp::Millis(1000));
  SM_EXPECT_EQ(high.percent(), 20);  // kMaxPacketLossFraction.
  OpusPacketLossRate low(Timestamp::Millis(0));
  low.OnReceivedUplinkPacketLossFraction(0.004f, Timestamp::Millis(1000));
  SM_EXPECT_EQ(low.percent(), 0);  // Rounded to a whole percent: no LBRR.
  low.OnReceivedUplinkPacketLossFraction(0.006f, Timestamp::Millis(1001));
  SM_EXPECT_EQ(low.percent(), 0);  // 0.0040002 after one ms.
}
