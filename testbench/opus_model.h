// The test bench's model of libwebrtc's Opus audio send path, `audio v2`
// (issue #126). The library carries no codec, so what the bench can port is
// what the encoder asks for and what it puts on the wire: the defaults of
// the encoder configuration and the size of each encoded frame.
//
// Defaults, from compare/webrtc/reference/ at M157:
//   api/audio_codecs/opus/audio_encoder_opus_config.{h,cc}  20 ms frames, 32 kbps,
//       VBR (cbr_enabled = false), DTX off, bitrate clamped to 6..510 kbps
//   modules/audio_coding/codecs/opus/audio_encoder_opus.cc  OnReceivedUplinkAllocation:
//       the Opus bitrate is the allocation less the per-packet overhead at the
//       packet rate (GetBitrateRange is not overridden, so the allocator's
//       constraints are the configured rate plus that overhead)
//   audio/audio_send_stream.cc  GetMinMaxBitrateConstraints / ConfigureBitrateObserver
//   modules/audio_coding/codecs/red/audio_encoder_copy_red.cc  one redundant block,
//       a 4 byte header per redundant block and 1 byte for the primary
//       (src/loss/red.cc already writes this layout)
//
// Frame sizes are a model, not libopus: libwebrtc's sizes were read off the
// forward-link trace of `very_low_capacity_allocator_v2` (tools/link_trace.py,
// seed 1, 2000 packets) by removing the headers and the redundant block:
//   speech       mean 72.1 B, sd 12.9 B, lag-1 autocorrelation 0.89 at 32 kbps
//   digital zero a constant 3, 17, 21, 32 or 35 B for the whole silent run
//   onset        up to 144 B (the first frame after digital zero)
// Listed as a deviation in tools/catalog.py (the size model stands in for libopus).
//
// In-band FEC (LBRR, issue #62), as libwebrtc sends it with sm_compare's
// format (`useinbandfec=1`, compare/webrtc/webrtc_call.cc):
//   audio/channel_send.cc  OnReportBlockDataUpdated: each report block's fraction
//       lost for the audio SSRC goes to the encoder
//   modules/audio_coding/codecs/opus/audio_encoder_opus.cc
//       OnReceivedUplinkPacketLossFraction -> PacketLossFractionSmoother (an
//       ExpFilter, alpha 0.9999 per ms: a 10 s time constant) ->
//       SetProjectedPacketLossRate (clamped to kMaxPacketLossFraction 0.2, a
//       whole percent to WebRtcOpus_SetPacketLossRate)
// Which frames libopus then codes LBRR for is decided inside libopus, which
// is not in compare/webrtc/reference (#57), so it is a model: while the
// percentage is above 0, a packet carries the LBRR copy of the previous frame
// if that frame was not digital silence. The frame sizes do not change with
// it: libwebrtc's audio packets on audio_only_lossy (10% loss) are the size
// of clean_link's (5 s means 158-178 B and 160-177 B, seed 1 traces), since
// libopus fits LBRR inside the VBR budget. The payload's first byte carries
// the flag (kLbrrFlag), standing in for the TOC byte and SILK's LBRR flags.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "sm/cc/frame_dropper.h"
#include "sm/core/random.h"
#include "sm/core/units.h"

namespace sm::testbench {

// AudioEncoderOpusConfig defaults (audio_encoder_opus_config.h).
struct OpusDefaults {
  static constexpr int kFrameSizeMs = 20;
  static constexpr int kBitrateBps = 32'000;
  static constexpr int kMinBitrateBps = 6'000;
  static constexpr int kMaxBitrateBps = 510'000;
  static constexpr bool kCbr = false;
  static constexpr bool kDtx = false;
};

// AudioEncoderOpusImpl::OnReceivedUplinkAllocation: allocation less overhead,
// clamped to the encoder's range.
inline int OpusBitrateBps(int allocation_bps, size_t overhead_bytes_per_packet, int frame_size_ms = OpusDefaults::kFrameSizeMs) {
  const int overhead_bps = static_cast<int>(overhead_bytes_per_packet * 8 * 100 / static_cast<size_t>(frame_size_ms / 10));
  return std::min(OpusDefaults::kMaxBitrateBps, std::max(OpusDefaults::kMinBitrateBps, allocation_bps - overhead_bps));
}

// Bytes of one 20 ms Opus frame for the given input.
class OpusSizeModel {
 public:
  static constexpr double kSpeechShare = 74.0 / 80.0;  // Mean frame / (32 kbps * 20 ms).
  static constexpr double kSpeechSd = 10.0 / 80.0;     // As a share of the nominal frame at the bitrate.
  static constexpr double kAutocorrelation = 0.89;
  static constexpr double kSilenceRms = 1e-5;
  static constexpr size_t kMinSpeechShare = 36;        // Observed floor at 32 kbps.
  static constexpr size_t kMaxFrame = 144;

  OpusSizeModel(uint64_t seed, int bitrate_bps) : rng_(seed), bitrate_bps_(bitrate_bps) {}

  void SetBitrate(int bitrate_bps) { bitrate_bps_ = bitrate_bps; }
  // Whether the last frame was speech rather than digital silence (false
  // before the first frame): the in-band FEC model's input.
  bool LastFrameActive() const { return frames_ > 0 && !silent_; }

  size_t NextFrameBytes(std::span<const float> frame) {
    double e = 0;
    for (float v : frame) e += static_cast<double>(v) * v;
    const double rms = frame.empty() ? 0.0 : std::sqrt(e / static_cast<double>(frame.size()));
    const double nominal = static_cast<double>(bitrate_bps_) / 8.0 * 0.02;  // CBR size of the frame.
    ++frames_;
    if (rms < kSilenceRms) {
      if (!silent_) {
        silent_ = true;
        // Weights of the 18 silent runs in the trace: 3 x6, 17 x5, 21 x2, 32 x1, 35 x4.
        const int64_t k = rng_.UniformInt(0, 17);
        silence_bytes_ = k < 6 ? 3 : k < 11 ? 17 : k < 13 ? 21 : k < 14 ? 32 : 35;
      }
      return silence_bytes_;
    }
    const bool onset = silent_;
    silent_ = false;
    state_ = kAutocorrelation * state_ + std::sqrt(1 - kAutocorrelation * kAutocorrelation) * rng_.Gaussian();
    double bytes = nominal * kSpeechShare + state_ * nominal * kSpeechSd;
    if (onset) bytes = std::max(bytes, nominal * rng_.Uniform(0.9, 1.8));  // The first frame after digital zero.
    const double lo = static_cast<double>(kMinSpeechShare) * nominal / 80.0;
    const double hi = static_cast<double>(kMaxFrame) * nominal / 80.0;
    return static_cast<size_t>(std::lround(std::clamp(bytes, lo, hi)));
  }

 private:
  Random rng_;
  int bitrate_bps_;
  double state_ = 0;
  bool silent_ = false;
  size_t silence_bytes_ = 3;
  int64_t frames_ = 0;
};

// The payload's first byte: the frame carries the LBRR copy of the previous one.
inline constexpr uint8_t kLbrrFlag = 0x01;

// AudioEncoderOpusImpl's loss input: PacketLossFractionSmoother and
// SetProjectedPacketLossRate (audio_encoder_opus.cc).
class OpusPacketLossRate {
 public:
  static constexpr float kAlphaForPacketLossFractionSmoother = 0.9999f;
  static constexpr float kMaxPacketLossFraction = 0.2f;

  // The smoother starts its clock when the encoder is made.
  explicit OpusPacketLossRate(Timestamp now) : last_sample_time_(now), smoother_(kAlphaForPacketLossFractionSmoother) {}

  // OnReceivedUplinkPacketLossFraction.
  void OnReceivedUplinkPacketLossFraction(float fraction, Timestamp now) {
    smoother_.Apply(static_cast<float>((now - last_sample_time_).ms_f()), fraction);
    last_sample_time_ = now;
    const float average = smoother_.filtered() == cc::ExpFilter::kValueUndefined ? 0.0f : smoother_.filtered();
    const float clamped = std::min(std::max(average, 0.0f), kMaxPacketLossFraction);
    percent_ = static_cast<int>(clamped * 100 + .5f);
  }
  // The percentage libopus is given (WebRtcOpus_SetPacketLossRate).
  int percent() const { return percent_; }

 private:
  Timestamp last_sample_time_;
  cc::ExpFilter smoother_;
  int percent_ = 0;
};

}  // namespace sm::testbench
