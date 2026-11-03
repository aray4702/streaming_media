// Camera capture through AVCaptureSession (#1105), after libwebrtc's
// RTCCameraVideoCapturer (M157): NV12 full-range output, late frames kept
// (alwaysDiscardsLateVideoFrames = NO), the format closest to the requested
// size, and the frame duration set from the requested rate. On iOS each frame
// carries the rotation the capturer derives from UIDevice's orientation and the
// camera's position (RTCCameraVideoCapturer.m:84-88, 176-198, 243-309, 567-572).
#import <AVFoundation/AVCaptureDevice.h>
#import <AVFoundation/AVCaptureInput.h>
#import <AVFoundation/AVCaptureSession.h>
#import <AVFoundation/AVCaptureVideoDataOutput.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#endif

#include <atomic>
#include <mutex>

#include "sm/device/camera_capturer.h"

#if !__has_feature(objc_arc)
#error "build with -fobjc-arc"
#endif

namespace sm::device {
class AvfCameraCapturer;
}

@interface SMCaptureDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, assign) sm::device::AvfCameraCapturer* owner;
@end

namespace sm::device {
namespace {

NSArray<AVCaptureDevice*>* Devices() {
  NSMutableArray* types = [NSMutableArray arrayWithObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];
#if TARGET_OS_OSX
  if (@available(macOS 14.0, *)) [types addObject:AVCaptureDeviceTypeExternal];
#endif
  AVCaptureDeviceDiscoverySession* d =
      [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:types
                                                             mediaType:AVMediaTypeVideo
                                                              position:AVCaptureDevicePositionUnspecified];
  return d.devices;
}

CameraPosition PositionOf(AVCaptureDevice* d) {
  switch (d.position) {
    case AVCaptureDevicePositionFront: return CameraPosition::kFront;
    case AVCaptureDevicePositionBack: return CameraPosition::kBack;
    default: return CameraPosition::kDefault;
  }
}

std::vector<CaptureFormat> FormatsOf(AVCaptureDevice* d) {
  std::vector<CaptureFormat> out;
  for (AVCaptureDeviceFormat* f in d.formats) {
    CMVideoDimensions dim = CMVideoFormatDescriptionGetDimensions(f.formatDescription);
    double max_fps = 0;
    for (AVFrameRateRange* r in f.videoSupportedFrameRateRanges) max_fps = std::max(max_fps, r.maxFrameRate);
    out.push_back({dim.width, dim.height, max_fps});
  }
  return out;
}

AVCaptureDevice* FindDevice(const CameraConfig& c) {
  if (!c.unique_id.empty()) {
    return [AVCaptureDevice deviceWithUniqueID:[NSString stringWithUTF8String:c.unique_id.c_str()]];
  }
  if (c.position != CameraPosition::kDefault) {
    for (AVCaptureDevice* d in Devices()) {
      if (PositionOf(d) == c.position) return d;
    }
  }
  return [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
}

}  // namespace

class AvfCameraCapturer : public CameraCapturer {
 public:
  AvfCameraCapturer() {
    delegate_ = [[SMCaptureDelegate alloc] init];
    delegate_.owner = this;
    queue_ = dispatch_queue_create("sm.camera", DISPATCH_QUEUE_SERIAL);
  }
  ~AvfCameraCapturer() override {
    Stop();
    delegate_.owner = nullptr;
  }

  bool Start(const CameraConfig& config, FrameCallback on_frame) override {
    Stop();
    AVCaptureDevice* device = FindDevice(config);
    if (device == nil) return false;
    const std::vector<CaptureFormat> formats = FormatsOf(device);
    const int index = SelectCaptureFormat(formats, config.width, config.height, config.fps);
    if (index < 0) return false;
    NSError* error = nil;
    AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
    if (input == nil) {
      NSLog(@"sm camera: no input: %@", error);
      return false;
    }
    AVCaptureSession* session = [[AVCaptureSession alloc] init];
    [session beginConfiguration];
    if (![session canAddInput:input]) return false;
    [session addInput:input];
    AVCaptureVideoDataOutput* output = [[AVCaptureVideoDataOutput alloc] init];
    output.videoSettings = @{
      (NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange)
    };
    output.alwaysDiscardsLateVideoFrames = NO;
    [output setSampleBufferDelegate:delegate_ queue:queue_];
    if (![session canAddOutput:output]) return false;
    [session addOutput:output];
    // The format and frame duration go on the device after the session took
    // it, so the session preset does not override them.
    if ([device lockForConfiguration:&error]) {
      AVCaptureDeviceFormat* f = device.formats[static_cast<NSUInteger>(index)];
      device.activeFormat = f;
      double fps = config.fps;
      double min_fps = 0;
      double max_fps = 0;
      for (AVFrameRateRange* r in f.videoSupportedFrameRateRanges) {
        if (max_fps == 0 || r.maxFrameRate > max_fps) max_fps = r.maxFrameRate;
        if (min_fps == 0 || r.minFrameRate < min_fps) min_fps = r.minFrameRate;
      }
      if (max_fps > 0) fps = std::clamp(fps, min_fps, max_fps);
      if (fps > 0) {
        const CMTime duration = CMTimeMake(1000, static_cast<int32_t>(fps * 1000 + 0.5));
        device.activeVideoMinFrameDuration = duration;
        device.activeVideoMaxFrameDuration = duration;
      }
      [device unlockForConfiguration];
    } else {
      NSLog(@"sm camera: lockForConfiguration failed: %@", error);
    }
    [session commitConfiguration];
    {
      std::lock_guard<std::mutex> lock(mu_);
      config_ = config;
      on_frame_ = std::move(on_frame);
      active_ = formats[static_cast<size_t>(index)];
      pacer_ = FramerateController(config.fps);
    }
#if TARGET_OS_IPHONE
    // The capturer starts at portrait (90), follows UIDeviceOrientationDidChangeNotification, and reads the
    // orientation once at start (updateOrientation).
    front_ = device.position == AVCaptureDevicePositionFront;
    rotation_ = video::Rotation::k90;
    orientation_ = static_cast<int>(DeviceOrientation::kPortrait);
    std::atomic<int>* orientation = &orientation_;
    observer_ = [[NSNotificationCenter defaultCenter]
        addObserverForName:UIDeviceOrientationDidChangeNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification*) {
                  *orientation = static_cast<int>([UIDevice currentDevice].orientation);
                }];
    dispatch_async(dispatch_get_main_queue(), ^{
      [[UIDevice currentDevice] beginGeneratingDeviceOrientationNotifications];
      *orientation = static_cast<int>([UIDevice currentDevice].orientation);
    });
#endif
    session_ = session;
    [session_ startRunning];
    running_ = session_.isRunning;
    return running_;
  }

  void Stop() override {
    if (session_ != nil) {
      [session_ stopRunning];
      session_ = nil;
    }
#if TARGET_OS_IPHONE
    if (observer_ != nil) {
      [[NSNotificationCenter defaultCenter] removeObserver:observer_];
      observer_ = nil;
      dispatch_async(dispatch_get_main_queue(), ^{
        [[UIDevice currentDevice] endGeneratingDeviceOrientationNotifications];
      });
    }
#endif
    // Wait for a callback in flight on the capture queue.
    dispatch_sync(queue_, ^{
                  });
    running_ = false;
  }

  bool IsRunning() const override { return running_; }
  CaptureFormat ActiveFormat() const override {
    std::lock_guard<std::mutex> lock(mu_);
    return active_;
  }

  void OnSampleBuffer(CMSampleBufferRef sample) {
    CVImageBufferRef pixels = CMSampleBufferGetImageBuffer(sample);
    if (pixels == nullptr || CVPixelBufferGetPlaneCount(pixels) != 2) return;
    const CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
    const int64_t ts_ns = CMTIME_IS_VALID(pts) ? static_cast<int64_t>(CMTimeGetSeconds(pts) * 1e9) : 0;
    std::lock_guard<std::mutex> lock(mu_);
    if (!on_frame_ || pacer_.ShouldDropFrame(ts_ns)) return;
    CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
    const auto* y = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pixels, 0));
    const auto* uv = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pixels, 1));
    const int stride_y = static_cast<int>(CVPixelBufferGetBytesPerRowOfPlane(pixels, 0));
    const int stride_uv = static_cast<int>(CVPixelBufferGetBytesPerRowOfPlane(pixels, 1));
    const int w = static_cast<int>(CVPixelBufferGetWidth(pixels));
    const int h = static_cast<int>(CVPixelBufferGetHeight(pixels));
    frame_.timestamp_us = ts_ns / 1000;
#if TARGET_OS_IPHONE
    rotation_ = CaptureRotation(static_cast<DeviceOrientation>(orientation_.load()), front_, rotation_);
    frame_.rotation = rotation_;
#else
    frame_.rotation = video::Rotation::k0;  // No rotation on Mac.
#endif
    frame_.format = config_.pixel_format;
    if (config_.pixel_format == CapturePixelFormat::kNV12) {
      CopyNv12(y, stride_y, uv, stride_uv, w, h, &frame_.nv12);
    } else {
      Nv12ToI420(y, stride_y, uv, stride_uv, w, h, &frame_.i420);
    }
    CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
    if (config_.pixel_format == CapturePixelFormat::kI420 && config_.width > 0 && config_.height > 0) {
      frame_.i420 = CropAndScaleI420(frame_.i420, config_.width, config_.height);
    }
    on_frame_(frame_);
  }

 private:
  SMCaptureDelegate* delegate_ = nil;
  dispatch_queue_t queue_ = nil;
  AVCaptureSession* session_ = nil;
  std::atomic<bool> running_{false};
  mutable std::mutex mu_;  // guards what the capture queue reads
  CameraConfig config_;
  FrameCallback on_frame_;
  CaptureFormat active_;
  FramerateController pacer_;
  CapturedFrame frame_;
#if TARGET_OS_IPHONE
  id observer_ = nil;
  std::atomic<int> orientation_{static_cast<int>(DeviceOrientation::kPortrait)};
  bool front_ = false;
  video::Rotation rotation_ = video::Rotation::k90;  // Capture queue.
#endif
};

std::vector<CameraInfo> ListCameras() {
  std::vector<CameraInfo> out;
  for (AVCaptureDevice* d in Devices()) {
    CameraInfo info;
    info.unique_id = d.uniqueID.UTF8String;
    info.name = d.localizedName.UTF8String;
    info.position = PositionOf(d);
    info.formats = FormatsOf(d);
    out.push_back(std::move(info));
  }
  return out;
}

std::unique_ptr<CameraCapturer> CreatePlatformCameraCapturer() {
  return std::make_unique<AvfCameraCapturer>();
}

}  // namespace sm::device

@implementation SMCaptureDelegate
- (void)captureOutput:(AVCaptureOutput*)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection*)connection {
  if (_owner != nullptr) _owner->OnSampleBuffer(sampleBuffer);
}
@end
