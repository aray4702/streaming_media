// The test bench's Opus receive path (issue #476): libopus 55513e81, the
// revision libwebrtc M157 builds (as for the send path, testbench/opus_encoder.h),
// called as libwebrtc's AudioDecoderOpusImpl calls it, so our jitter buffer
// gets the samples libwebrtc's NetEq gets for the same packets.
//
// From compare/webrtc/reference/ at M157:
//   modules/audio_coding/codecs/opus/audio_decoder_opus.cc
//       AudioDecoderOpusImpl: one decoder state, created at the configured rate
//       (16 or 48 kHz) and channel count; DecodeInternal -> WebRtcOpus_Decode;
//       DecodeRedundantInternal -> WebRtcOpus_DecodeFec for a packet with an
//       LBRR copy (otherwise a RED block, decoded as a primary);
//       PacketDurationRedundant -> WebRtcOpus_FecDurationEst
//   modules/audio_coding/codecs/opus/opus_interface.cc
//       WebRtcOpus_DecoderCreate (opus_decoder_create), WebRtcOpus_Decode
//       (opus_decode with room for kWebRtcOpusMaxDecodeFrameSizeMs = 120 ms),
//       WebRtcOpus_DecodeFec (opus_decode with decode_fec = 1 and the packet's
//       samples per frame), WebRtcOpus_FecDurationEst
// sm_compare's decoder map is `opus/48000/2` with no `stereo` parameter
// (compare/webrtc/webrtc_call.cc), which AudioDecoderOpus::SdpToConfig turns
// into one channel at its default 48 kHz (api/audio_codecs/opus/
// audio_decoder_opus.cc; that file is not in the reference tree).
#pragma once

#include <opus.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include "opus_encoder.h"
#include "sm/audio/sinc_resampler.h"
#include "sm/core/seq_num.h"
#include "sm/rtp/rtp_receiver.h"

namespace sm::testbench {

// AudioDecoderOpusImpl over libopus, for one mono 48 kHz stream.
class OpusReceiveDecoder {
 public:
  static constexpr int kSampleRateHz = 48'000;
  static constexpr int kChannels = 1;
  static constexpr int kMaxDecodeFrameSizeMs = 120;  // kWebRtcOpusMaxDecodeFrameSizeMs.

  OpusReceiveDecoder() {
    int error = OPUS_OK;
    dec_ = opus_decoder_create(kSampleRateHz, kChannels, &error);
    ok_ = error == OPUS_OK && dec_ != nullptr;
  }
  ~OpusReceiveDecoder() {
    if (dec_ != nullptr) opus_decoder_destroy(dec_);
  }
  OpusReceiveDecoder(const OpusReceiveDecoder&) = delete;
  OpusReceiveDecoder& operator=(const OpusReceiveDecoder&) = delete;

  bool ok() const { return ok_; }

  // WebRtcOpus_Decode for a non-empty payload; empty on a decode error.
  std::vector<int16_t> Decode(std::span<const uint8_t> payload) {
    return DecodeNative(payload, kMaxDecodeFrameSizeMs * (kSampleRateHz / 1000), 0);
  }

  // DecodeRedundantInternal: the LBRR copy of the previous frame
  // (WebRtcOpus_DecodeFec), or the payload itself when it has none (a RED
  // block). Empty on a decode error.
  std::vector<int16_t> DecodeRedundant(std::span<const uint8_t> payload) {
    if (!OpusSendEncoder::PacketHasFec(payload)) return Decode(payload);
    return DecodeNative(payload, opus_packet_get_samples_per_frame(payload.data(), kSampleRateHz), 1);
  }

  // libopus's whole decoder state, to go back to (opus_decoder_get_size: the
  // state holds offsets, not pointers into itself, so a byte copy restores it).
  std::vector<uint8_t> SaveState() const {
    if (!ok_) return {};
    const auto* p = reinterpret_cast<const uint8_t*>(dec_);
    return std::vector<uint8_t>(p, p + opus_decoder_get_size(kChannels));
  }
  void RestoreState(const std::vector<uint8_t>& state) {
    if (ok_ && state.size() == static_cast<size_t>(opus_decoder_get_size(kChannels))) {
      std::copy(state.begin(), state.end(), reinterpret_cast<uint8_t*>(dec_));
    }
  }

  // WebRtcOpus_FecDurationEst at 48 kHz: the samples of the LBRR copy, 0 if
  // there is none or its duration is out of range.
  static int FecDuration(std::span<const uint8_t> payload) {
    if (!OpusSendEncoder::PacketHasFec(payload)) return 0;
    const int samples = opus_packet_get_samples_per_frame(payload.data(), kSampleRateHz);
    const int samples_per_ms = kSampleRateHz / 1000;
    if (samples < 10 * samples_per_ms || samples > 120 * samples_per_ms) return 0;
    return samples;
  }

 private:
  // DecodeNative for the single-stream decoder.
  std::vector<int16_t> DecodeNative(std::span<const uint8_t> payload, int frame_size, int decode_fec) {
    if (!ok_ || payload.empty()) return {};
    std::vector<int16_t> out(static_cast<size_t>(frame_size) * kChannels);
    const int n = opus_decode(dec_, payload.data(), static_cast<opus_int32>(payload.size()), out.data(), frame_size, decode_fec);
    if (n <= 0) return {};
    out.resize(static_cast<size_t>(n) * kChannels);
    return out;
  }

  OpusDecoder* dec_ = nullptr;
  bool ok_ = false;
};

// The bench's audio decoder (rtp::AudioDecoder): OpusReceiveDecoder (issue
// #476), so the jitter buffer gets the decoded samples libwebrtc's NetEq gets,
// LBRR copies at their own lower quality.
//
// Our jitter buffer stores samples, so the receiver decodes each frame when it
// arrives, while NetEq decodes at playout, in timestamp order, one copy of each
// frame. libopus keeps state from frame to frame, so this keeps the sequence
// libopus sees to NetEq's:
// - a frame (RTP timestamp) is decoded once: a duplicate, the RED copy of a
//   frame already decoded, or the LBRR copy of a frame that already arrived
//   gets the samples decoded before (NetEq drops the extra copy unplayed);
// - an LBRR copy is decoded only for a frame not seen yet, as NetEq decodes it
//   in place of a frame that is missing (DecodeRedundantInternal);
// - frames are decoded in timestamp order: a frame that arrives after a later
//   one is decoded from libopus's state after the frame before it, and the
//   later frames are decoded again after it, so the state the next frame starts
//   from is the one NetEq's decoder has. (The samples of the frames it
//   overtook are already in the jitter buffer; NetEq would have decoded them
//   after it.) A primary that arrives after its LBRR copy replaces it, as NetEq
//   prefers the primary.
// libwebrtc decodes at 48 kHz; at another bench rate the 48 kHz output is
// resampled, as the playout path resamples NetEq's output to the device rate.
class OpusAudioDecoder : public rtp::AudioDecoder {
 public:
  // Frames remembered, in timestamp order: 5 s of 20 ms frames.
  static constexpr size_t kRemembered = 250;

  explicit OpusAudioDecoder(int sample_rate_hz) : sample_rate_hz_(sample_rate_hz), state_before_first_(decoder_.SaveState()) {
    if (sample_rate_hz_ != OpusReceiveDecoder::kSampleRateHz) {
      resampler_.emplace(static_cast<size_t>(OpusReceiveDecoder::kSampleRateHz / 50), static_cast<size_t>(sample_rate_hz_ / 50));
    }
  }
  std::optional<std::vector<float>> Decode(std::span<const uint8_t> frame, uint32_t rtp_timestamp) override {
    const int64_t ts = unwrap_.Unwrap(rtp_timestamp);
    if (auto it = frames_.find(ts); it != frames_.end() && it->second.primary) return it->second.samples;
    return Insert(ts, frame, true);
  }
  uint32_t FecDuration(std::span<const uint8_t> frame) const override {
    return static_cast<uint32_t>(static_cast<int64_t>(OpusReceiveDecoder::FecDuration(frame)) * sample_rate_hz_ /
                                 OpusReceiveDecoder::kSampleRateHz);
  }
  std::optional<std::vector<float>> DecodeFec(std::span<const uint8_t> frame, uint32_t rtp_timestamp) override {
    const int64_t ts = unwrap_.Unwrap(rtp_timestamp);
    if (auto it = frames_.find(ts); it != frames_.end()) return it->second.samples;
    return Insert(ts, frame, false);
  }

 private:
  struct Frame {
    std::vector<uint8_t> payload;  // The packet: the frame, or the one whose LBRR copy it is.
    bool primary = false;
    std::vector<float> samples;
    std::vector<uint8_t> state_after;  // libopus after decoding it.
  };
  std::optional<std::vector<float>> Insert(int64_t ts, std::span<const uint8_t> payload, bool primary) {
    // Not after the last frame decoded (a late frame, or the primary of a frame
    // decoded from its LBRR copy).
    const bool reordered = frames_.upper_bound(ts) != frames_.end() || frames_.count(ts) > 0;
    if (reordered) {
      // Back to the state after the frame before it (or before the first one remembered).
      auto prev = frames_.lower_bound(ts);
      decoder_.RestoreState(prev == frames_.begin() ? state_before_first_ : std::prev(prev)->second.state_after);
    }
    Frame f{std::vector<uint8_t>(payload.begin(), payload.end()), primary, {}, {}};
    const std::vector<int16_t> pcm = DecodeOne(f);
    // Older than every frame remembered, with the window full (issue #533): the
    // trim below would erase it at once. Its samples are returned, but it is not
    // kept and the later frames are not decoded again; the decoder goes back to
    // the state after the last frame.
    const bool before_window = frames_.size() >= kRemembered && ts < frames_.begin()->first;
    if (pcm.empty() || before_window) {
      if (reordered) decoder_.RestoreState(std::prev(frames_.end())->second.state_after);
      if (pcm.empty()) return std::nullopt;
      return ToSamples(pcm);
    }
    f.samples = ToSamples(pcm);
    f.state_after = decoder_.SaveState();
    auto it = frames_.insert_or_assign(ts, std::move(f)).first;
    // The later frames, decoded again for the state the next one starts from.
    for (auto later = std::next(it); later != frames_.end(); ++later) {
      DecodeOne(later->second);
      later->second.state_after = decoder_.SaveState();
    }
    while (frames_.size() > kRemembered) {
      state_before_first_ = frames_.begin()->second.state_after;
      frames_.erase(frames_.begin());
    }
    return it->second.samples;
  }

  std::vector<int16_t> DecodeOne(const Frame& f) {
    return f.primary ? decoder_.Decode(f.payload) : decoder_.DecodeRedundant(f.payload);
  }

  std::vector<float> ToSamples(const std::vector<int16_t>& pcm) {
    std::vector<float> samples(pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) samples[i] = static_cast<float>(pcm[i]) / 32767.f;  // OpusSendEncoder::ToPcm's scale.
    if (resampler_ && samples.size() == static_cast<size_t>(OpusReceiveDecoder::kSampleRateHz / 50)) {
      std::vector<float> out(static_cast<size_t>(sample_rate_hz_ / 50));
      resampler_->Resample(samples, out);
      samples = std::move(out);
    }
    return samples;
  }

  const int sample_rate_hz_;
  OpusReceiveDecoder decoder_;
  std::optional<audio::PushSincResampler> resampler_;
  SeqNumUnwrapper<uint32_t> unwrap_;
  std::map<int64_t, Frame> frames_;
  std::vector<uint8_t> state_before_first_;
};

}  // namespace sm::testbench
