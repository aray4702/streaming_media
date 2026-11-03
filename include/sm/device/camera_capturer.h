// Camera capture (#1105): AVCaptureSession on Apple platforms, after
// libwebrtc's sdk/objc/components/capturer/RTCCameraVideoCapturer (M157),
// followed through Apple's documented AVFoundation API.
//
// The camera delivers NV12 (kCVPixelFormatType_420YpCbCr8BiPlanarFullRange, the
// capturer's preferred output format). Start() picks the camera format
// closest to the requested size and rate (SelectCaptureFormat), sets the
// frame duration, and drops frames down to the requested rate
// (FramerateController). Frames come out as I420 or NV12; I420 is cropped and
// scaled to the exact requested size when no camera format has it.
//
// Frames arrive on the capture queue's thread, not the app's.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "sm/device/video_capture_util.h"
#include "sm/video/frame.h"

namespace sm::device {

enum class CameraPosition { kDefault, kFront, kBack };
enum class CapturePixelFormat { kI420, kNV12 };

struct CameraInfo {
  std::string unique_id;
  std::string name;
  CameraPosition position = CameraPosition::kDefault;
  std::vector<CaptureFormat> formats;
};

struct CameraConfig {
  int width = 640;
  int height = 480;
  int fps = 30;
  // iOS: front or back camera. macOS: the default camera (kDefault), or the
  // first one at that position.
  CameraPosition position = CameraPosition::kDefault;
  std::string unique_id;  // a specific camera from ListCameras(), if set
  CapturePixelFormat pixel_format = CapturePixelFormat::kI420;
};

struct CapturedFrame {
  CapturePixelFormat format = CapturePixelFormat::kI420;
  video::I420Frame i420;  // when format == kI420
  Nv12Frame nv12;         // when format == kNV12
  int64_t timestamp_us = 0;  // the sample buffer's presentation time
};

class CameraCapturer {
 public:
  using FrameCallback = std::function<void(const CapturedFrame&)>;
  virtual ~CameraCapturer() = default;
  virtual bool Start(const CameraConfig& config, FrameCallback on_frame) = 0;
  virtual void Stop() = 0;
  virtual bool IsRunning() const = 0;
  // The camera format Start() chose.
  virtual CaptureFormat ActiveFormat() const = 0;
};

// The platform's cameras and capturer: AVFoundation on Apple platforms. Empty
// list / nullptr where there is none.
std::vector<CameraInfo> ListCameras();
std::unique_ptr<CameraCapturer> CreatePlatformCameraCapturer();

}  // namespace sm::device
