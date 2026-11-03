// Camera capture helpers (#1105), plain C++ so they are tested without a camera:
//  - SelectCaptureFormat: the camera format closest to the requested size and
//    rate, as libwebrtc's ARDCaptureController / RTCCameraVideoCapturer pick it
//    (smallest |width difference| + |height difference|, among formats that
//    reach the requested fps if any do).
//  - FramerateController: drops frames down to the requested fps, libwebrtc's
//    common_video/framerate_controller.cc (M157), which VideoAdapter uses.
//  - Nv12ToI420 and ScaleI420: the camera's NV12 (kCVPixelFormatType_420YpCbCr8BiPlanar*)
//    to the library's I420, and to the exact requested size when no camera
//    format has it.
//  - CaptureRotation: the rotation a camera frame carries, from the device's
//    orientation and the camera's position, as RTCCameraVideoCapturer sets it
//    (sdk/objc/components/capturer/RTCCameraVideoCapturer.m:269-309).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "sm/video/frame.h"
#include "sm/video/orientation.h"

namespace sm::device {

struct CaptureFormat {
  int width = 0;
  int height = 0;
  double max_fps = 0;
};

// Index into `formats`, or -1 when it is empty.
int SelectCaptureFormat(const std::vector<CaptureFormat>& formats, int width, int height, double fps);

class FramerateController {
 public:
  explicit FramerateController(double max_fps = 0) { SetMaxFramerate(max_fps); }
  // 0 or less: no limit.
  void SetMaxFramerate(double max_fps);
  // True when the frame at `timestamp_ns` should be dropped.
  bool ShouldDropFrame(int64_t timestamp_ns);
  void Reset() { next_frame_timestamp_ns_.reset(); }

 private:
  double max_fps_ = 0;
  std::optional<int64_t> next_frame_timestamp_ns_;
};

// NV12 frame: a Y plane and an interleaved UV plane at half size.
struct Nv12Frame {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> y;
  std::vector<uint8_t> uv;
  Nv12Frame() = default;
  Nv12Frame(int w, int h)
      : width(w), height(h), y(static_cast<size_t>(w) * h),
        uv(static_cast<size_t>((w + 1) / 2) * 2 * ((h + 1) / 2)) {}
};

// From planes with strides (CVPixelBufferGetBytesPerRowOfPlane) to I420.
void Nv12ToI420(const uint8_t* src_y, int stride_y, const uint8_t* src_uv, int stride_uv, int width, int height,
                video::I420Frame* out);
// The same into an NV12 copy without padding.
void CopyNv12(const uint8_t* src_y, int stride_y, const uint8_t* src_uv, int stride_uv, int width, int height,
              Nv12Frame* out);
// Center-crops to the target aspect ratio and scales bilinearly to
// `width` x `height` (libyuv's I420Scale with kFilterBilinear in libwebrtc's
// adapted frames; a plain bilinear filter here).
video::I420Frame CropAndScaleI420(const video::I420Frame& in, int width, int height);

// UIDeviceOrientation's values (UIKit), so the mapping is plain C++.
enum class DeviceOrientation {
  kUnknown = 0,
  kPortrait = 1,
  kPortraitUpsideDown = 2,
  kLandscapeLeft = 3,
  kLandscapeRight = 4,
  kFaceUp = 5,
  kFaceDown = 6,
};
// RTCCameraVideoCapturer's captureOutput: portrait 90, upside down 270, landscape left 180 on the front camera and 0
// on the back, landscape right the other way round; face up, face down and unknown keep `previous` (the capturer
// starts at portrait, 90). The rotation is the clockwise turn the receiver applies to show the frame upright.
video::Rotation CaptureRotation(DeviceOrientation orientation, bool front_camera, video::Rotation previous);

}  // namespace sm::device
