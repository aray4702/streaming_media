// 8.2 Audio/video synchronization (lip sync), as libwebrtc M157's
// StreamSynchronization (video/stream_synchronization.*) and
// RtpStreamsSynchronizer (video/rtp_streams_synchronizer2.*, call/syncable.h).
//
// Audio and video take different paths (network, jitter buffers, decode),
// so they play out with different delays. Using each stream's RTP->NTP
// mapping (RTCP SR), the receiver maps the RTP timestamps of the latest
// received audio and video packets to sender capture time and computes the
// relative delay (ComputeRelativeDelay):
//     relative = (video_receive - audio_receive) - (video_capture - audio_capture)
// in integer milliseconds, with no update beyond +-`kMaxDeltaDelayMs` 10 s.
// ComputeDelays adds the difference of the current video target delay and
// the current audio delay, filters it as `avg = (3 * avg + cur) / 4`
// (integer, from 0), and does nothing while |avg| < `kMinDeltaMs` 30 ms.
// Otherwise it moves half of it, at most `kMaxChangeMs` 80 ms, resets the
// average to 0, and changes one stream at a time: video late -> remove
// extra video delay if any above the base target, else add audio delay;
// video early -> remove extra audio delay if any, else add video delay. The
// outputs are minimum total playout delays for both streams, each capped
// at base + 10 s, video never below the base.
//
// RtpStreamsSynchronizer runs that every `kSyncInterval` 1 s once a video
// stream is configured with an audio stream: it reads both Syncables'
// Info, feeds their SRs to the two RtpToNtpEstimators, skips the run when
// either has no new packet since the last one, and applies the targets with
// SetMinimumPlayoutDelay; a failed set lowers that stream's extra delay by
// 10%. GetStreamSyncOffsetInMs is libwebrtc's A/V offset stat. The library
// reads no clock: the caller passes `now` and calls Process.
//
// MediaSyncGroups pairs receive streams for synchronization as libwebrtc's
// Call::ConfigureSync (call/call.cc:1300-1341): only streams of the same
// sync group (the MediaStream id; one participant's capture clock) are
// synchronized, and a stream with an empty sync group never is. In a group
// the first audio stream (FindAudioStreamForSyncGroup) is paired with the
// first video stream; further video streams of the group get no audio
// (bugs.webrtc.org/4762: one A/V pair per group). libwebrtc walks its
// std::set of stream pointers, whose order is the allocator's; here the
// order is the order the streams were added. The pairs are recomputed on
// every change, so removing the paired audio or video stream pairs the next
// one of the group, and moving an audio stream to another group
// (Call::OnUpdateSyncGroup, AddStream again) re-pairs both groups.
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

class StreamSynchronization {
 public:
  static constexpr int kMaxChangeMs = 80;
  static constexpr int kMaxDeltaDelayMs = 10000;
  static constexpr int kFilterLength = 4;
  static constexpr int kMinDeltaMs = 30;  // Minimum A/V difference to act on.

  struct Measurements {
    RtpToNtpEstimator rtp_to_ntp;
    int64_t latest_receive_time_ms = 0;
    uint32_t latest_timestamp = 0;
  };
  struct DelayTargets {
    int total_audio_delay_target_ms = 0;
    int total_video_delay_target_ms = 0;
  };

  StreamSynchronization(uint32_t video_stream_id, uint32_t audio_stream_id)
      : video_stream_id_(video_stream_id), audio_stream_id_(audio_stream_id) {}
  // libwebrtc's ComputeDelays with `*total_video_delay_target_ms` passed in
  // as `current_video_delay_ms`; nullopt where it returns false.
  std::optional<DelayTargets> ComputeDelays(int relative_delay_ms, int current_audio_delay_ms, int current_video_delay_ms);
  // How many ms later video is rendered relative to audio (negative when
  // audio plays later); nullopt without both estimates or beyond +-10 s.
  static std::optional<int> ComputeRelativeDelay(const Measurements& audio, const Measurements& video);
  // Audio and video are delayed by at least `target_delay_ms`.
  void SetTargetBufferingDelay(int target_delay_ms);
  void ReduceAudioDelay();  // Extra audio delay x 0.9, to recover from errors.
  void ReduceVideoDelay();
  uint32_t audio_stream_id() const { return audio_stream_id_; }
  uint32_t video_stream_id() const { return video_stream_id_; }

 private:
  struct SynchronizationDelays {
    int extra_ms = 0;
    int last_ms = 0;
  };
  const uint32_t video_stream_id_;
  const uint32_t audio_stream_id_;
  SynchronizationDelays audio_delay_;
  SynchronizationDelays video_delay_;
  int base_target_delay_ms_ = 0;
  int avg_diff_ms_ = 0;
};

// call/syncable.h: what a receive stream gives the synchronizer.
class Syncable {
 public:
  struct Info {
    Timestamp latest_receive_time = Timestamp::Micros(0);  // Of the last RTP packet.
    uint32_t latest_received_capture_rtp_timestamp = 0;
    uint32_t capture_time_rtp = 0;  // From the last SR received.
    timing::NtpTime capture_time_ntp;
    TimeDelta current_delay;  // Current playout delay.
  };
  struct PlayoutInfo {
    Timestamp time;
    uint32_t rtp_timestamp = 0;
  };
  virtual ~Syncable() = default;
  virtual uint32_t id() const = 0;
  virtual std::optional<Info> GetInfo() const = 0;
  virtual std::optional<PlayoutInfo> GetPlayoutRtpTimestamp() const = 0;
  virtual bool SetMinimumPlayoutDelay(TimeDelta delay) = 0;
  virtual void SetEstimatedPlayoutNtpTimestamp(timing::NtpTime ntp, Timestamp time) = 0;
};

class RtpStreamsSynchronizer : public MetricsSource {
 public:
  static constexpr TimeDelta kSyncInterval = TimeDelta::Millis(1000);

  struct StreamSyncOffset {
    int64_t video_playout_ntp_ms = 0;
    int64_t stream_offset_ms = 0;  // Audio playout NTP minus video's.
    double estimated_freq_khz = 0;
  };

  explicit RtpStreamsSynchronizer(Syncable* syncable_video, EventSink* events = nullptr)
      : syncable_video_(syncable_video), events_(events) {}
  // Pairs the video stream with `syncable_audio` (nullptr stops syncing).
  // The first update runs kSyncInterval after `now`.
  void ConfigureSync(Syncable* syncable_audio, Timestamp now);
  // Runs UpdateDelay when the 1 s repeating task is due. The next run is
  // timed from the scheduled time, as RepeatingTaskHandle does (#902), so
  // NextProcessTime can be at or before `now` after a late call.
  void Process(Timestamp now);
  std::optional<Timestamp> NextProcessTime() const { return next_run_; }
  // libwebrtc's UpdateDelay (one run of the repeating task).
  void UpdateDelay(Timestamp now);
  std::optional<StreamSyncOffset> GetStreamSyncOffsetInMs(uint32_t rtp_timestamp, int64_t render_time_ms,
                                                          Timestamp now) const;
  uint64_t updates() const { return updates_; }  // Runs that computed new delay targets.
  const StreamSynchronization::DelayTargets& last_targets() const { return last_targets_; }
  void ExportMetrics(MetricsWriter& w) const override;

 private:
  Syncable* const syncable_video_;
  EventSink* events_;
  Syncable* syncable_audio_ = nullptr;
  std::optional<StreamSynchronization> sync_;
  StreamSynchronization::Measurements audio_measurement_;
  StreamSynchronization::Measurements video_measurement_;
  std::optional<Timestamp> next_run_;
  uint64_t runs_ = 0, updates_ = 0, failed_sets_ = 0;
  int last_relative_delay_ms_ = 0;
  StreamSynchronization::DelayTargets last_targets_;
};

class MediaSyncGroups {
 public:
  enum class Kind { kAudio, kVideo };
  // Adds a receive stream, or moves an added one to `sync_group` (it keeps its place in the order).
  void AddStream(uint32_t ssrc, const std::string& sync_group, Kind kind);
  void RemoveStream(uint32_t ssrc);
  // (audio ssrc, video ssrc) per sync group that has both, in the order of the groups' first video streams.
  std::vector<std::pair<uint32_t, uint32_t>> SyncPairs() const;

 private:
  struct Stream {
    uint32_t ssrc;
    std::string sync_group;
    Kind kind;
  };
  std::vector<Stream> streams_;  // In the order they were added.
};

}  // namespace sm::sync
