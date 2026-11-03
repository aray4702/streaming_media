// Camera capture helpers (#1105): format selection, frame pacing
// (libwebrtc's common_video/framerate_controller behaviour, as its
// framerate_controller_unittest checks it) and NV12/I420 conversion.
#include <cstdint>
#include <vector>

#include "sm/device/video_capture_util.h"
#include "sm_test.h"

using namespace sm::device;
using sm::video::I420Frame;

namespace {
constexpr int64_t kSec = 1'000'000'000;

int KeptFrames(FramerateController& c, double in_fps, int seconds, int64_t jitter_ns = 0) {
  int kept = 0;
  const int n = static_cast<int>(in_fps * seconds);
  for (int i = 0; i < n; ++i) {
    const int64_t jitter = (i % 2 == 0) ? jitter_ns : -jitter_ns;
    if (!c.ShouldDropFrame(static_cast<int64_t>(i * kSec / in_fps) + jitter)) ++kept;
  }
  return kept;
}
}  // namespace

SM_TEST(DeviceCaptureFormat, PicksTheClosestFormatThatReachesTheRate) {
  const std::vector<CaptureFormat> formats = {
      {320, 240, 30}, {640, 480, 30}, {1280, 720, 30}, {1920, 1080, 30}, {1280, 720, 60}, {640, 360, 60}};
  SM_EXPECT_EQ(SelectCaptureFormat(formats, 640, 480, 30), 1);
  SM_EXPECT_EQ(SelectCaptureFormat(formats, 1280, 720, 30), 2);  // first of equals
  SM_EXPECT_EQ(SelectCaptureFormat(formats, 1280, 720, 60), 4);
  SM_EXPECT_EQ(SelectCaptureFormat(formats, 600, 460, 30), 1);
  SM_EXPECT_EQ(SelectCaptureFormat(formats, 600, 400, 30), 5);  // 640x360: |40| + |40| beats |40| + |80|
  SM_EXPECT_EQ(SelectCaptureFormat(formats, 640, 480, 60), 5);  // the rate first, then the size
  SM_EXPECT_EQ(SelectCaptureFormat(formats, 4000, 3000, 30), 3);
  SM_EXPECT_EQ(SelectCaptureFormat(formats, 640, 480, 120), 1);  // none reaches it: closest size
  SM_EXPECT_EQ(SelectCaptureFormat({}, 640, 480, 30), -1);
}

SM_TEST(DeviceFramerateController, HalvesTheRate) {
  FramerateController c(15);
  const int kept = KeptFrames(c, 30, 10);
  SM_EXPECT_GE(kept, 150);
  SM_EXPECT_LE(kept, 151);
}

SM_TEST(DeviceFramerateController, KeepsEveryFrameAtTheRateEvenWithJitter) {
  FramerateController c(30);
  SM_EXPECT_EQ(KeptFrames(c, 30, 10, 5'000'000), 300);
}

SM_TEST(DeviceFramerateController, SixtyToThirty) {
  FramerateController c(30);
  const int kept = KeptFrames(c, 60, 10);
  SM_EXPECT_GE(kept, 299);
  SM_EXPECT_LE(kept, 301);
}

SM_TEST(DeviceFramerateController, NoLimitAndReset) {
  FramerateController none;
  SM_EXPECT_EQ(KeptFrames(none, 30, 2), 60);
  FramerateController c(10);
  SM_EXPECT_FALSE(c.ShouldDropFrame(0));
  SM_EXPECT_TRUE(c.ShouldDropFrame(kSec / 30));
  // A jump far outside the expected range resets instead of dropping a burst.
  SM_EXPECT_FALSE(c.ShouldDropFrame(100 * kSec));
  SM_EXPECT_TRUE(c.ShouldDropFrame(100 * kSec + kSec / 30));
  SM_EXPECT_FALSE(c.ShouldDropFrame(100 * kSec + kSec / 10));
  c.SetMaxFramerate(0);
  SM_EXPECT_FALSE(c.ShouldDropFrame(100 * kSec + kSec / 10 + 1));
}

SM_TEST(DeviceNv12, ToI420WithStrides) {
  const int w = 6, h = 4, stride_y = 8, stride_uv = 10;
  std::vector<uint8_t> y(static_cast<size_t>(stride_y) * h, 0xEE);
  std::vector<uint8_t> uv(static_cast<size_t>(stride_uv) * (h / 2), 0xEE);
  for (int r = 0; r < h; ++r)
    for (int c = 0; c < w; ++c) y[r * stride_y + c] = static_cast<uint8_t>(r * 10 + c);
  for (int r = 0; r < h / 2; ++r)
    for (int c = 0; c < w / 2; ++c) {
      uv[r * stride_uv + 2 * c] = static_cast<uint8_t>(100 + r * 10 + c);
      uv[r * stride_uv + 2 * c + 1] = static_cast<uint8_t>(200 + r * 10 + c);
    }
  I420Frame f;
  Nv12ToI420(y.data(), stride_y, uv.data(), stride_uv, w, h, &f);
  SM_ASSERT_EQ(f.width, w);
  SM_ASSERT_EQ(f.height, h);
  for (int r = 0; r < h; ++r)
    for (int c = 0; c < w; ++c) SM_ASSERT_EQ(f.Y(c, r), static_cast<uint8_t>(r * 10 + c));
  for (int r = 0; r < h / 2; ++r)
    for (int c = 0; c < w / 2; ++c) {
      SM_ASSERT_EQ(f.u[r * 3 + c], static_cast<uint8_t>(100 + r * 10 + c));
      SM_ASSERT_EQ(f.v[r * 3 + c], static_cast<uint8_t>(200 + r * 10 + c));
    }
  Nv12Frame n;
  CopyNv12(y.data(), stride_y, uv.data(), stride_uv, w, h, &n);
  SM_EXPECT_EQ(n.y.size(), size_t{24});
  SM_EXPECT_EQ(n.uv.size(), size_t{12});
  SM_EXPECT_EQ(n.y[7], uint8_t{11});
  SM_EXPECT_EQ(n.uv[7], uint8_t{210});
}

SM_TEST(DeviceNv12, OddSizes) {
  const int w = 5, h = 3;  // chroma 3 x 2
  std::vector<uint8_t> y(15, 1), uv(12, 2);
  uv[11] = 9;
  I420Frame f;
  Nv12ToI420(y.data(), w, uv.data(), 6, w, h, &f);
  SM_EXPECT_EQ(f.u.size(), size_t{6});
  SM_EXPECT_EQ(f.v[5], uint8_t{9});
}

SM_TEST(DeviceScale, SameSizeIsACopyAndFlatStaysFlat) {
  I420Frame in = sm::video::MakeTestFrame(64, 48, 3);
  SM_EXPECT_TRUE(CropAndScaleI420(in, 64, 48) == in);
  I420Frame flat(1280, 720);
  for (auto& p : flat.y) p = 77;
  for (auto& p : flat.u) p = 99;
  for (auto& p : flat.v) p = 111;
  I420Frame out = CropAndScaleI420(flat, 640, 480);
  SM_ASSERT_EQ(out.width, 640);
  SM_ASSERT_EQ(out.height, 480);
  for (uint8_t p : out.y) SM_ASSERT_EQ(p, uint8_t{77});
  for (uint8_t p : out.u) SM_ASSERT_EQ(p, uint8_t{99});
  for (uint8_t p : out.v) SM_ASSERT_EQ(p, uint8_t{111});
}

SM_TEST(DeviceScale, CropsTheSidesForANarrowerAspect) {
  // 16:9 with dark side bars outside the 4:3 centre: a 4:3 output shows none.
  I420Frame in(1280, 720);
  for (int r = 0; r < 720; ++r)
    for (int c = 0; c < 1280; ++c) in.Y(c, r) = (c < 160 || c >= 1120) ? 0 : 200;
  I420Frame out = CropAndScaleI420(in, 320, 240);
  for (uint8_t p : out.y) SM_ASSERT_EQ(p, uint8_t{200});
}

SM_TEST(DeviceScale, DownAndUpKeepsThePicture) {
  I420Frame in = sm::video::MakeTestFrame(640, 480, 5);
  I420Frame small = CropAndScaleI420(in, 320, 240);
  I420Frame back = CropAndScaleI420(small, 640, 480);
  const double psnr = sm::video::PsnrY(in, back);
  SM_RECORD("psnr_db", psnr);
  SM_EXPECT_GT(psnr, 25.0);
}

// RTCCameraVideoCapturer's rotation (#1116): what the CVO extension carries for each device orientation.
SM_TEST(DeviceCaptureRotation, FollowsRtcCameraVideoCapturer) {
  using sm::video::Rotation;
  for (bool front : {true, false}) {
    SM_EXPECT_TRUE(CaptureRotation(DeviceOrientation::kPortrait, front, Rotation::k0) == Rotation::k90);
    SM_EXPECT_TRUE(CaptureRotation(DeviceOrientation::kPortraitUpsideDown, front, Rotation::k0) == Rotation::k270);
    // Face up, face down and unknown keep the last rotation.
    SM_EXPECT_TRUE(CaptureRotation(DeviceOrientation::kFaceUp, front, Rotation::k270) == Rotation::k270);
    SM_EXPECT_TRUE(CaptureRotation(DeviceOrientation::kFaceDown, front, Rotation::k90) == Rotation::k90);
    SM_EXPECT_TRUE(CaptureRotation(DeviceOrientation::kUnknown, front, Rotation::k180) == Rotation::k180);
  }
  SM_EXPECT_TRUE(CaptureRotation(DeviceOrientation::kLandscapeLeft, true, Rotation::k90) == Rotation::k180);
  SM_EXPECT_TRUE(CaptureRotation(DeviceOrientation::kLandscapeLeft, false, Rotation::k90) == Rotation::k0);
  SM_EXPECT_TRUE(CaptureRotation(DeviceOrientation::kLandscapeRight, true, Rotation::k90) == Rotation::k0);
  SM_EXPECT_TRUE(CaptureRotation(DeviceOrientation::kLandscapeRight, false, Rotation::k90) == Rotation::k180);
}
