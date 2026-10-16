# Send-side media controller: API note (issue #37, PR 1 of 2)

Status: **proposal, for the Owner to merge.** No code changes in this PR.
The behaviour-preserving move (PR 2) follows once this note is accepted.

Decision (Owner, 2026-10-15): the library owns encoder-side adaptation as
well as RTP/RTCP, as `VideoStreamEncoder` does in libwebrtc. The app runs the
codec and nothing else. Library conventions apply: `now` is passed in, no
threads, no callbacks, no clock reads, pull-based.

libwebrtc sources are cited from `compare/webrtc/reference/` (M157).

## 1. Where the logic lives today

| Concern | Today | Owner of the code |
|---|---|---|
| Split of the target into audio, video, protection | `rtp::SendSideController::ComputeMeasuredRates()` (`SendRates`) | library |
| Congestion-window pushback: target cut and frame-drop interval | `SendSideController::ApplyPushback()`, `DropCapturedFrame()` | library |
| Outage pause / resume / keyframe | `ControllerActions`, `kEncoderPause`, `kKeyframeRequested` | library |
| Frame dropper | `cc::FrameDropper`, driven from `testbench/call.cc` (`SetRates`, `ShouldDrop`, `OnEncodedFrame`) | **app glue** |
| Keyframe on request or periodically | `keyframe_needed`, `periodic` in `call.cc` | **app glue** |
| Encoder overshoot correction | `cc::EncoderOvershootDetector` (unused by `call.cc`) | library, unwired |
| Quality scaler, CPU overuse, degradation ladder | `video::QualityScaler`, `cc::CpuOveruseDetector`, `cc::DegradationController` (unused by `call.cc`) | library, unwired |
| Audio vs video allocation | `cc::BitrateAllocator` (unused by the session) | library, unwired |

## 2. libwebrtc mapping

| libwebrtc | Source | Controller piece |
|---|---|---|
| `VideoStreamEncoder::OnFrame` (cwnd drop, `cwnd_frame_counter_ % interval`) | `video/video_stream_encoder.cc` ~1635 | `OnCapturedFrame()`, step 1 |
| `VideoStreamEncoder::UpdateTargetBitrate` (cwnd interval = `max(2, target / reduce)`, target cut by `1/interval`, min-bitrate floor) | same, ~2430 | already ported as `ApplyPushback()` (#56); the controller consumes `SendRates` |
| `MaybeEncodeVideoFrame`: `frame_dropper_.Leak(fps)`, `DropFrame()`, `DropDueToSize`, initial frame drop | same, ~1882-2000 | `OnCapturedFrame()`, steps 2-3 |
| `FrameDropper` (`Leak`, `Fill`, `SetRates`, accumulator cap of 3 s) | `modules/video_coding/utility/frame_dropper.cc` | `cc::FrameDropper`; the port of its exact arithmetic is #5, not this issue |
| `OnBitrateUpdated`: `frame_dropper_.SetRates`, `UpdateBitrateAllocation`, `SetEncoderRates` | `video/video_stream_encoder.cc` ~2459 | `OnRates()` |
| `EncoderBitrateAdjuster` and `EncoderOvershootDetector` | `video/encoder_bitrate_adjuster.*`, `video/encoder_overshoot_detector.*` | the rate in `EncodeDecision::target` |
| `RunPostEncode`: `frame_dropper_.Fill`, resource manager, adjuster | `video/video_stream_encoder.cc` ~2613 | `OnEncoded()` |
| `QualityScalerResource`, `EncodeUsageResource` (`OveruseFrameDetector`), `VideoStreamEncoderResourceManager`, `BalancedConstraint`, `BitrateConstraint` | `video/adaptation/*` | `video::QualityScaler`, `cc::CpuOveruseDetector`, `cc::DegradationController`, driven from `OnEncoded()` |
| `SendKeyFrame` / `next_frame_types_` | `video/video_stream_encoder.cc` ~2204 | `EncodeDecision::keyframe` |
| `RtpVideoSender::OnBitrateUpdated` (`FecControllerDefault::UpdateFecRates`, post-encode overhead) | `call/rtp_video_sender.cc` ~853 | `SendRates` (existing, #21) |
| `BitrateAllocator` (`call/bitrate_allocator.cc`) | **not in `compare/webrtc/reference/`** | `cc::BitrateAllocator`; see section 7 |
| `GoogCcNetworkController` target and `CongestionWindowPushback` | `call/rtp_transport_controller_send.cc` | `SendSideController` (existing) |

## 3. API

A new `sm::rtp::SendController` (header `include/sm/rtp/send_controller.h`),
one per video stream, owned by the app next to the encoder and attached to a
`SendSideController` (not owned). It composes the existing modules; it adds no
new algorithm.

```cpp
struct SendControllerConfig {
  cc::FrameDropperConfig dropper;
  cc::DegradationConfig degradation;        // input format, preference, limits
  video::QualityScalerConfig quality;       // thresholds per codec
  cc::CpuOveruseConfig cpu;
  DataRate audio_min, audio_max;            // Opus range, for allocation
  TimeDelta keyframe_interval;              // periodic IDR; zero = off
  EventSink* events = nullptr;
};

struct CapturedFrame { Timestamp capture_time; int width, height; };

struct EncodeDecision {
  enum class Action { kEncode, kDrop } action;
  DropReason reason;       // kPaused, kCongestionWindow, kFrameDropper, kSize
  bool keyframe;
  DataRate target;         // rate to configure the encoder with
  int width, height;       // resolution to encode at (a change implies reconfigure)
  double fps;
};

struct EncodedFrameReport {
  DataSize size; bool keyframe; int qp /* -1 if unknown */;
  TimeDelta encode_time;   // zero if unknown (CPU overuse needs it)
  bool failed = false;
};

class SendController : public MetricsSource {
 public:
  SendController(SendControllerConfig, SendSideController& send_side);

  // Per captured frame, before encoding.
  EncodeDecision OnCapturedFrame(const CapturedFrame&, Timestamp now);
  // After encoding (or after the encoder rejected the frame).
  void OnEncoded(const EncodedFrameReport&, Timestamp now);
  // The app saw SessionEventType::kKeyframeRequested (PLI, FIR, outage monitor).
  void RequestKeyframe();
  // Audio: bitrate to configure the Opus encoder with.
  DataRate AudioBitrate() const;
};
```

Contract:

- The controller reads `send_side.ComputeMeasuredRates()` on each
  `OnCapturedFrame()`; the app no longer calls `rates()` for the video rate.
- Pause and resume come from the session's events: the controller consumes
  `ControllerActions` itself, so the app handles neither `kEncoderPause`,
  `kEncoderResume` nor the outage monitor's keyframe request.
  `kKeyframeRequested` from a peer PLI/FIR is still reported through
  `RequestKeyframe()` because the app owns the session event loop.
- An `OnCapturedFrame()` that returns `kDrop` must not be followed by
  `OnEncoded()`. Dropped frames feed the quality scaler's drop ratio.
- A keyframe decision is sticky until `OnEncoded({.keyframe = true})`, so a
  failed encode does not lose the request.
- `fps` is the measured input rate (libwebrtc `GetInputFramerateFps`), not
  the configured one.

## 4. Order of decisions in `OnCapturedFrame()`

As `VideoStreamEncoder::OnFrame` + `MaybeEncodeVideoFrame`:

1. paused by the outage monitor (target zero) -> drop;
2. congestion-window pushback interval (`DropCapturedFrame()`) -> drop,
   keyframes included (libwebrtc drops them too);
3. `DropDueToSize` / resolution change from the degradation ladder;
4. `FrameDropper::ShouldDrop()` unless a keyframe is due. libwebrtc runs the
   dropper on keyframes too; the current test bench exempts them. Keeping the
   exemption is a **behaviour-preserving** choice for PR 2 and is listed as a
   deviation (section 6) for #5 to resolve.

## 5. Rate sharing with `SendRates`

`SendRates` stays the single source of the split (target, audio wire rate,
video target after pushback, drop interval). The controller derives
`EncodeDecision::target` as `SendRates::video_target`, after the
`EncoderOvershootDetector` correction (libwebrtc
`EncoderBitrateAdjuster`). The overshoot correction is **not** enabled in PR 2
(libwebrtc enables it through a field trial, `UseEncoderBitrateAdjuster`); the
module is wired but off, so test bench numbers are unchanged. Turning it on is
a separate, measured step.

`BitrateAllocator` (audio vs video) is wired for `AudioBitrate()` only. The
session's existing audio wire rate stays the rule until the allocator is
checked against `call/bitrate_allocator.cc` (not in the reference tree).

## 6. Deviations and open points (for `tools/catalog.py` once the code lands)

| Point | libwebrtc | Here | Resolved by |
|---|---|---|---|
| Keyframes bypass the frame dropper | they do not | they do | #5 |
| Frame dropper arithmetic (accumulator, `Leak`/`Fill`, 1.5 s cap vs 3 s) | `frame_dropper.cc` | custom leaky bucket | #5 |
| Quality scaler / degradation ladder unused by the test bench | active when a stream has QP | wired but the fake encoder reports no QP, so inactive | PR 2 keeps it inactive; an AI-camera scenario needs a QP model |
| Initial frame drop, `DropDueToSize` | present | absent | later issue, if a scenario needs it |
| Encoder bitrate adjuster | field-trial gated | off | later issue |
| Simulcast / SVC | supported | one stream | out of scope (one-to-one calls) |

## 7. Missing reference

`call/bitrate_allocator.cc` and `modules/video_coding/utility/frame_dropper.*`
callers are not both in the reference tree: `frame_dropper.cc` is, but
`bitrate_allocator.cc` is not. Reported on #57. Until it is added the
allocator mapping above is by name only, and PR 2 does not change it.

## 8. PR 2 plan (behaviour-preserving)

1. Add `SendController` and its unit tests (drop reasons, sticky keyframe,
   pause/resume, pushback interval, failed encode).
2. `testbench/call.cc` calls `OnCapturedFrame()` / `OnEncoded()`; the
   `dropper`, `keyframe_needed`, `periodic` and `video_paused` glue is deleted.
3. An end-to-end test over `sim::NetworkLink` with a capacity drop.
4. Test bench metrics byte-identical to `testbench/baseline.json`, or each
   difference explained.
5. Update `docs/design/architecture.md` and the README usage example; move the
   `cc::FrameDropper...` row of `docs/design/rtp-session.md` section 5.1 from
   "App" to "Library (`SendController`)".
6. Builds: native, ASan, no-exceptions, wasm (CI), embedded.

## 9. Questions for the Owner

1. One `SendController` per video stream, or one per call with audio inside?
   Recommendation: per video stream, with `AudioBitrate()` on the same object
   only while there is a single audio stream.
2. Keep the keyframe exemption from the frame dropper in PR 2 (recommended,
   keeps metrics identical) or follow libwebrtc immediately?
3. Is a header named `sm/rtp/send_controller.h` acceptable next to the existing
   `send_side_controller.h`, given the similar names? Alternative:
   `sm/rtp/encoder_controller.h`.
