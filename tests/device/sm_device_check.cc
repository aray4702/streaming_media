// sm_device_check (#1105): the device test the Owner runs by hand on a Mac
// (real microphone, speaker and camera; not part of CTest).
//
//   sm_device_check --list                       cameras and their formats, the audio device's rate
//   sm_device_check --loopback [seconds] [--system-aec]
//                                                 microphone to speaker, 500 ms later; prints the level
//   sm_device_check --camera [seconds] [WxH@fps] [--front|--back] [--nv12]
//                                                 captures and prints the measured fps and size
//
// The first run asks for microphone / camera permission for the terminal.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "sm/device/audio_device.h"
#include "sm/device/camera_capturer.h"

using namespace sm::device;

namespace {

const char* PositionName(CameraPosition p) {
  switch (p) {
    case CameraPosition::kFront: return "front";
    case CameraPosition::kBack: return "back";
    default: return "default";
  }
}

int List() {
  const std::vector<CameraInfo> cams = ListCameras();
  std::printf("cameras: %zu\n", cams.size());
  for (const CameraInfo& c : cams) {
    std::printf("  %s [%s] id=%s\n", c.name.c_str(), PositionName(c.position), c.unique_id.c_str());
    for (const CaptureFormat& f : c.formats) std::printf("    %dx%d up to %.0f fps\n", f.width, f.height, f.max_fps);
  }
  auto dev = CreatePlatformAudioDevice({});
  if (!dev) {
    std::printf("audio: no platform device\n");
    return 1;
  }
  const bool ok = dev->InitRecording();
  std::printf("audio: VoiceProcessingIO %s, %d Hz, AEC: %s\n", ok ? "ready" : "FAILED", dev->SampleRateHz(),
              dev->BuiltInAecEnabled() ? "system" : "library (bypassed)");
  return ok ? 0 : 1;
}

// Microphone in, the same audio out 500 ms later.
class Loopback : public AudioTransport {
 public:
  void RecordedDataIsAvailable(const int16_t* s, size_t n, int, int rate, int delay_ms) override {
    std::lock_guard<std::mutex> lock(mu_);
    rate_ = rate;
    delay_ms_ = delay_ms;
    double e = 0;
    for (size_t i = 0; i < n; ++i) e += static_cast<double>(s[i]) * s[i];
    energy_ += e;
    samples_ += n;
    ++rec_frames_;
    fifo_.insert(fifo_.end(), s, s + n);
  }
  size_t NeedMorePlayData(int16_t* out, size_t n, int, int rate) override {
    std::lock_guard<std::mutex> lock(mu_);
    ++play_frames_;
    const size_t delay = static_cast<size_t>(rate / 2);
    for (size_t i = 0; i < n; ++i) {
      if (fifo_.size() > delay) {
        out[i] = fifo_.front();
        fifo_.pop_front();
      } else {
        out[i] = 0;
      }
    }
    return n;
  }
  void Report(int second) {
    std::lock_guard<std::mutex> lock(mu_);
    const double rms = samples_ ? std::sqrt(energy_ / samples_) : 0;
    const double dbfs = rms > 0 ? 20 * std::log10(rms / 32768.0) : -120;
    std::printf("%2ds: %d Hz, rec %d frames, play %d frames, level %.1f dBFS, delay estimate %d ms\n", second, rate_,
                rec_frames_, play_frames_, dbfs, delay_ms_);
    energy_ = 0;
    samples_ = 0;
    rec_frames_ = 0;
    play_frames_ = 0;
  }

 private:
  std::mutex mu_;
  std::deque<int16_t> fifo_;
  double energy_ = 0;
  size_t samples_ = 0;
  int rec_frames_ = 0;
  int play_frames_ = 0;
  int rate_ = 0;
  int delay_ms_ = 0;
};

int RunLoopback(int seconds, bool system_aec) {
  AudioDeviceConfig config;
  config.echo_canceller = system_aec ? EchoCanceller::kSystem : EchoCanceller::kLibrary;
  auto dev = CreatePlatformAudioDevice(config);
  if (!dev) return 1;
  Loopback loop;
  dev->RegisterAudioCallback(&loop);
  if (!dev->InitPlayout() || !dev->InitRecording() || !dev->StartPlayout() || !dev->StartRecording()) {
    std::printf("loopback: could not start the audio device\n");
    return 1;
  }
  std::printf("loopback: speak; you hear yourself 500 ms later (use headphones). %d s\n", seconds);
  for (int s = 1; s <= seconds; ++s) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    loop.Report(s);
  }
  dev->StopRecording();
  dev->StopPlayout();
  return 0;
}

int RunCamera(int seconds, const CameraConfig& config) {
  auto cap = CreatePlatformCameraCapturer();
  if (!cap) return 1;
  std::atomic<int> frames{0};
  std::atomic<int> width{0}, height{0};
  std::atomic<int> mean_y{0};
  std::atomic<int64_t> last_ts{0};
  if (!cap->Start(config, [&](const CapturedFrame& f) {
        const std::vector<uint8_t>& y = f.format == CapturePixelFormat::kI420 ? f.i420.y : f.nv12.y;
        width = f.format == CapturePixelFormat::kI420 ? f.i420.width : f.nv12.width;
        height = f.format == CapturePixelFormat::kI420 ? f.i420.height : f.nv12.height;
        uint64_t sum = 0;
        for (size_t i = 0; i < y.size(); i += 97) sum += y[i];
        mean_y = y.empty() ? 0 : static_cast<int>(sum / ((y.size() + 96) / 97));
        last_ts = f.timestamp_us;
        ++frames;
      })) {
    std::printf("camera: could not start (permission? no camera?)\n");
    return 1;
  }
  const CaptureFormat a = cap->ActiveFormat();
  std::printf("camera: asked %dx%d@%d, camera format %dx%d up to %.0f fps\n", config.width, config.height,
              config.fps, a.width, a.height, a.max_fps);
  for (int s = 1; s <= seconds; ++s) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::printf("%2ds: %d fps, %dx%d %s, mean Y %d, ts %lld us\n", s, frames.exchange(0), width.load(), height.load(),
                config.pixel_format == CapturePixelFormat::kI420 ? "I420" : "NV12", mean_y.load(),
                static_cast<long long>(last_ts.load()));
  }
  cap->Stop();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string mode;
  int seconds = 10;
  bool system_aec = false;
  CameraConfig cam;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    int w = 0, h = 0, fps = 0;
    if (a == "--list" || a == "--loopback" || a == "--camera") {
      mode = a;
    } else if (a == "--system-aec") {
      system_aec = true;
    } else if (a == "--front") {
      cam.position = CameraPosition::kFront;
    } else if (a == "--back") {
      cam.position = CameraPosition::kBack;
    } else if (a == "--nv12") {
      cam.pixel_format = CapturePixelFormat::kNV12;
    } else if (std::sscanf(a.c_str(), "%dx%d@%d", &w, &h, &fps) == 3) {
      cam.width = w;
      cam.height = h;
      cam.fps = fps;
    } else if (std::atoi(a.c_str()) > 0) {
      seconds = std::atoi(a.c_str());
    } else {
      mode.clear();
      break;
    }
  }
  if (mode == "--list") return List();
  if (mode == "--loopback") return RunLoopback(seconds, system_aec);
  if (mode == "--camera") return RunCamera(seconds, cam);
  std::fprintf(stderr,
               "usage: sm_device_check --list | --loopback [seconds] [--system-aec] |\n"
               "       --camera [seconds] [WxH@fps] [--front|--back] [--nv12]\n");
  return 2;
}
