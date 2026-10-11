// 8.2 Audio/video synchronization (lip sync) with CNAME sync groups.
//
// Audio and video take different paths (network, jitter buffers, decode),
// so they play out with different delays. Using each stream's RTP->NTP
// mapping (RTCP SR), the receiver maps the latest audio and video RTP
// timestamps to sender capture time and computes the relative delay:
//     relative = (video_arrival - audio_arrival) - (video_capture - audio_capture)
// plus the difference of the current playout delays. Every update the
// filtered offset is reduced by at most `max_change` (half of it per step,
// to avoid audible/visible jumps), first by removing extra delay from the
// stream that is late, then by adding extra delay to the stream that is
// early. Offsets under `tolerance` are left alone. The extra delays become
// minimum playout delays of the audio jitter buffer and the video timing.
//
// MediaSyncGroups pairs streams for synchronization: only streams of the
// same CNAME (same participant/capture clock) are synchronized.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "sm/core/event_log.h"
#include "sm/core/metrics.h"
#include "sm/sync/rtp_to_ntp.h"

namespace sm::sync {

struct StreamSyncConfig {
  TimeDelta tolerance = TimeDelta::Millis(30);
  TimeDelta max_change = TimeDelta::Millis(80);
  TimeDelta max_extra_delay = TimeDelta::Millis(10000);
  double filter = 0.25;  // EMA weight of a new offset sample.
  EventSink* events = nullptr;
};

struct SyncDelays {
  TimeDelta audio_extra;  // Minimum playout delay to add to audio.
  TimeDelta video_extra;
};

class StreamSynchronization : public MetricsSource {
 public:
  explicit StreamSynchronization(StreamSyncConfig cfg = {}) : cfg_(cfg) {}
  RtpToNtpEstimator& audio_map() { return audio_map_; }
  RtpToNtpEstimator& video_map() { return video_map_; }
  void OnAudioPacket(uint32_t rtp, Timestamp arrival);
  void OnVideoFrame(uint32_t rtp, Timestamp arrival);
  // current_*_delay: the stream's current playout delay (jitter buffer +
  // decode + render) *excluding* the extra delay this class requested.
  std::optional<SyncDelays> Update(TimeDelta current_audio_delay, TimeDelta current_video_delay, Timestamp now);
  // Last measured A/V offset: positive means video plays later than audio.
  TimeDelta last_offset() const { return last_offset_; }
  const SyncDelays& delays() const { return delays_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  StreamSyncConfig cfg_;
  RtpToNtpEstimator audio_map_, video_map_;
  std::optional<std::pair<uint32_t, Timestamp>> last_audio_, last_video_;
  double avg_offset_ms_ = 0;
  bool has_avg_ = false;
  TimeDelta last_offset_;
  SyncDelays delays_;
  uint64_t updates_ = 0;
  Histogram offset_ms_;
};

class MediaSyncGroups {
 public:
  enum class Kind { kAudio, kVideo };
  void AddStream(uint32_t ssrc, const std::string& cname, Kind kind);
  void RemoveStream(uint32_t ssrc);
  // (audio ssrc, video ssrc) per CNAME that has both.
  std::vector<std::pair<uint32_t, uint32_t>> SyncPairs() const;

 private:
  struct Info {
    std::string cname;
    Kind kind;
  };
  std::map<uint32_t, Info> streams_;
};

}  // namespace sm::sync
