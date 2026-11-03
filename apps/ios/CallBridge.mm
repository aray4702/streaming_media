// The Objective-C++ bridge from the SwiftUI screens to the C++ call controller (#1116); see CallBridge.h.
#import "CallBridge.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "call_client.h"
#include "sm/device/audio_device.h"
#include "sm/device/camera_capturer.h"

namespace {

sm::Timestamp Now() {
  const auto d = std::chrono::steady_clock::now().time_since_epoch();
  return sm::Timestamp::Micros(std::chrono::duration_cast<std::chrono::microseconds>(d).count() + 10'000'000);
}

// The device's 10 ms frames to and from the controller (on the audio thread).
class AudioAdapter final : public sm::device::AudioTransport {
 public:
  explicit AudioAdapter(sm::apps::CallClient* client) : client_(client) {}
  void RecordedDataIsAvailable(const int16_t* samples, size_t samples_per_channel, int channels, int sample_rate_hz,
                               int total_delay_ms) override {
    if (channels != 1 || sample_rate_hz != 48000) return;  // The controller runs at 48 kHz mono, as the device does.
    client_->PushCapturedAudio(std::span<const int16_t>(samples, samples_per_channel), total_delay_ms);
  }
  size_t NeedMorePlayData(int16_t* out, size_t samples_per_channel, int channels, int sample_rate_hz) override {
    std::fill(out, out + samples_per_channel * static_cast<size_t>(channels), int16_t{0});
    if (channels != 1 || sample_rate_hz != 48000) return samples_per_channel;
    client_->PullPlayout(std::span<int16_t>(out, samples_per_channel));
    return samples_per_channel;
  }

 private:
  sm::apps::CallClient* client_;
};

UIImage* ToImage(const sm::video::I420Frame& f) {
  const int w = f.width, h = f.height;
  if (w <= 0 || h <= 0) return nil;
  std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      // BT.601 full range: the decoder's NV12 output is full range (videotoolbox_h264.h).
      const int Y = f.y[static_cast<size_t>(y) * w + x];
      const size_t ci = static_cast<size_t>(y / 2) * f.chroma_width() + x / 2;
      const int U = f.u[ci] - 128, V = f.v[ci] - 128;
      auto c = [](int v) { return static_cast<uint8_t>(std::clamp(v >> 8, 0, 255)); };
      uint8_t* p = &rgba[(static_cast<size_t>(y) * w + x) * 4];
      p[0] = c(256 * Y + 359 * V + 128);
      p[1] = c(256 * Y - 88 * U - 183 * V + 128);
      p[2] = c(256 * Y + 454 * U + 128);
      p[3] = 255;
    }
  }
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGContextRef ctx = CGBitmapContextCreate(rgba.data(), w, h, 8, static_cast<size_t>(w) * 4, cs,
                                           kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big);
  CGImageRef img = ctx ? CGBitmapContextCreateImage(ctx) : nullptr;
  UIImage* out = img ? [UIImage imageWithCGImage:img] : nil;
  if (img) CGImageRelease(img);
  if (ctx) CGContextRelease(ctx);
  CGColorSpaceRelease(cs);
  return out;
}

}  // namespace

@implementation SMCallBridge {
  std::unique_ptr<sm::apps::CallClient> _client;
  std::unique_ptr<AudioAdapter> _audioAdapter;
  std::unique_ptr<sm::device::AudioDevice> _audio;
  std::unique_ptr<sm::device::CameraCapturer> _camera;
  std::thread _thread;
  std::atomic<bool> _running;
  std::atomic<bool> _hangupRequested;
  std::atomic<bool> _wsClosed;
  std::mutex _inboxMu;
  std::vector<std::string> _inbox;
  NSURLSession* _session;
  NSURLSessionWebSocketTask* _ws;
  NSString* _status;
  int _generation;
  BOOL _muted, _videoEnabled;
}

+ (NSString*)newAiRoom {
  std::random_device rd;
  return [NSString stringWithFormat:@"ai-%06u", static_cast<unsigned>(rd() % 1000000)];
}

- (instancetype)init {
  if ((self = [super init])) {
    _status = @"idle";
    _videoEnabled = YES;
    _session = [NSURLSession sessionWithConfiguration:NSURLSessionConfiguration.defaultSessionConfiguration];
  }
  return self;
}

- (void)dealloc {
  [self stopCall];
}

- (BOOL)active {
  return _client != nullptr || [_status isEqualToString:@"fetching /turn"];
}

- (NSString*)stateText {
  if (!_client) return _status;
  return [NSString stringWithUTF8String:sm::apps::CallStateName(_client->state())];
}

- (NSString*)statsText {
  if (!_client) return @"";
  return [NSString stringWithUTF8String:_client->stats().Text().c_str()];
}

- (UIImage*)takeRemoteFrame {
  sm::video::I420Frame f;
  if (!_client || !_client->TakeRemoteVideo(&f)) return nil;
  return ToImage(f);
}

- (BOOL)muted {
  return _muted;
}
- (void)setMuted:(BOOL)muted {
  _muted = muted;
  if (_client) _client->SetMuted(muted);
}
- (BOOL)videoEnabled {
  return _videoEnabled;
}
- (void)setVideoEnabled:(BOOL)on {
  _videoEnabled = on;
  if (!_client) return;
  _client->SetVideoEnabled(on);
  if (on) [self startCamera];
  else if (_camera) _camera->Stop();
}

- (void)startWithServer:(NSString*)server room:(NSString*)room video:(BOOL)video {
  [self stopCall];
  _videoEnabled = video;
  NSString* base = [server stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
  while ([base hasSuffix:@"/"]) base = [base substringToIndex:base.length - 1];
  NSURL* turnURL = [NSURL URLWithString:[base stringByAppendingString:@"/turn"]];
  NSString* wsBase = [base hasPrefix:@"https://"] ? [@"wss://" stringByAppendingString:[base substringFromIndex:8]]
                     : [base hasPrefix:@"http://"] ? [@"ws://" stringByAppendingString:[base substringFromIndex:7]]
                                                   : base;
  NSURL* wsURL = [NSURL URLWithString:[wsBase stringByAppendingString:@"/ws"]];
  // NSURLSession throws for a WebSocket task on any other scheme (a server typed without https://).
  const BOOL wsScheme = [wsURL.scheme isEqualToString:@"wss"] || [wsURL.scheme isEqualToString:@"ws"];
  if (!turnURL || !wsURL || !wsScheme || room.length == 0) {
    _status = @"bad server URL or room";
    return;
  }
  _status = @"fetching /turn";
  NSLog(@"SmCall: call %@ via %@", room, base);
  const int generation = ++_generation;
  std::string roomCode = room.UTF8String;
  __weak SMCallBridge* weakSelf = self;
  NSURLRequest* req = [NSURLRequest requestWithURL:turnURL cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                   timeoutInterval:5];
  [[_session dataTaskWithRequest:req
               completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                 NSInteger code = [response isKindOfClass:NSHTTPURLResponse.class]
                                      ? ((NSHTTPURLResponse*)response).statusCode : 0;
                 NSLog(@"SmCall: GET /turn: HTTP %ld%@", (long)code, error ? error.localizedDescription : @"");
                 std::string body;
                 if (!error && code == 200 && data) body.assign(static_cast<const char*>(data.bytes), data.length);
                 dispatch_async(dispatch_get_main_queue(), ^{
                   SMCallBridge* s = weakSelf;
                   if (s) [s beginCall:roomCode turn:body ws:wsURL generation:generation];
                 });
               }] resume];
}

- (void)beginCall:(const std::string&)room turn:(const std::string&)turn ws:(NSURL*)wsURL generation:(int)generation {
  if (generation != _generation || _client) return;  // Hung up or restarted meanwhile.
  _ws = [_session webSocketTaskWithURL:wsURL];
  NSURLSessionWebSocketTask* ws = _ws;
  sm::apps::CallClientConfig cfg;
  cfg.room = room;
  cfg.verbose = true;
  cfg.relay_only = self.relayOnly;
  // libwebrtc's iOS client (#1124): VoiceProcessingIO's echo canceller and AGC, the software AEC3 and AGC off.
  sm::apps::ApplyIosAudioOptions(&cfg);
  _client = std::make_unique<sm::apps::CallClient>(cfg, [ws](const std::string& m) {
    NSString* text = [[NSString alloc] initWithBytes:m.data() length:m.size() encoding:NSUTF8StringEncoding];
    [ws sendMessage:[[NSURLSessionWebSocketMessage alloc] initWithString:text] completionHandler:^(NSError*) {}];
  });
  if (!turn.empty()) _client->SetTurnResponse(turn);
  _client->SetMuted(_muted);
  _client->SetVideoEnabled(_videoEnabled);
  _wsClosed = false;
  _hangupRequested = false;
  {
    std::lock_guard<std::mutex> lock(_inboxMu);
    _inbox.clear();
  }
  [ws resume];
  [self receiveOn:ws];

  _audioAdapter = std::make_unique<AudioAdapter>(_client.get());
  sm::device::AudioDeviceConfig audioConfig;
  audioConfig.echo_canceller = sm::device::EchoCanceller::kSystem;  // VoiceProcessingIO's processing on (#1124).
  _audio = sm::device::CreatePlatformAudioDevice(audioConfig);
  if (_audio) {
    _audio->RegisterAudioCallback(_audioAdapter.get());
    if (_audio->InitPlayout()) _audio->StartPlayout();
    if (_audio->InitRecording()) _audio->StartRecording();
  }
  if (_videoEnabled) [self startCamera];

  _running = true;
  sm::apps::CallClient* client = _client.get();
  _thread = std::thread([self, client] {
    sm::Timestamp now = Now();
    client->Start(now);
    while (_running) {
      std::vector<std::string> msgs;
      {
        std::lock_guard<std::mutex> lock(_inboxMu);
        msgs.swap(_inbox);
      }
      now = Now();
      for (const std::string& m : msgs) client->OnSignal(m, now);
      if (_wsClosed) client->OnSignalingClosed(now);
      if (_hangupRequested.exchange(false)) client->Hangup(now);
      const sm::Timestamp next = client->Step(now);
      client->Wait(now, next - now);
    }
  });
}

- (void)receiveOn:(NSURLSessionWebSocketTask*)ws {
  __weak SMCallBridge* weakSelf = self;
  [ws receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage* msg, NSError* error) {
    SMCallBridge* s = weakSelf;
    if (!s || s->_ws != ws) return;
    if (error) {
      NSLog(@"SmCall: signaling closed: %@", error.localizedDescription);
      s->_wsClosed = true;
      return;
    }
    if (msg.type == NSURLSessionWebSocketMessageTypeString && msg.string) {
      std::lock_guard<std::mutex> lock(s->_inboxMu);
      s->_inbox.emplace_back(msg.string.UTF8String);
    }
    [s receiveOn:ws];
  }];
}

- (void)startCamera {
  if (!_client) return;
  if (!_camera) _camera = sm::device::CreatePlatformCameraCapturer();
  if (!_camera || _camera->IsRunning()) return;
  sm::device::CameraConfig cfg;
  cfg.width = 640;
  cfg.height = 480;
  cfg.fps = 30;
  cfg.position = sm::device::CameraPosition::kFront;
  sm::apps::CallClient* client = _client.get();
  _camera->Start(cfg, [client](const sm::device::CapturedFrame& f) {
    if (f.format == sm::device::CapturePixelFormat::kI420) client->PushCameraFrame(f.i420, f.rotation);
  });
}

- (void)hangup {
  ++_generation;
  if (!_client) {
    _status = @"idle";
    return;
  }
  _hangupRequested = true;
  // Let the call thread send bye, then take the call down.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    [self stopCall];
    self->_status = @"ended";
  });
}

- (void)stopCall {
  if (_camera) _camera->Stop();
  if (_audio) {
    _audio->StopRecording();
    _audio->StopPlayout();
  }
  _running = false;
  if (_thread.joinable()) _thread.join();
  [_ws cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
  _ws = nil;
  // One JSON line per call (#1121), as sm_call_smoke and the web app write it: the call's last stats, in the log.
  if (_client) NSLog(@"SmCall: call-result %s", _client->stats().ToJson().c_str());
  _camera.reset();
  _audio.reset();
  _client.reset();
  _audioAdapter.reset();
}

@end
