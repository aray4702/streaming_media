// VideoToolbox H.264 encoder and decoder (issue #1104); see the header.
#include "sm/video/videotoolbox_h264.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <TargetConditionals.h>
#include <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "sm/video/bitrate_adjuster.h"

namespace sm::video {
namespace {

constexpr uint8_t kStartCode[4] = {0, 0, 0, 1};
constexpr size_t kAvccHeaderSize = 4;
// RTCVideoEncoderH264.mm: kNV12PixelFormat, kLimitToAverageBitRateFactor,
// the key-frame interval limits and the BitrateAdjuster's bounds.
constexpr OSType kNv12PixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
constexpr double kLimitToAverageBitRateFactor = 1.5;
constexpr int64_t kMaxKeyFrameInterval = 7200;
constexpr int64_t kMaxKeyFrameIntervalDurationSec = 240;
constexpr float kMinAdjustedBitratePct = 0.5f;
constexpr float kMaxAdjustedBitratePct = 0.95f;
constexpr uint8_t kNaluTypeMask = 0x1f;
constexpr uint8_t kNaluSps = 7;
constexpr uint8_t kNaluPps = 8;

struct Nalu {
  const uint8_t* data;
  size_t size;
};

// H264::FindNaluIndices: the units of an Annex B buffer (3- or 4-byte start
// codes), without their start codes.
std::vector<Nalu> FindNalus(std::span<const uint8_t> buf) {
  std::vector<size_t> starts;
  const size_t n = buf.size();
  for (size_t i = 0; i + 3 <= n;) {
    if (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1) {
      starts.push_back(i + 3);
      i += 3;
    } else {
      ++i;
    }
  }
  std::vector<Nalu> out;
  for (size_t k = 0; k < starts.size(); ++k) {
    size_t end = n;
    if (k + 1 < starts.size()) {
      end = starts[k + 1] - 3;
      if (end > starts[k] && buf[end - 1] == 0) --end;  // The leading zero of a 4-byte start code.
    }
    if (end > starts[k]) out.push_back({buf.data() + starts[k], end - starts[k]});
  }
  return out;
}

uint8_t NaluType(const Nalu& u) { return static_cast<uint8_t>(u.data[0] & kNaluTypeMask); }

CFDictionaryRef MakeDict(const void** keys, const void** values, CFIndex n) {
  return CFDictionaryCreate(kCFAllocatorDefault, keys, values, n, &kCFTypeDictionaryKeyCallBacks,
                            &kCFTypeDictionaryValueCallBacks);
}

void SetIntProperty(VTSessionRef s, CFStringRef key, int64_t value) {
  CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &value);
  VTSessionSetProperty(s, key, n);
  CFRelease(n);
}

void SetBoolProperty(VTSessionRef s, CFStringRef key, bool value) {
  VTSessionSetProperty(s, key, value ? kCFBooleanTrue : kCFBooleanFalse);
}

// The pixel-buffer attributes both sessions use: IOSurface backed, NV12 full range.
CFDictionaryRef MakePixelBufferAttributes(bool metal_compatible) {
  CFDictionaryRef io_surface = MakeDict(nullptr, nullptr, 0);
  int64_t format = kNv12PixelFormat;
  CFNumberRef pixel_format = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &format);
  const void* keys[3] = {kCVPixelBufferIOSurfacePropertiesKey, kCVPixelBufferPixelFormatTypeKey,
                         kCVPixelBufferMetalCompatibilityKey};
  const void* values[3] = {io_surface, pixel_format, kCFBooleanTrue};
  CFDictionaryRef attrs = MakeDict(keys, values, metal_compatible ? 3 : 2);
  CFRelease(io_surface);
  CFRelease(pixel_format);
  return attrs;
}

// CopyVideoFrameToNV12PixelBuffer (libyuv I420ToNV12).
bool CopyI420ToNv12(const I420Frame& f, CVPixelBufferRef pb) {
  if (CVPixelBufferLockBaseAddress(pb, 0) != kCVReturnSuccess) return false;
  auto* dst_y = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pb, 0));
  auto* dst_uv = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pb, 1));
  const size_t stride_y = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
  const size_t stride_uv = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
  for (int r = 0; r < f.height; ++r) {
    std::memcpy(dst_y + static_cast<size_t>(r) * stride_y, f.y.data() + static_cast<size_t>(r) * f.width,
                static_cast<size_t>(f.width));
  }
  const int cw = f.chroma_width();
  for (int r = 0; r < f.chroma_height(); ++r) {
    uint8_t* row = dst_uv + static_cast<size_t>(r) * stride_uv;
    const size_t src = static_cast<size_t>(r) * cw;
    for (int c = 0; c < cw; ++c) {
      row[2 * c] = f.u[src + c];
      row[2 * c + 1] = f.v[src + c];
    }
  }
  CVPixelBufferUnlockBaseAddress(pb, 0);
  return true;
}

bool CopyNv12ToI420(CVImageBufferRef pb, I420Frame& out) {
  if (CVPixelBufferGetPixelFormatType(pb) != kNv12PixelFormat) return false;
  if (CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) return false;
  const int w = static_cast<int>(CVPixelBufferGetWidth(pb));
  const int h = static_cast<int>(CVPixelBufferGetHeight(pb));
  out = I420Frame(w, h);
  const auto* src_y = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pb, 0));
  const auto* src_uv = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pb, 1));
  const size_t stride_y = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
  const size_t stride_uv = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
  for (int r = 0; r < h; ++r) {
    std::memcpy(out.y.data() + static_cast<size_t>(r) * w, src_y + static_cast<size_t>(r) * stride_y,
                static_cast<size_t>(w));
  }
  const int cw = out.chroma_width();
  for (int r = 0; r < out.chroma_height(); ++r) {
    const uint8_t* row = src_uv + static_cast<size_t>(r) * stride_uv;
    const size_t dst = static_cast<size_t>(r) * cw;
    for (int c = 0; c < cw; ++c) {
      out.u[dst + c] = row[2 * c];
      out.v[dst + c] = row[2 * c + 1];
    }
  }
  CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
  return true;
}

// H264CMSampleBufferToAnnexBBuffer (nalu_rewriter.cc): the SPS and PPS from
// the format description first on a keyframe, then each AVCC unit with a
// start code in place of its length.
bool SampleBufferToAnnexB(CMSampleBufferRef sb, bool keyframe, std::vector<uint8_t>& out) {
  CMVideoFormatDescriptionRef desc = CMSampleBufferGetFormatDescription(sb);
  if (!desc) return false;
  int nalu_header_size = 0;
  size_t param_set_count = 0;
  if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(desc, 0, nullptr, nullptr, &param_set_count,
                                                         &nalu_header_size) != noErr ||
      nalu_header_size != static_cast<int>(kAvccHeaderSize)) {
    return false;
  }
  if (keyframe) {
    for (size_t i = 0; i < param_set_count; ++i) {
      const uint8_t* ps = nullptr;
      size_t ps_size = 0;
      if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(desc, i, &ps, &ps_size, nullptr, nullptr) != noErr) {
        return false;
      }
      out.insert(out.end(), kStartCode, kStartCode + 4);
      out.insert(out.end(), ps, ps + ps_size);
    }
  }
  CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sb);
  if (!block) return false;
  CMBlockBufferRef contiguous = nullptr;
  if (CMBlockBufferIsRangeContiguous(block, 0, 0)) {
    contiguous = block;
    CFRetain(contiguous);
  } else if (CMBlockBufferCreateContiguous(kCFAllocatorDefault, block, kCFAllocatorDefault, nullptr, 0, 0, 0,
                                           &contiguous) != kCMBlockBufferNoErr) {
    return false;
  }
  size_t length = 0;
  char* data = nullptr;
  bool ok = CMBlockBufferGetDataPointer(contiguous, 0, nullptr, &length, &data) == kCMBlockBufferNoErr;
  const auto* p = reinterpret_cast<const uint8_t*>(data);
  size_t remaining = ok ? length : 0;
  while (ok && remaining > 0) {
    if (remaining < kAvccHeaderSize) {
      ok = false;
      break;
    }
    const size_t unit = (static_cast<size_t>(p[0]) << 24) | (static_cast<size_t>(p[1]) << 16) |
                        (static_cast<size_t>(p[2]) << 8) | static_cast<size_t>(p[3]);
    if (unit > remaining - kAvccHeaderSize) {
      ok = false;
      break;
    }
    out.insert(out.end(), kStartCode, kStartCode + 4);
    out.insert(out.end(), p + kAvccHeaderSize, p + kAvccHeaderSize + unit);
    p += kAvccHeaderSize + unit;
    remaining -= kAvccHeaderSize + unit;
  }
  CFRelease(contiguous);
  return ok;
}

}  // namespace

// ---------------------------------------------------------------- encoder

struct VideoToolboxH264Encoder::Impl {
  VTCompressionSessionRef session = nullptr;
  int width = 0;
  int height = 0;
  BitrateAdjuster adjuster{kMinAdjustedBitratePct, kMaxAdjustedBitratePct};
  double max_framerate = 30;
  uint32_t encoder_bitrate_bps = 0;
  double encoder_framerate = 0;
  // Set by the output callback for the frame being encoded.
  std::optional<H264EncodedFrame>* pending = nullptr;
  bool callback_error = false;
  bool callback_dropped = false;

  ~Impl() { DestroySession(); }

  void DestroySession() {
    if (session) {
      VTCompressionSessionInvalidate(session);
      CFRelease(session);
      session = nullptr;
    }
  }

  static void OutputCallback(void* refcon, void* /*source_frame*/, OSStatus status, VTEncodeInfoFlags flags,
                             CMSampleBufferRef sb) {
    auto* self = static_cast<Impl*>(refcon);
    if (status != noErr) {
      self->callback_error = true;
      return;
    }
    if ((flags & kVTEncodeInfo_FrameDropped) || !sb) {
      self->callback_dropped = true;
      return;
    }
    bool keyframe = true;
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sb, false);
    if (attachments && CFArrayGetCount(attachments) > 0) {
      auto dict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
      keyframe = !CFDictionaryContainsKey(dict, kCMSampleAttachmentKey_NotSync);
    }
    H264EncodedFrame f;
    f.keyframe = keyframe;
    if (!SampleBufferToAnnexB(sb, keyframe, f.annexb)) {
      self->callback_error = true;
      return;
    }
    if (self->pending) *self->pending = std::move(f);
  }

  // setEncoderBitrateBps:frameRate:.
  void SetEncoderBitrate(uint32_t bps, double fps) {
    if (session) {
      SetIntProperty(session, kVTCompressionPropertyKey_AverageBitRate, bps);
      if (fps > 0) SetIntProperty(session, kVTCompressionPropertyKey_ExpectedFrameRate, std::llround(fps));
      int64_t bytes_per_second = static_cast<int64_t>(bps * kLimitToAverageBitRateFactor / 8);
      int64_t one_second = 1;
      CFNumberRef bytes = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &bytes_per_second);
      CFNumberRef seconds = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &one_second);
      const void* nums[2] = {bytes, seconds};
      CFArrayRef limits = CFArrayCreate(kCFAllocatorDefault, nums, 2, &kCFTypeArrayCallBacks);
      VTSessionSetProperty(session, kVTCompressionPropertyKey_DataRateLimits, limits);
      CFRelease(bytes);
      CFRelease(seconds);
      CFRelease(limits);
    }
    encoder_bitrate_bps = bps;
    encoder_framerate = fps;
  }

  // setBitrateBps:frameRate:.
  void SetBitrate(uint32_t bps, double fps) {
    if (encoder_bitrate_bps != bps || encoder_framerate != fps) SetEncoderBitrate(bps, fps);
  }

  // resetCompressionSession + configureCompressionSession.
  bool ResetSession() {
    DestroySession();
    CFDictionaryRef specs = nullptr;
#if TARGET_OS_OSX
    const void* spec_keys[1] = {kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder};
    const void* spec_values[1] = {kCFBooleanTrue};
    specs = MakeDict(spec_keys, spec_values, 1);
#endif
    CFDictionaryRef source_attrs = MakePixelBufferAttributes(/*metal_compatible=*/false);
    const OSStatus status = VTCompressionSessionCreate(kCFAllocatorDefault, width, height, kCMVideoCodecType_H264,
                                                       specs, source_attrs, kCFAllocatorDefault, &OutputCallback,
                                                       this, &session);
    if (specs) CFRelease(specs);
    CFRelease(source_attrs);
    if (status != noErr) {
      session = nullptr;
      return false;
    }
    SetBoolProperty(session, kVTCompressionPropertyKey_RealTime, true);
    // profile-level-id 42e01f: Constrained Baseline, level 3.1.
    VTSessionSetProperty(session, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_Baseline_3_1);
    SetBoolProperty(session, kVTCompressionPropertyKey_AllowFrameReordering, false);
    SetEncoderBitrate(adjuster.GetAdjustedBitrateBps(), encoder_framerate);
    SetIntProperty(session, kVTCompressionPropertyKey_MaxKeyFrameInterval, kMaxKeyFrameInterval);
    SetIntProperty(session, kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, kMaxKeyFrameIntervalDurationSec);
    return true;
  }
};

VideoToolboxH264Encoder::VideoToolboxH264Encoder(VideoToolboxEncoderConfig cfg)
    : cfg_(cfg), impl_(std::make_unique<Impl>()) {}

VideoToolboxH264Encoder::~VideoToolboxH264Encoder() = default;

EncoderStatus VideoToolboxH264Encoder::InitEncode(const EncoderCodecSettings& settings) {
  if (settings.width <= 0 || settings.height <= 0) return EncoderStatus::kError;
  impl_->width = settings.width;
  impl_->height = settings.height;
  impl_->adjuster.SetTargetBitrateBps(static_cast<uint32_t>(cfg_.start_bitrate.bps()));
  impl_->max_framerate = cfg_.max_framerate;
  impl_->encoder_framerate = cfg_.max_framerate;
  return impl_->ResetSession() ? EncoderStatus::kOk : EncoderStatus::kError;
}

void VideoToolboxH264Encoder::Release() { impl_->DestroySession(); }

void VideoToolboxH264Encoder::SetRates(const EncoderRates& rates) {
  impl_->adjuster.SetTargetBitrateBps(static_cast<uint32_t>(std::max<int64_t>(rates.bitrate.bps(), 0)));
  double fps = rates.framerate_fps;
  if (impl_->max_framerate > 0 && fps > impl_->max_framerate) fps = impl_->max_framerate;
  impl_->SetBitrate(impl_->adjuster.GetAdjustedBitrateBps(), fps);
}

FallbackEncoderInfo VideoToolboxH264Encoder::GetEncoderInfo() const { return FallbackEncoderInfo{}; }

DataRate VideoToolboxH264Encoder::encoder_bitrate() const { return DataRate::BitsPerSec(impl_->encoder_bitrate_bps); }

EncoderStatus VideoToolboxH264Encoder::Encode(const I420Frame& frame, uint32_t rtp_timestamp, bool request_keyframe,
                                              Timestamp now, std::optional<H264EncodedFrame>& out) {
  out.reset();
  ++stats_.frames_in;
  if (!impl_->session || frame.width <= 0 || frame.height <= 0) {
    ++stats_.errors;
    return EncoderStatus::kError;
  }
  // A frame of another size, or a session whose pool is gone (invalidated,
  // e.g. on iOS backgrounding), restarts the session.
  CVPixelBufferPoolRef pool = VTCompressionSessionGetPixelBufferPool(impl_->session);
  if (frame.width != impl_->width || frame.height != impl_->height || !pool) {
    impl_->width = frame.width;
    impl_->height = frame.height;
    ++stats_.session_restarts;
    SM_EVENT(cfg_.events, now, "videotoolbox_h264", "encoder_session_restart", frame.width, frame.height);
    if (!impl_->ResetSession()) {
      ++stats_.errors;
      return EncoderStatus::kError;
    }
    pool = VTCompressionSessionGetPixelBufferPool(impl_->session);
    if (!pool) {
      ++stats_.errors;
      return EncoderStatus::kError;
    }
  }
  CVPixelBufferRef pb = nullptr;
  if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &pb) != kCVReturnSuccess || !pb) {
    ++stats_.errors;
    return EncoderStatus::kError;
  }
  if (!CopyI420ToNv12(frame, pb)) {
    CVPixelBufferRelease(pb);
    ++stats_.errors;
    return EncoderStatus::kError;
  }
  // Follow the adjuster's rate before each frame, as RTCVideoEncoderH264 does.
  impl_->SetBitrate(impl_->adjuster.GetAdjustedBitrateBps(), impl_->encoder_framerate);

  CFDictionaryRef props = nullptr;
  if (request_keyframe) {
    const void* keys[1] = {kVTEncodeFrameOptionKey_ForceKeyFrame};
    const void* values[1] = {kCFBooleanTrue};
    props = MakeDict(keys, values, 1);
    SM_EVENT(cfg_.events, now, "videotoolbox_h264", "keyframe_requested", 1, rtp_timestamp);
  }
  const CMTime pts = CMTimeMake(now.ms(), 1000);
  impl_->pending = &out;
  impl_->callback_error = false;
  impl_->callback_dropped = false;
  OSStatus status = VTCompressionSessionEncodeFrame(impl_->session, pb, pts, kCMTimeInvalid, props, nullptr, nullptr);
  if (status == noErr) status = VTCompressionSessionCompleteFrames(impl_->session, pts);
  impl_->pending = nullptr;
  if (props) CFRelease(props);
  CVPixelBufferRelease(pb);
  if (status != noErr || impl_->callback_error) {
    out.reset();
    ++stats_.errors;
    SM_EVENT(cfg_.events, now, "videotoolbox_h264", "encode_error", status, rtp_timestamp);
    return EncoderStatus::kError;
  }
  if (!out) {
    ++stats_.dropped;
    return EncoderStatus::kOk;
  }
  out->rtp_timestamp = rtp_timestamp;
  out->width = frame.width;
  out->height = frame.height;
  ++stats_.frames_out;
  if (out->keyframe) ++stats_.keyframes;
  stats_.bytes_out += out->annexb.size();
  impl_->adjuster.Update(out->annexb.size(), now);
  return EncoderStatus::kOk;
}

void VideoToolboxH264Encoder::ExportMetrics(MetricsWriter& w) const {
  w.Counter("videotoolbox_h264.encoder.frames_in", stats_.frames_in);
  w.Counter("videotoolbox_h264.encoder.frames_out", stats_.frames_out);
  w.Counter("videotoolbox_h264.encoder.keyframes", stats_.keyframes);
  w.Counter("videotoolbox_h264.encoder.dropped", stats_.dropped);
  w.Counter("videotoolbox_h264.encoder.errors", stats_.errors);
  w.Counter("videotoolbox_h264.encoder.bytes_out", stats_.bytes_out);
  w.Counter("videotoolbox_h264.encoder.session_restarts", stats_.session_restarts);
  w.Gauge("videotoolbox_h264.encoder.target_bps", impl_->adjuster.GetTargetBitrateBps());
  w.Gauge("videotoolbox_h264.encoder.adjusted_bps", impl_->encoder_bitrate_bps);
}

// ---------------------------------------------------------------- decoder

struct VideoToolboxH264Decoder::Impl {
  VTDecompressionSessionRef session = nullptr;
  CMVideoFormatDescriptionRef format = nullptr;
  bool configured = false;
  I420Frame* out = nullptr;
  bool got_frame = false;
  bool callback_error = false;

  ~Impl() {
    DestroySession();
    SetFormat(nullptr);
  }

  void DestroySession() {
    if (session) {
      VTDecompressionSessionWaitForAsynchronousFrames(session);
      VTDecompressionSessionInvalidate(session);
      CFRelease(session);
      session = nullptr;
    }
  }

  void SetFormat(CMVideoFormatDescriptionRef f) {
    if (f) CFRetain(f);
    if (format) CFRelease(format);
    format = f;
  }

  static void OutputCallback(void* refcon, void* /*source_frame*/, OSStatus status, VTDecodeInfoFlags /*flags*/,
                             CVImageBufferRef image, CMTime /*pts*/, CMTime /*duration*/) {
    auto* self = static_cast<Impl*>(refcon);
    if (status != noErr || !image || !self->out) {
      self->callback_error = true;
      return;
    }
    if (!CopyNv12ToI420(image, *self->out)) {
      self->callback_error = true;
      return;
    }
    self->got_frame = true;
  }

  // resetDecompressionSession: waits for the first SPS before it can start.
  bool ResetSession() {
    DestroySession();
    if (!format) return true;
    CFDictionaryRef attrs = MakePixelBufferAttributes(/*metal_compatible=*/true);
    VTDecompressionOutputCallbackRecord record = {&OutputCallback, this};
    const OSStatus status =
        VTDecompressionSessionCreate(kCFAllocatorDefault, format, nullptr, attrs, &record, &session);
    CFRelease(attrs);
    if (status != noErr) {
      session = nullptr;
      return false;
    }
#if TARGET_OS_IPHONE
    SetBoolProperty(session, kVTDecompressionPropertyKey_RealTime, true);
#endif
    return true;
  }
};

namespace {

// CreateVideoFormatDescription (nalu_rewriter.cc): the first SPS and the unit
// after it, the PPS.
CMVideoFormatDescriptionRef CreateFormatDescription(const std::vector<Nalu>& units) {
  for (size_t i = 0; i < units.size(); ++i) {
    if (NaluType(units[i]) != kNaluSps) continue;
    if (i + 1 >= units.size() || NaluType(units[i + 1]) != kNaluPps) return nullptr;
    const uint8_t* sets[2] = {units[i].data, units[i + 1].data};
    const size_t sizes[2] = {units[i].size, units[i + 1].size};
    CMVideoFormatDescriptionRef desc = nullptr;
    if (CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault, 2, sets, sizes,
                                                            static_cast<int>(kAvccHeaderSize), &desc) != noErr) {
      return nullptr;
    }
    return desc;
  }
  return nullptr;
}

// H264AnnexBBufferToCMSampleBuffer (nalu_rewriter.cc): with an SPS, the units
// from the one after its PPS on; otherwise all of them; in AVCC form.
CMSampleBufferRef CreateSampleBuffer(const std::vector<Nalu>& units, CMVideoFormatDescriptionRef format) {
  size_t first = 0;
  for (size_t i = 0; i < units.size(); ++i) {
    if (NaluType(units[i]) == kNaluSps) {
      first = i + 2;
      break;
    }
  }
  std::vector<uint8_t> avcc;
  for (size_t i = first; i < units.size(); ++i) {
    const size_t s = units[i].size;
    const uint8_t len[4] = {static_cast<uint8_t>(s >> 24), static_cast<uint8_t>(s >> 16),
                            static_cast<uint8_t>(s >> 8), static_cast<uint8_t>(s)};
    avcc.insert(avcc.end(), len, len + 4);
    avcc.insert(avcc.end(), units[i].data, units[i].data + s);
  }
  if (avcc.empty()) return nullptr;
  CMBlockBufferRef block = nullptr;
  if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr, avcc.size(), kCFAllocatorDefault, nullptr, 0,
                                         avcc.size(), kCMBlockBufferAssureMemoryNowFlag,
                                         &block) != kCMBlockBufferNoErr) {
    return nullptr;
  }
  if (CMBlockBufferReplaceDataBytes(avcc.data(), block, 0, avcc.size()) != kCMBlockBufferNoErr) {
    CFRelease(block);
    return nullptr;
  }
  CMSampleBufferRef sb = nullptr;
  const OSStatus status =
      CMSampleBufferCreate(kCFAllocatorDefault, block, true, nullptr, nullptr, format, 1, 0, nullptr, 0, nullptr, &sb);
  CFRelease(block);
  return status == noErr ? sb : nullptr;
}

}  // namespace

VideoToolboxH264Decoder::VideoToolboxH264Decoder(EventSink* events)
    : events_(events), impl_(std::make_unique<Impl>()) {}

VideoToolboxH264Decoder::~VideoToolboxH264Decoder() = default;

bool VideoToolboxH264Decoder::Configure(const DecoderSettings& settings) {
  if (settings.codec_type != DecoderCodecType::kH264) return false;
  impl_->configured = true;
  return true;
}

DecoderStatus VideoToolboxH264Decoder::Release() {
  impl_->DestroySession();
  impl_->SetFormat(nullptr);
  impl_->configured = false;
  return DecoderStatus::kOk;
}

FallbackDecoderInfo VideoToolboxH264Decoder::GetDecoderInfo() const {
  return FallbackDecoderInfo{"VideoToolbox", true};
}

DecoderStatus VideoToolboxH264Decoder::Decode(std::span<const uint8_t> annexb, Timestamp now, I420Frame& out) {
  if (!impl_->configured) return DecoderStatus::kUninitialized;
  ++stats_.frames_in;
  const std::vector<Nalu> units = FindNalus(annexb);
  if (CMVideoFormatDescriptionRef input_format = CreateFormatDescription(units)) {
    if (!impl_->format || !CMFormatDescriptionEqual(input_format, impl_->format)) {
      impl_->SetFormat(input_format);
      ++stats_.format_changes;
      SM_EVENT(events_, now, "videotoolbox_h264", "decoder_format_change", 1, 0);
      if (!impl_->ResetSession()) {
        CFRelease(input_format);
        ++stats_.errors;
        return DecoderStatus::kError;
      }
    }
    CFRelease(input_format);
  }
  if (!impl_->format || !impl_->session) {
    // No SPS/PPS yet: the caller asks for a keyframe.
    ++stats_.errors;
    SM_EVENT(events_, now, "videotoolbox_h264", "decoder_missing_format", 1, 0);
    return DecoderStatus::kError;
  }
  CMSampleBufferRef sb = CreateSampleBuffer(units, impl_->format);
  if (!sb) {
    ++stats_.errors;
    return DecoderStatus::kError;
  }
  impl_->out = &out;
  impl_->got_frame = false;
  impl_->callback_error = false;
  VTDecodeInfoFlags info = 0;
  OSStatus status = VTDecompressionSessionDecodeFrame(impl_->session, sb, 0, nullptr, &info);
  if (status == noErr) status = VTDecompressionSessionWaitForAsynchronousFrames(impl_->session);
  impl_->out = nullptr;
  CFRelease(sb);
  if (status != noErr || impl_->callback_error || !impl_->got_frame) {
    ++stats_.errors;
    SM_EVENT(events_, now, "videotoolbox_h264", "decode_error", status, 0);
    return DecoderStatus::kError;
  }
  ++stats_.frames_out;
  return DecoderStatus::kOk;
}

void VideoToolboxH264Decoder::ExportMetrics(MetricsWriter& w) const {
  w.Counter("videotoolbox_h264.decoder.frames_in", stats_.frames_in);
  w.Counter("videotoolbox_h264.decoder.frames_out", stats_.frames_out);
  w.Counter("videotoolbox_h264.decoder.errors", stats_.errors);
  w.Counter("videotoolbox_h264.decoder.format_changes", stats_.format_changes);
}

}  // namespace sm::video
