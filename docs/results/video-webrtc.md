# Video modules vs libwebrtc (#63, section 7)

`sm_video_compare` (`compare/webrtc/video_compare.cc`) feeds the same
synthetic inputs to this library's video receive and adaptation modules and
to libwebrtc M157's, and reads both with the same metric code. The inputs
are deterministic (fixed seeds), so one run is the result.

```sh
compare/webrtc/fetch.sh
CXX=clang++-21 cmake -S compare/webrtc -B out/compare -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/compare -j 4 --target sm_video_compare   # prints the table after the build
```

## What runs on each side

| Block | Ours | libwebrtc | Input |
|---|---|---|---|
| Freeze detection | `video::FreezeDetector` (`src/video/freeze_detector.cc`), which the test bench and both sides of `sm_compare` use for `video.freeze_*` | `internal::VideoQualityObserver` (`video/video_quality_observer2.cc`), the class behind getStats' `freezeCount` and `totalFreezesDuration` | Render times. A gap of 5 s or more gets `OnStreamInactive()` 5 s into it, as `VideoReceiveStream2` calls it when no packet arrived for `kInactiveDuration` (`video/video_receive_stream2.cc`) |
| Quality scaling | `video::QualityScaler` (`src/video/quality_scaler.cc`; not in the call path yet) | `QualityScaler` (`modules/video_coding/utility/quality_scaler.cc`) at its default field trials, on a simulated task queue | Per-frame QP at 30 fps for 30 s, H.264 thresholds 24/37. Both clear their samples after a signal, as libwebrtc's `CheckQpTask` does (ours through `OnResolutionChanged()`); the QP input does not react to the signal |
| Frame timing | `timing::VcmTiming` (`src/timing/vcm_timing.cc`, the #24 port) | `VCMTiming` with its `DefaultVideoJitterTiming` (`modules/video_coding/timing/timing.cc`) | Frame arrivals (30 or 15 fps, 50 ms base delay, Gaussian jitter, a delay step, retransmitted frames at RTT 100 ms), 40 KB keyframe then 4-6 KB frames. Driven in `video/video_stream_buffer_controller.cc`'s order: `OnCompleteFrame` on arrival; the front frame is scheduled at its latest decode time (render time - decode estimate - render delay, or at once if that has passed, as `frame_decode_timing.cc`), then `OnDecodableTemporalUnit`, `UpdateCurrentDelay` unless retransmitted, and a 5 ms decode time. Metrics skip the first quarter of the run |

## Results

`main` @ f944afa plus this tool.

| module | scenario | metric | ours | libwebrtc | note |
|---|---|---|---|---|---|
| freeze detection | 30 fps, one gap of 180, 185, ..., 230 ms every 5 s | freezes | 9.0 | 9.0 |  |
| freeze detection | 30 fps, one gap of 180, 185, ..., 230 ms every 5 s | total freeze duration (ms) | 1890.0 | 1890.0 |  |
| freeze detection | 30 fps, one gap of 180, 185, ..., 230 ms every 5 s | harmonic fps | 25.0 | 25.0 |  |
| freeze detection | 30 fps, render jitter 8 ms, 60 s | freezes | 0.0 | 0.0 |  |
| freeze detection | 30 fps, render jitter 8 ms, 60 s | total freeze duration (ms) | 0.0 | 0.0 |  |
| freeze detection | 30 fps, render jitter 8 ms, 60 s | harmonic fps | 27.0 | 27.0 |  |
| freeze detection | 30 fps, a 400 ms gap every 1 s, 30 s | freezes | 29.0 | 29.0 |  |
| freeze detection | 30 fps, a 400 ms gap every 1 s, 30 s | total freeze duration (ms) | 11600.0 | 11600.0 |  |
| freeze detection | 30 fps, a 400 ms gap every 1 s, 30 s | harmonic fps | 5.6 | 5.6 |  |
| freeze detection | 30 fps, 3 s at 3 fps, 30 fps | freezes | 7.0 | 7.0 |  |
| freeze detection | 30 fps, 3 s at 3 fps, 30 fps | total freeze duration (ms) | 2331.0 | 2331.0 |  |
| freeze detection | 30 fps, 3 s at 3 fps, 30 fps | harmonic fps | 7.5 | 7.5 |  |
| freeze detection | 15 fps, one gap of 250, 300, ..., 450 ms every 4 s | freezes | 5.0 | 5.0 |  |
| freeze detection | 15 fps, one gap of 250, 300, ..., 450 ms every 4 s | total freeze duration (ms) | 1750.0 | 1750.0 |  |
| freeze detection | 15 fps, one gap of 250, 300, ..., 450 ms every 4 s | harmonic fps | 11.3 | 11.3 |  |
| freeze detection | 30 fps, outages of 2 s, 6 s and 1 s | freezes | 2.0 | 2.0 |  |
| freeze detection | 30 fps, outages of 2 s, 6 s and 1 s | total freeze duration (ms) | 3000.0 | 3000.0 |  |
| freeze detection | 30 fps, outages of 2 s, 6 s and 1 s | pauses | 1.0 | 1.0 |  |
| freeze detection | 30 fps, outages of 2 s, 6 s and 1 s | total pause duration (ms) | 6033.0 | 6033.0 |  |
| freeze detection | 30 fps, outages of 2 s, 6 s and 1 s | harmonic fps | 0.7 | 0.7 |  |
| freeze detection | 30 fps, 2% of frames followed by a 100-800 ms gap, 120 s | freezes | 57.0 | 57.0 |  |
| freeze detection | 30 fps, 2% of frames followed by a 100-800 ms gap, 120 s | total freeze duration (ms) | 26580.0 | 26580.0 |  |
| freeze detection | 30 fps, 2% of frames followed by a 100-800 ms gap, 120 s | harmonic fps | 7.0 | 7.0 |  |
| quality scaler | H.264, QP 40 (above 37), 30 fps, 30 s | adapt-down signals | 19.0 | 6.0 |  |
| quality scaler | H.264, QP 40 (above 37), 30 fps, 30 s | first adapt-down (s) | 1.5 | 2.0 | -1: none |
| quality scaler | H.264, QP 40 (above 37), 30 fps, 30 s | adapt-up signals | 0.0 | 0.0 |  |
| quality scaler | H.264, QP 40 (above 37), 30 fps, 30 s | first adapt-up (s) | -1.0 | -1.0 | -1: none |
| quality scaler | H.264, QP 30 (between 24 and 37) | adapt-down signals | 0.0 | 0.0 |  |
| quality scaler | H.264, QP 30 (between 24 and 37) | first adapt-down (s) | -1.0 | -1.0 | -1: none |
| quality scaler | H.264, QP 30 (between 24 and 37) | adapt-up signals | 0.0 | 0.0 |  |
| quality scaler | H.264, QP 30 (between 24 and 37) | first adapt-up (s) | -1.0 | -1.0 | -1: none |
| quality scaler | H.264, QP 18 (below 24) | adapt-down signals | 0.0 | 0.0 |  |
| quality scaler | H.264, QP 18 (below 24) | first adapt-down (s) | -1.0 | -1.0 | -1: none |
| quality scaler | H.264, QP 18 (below 24) | adapt-up signals | 19.0 | 15.0 |  |
| quality scaler | H.264, QP 18 (below 24) | first adapt-up (s) | 1.5 | 2.0 | -1: none |
| quality scaler | H.264, QP 40 for 10 s, then 18 | adapt-down signals | 6.0 | 2.0 |  |
| quality scaler | H.264, QP 40 for 10 s, then 18 | first adapt-down (s) | 1.5 | 2.0 | -1: none |
| quality scaler | H.264, QP 40 for 10 s, then 18 | adapt-up signals | 13.0 | 2.0 |  |
| quality scaler | H.264, QP 40 for 10 s, then 18 | first adapt-up (s) | 10.5 | 22.0 | -1: none |
| quality scaler | H.264, QP uniform 30-45 (mean 37.5) | adapt-down signals | 15.0 | 0.0 |  |
| quality scaler | H.264, QP uniform 30-45 (mean 37.5) | first adapt-down (s) | 1.5 | -1.0 | -1: none |
| quality scaler | H.264, QP uniform 30-45 (mean 37.5) | adapt-up signals | 0.0 | 0.0 |  |
| quality scaler | H.264, QP uniform 30-45 (mean 37.5) | first adapt-up (s) | -1.0 | -1.0 | -1: none |
| quality scaler | H.264, QP 30, 70% of frames dropped | adapt-down signals | 19.0 | 6.0 |  |
| quality scaler | H.264, QP 30, 70% of frames dropped | first adapt-down (s) | 1.5 | 2.0 | -1: none |
| quality scaler | H.264, QP 30, 70% of frames dropped | adapt-up signals | 0.0 | 0.0 |  |
| quality scaler | H.264, QP 30, 70% of frames dropped | first adapt-up (s) | -1.0 | -1.0 | -1: none |
| frame timing | 30 fps, 50 ms delay, no jitter | jitter delay, mean (ms) | 11.0 | 11.0 |  |
| frame timing | 30 fps, 50 ms delay, no jitter | target delay, p95 (ms) | 26.0 | 26.0 |  |
| frame timing | 30 fps, 50 ms delay, no jitter | current delay, mean (ms) | 21.2 | 21.2 |  |
| frame timing | 30 fps, 50 ms delay, no jitter | arrival to render, mean (ms) | 21.2 | 21.2 |  |
| frame timing | 30 fps, 50 ms delay, no jitter | capture to render, p95 (ms) | 71.5 | 71.5 |  |
| frame timing | 30 fps, 50 ms delay, no jitter | frames released late | 1.0 | 1.0 | > 5 ms past the latest decode time |
| frame timing | 30 fps, jitter 10 ms | jitter delay, mean (ms) | 24.1 | 24.1 |  |
| frame timing | 30 fps, jitter 10 ms | target delay, p95 (ms) | 51.4 | 51.3 |  |
| frame timing | 30 fps, jitter 10 ms | current delay, mean (ms) | 33.9 | 33.9 |  |
| frame timing | 30 fps, jitter 10 ms | arrival to render, mean (ms) | 33.7 | 33.7 |  |
| frame timing | 30 fps, jitter 10 ms | capture to render, p95 (ms) | 90.6 | 90.6 |  |
| frame timing | 30 fps, jitter 10 ms | frames released late | 15.0 | 15.0 | > 5 ms past the latest decode time |
| frame timing | 30 fps, jitter 30 ms | jitter delay, mean (ms) | 74.0 | 73.9 |  |
| frame timing | 30 fps, jitter 30 ms | target delay, p95 (ms) | 108.5 | 108.4 |  |
| frame timing | 30 fps, jitter 30 ms | current delay, mean (ms) | 75.2 | 75.1 |  |
| frame timing | 30 fps, jitter 30 ms | arrival to render, mean (ms) | 73.1 | 73.0 |  |
| frame timing | 30 fps, jitter 30 ms | capture to render, p95 (ms) | 144.1 | 144.0 |  |
| frame timing | 30 fps, jitter 30 ms | frames released late | 12.0 | 12.0 | > 5 ms past the latest decode time |
| frame timing | 30 fps, jitter 5 ms, +150 ms delay step at 10 s | jitter delay, mean (ms) | 17.3 | 17.3 |  |
| frame timing | 30 fps, jitter 5 ms, +150 ms delay step at 10 s | target delay, p95 (ms) | 38.7 | 38.6 |  |
| frame timing | 30 fps, jitter 5 ms, +150 ms delay step at 10 s | current delay, mean (ms) | 29.5 | 29.5 |  |
| frame timing | 30 fps, jitter 5 ms, +150 ms delay step at 10 s | arrival to render, mean (ms) | -20.3 | -20.3 |  |
| frame timing | 30 fps, jitter 5 ms, +150 ms delay step at 10 s | capture to render, p95 (ms) | 228.5 | 228.5 |  |
| frame timing | 30 fps, jitter 5 ms, +150 ms delay step at 10 s | frames released late | 151.0 | 151.0 | > 5 ms past the latest decode time |
| frame timing | 30 fps, jitter 5 ms, 3% of frames retransmitted (RTT 100 ms) | jitter delay, mean (ms) | 107.0 | 106.9 |  |
| frame timing | 30 fps, jitter 5 ms, 3% of frames retransmitted (RTT 100 ms) | target delay, p95 (ms) | 131.4 | 131.4 |  |
| frame timing | 30 fps, jitter 5 ms, 3% of frames retransmitted (RTT 100 ms) | current delay, mean (ms) | 83.1 | 83.1 |  |
| frame timing | 30 fps, jitter 5 ms, 3% of frames retransmitted (RTT 100 ms) | arrival to render, mean (ms) | 78.5 | 78.5 |  |
| frame timing | 30 fps, jitter 5 ms, 3% of frames retransmitted (RTT 100 ms) | capture to render, p95 (ms) | 137.2 | 137.2 |  |
| frame timing | 30 fps, jitter 5 ms, 3% of frames retransmitted (RTT 100 ms) | frames released late | 22.0 | 22.0 | > 5 ms past the latest decode time |
| frame timing | 15 fps, jitter 10 ms | jitter delay, mean (ms) | 21.3 | 21.2 |  |
| frame timing | 15 fps, jitter 10 ms | target delay, p95 (ms) | 47.8 | 47.8 |  |
| frame timing | 15 fps, jitter 10 ms | current delay, mean (ms) | 31.3 | 31.3 |  |
| frame timing | 15 fps, jitter 10 ms | arrival to render, mean (ms) | 30.5 | 30.5 |  |
| frame timing | 15 fps, jitter 10 ms | capture to render, p95 (ms) | 84.7 | 84.7 |  |
| frame timing | 15 fps, jitter 10 ms | frames released late | 6.0 | 6.0 | > 5 ms past the latest decode time |

## Reading it

- **Frame timing: no gap.** Every metric is within 0.1 ms on every case,
  including the delay step and retransmitted frames. #24's port of
  `VCMTiming`, `JitterEstimator` and `TimestampExtrapolator` does what
  libwebrtc does. (The negative "arrival to render" on the delay-step case is
  real on both sides: after the step, frames arrive after their render time
  until the timing catches up, and both release them at once.)
- **Freeze detection: the same (fixed by #268).** Before #268,
  `FreezeDetector` tested each interval against the previous ones only and
  left freezes out of the average, so it counted a 185 ms gap at 30 fps as a
  freeze and kept the 30 fps average longer when the frame rate dropped to
  3 fps (9 freezes against 7). It now does what `VideoQualityObserver`
  does: the current interval enters the 30-frame average *before* the test,
  freezes stay in it, the average is rounded down to whole ms, and the test
  starts at the 5th interval. Every freeze row above is the same on both
  sides ([fix record](../fixes/268-freeze-rule-as-libwebrtc.md)).
- **Quality scaling: a large gap (owned by #84).** libwebrtc checks QP 2 s
  after the start and after a "low QP" signal (`fast_rampup_`), and 5 s
  after a "high QP" one (`kMeasureMs` 2000 x `kSamplePeriodScaleFactor`
  2.5); it needs 60 frames (`kMinFramesNeededToScale`), averages QP over the
  last 150 frames (rounded down), and adapts down at a drop rate of 60% or
  more. Ours checks every 1.5 s with an exponential average (0.15 per
  frame) and 15 frames. With QP 40 throughout, ours signals adapt-down 19
  times in 30 s against libwebrtc's 6. After QP falls from 40 to 18 at 10 s,
  ours adapts up at 10.5 s and libwebrtc at 22 s: its check at 12 s still
  averages QP-40 frames, and `QualityScalingExperiment`, on by default in
  the binary, smooths the QP further (with
  `WebRTC-Video-QualityScaling/Disabled/` it adapts up at 17 s instead). A
  QP that averages 37.5 is "high" for ours and not for libwebrtc. Our
  scaler is not in the call path
  yet; #84 (video adaptation as libwebrtc) ports it with the rest of
  `VideoStreamEncoder`'s adaptation.

## Caveats

- The inputs are synthetic and the QP input does not change when a side
  signals, so the quality-scaler rows count signals, not resolution steps.
  `QualityScalingExperiment`'s source (`rtc_base/experiments/
  quality_scaling_experiment.cc`) is not in `compare/webrtc/reference/`;
  the libwebrtc column is what the prebuilt binary does with its default
  field trials.
- Frame timing is driven one frame at a time with the decoder always free;
  frame dropping and fast-forwarding (`timing::FrameReleaseController`
  against `VideoStreamBufferController`) are measured end to end by
  `sm_compare`, not here.
