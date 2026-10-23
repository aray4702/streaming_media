#include "sm/loss/fec_controller.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "fec_rate_table.h"

namespace sm::loss {

namespace {

// libwebrtc's kPacketLossMax (media_opt_util.cc): loss rates of the table go up to 128/255.
constexpr int kPacketLossMax = 129;
// VCMFecMethod's limits (media_opt_util.h).
constexpr int kUpperLimitFramesFec = 6;
constexpr int kMaxBytesPerFrameForFec = 700;
constexpr int kMaxBytesPerFrameForFecLow = 400;
constexpr int kMaxBytesPerFrameForFecHigh = 1000;
// VCMProtectionMethod::_maxPayloadSize and _scaleProtKey: constants of the method, not the
// controller's max_payload_size_.
constexpr int32_t kMethodMaxPayloadSize = 1460;
constexpr float kScaleProtKey = 2.0f;
// SimulcastRateAllocator::GetTemporalRateAllocation, the base layer's share
// (WebRTC-VP8-BaseHeavyTl3Alloc off, its default).
constexpr float kBaseLayerRateAllocation[4] = {1.0f, 0.6f, 0.4f, 0.25f};

// rtc::saturated_cast from a floating-point value: NaN gives 0, values out of
// range clamp, the rest truncate toward zero.
template <typename T, typename F>
T SaturatedCast(F v) {
  if (std::isnan(v)) return 0;
  if (v <= static_cast<F>(std::numeric_limits<T>::lowest())) return std::numeric_limits<T>::lowest();
  if (v >= static_cast<F>(std::numeric_limits<T>::max())) return std::numeric_limits<T>::max();
  return static_cast<T>(v);
}

// VCMFecMethod::ConvertFECRate: from a share of all packets (media + FEC) to a
// share of the media packets, which the RTP module's FEC takes.
uint8_t ConvertFecRate(uint8_t code_rate_rtp) {
  return SaturatedCast<uint8_t>(
      std::min<double>(255, (0.5 + 255.0 * code_rate_rtp / static_cast<float>(255 - code_rate_rtp))));
}

}  // namespace

void FecControllerDefault::ExpFilter::Apply(float exp, float sample) {
  if (filtered == -1.0f) {
    filtered = sample;
  } else if (exp == 1.0f) {
    filtered = alpha * filtered + (1 - alpha) * sample;
  } else {
    const float a = std::pow(alpha, exp);
    filtered = a * filtered + (1 - a) * sample;
  }
}

FecControllerDefault::FecControllerDefault(EventSink* events) : events_(events) {}

void FecControllerDefault::SetProtectionMethod(bool enable_fec, bool enable_nack) {
  ProtectionMethod m = ProtectionMethod::kNone;
  if (enable_fec && enable_nack) {
    m = ProtectionMethod::kNackFec;
  } else if (enable_nack) {
    m = ProtectionMethod::kNack;
  } else if (enable_fec) {
    m = ProtectionMethod::kFec;
  }
  // VCMLossProtectionLogic::SetMethod: the same type keeps its state; another starts afresh.
  if (m == method_) return;
  method_ = m;
  protection_factor_k_ = protection_factor_d_ = 0;
  max_frames_fec_ = 1;
}

void FecControllerDefault::SetEncodingData(size_t width, size_t height, size_t num_temporal_layers,
                                           size_t max_payload_size) {
  codec_width_ = width;
  codec_height_ = height;
  num_layers_ = num_temporal_layers == 0 ? 1 : static_cast<int>(num_temporal_layers);
  max_payload_size_ = max_payload_size;
}

void FecControllerDefault::UpdateWithEncodedData(size_t encoded_size, bool keyframe, Timestamp now) {
  if (encoded_size == 0 || max_payload_size_ == 0) return;
  const float min_packets_per_frame = static_cast<float>(encoded_size) / static_cast<float>(max_payload_size_);
  const int64_t now_ms = now.ms();
  if (!keyframe) {
    packets_per_frame_.Apply(static_cast<float>(now_ms - last_packets_per_frame_update_ms_), min_packets_per_frame);
    last_packets_per_frame_update_ms_ = now_ms;
  } else {
    packets_per_frame_key_.Apply(static_cast<float>(now_ms - last_packets_per_frame_key_update_ms_),
                                 min_packets_per_frame);
    last_packets_per_frame_key_update_ms_ = now_ms;
  }
}

void FecControllerDefault::UpdateMaxLossHistory(uint8_t loss_pr255, int64_t now) {
  if (loss_history_[0].time_ms >= 0 && now - loss_history_[0].time_ms < kLossPrShortFilterWinMs) {
    if (loss_pr255 > short_max_loss_pr255_) short_max_loss_pr255_ = loss_pr255;
    return;
  }
  // Only add a new value to the history once a second.
  if (loss_history_[0].time_ms == -1) {
    short_max_loss_pr255_ = loss_pr255;  // First, no shift.
  } else {
    for (int i = kLossPrHistorySize - 2; i >= 0; --i) loss_history_[i + 1] = loss_history_[i];
  }
  if (short_max_loss_pr255_ == 0) short_max_loss_pr255_ = loss_pr255;
  loss_history_[0].loss_pr255 = short_max_loss_pr255_;
  loss_history_[0].time_ms = now;
  short_max_loss_pr255_ = 0;
}

uint8_t FecControllerDefault::MaxFilteredLossPr(int64_t now_ms) const {
  uint8_t max_found = short_max_loss_pr255_;
  if (loss_history_[0].time_ms == -1) return max_found;
  for (const LossSample& s : loss_history_) {
    if (s.time_ms == -1) break;
    // This sample (and all after it) is too old.
    if (now_ms - s.time_ms > kLossPrHistorySize * kLossPrShortFilterWinMs) break;
    max_found = std::max(max_found, s.loss_pr255);
  }
  return max_found;
}

int FecControllerDefault::BitsPerFrame(const Params& p) const {
  // With temporal layers FEC protects the base layer only.
  const float bit_rate_ratio = kBaseLayerRateAllocation[std::clamp(p.num_layers, 1, 4) - 1];
  const float frame_rate_ratio = std::pow(1 / 2.0f, static_cast<float>(p.num_layers - 1));
  const float bit_rate = p.bit_rate * bit_rate_ratio;
  float frame_rate = p.frame_rate * frame_rate_ratio;
  if (frame_rate < 1.0f) frame_rate = 1.0f;
  // Average bits per frame, in kbit.
  return SaturatedCast<int>(1.0f * bit_rate / frame_rate);
}

void FecControllerDefault::FecProtectionFactor(const Params& p) {
  // No protection if the (filtered) packet loss is 0.
  uint8_t packet_loss = SaturatedCast<uint8_t>(255 * p.loss_pr);
  if (packet_loss == 0) {
    protection_factor_k_ = 0;
    protection_factor_d_ = 0;
    return;
  }
  // First partition protection: ~20%.
  const uint8_t first_partition_prot = SaturatedCast<uint8_t>(255 * 0.20);
  // Thresholds on the loss and the packets per frame above which the first partition is covered.
  const uint8_t loss_thr = 0;
  const uint8_t packet_num_thr = 1;
  // Range of the rate index of the table.
  const uint8_t rate_par1 = 5;
  const uint8_t rate_par2 = 49;
  // Spatial resolution, relative to 704x576, softened by the exponent 0.3.
  const float spatial_size_to_ref =
      static_cast<float>(p.codec_width * p.codec_height) / static_cast<float>(704 * 576);
  const float resoln_fac = static_cast<float>(1.0 / std::pow(spatial_size_to_ref, 0.3f));
  const int bit_rate_per_frame = BitsPerFrame(p);
  // Average number of packets per frame (source and FEC).
  const uint8_t avg_tot_packets = SaturatedCast<uint8_t>(
      1.5f + static_cast<float>(bit_rate_per_frame) * 1000.0f / static_cast<float>(8.0 * kMethodMaxPayloadSize));

  // The table index: an effective rate, ~200 to ~8000 kbps at 30 fps.
  const uint16_t eff_rate_fec_table = SaturatedCast<uint16_t>(resoln_fac * static_cast<float>(bit_rate_per_frame));
  uint8_t rate_index_table = static_cast<uint8_t>(
      std::max(std::min((eff_rate_fec_table - rate_par1) / rate_par1, static_cast<int>(rate_par2)), 0));
  // The tables are defined up to 50% loss.
  if (packet_loss >= kPacketLossMax) packet_loss = kPacketLossMax - 1;
  const uint16_t index_table = static_cast<uint16_t>(rate_index_table * kPacketLossMax + packet_loss);
  // Protection factor for delta frames.
  uint8_t code_rate_delta = internal::kFecRateTable[index_table];
  // A minimum based on the first partition size.
  if (packet_loss > loss_thr && avg_tot_packets > packet_num_thr && code_rate_delta < first_partition_prot) {
    code_rate_delta = first_partition_prot;
  }
  // At most 50% for delta frames.
  if (code_rate_delta >= kPacketLossMax) code_rate_delta = kPacketLossMax - 1;

  // Key frames: effectively a higher rate, so the rate is boosted
  // (BoostCodeRateKey: the key/delta packet ratio, at least 2).
  const uint8_t packet_frame_delta = SaturatedCast<uint8_t>(0.5f + p.packets_per_frame);
  const uint8_t packet_frame_key = SaturatedCast<uint8_t>(0.5f + p.packets_per_frame_key);
  uint8_t ratio = 1;
  if (packet_frame_delta > 0) ratio = static_cast<uint8_t>(static_cast<int8_t>(packet_frame_key / packet_frame_delta));
  const uint8_t boost_key = std::max<uint8_t>(2, ratio);
  rate_index_table = SaturatedCast<uint8_t>(static_cast<float>(std::max(
      std::min(1 + (boost_key * eff_rate_fec_table - rate_par1) / rate_par1, static_cast<int>(rate_par2)), 0)));
  const uint16_t index_table_key = std::min<uint16_t>(
      static_cast<uint16_t>(rate_index_table * kPacketLossMax + packet_loss),
      static_cast<uint16_t>(internal::kFecRateTableSize - 1));
  uint8_t code_rate_key = internal::kFecRateTable[index_table_key];
  int boost_key_prot = static_cast<int>(kScaleProtKey * code_rate_delta);
  if (boost_key_prot >= kPacketLossMax) boost_key_prot = kPacketLossMax - 1;
  // Key frames get at least the delta protection and at least the filtered loss.
  code_rate_key = SaturatedCast<uint8_t>(
      static_cast<float>(std::max<int>(packet_loss, std::max<int>(boost_key_prot, code_rate_key))));
  // At most 50% for key frames.
  if (code_rate_key >= kPacketLossMax) code_rate_key = kPacketLossMax - 1;

  protection_factor_k_ = code_rate_key;
  protection_factor_d_ = code_rate_delta;
}

bool FecControllerDefault::BitRateTooLowForFec(const Params& p) const {
  // A threshold on bytes per frame, with some effect of the frame size.
  const int estimate_bytes_per_frame = 1000 * BitsPerFrame(p) / 8;
  int max_bytes_per_frame = kMaxBytesPerFrameForFec;
  const int num_pixels = p.codec_width * p.codec_height;
  if (num_pixels <= 352 * 288) {
    max_bytes_per_frame = kMaxBytesPerFrameForFecLow;
  } else if (num_pixels > 640 * 480) {
    max_bytes_per_frame = kMaxBytesPerFrameForFecHigh;
  }
  constexpr int64_t kMaxRttTurnOffFec = 200;
  return estimate_bytes_per_frame < max_bytes_per_frame && p.num_layers < 3 && p.rtt < kMaxRttTurnOffFec;
}

int FecControllerDefault::ComputeMaxFramesFec(const Params& p) const {
  // Beyond 2 temporal layers FEC protects the base layer only, whose frames are far apart.
  if (p.num_layers > 2) return 1;
  // On average, complete frames within one RTT.
  const float base_layer_framerate = p.frame_rate / static_cast<float>(1 << (p.num_layers - 1));
  const int max_frames_fec = std::max(
      SaturatedCast<int>(2.0f * base_layer_framerate * static_cast<float>(p.rtt) / 1000.0f + 0.5f), 1);
  return std::min(max_frames_fec, kUpperLimitFramesFec);
}

void FecControllerDefault::UpdateMethod(const Params& p) {
  switch (method_) {
    case ProtectionMethod::kNone:
    case ProtectionMethod::kNack:
      // VCMNackMethod computes no protection factors.
      return;
    case ProtectionMethod::kFec:
      FecProtectionFactor(p);
      break;
    case ProtectionMethod::kNackFec:
      // VCMNackFecMethod::ProtectionFactor: NACK alone below kLowRttNackMs; above
      // it the delta rate is kept (the RTT adjustment is 1.0, no high threshold).
      FecProtectionFactor(p);
      if (p.rtt < kLowRttNackMs) protection_factor_d_ = 0;
      max_frames_fec_ = ComputeMaxFramesFec(p);
      if (BitRateTooLowForFec(p)) {
        protection_factor_k_ = 0;
        protection_factor_d_ = 0;
      }
      break;
  }
  // The factors above are shares of all packets; the RTP module's FEC takes a share of the media packets.
  protection_factor_k_ = ConvertFecRate(protection_factor_k_);
  protection_factor_d_ = ConvertFecRate(protection_factor_d_);
}

std::optional<FecProtectionRequest> FecControllerDefault::UpdateFecRates(DataRate estimated_bitrate,
                                                                         int actual_framerate_fps,
                                                                         uint8_t fraction_lost,
                                                                         int64_t round_trip_time_ms, Timestamp now) {
  ++updates_;
  const float target_bitrate_kbps = static_cast<float>(estimated_bitrate.bps()) / 1000.0f;
  if (actual_framerate_fps < 1) actual_framerate_fps = 1;
  // VCMLossProtectionLogic::FilteredLoss with kMaxFilter.
  const int64_t now_ms = now.ms();
  UpdateMaxLossHistory(fraction_lost, now_ms);
  filtered_loss_ = MaxFilteredLossPr(now_ms);
  if (method_ == ProtectionMethod::kNone) return std::nullopt;

  Params p;
  p.rtt = round_trip_time_ms;
  // UpdateFilteredLossPr: in float, as libwebrtc stores it.
  p.loss_pr = static_cast<float>(static_cast<double>(static_cast<float>(filtered_loss_)) / 255.0);
  p.bit_rate = target_bitrate_kbps;
  p.frame_rate = static_cast<float>(actual_framerate_fps);
  p.packets_per_frame = packets_per_frame_.filtered;
  p.packets_per_frame_key = packets_per_frame_key_.filtered;
  p.codec_width = static_cast<uint16_t>(codec_width_);
  p.codec_height = static_cast<uint16_t>(codec_height_);
  p.num_layers = num_layers_;
  UpdateMethod(p);

  FecProtectionRequest r;
  r.key.fec_rate = protection_factor_k_;
  r.delta.fec_rate = protection_factor_d_;
  // The RTP module needs the same max_fec_frames for key and delta frames. Only kNackFec computes it.
  const int max_frames = method_ == ProtectionMethod::kNackFec ? max_frames_fec_ : 1;
  r.delta.max_fec_frames = r.key.max_fec_frames = max_frames;
  // Random masks: there is no feedback on how correlated the losses are.
  r.delta.fec_mask_type = r.key.fec_mask_type = ulpfec::FecMaskType::kRandom;
  if (r.delta.fec_rate > 0 || r.key.fec_rate > 0) ++requests_with_fec_;
  if (r.delta.fec_rate != last_.delta.fec_rate || r.key.fec_rate != last_.key.fec_rate) {
    SM_EVENT(events_, now, "protection", "fec_rate", r.delta.fec_rate, r.key.fec_rate);
  }
  last_ = r;
  return r;
}

void FecControllerDefault::ExportMetrics(MetricsWriter& w) const {
  w.Counter("updates", updates_);
  w.Counter("updates_with_fec", requests_with_fec_);
  w.Gauge("filtered_loss", filtered_loss_ / 255.0);
  w.Gauge("fec_protection", last_.delta.fec_rate / 256.0);
  w.Gauge("fec_protection_key", last_.key.fec_rate / 256.0);
  w.Gauge("max_fec_frames", last_.delta.max_fec_frames);
}

}  // namespace sm::loss
