// The test bench's Opus send path, `audio v2` (issues #126, #457): libopus
// 55513e81, the revision libwebrtc M157 builds (compare/webrtc/reference/fetch.sh
// OPUS_REV; fetched and built by cmake/SmOpus.cmake, in the test bench only),
// configured as libwebrtc's AudioEncoderOpusImpl configures it. It replaced a
// frame-size model fitted to libwebrtc's traces (Owner's decision C on #452).
//
// Defaults, from compare/webrtc/reference/ at M157:
//   api/audio_codecs/opus/audio_encoder_opus_config.{h,cc}  20 ms frames, 48 kHz,
//       mono VOIP, 32 kbps, VBR (cbr_enabled = false), DTX off, complexity 9
//       (kDefaultComplexity on desktop; the low-rate complexity is the same 9),
//       max playback rate 48 kHz, bitrate clamped to 6..510 kbps
//   modules/audio_coding/codecs/opus/audio_encoder_opus.cc
//       RecreateEncoderInstance: the encoder's settings, in this order;
//       OnReceivedUplinkAllocation: the Opus bitrate is the allocation less the
//       per-packet overhead at the packet rate (GetBitrateRange is not
//       overridden, so the allocator's constraints are the configured rate
//       plus that overhead)
//   modules/audio_coding/codecs/opus/opus_interface.cc  WebRtcOpus_* (the
//       opus_encoder_ctl calls; WebRtcOpus_Encode's DTX packets;
//       WebRtcOpus_PacketHasFec)
//   audio/audio_send_stream.cc  GetMinMaxBitrateConstraints / ConfigureBitrateObserver
//   modules/audio_coding/codecs/red/audio_encoder_copy_red.cc  one redundant block,
//       a 4 byte header per redundant block and 1 byte for the primary
//       (src/loss/red.cc already writes this layout)
//
// In-band FEC (LBRR, issue #62), as libwebrtc sends it with sm_compare's
// format (`useinbandfec=1`, compare/webrtc/webrtc_call.cc):
//   audio/channel_send.cc  OnReportBlockDataUpdated: each report block's fraction
//       lost for the audio SSRC goes to the encoder
//   audio_encoder_opus.cc  OnReceivedUplinkPacketLossFraction ->
//       PacketLossFractionSmoother (an ExpFilter, alpha 0.9999 per ms: a 10 s
//       time constant) -> SetProjectedPacketLossRate (clamped to
//       kMaxPacketLossFraction 0.2, a whole percent to WebRtcOpus_SetPacketLossRate)
// libopus then decides per packet whether to code LBRR (opus_encoder.c decide_fec,
// silk/control_codec.c silk_setup_LBRR).
//
// The input is the signal sm_compare gives libwebrtc's ADM: sim::SpeechLike at
// 48 kHz with the call's seed + 7, as 16-bit PCM (compare/webrtc/webrtc_call.cc).
// libwebrtc resamples a capture at another rate to the encoder's 48 kHz; the
// bench encodes the 48 kHz signal whatever its capture rate, and plays out the
// capture-rate signal (decoded with libopus too: testbench/opus_decoder.h, issue #476).
#pragma once

#include <opus.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "sm/cc/frame_dropper.h"
#include "sm/core/units.h"

namespace sm::testbench {

// AudioEncoderOpusConfig defaults (audio_encoder_opus_config.h).
struct OpusDefaults {
  static constexpr int kFrameSizeMs = 20;
  static constexpr int kSampleRateHz = 48'000;
  static constexpr int kBitrateBps = 32'000;
  static constexpr int kMinBitrateBps = 6'000;
  static constexpr int kMaxBitrateBps = 510'000;
  static constexpr bool kCbr = false;
  static constexpr bool kDtx = false;
  static constexpr int kComplexity = 9;  // kDefaultComplexity (desktop).
  static constexpr int kMaxPlaybackRateHz = 48'000;
};

// AudioEncoderOpusImpl::OnReceivedUplinkAllocation: allocation less overhead,
// clamped to the encoder's range.
inline int OpusBitrateBps(int allocation_bps, size_t overhead_bytes_per_packet, int frame_size_ms = OpusDefaults::kFrameSizeMs) {
  const int overhead_bps = static_cast<int>(overhead_bytes_per_packet * 8 * 100 / static_cast<size_t>(frame_size_ms / 10));
  return std::min(OpusDefaults::kMaxBitrateBps, std::max(OpusDefaults::kMinBitrateBps, allocation_bps - overhead_bps));
}

// AudioEncoderOpusImpl over libopus, for one mono 48 kHz stream.
class OpusSendEncoder {
 public:
  static constexpr size_t kFrameSamples = OpusDefaults::kSampleRateHz / 1000 * OpusDefaults::kFrameSizeMs;

  // RecreateEncoderInstance with `fec_enabled` (useinbandfec) and a projected
  // packet loss of 0.
  OpusSendEncoder(int bitrate_bps, bool fec_enabled) : bitrate_bps_(bitrate_bps) {
    int error = OPUS_OK;
    enc_ = opus_encoder_create(OpusDefaults::kSampleRateHz, 1, OPUS_APPLICATION_VOIP, &error);
    ok_ = error == OPUS_OK && enc_ != nullptr;
    if (!ok_) return;
    Ctl(opus_encoder_ctl(enc_, OPUS_SET_BITRATE(bitrate_bps)));
    Ctl(opus_encoder_ctl(enc_, OPUS_SET_INBAND_FEC(fec_enabled ? 1 : 0)));
    Ctl(opus_encoder_ctl(enc_, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND)));  // WebRtcOpus_SetMaxPlaybackRate(48000).
    Ctl(opus_encoder_ctl(enc_, OPUS_SET_COMPLEXITY(OpusDefaults::kComplexity)));
    Ctl(opus_encoder_ctl(enc_, OPUS_SET_DTX(OpusDefaults::kDtx ? 1 : 0)));
    Ctl(opus_encoder_ctl(enc_, OPUS_SET_PACKET_LOSS_PERC(0)));
    Ctl(opus_encoder_ctl(enc_, OPUS_SET_VBR(OpusDefaults::kCbr ? 0 : 1)));
  }
  ~OpusSendEncoder() {
    if (enc_ != nullptr) opus_encoder_destroy(enc_);
  }
  OpusSendEncoder(const OpusSendEncoder&) = delete;
  OpusSendEncoder& operator=(const OpusSendEncoder&) = delete;

  // Whether libopus took every setting.
  bool ok() const { return ok_; }
  OpusEncoder* opus() { return enc_; }

  // The bitrate libopus has (OPUS_GET_BITRATE), -1 if it can't say.
  int bitrate_bps() const {
    opus_int32 bps = -1;
    if (enc_ == nullptr || opus_encoder_ctl(enc_, OPUS_GET_BITRATE(&bps)) != OPUS_OK) return -1;
    return bps;
  }

  // SetProjectedPacketLossRate, given the whole percent it hands libopus.
  void SetPacketLossPercent(int percent) {
    if (percent == loss_percent_) return;
    loss_percent_ = percent;
    Ctl(opus_encoder_ctl(enc_, OPUS_SET_PACKET_LOSS_PERC(percent)));
  }

  // AudioEncoderOpusImpl::SufficientOutputBufferSize: twice the bytes the
  // configured bitrate gives a packet, rounded up per millisecond (200 bytes
  // for 20 ms at 32 kbps). EncodeImpl hands libopus this many bytes, and
  // libopus's rate control codes to it (opus_encode's max_data_bytes), so
  // another size codes other bits on some frames (issue #547).
  size_t SufficientOutputBufferSize() const {
    const size_t bytes_per_millisecond = static_cast<size_t>(bitrate_bps_ / (1000 * 8) + 1);
    const size_t approx_encoded_bytes = static_cast<size_t>(OpusDefaults::kFrameSizeMs / 10) * 10 * bytes_per_millisecond;
    return 2 * approx_encoded_bytes;
  }

  // One 20 ms frame of 48 kHz PCM (zeros past the end of `pcm`): the payload
  // WebRtcOpus_Encode returns, coded into SufficientOutputBufferSize bytes as
  // AudioEncoderOpusImpl::EncodeImpl codes it. With DTX, a second and later
  // header-only packet in a row comes back empty, as libwebrtc's.
  std::vector<uint8_t> Encode(std::span<const int16_t> pcm) {
    int16_t in[kFrameSamples] = {};
    std::copy_n(pcm.begin(), std::min(pcm.size(), kFrameSamples), in);
    std::vector<uint8_t> out(SufficientOutputBufferSize());
    const int res = ok_ ? opus_encode(enc_, in, static_cast<int>(kFrameSamples), out.data(), static_cast<opus_int32>(out.size())) : -1;
    if (res <= 0) {
      ok_ = false;
      return {};
    }
    if (res <= 2) {
      if (in_dtx_mode_) return {};
      in_dtx_mode_ = true;
    } else {
      in_dtx_mode_ = false;
    }
    out.resize(static_cast<size_t>(res));
    return out;
  }

  // WebRtcOpus_PacketHasFec: the first SILK frame of the packet carries an
  // LBRR copy of the frame before it (the LP layer's LBRR flag).
  static bool PacketHasFec(std::span<const uint8_t> payload) {
    if (payload.empty() || (payload[0] & 0x80)) return false;  // CELT-only packets have no FEC.
    int ms = opus_packet_get_samples_per_frame(payload.data(), 48000) / 48;
    if (ms < 10) ms = 10;
    const int silk_frames = ms == 10 || ms == 20 ? 1 : ms == 40 ? 2 : ms == 60 ? 3 : 0;
    if (silk_frames == 0) return false;
    const int channels = opus_packet_get_nb_channels(payload.data());
    opus_int16 frame_sizes[48];
    const unsigned char* frame_data[48];
    if (opus_packet_parse(payload.data(), static_cast<opus_int32>(payload.size()), nullptr, frame_data, frame_sizes, nullptr) < 0) {
      return false;
    }
    if (frame_sizes[0] < 1) return false;
    for (int n = 0; n < channels; ++n) {
      if (frame_data[0][0] & (0x80 >> ((n + 1) * (silk_frames + 1) - 1))) return true;
    }
    return false;
  }

  // The 16-bit PCM sm_compare hands libwebrtc's ADM for a float sample.
  static int16_t ToPcm(float v) { return static_cast<int16_t>(std::clamp(v, -1.f, 1.f) * 32767); }

 private:
  void Ctl(int rc) { ok_ = ok_ && rc == OPUS_OK; }

  int bitrate_bps_;  // The configured bitrate (AudioEncoderOpusConfig::bitrate_bps).
  OpusEncoder* enc_ = nullptr;
  bool ok_ = false;
  bool in_dtx_mode_ = false;
  int loss_percent_ = 0;
};

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
