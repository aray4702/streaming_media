// 3.7 Protection selection: how much video FEC to send, as libwebrtc M157's
// FecControllerDefault (modules/video_coding/fec_controller_default.cc) with
// media_opt_util.cc's VCMLossProtectionLogic and protection methods, read
// from compare/webrtc/reference/ (issue #29).
//
// - The method is fixed by what the stream has on (SetProtectionMethod, as
//   RtpVideoSender's constructor calls it): kNackFec with NACK and FEC,
//   kFec with FEC alone, kNack with NACK alone, kNone with neither. Only the
//   FEC methods ask for FEC; kNone makes no protection request at all.
// - The loss is a max filter over 10 s (kLossPrHistorySize 10 windows of
//   kLossPrShortFilterWinMs 1000 ms) of the fraction lost the network
//   estimate carries.
// - The delta-frame code rate comes from kFecRateTable (fec_rate_table.h),
//   indexed by the effective bits per frame (scaled by the resolution against
//   704x576) and the filtered loss in 1/255, at least 20% (first partition)
//   once there is loss and more than one packet per frame, at most 128/255.
//   Key frames get the larger of the table at the boosted rate, twice the
//   delta rate and the loss itself, also capped at 128/255.
// - kNackFec drops the delta rate to 0 below kLowRttNackMs (20 ms RTT) and
//   keeps it above (the RTT adjustment is the constant 1.0; NACK is never
//   switched off), turns FEC off altogether when a frame is smaller than
//   400/700/1000 bytes (by resolution) with an RTT under 200 ms
//   (BitRateTooLowForFec), and spreads FEC over max(2 * fps * RTT + 0.5, 1)
//   frames, at most 6 (kUpperLimitFramesFec). kFec uses one frame.
// - The rates are converted from "of all packets" to "of the media packets"
//   (ConvertFECRate), the Q8 rate ULPFEC's generator takes, and the masks are
//   always random.
//
// The other half of FecControllerDefault::UpdateFecRates, the encoder rate
// (the target less the measured RTX and FEC share, capped at 0.5), is
// rtp::SendSideController::AllocatedVideoTarget.
// Takes `now`, reads no clock and starts no thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/core/units.h"
#include "sm/loss/ulpfec.h"

namespace sm::loss {

// media_optimization::VCMProtectionMethodEnum.
enum class ProtectionMethod : uint8_t { kNack, kFec, kNackFec, kNone };

// What FecControllerDefault hands to VCMProtectionCallback::ProtectionRequest.
struct FecProtectionRequest {
  ulpfec::FecProtectionParams delta;
  ulpfec::FecProtectionParams key;
};

class FecControllerDefault : public MetricsSource {
 public:
  // media_optimization::kLowRttNackMs, the kNackFec method's low threshold.
  static constexpr int64_t kLowRttNackMs = 20;

  explicit FecControllerDefault(EventSink* events = nullptr);

  // SetProtectionMethod: kNackFec, kNack, kFec or kNone.
  void SetProtectionMethod(bool enable_fec, bool enable_nack);
  ProtectionMethod method() const { return method_; }
  // SetEncodingData, on each encoder (re)configuration: the resolution, the
  // temporal layers and RtpConfig::max_packet_size. Until the first call:
  // 704x576, one layer, 1460 bytes.
  void SetEncodingData(size_t width, size_t height, size_t num_temporal_layers, size_t max_payload_size);
  // UpdateWithEncodedData, for each encoded frame: the packets per frame
  // (size / max payload) of delta and key frames, filtered with
  // ExpFilter(0.9999) over the ms since the previous frame of that type.
  void UpdateWithEncodedData(size_t encoded_size, bool keyframe, Timestamp now);
  // UpdateFecRates, on each network estimate change (RtpVideoSender::
  // OnBitrateUpdated): the video payload rate, the sent frame rate, the
  // fraction lost (0-255) and the RTT. Returns the protection request, or
  // nullopt for kNone (no request is made).
  std::optional<FecProtectionRequest> UpdateFecRates(DataRate estimated_bitrate, int actual_framerate_fps,
                                                     uint8_t fraction_lost, int64_t round_trip_time_ms, Timestamp now);

  // VCMLossProtectionLogic state, for tests and metrics.
  uint8_t filtered_loss() const { return filtered_loss_; }
  float packets_per_frame() const { return packets_per_frame_.filtered; }
  float packets_per_frame_key() const { return packets_per_frame_key_.filtered; }
  int max_frames_fec() const { return max_frames_fec_; }
  const FecProtectionRequest& last() const { return last_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  // rtc::ExpFilter (rtc_base/numerics/exp_filter.cc).
  struct ExpFilter {
    float alpha = 0.9999f;
    float filtered = -1.0f;  // kValueUndefined.
    void Apply(float exp, float sample);
  };
  // VCMProtectionParameters.
  struct Params {
    int64_t rtt = 0;
    float loss_pr = 0;
    float bit_rate = 0;
    float packets_per_frame = 0;
    float packets_per_frame_key = 0;
    float frame_rate = 0;
    uint16_t codec_width = 0;
    uint16_t codec_height = 0;
    int num_layers = 1;
  };
  // VCMFecMethod::ProtectionFactor and BitsPerFrame; VCMNackFecMethod's.
  void FecProtectionFactor(const Params& p);
  int BitsPerFrame(const Params& p) const;
  bool BitRateTooLowForFec(const Params& p) const;
  int ComputeMaxFramesFec(const Params& p) const;
  // VCMLossProtectionLogic::UpdateMethod with the selected method's UpdateParameters.
  void UpdateMethod(const Params& p);
  // VCMLossProtectionLogic::UpdateMaxLossHistory and MaxFilteredLossPr.
  void UpdateMaxLossHistory(uint8_t loss_pr255, int64_t now_ms);
  uint8_t MaxFilteredLossPr(int64_t now_ms) const;

  static constexpr int kLossPrHistorySize = 10;
  static constexpr int kLossPrShortFilterWinMs = 1000;
  struct LossSample {
    uint8_t loss_pr255 = 0;
    int64_t time_ms = -1;
  };

  EventSink* events_;
  ProtectionMethod method_ = ProtectionMethod::kNone;
  size_t max_payload_size_ = 1460;
  size_t codec_width_ = 704, codec_height_ = 576;
  int num_layers_ = 1;
  ExpFilter packets_per_frame_, packets_per_frame_key_;
  int64_t last_packets_per_frame_update_ms_ = 0, last_packets_per_frame_key_update_ms_ = 0;
  LossSample loss_history_[kLossPrHistorySize];
  uint8_t short_max_loss_pr255_ = 0;
  uint8_t filtered_loss_ = 0;
  // The selected method's protection factors (VCMProtectionMethod) and max frames.
  uint8_t protection_factor_k_ = 0, protection_factor_d_ = 0;
  int max_frames_fec_ = 1;
  FecProtectionRequest last_;
  uint64_t updates_ = 0, requests_with_fec_ = 0;
};

}  // namespace sm::loss
