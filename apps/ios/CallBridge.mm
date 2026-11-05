// The Objective-C++ bridge from the SwiftUI screens to the C++ call controller (#1116); see CallBridge.h.
#import "CallBridge.h"
#import "RemoteVideoView.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "call_client.h"
#include "livekit_proto.h"
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

std::string PercentEncode(NSString* s) {
  return [s stringByAddingPercentEncodingWithAllowedCharacters:NSCharacterSet.URLQueryAllowedCharacterSet].UTF8String;
}

}  // namespace

@interface SMParticipant ()
- (instancetype)initWith:(const sm::apps::GroupParticipant&)p;
@end

@implementation SMParticipant
- (instancetype)initWith:(const sm::apps::GroupParticipant&)p {
  if ((self = [super init])) {
    _identity = [NSString stringWithUTF8String:p.identity.c_str()];
    _speaking = p.speaking;
    _audioLevel = p.audio_level;
    _audioMuted = p.audio_muted;
    _videoMuted = p.video_muted;
    _hasVideo = p.has_video;
    _statsText = [NSString stringWithFormat:@"audio %llu pkts, lost %lld, jitter %.0f ms\nvideo %dx%d %.0f fps",
                                            static_cast<unsigned long long>(p.audio_packets_received),
                                            static_cast<long long>(p.audio_packets_lost), p.audio_jitter_ms,
                                            p.video_width, p.video_height, p.video_fps];
  }
  return self;
}
@end

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
  SMRemoteVideoView* _remoteView;
  // Group calls (C20): one Metal view per participant, the local preview and its latest camera frame.
  BOOL _group;
  NSString* _localIdentity;
  NSMutableDictionary<NSString*, SMRemoteVideoView*>* _tiles;
  SMRemoteVideoView* _localView;
  std::mutex _previewMu;
  std::optional<sm::video::I420Frame> _preview;
  sm::video::Rotation _previewRotation;
  // Rendered and decoded frame rates (#1133): the last second's, and the call's from its first decoded frame.
  CFTimeInterval _rateTime, _firstTime;
  uint64_t _rateDrawn, _rateDecoded, _firstDrawn, _firstDecoded, _callDrawnBase;
  double _renderFps, _decodedFps;
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
    _remoteView = [[SMRemoteVideoView alloc] initWithFrame:CGRectMake(0, 0, 320, 240)];
    __weak SMCallBridge* weakSelf = self;
    // Called by the view at each display refresh: the latest frame the controller rendered, already turned upright
    // by its CVO rotation (call_client.cc), so the view draws it with no further turn.
    [_remoteView setFrameSource:[weakSelf](sm::video::I420Frame* frame, sm::video::Rotation* rotation) {
      SMCallBridge* s = weakSelf;
      if (!s || !s->_client || !s->_client->TakeRemoteVideo(frame)) return false;
      *rotation = sm::video::Rotation::k0;
      return true;
    }];
    _tiles = [NSMutableDictionary dictionary];
    _localIdentity = @"";
    // The local preview: the camera's latest frame, turned by its capture rotation as the encoder's CVO says.
    _localView = [[SMRemoteVideoView alloc] initWithFrame:CGRectMake(0, 0, 160, 120)];
    [_localView setFrameSource:[weakSelf](sm::video::I420Frame* frame, sm::video::Rotation* rotation) {
      SMCallBridge* s = weakSelf;
      if (!s) return false;
      std::lock_guard<std::mutex> lock(s->_previewMu);
      if (!s->_preview) return false;
      *frame = std::move(*s->_preview);
      s->_preview.reset();
      *rotation = s->_previewRotation;
      return true;
    }];
  }
  return self;
}

- (void)dealloc {
  [self stopCall];
}

- (BOOL)active {
  return _client != nullptr || [_status isEqualToString:@"fetching /turn"] || [_status isEqualToString:@"fetching /lk-token"];
}

- (NSString*)stateText {
  if (!_client) return _status;
  return [NSString stringWithUTF8String:sm::apps::CallStateName(_client->state())];
}

- (UIView*)remoteView {
  return _remoteView;
}

- (UIView*)localView {
  return _localView;
}

- (BOOL)inGroup {
  return _group && _client != nullptr;
}

- (NSString*)localIdentity {
  return _localIdentity;
}

- (BOOL)localSpeaking {
  return _client && _group && _client->active_speaker() == std::string(_localIdentity.UTF8String);
}

- (NSArray<SMParticipant*>*)participants {
  NSMutableArray* out = [NSMutableArray array];
  if (!_client || !_group) return out;
  for (const sm::apps::GroupParticipant& p : _client->participants()) [out addObject:[[SMParticipant alloc] initWith:p]];
  return out;
}

- (UIView*)videoViewForParticipant:(NSString*)identity {
  SMRemoteVideoView* view = _tiles[identity];
  if (view) return view;
  view = [[SMRemoteVideoView alloc] initWithFrame:CGRectMake(0, 0, 160, 120)];
  __weak SMCallBridge* weakSelf = self;
  const std::string who = identity.UTF8String;
  // The participant's latest decoded frame, already turned upright by the controller (as the 1:1 remote view's).
  [view setFrameSource:[weakSelf, who](sm::video::I420Frame* frame, sm::video::Rotation* rotation) {
    SMCallBridge* s = weakSelf;
    if (!s || !s->_client || !s->_client->TakeParticipantVideo(who, frame)) return false;
    *rotation = sm::video::Rotation::k0;
    return true;
  }];
  _tiles[identity] = view;
  return view;
}

- (NSString*)statsText {
  if (!_client) return @"";
  const sm::apps::CallStats stats = _client->stats();
  [self updateRates:stats];
  char line[160];
  std::snprintf(line, sizeof line, "\nrender: %.1f fps  decoded %.1f fps  drawn %llu", _renderFps, _decodedFps,
                static_cast<unsigned long long>(_remoteView.framesDrawn - _callDrawnBase));
  // A group call has a view per tile; the 1:1 remote view's render line says nothing there.
  return [NSString stringWithUTF8String:(stats.Text() + (_group ? "" : line)).c_str()];
}

// Called with each stats poll (10 Hz): the frame rates over the last second, and the call's start point.
- (void)updateRates:(const sm::apps::CallStats&)stats {
  const CFTimeInterval now = CACurrentMediaTime();
  const uint64_t drawn = _remoteView.framesDrawn - _callDrawnBase, decoded = stats.video_frames_decoded;
  if (_firstTime == 0) {
    if (decoded == 0) return;
    _firstTime = _rateTime = now;
    _firstDrawn = _rateDrawn = drawn;
    _firstDecoded = _rateDecoded = decoded;
    return;
  }
  if (now - _rateTime < 1.0) return;
  _renderFps = (drawn - _rateDrawn) / (now - _rateTime);
  _decodedFps = (decoded - _rateDecoded) / (now - _rateTime);
  _rateTime = now;
  _rateDrawn = drawn;
  _rateDecoded = decoded;
}

// The call's mean rendered and decoded frame rates since its first decoded frame, for the JSON line.
- (std::string)renderJson:(const sm::apps::CallStats&)stats {
  [self updateRates:stats];
  const double span = _firstTime > 0 ? _rateTime - _firstTime : 0;
  char buf[200];
  std::snprintf(buf, sizeof buf, "\"app_frames_drawn\":%llu,\"app_render_fps\":%.1f,\"app_decoded_fps\":%.1f",
                static_cast<unsigned long long>(_remoteView.framesDrawn - _callDrawnBase),
                span > 0 ? (_rateDrawn - _firstDrawn) / span : 0.0,
                span > 0 ? (_rateDecoded - _firstDecoded) / span : 0.0);
  return buf;
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

- (void)joinGroupWithServer:(NSString*)server room:(NSString*)room video:(BOOL)video {
  [self stopCall];
  _videoEnabled = video;
  NSString* base = [server stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
  while ([base hasSuffix:@"/"]) base = [base substringToIndex:base.length - 1];
  std::random_device rd;
  NSString* identity = [NSString stringWithFormat:@"ios-%06u", static_cast<unsigned>(rd() % 1000000)];
  NSString* query = [NSString stringWithFormat:@"/lk-token?room=%s&identity=%@", PercentEncode(room).c_str(), identity];
  NSURL* tokenURL = [NSURL URLWithString:[base stringByAppendingString:query]];
  NSURL* turnURL = [NSURL URLWithString:[base stringByAppendingString:@"/turn"]];
  NSURL* origin = [NSURL URLWithString:base];
  if (!tokenURL || !turnURL || !origin.host || room.length == 0) {
    _status = @"bad server URL or room";
    return;
  }
  _status = @"fetching /lk-token";
  NSLog(@"SmCall: group %@ via %@ as %@", room, base, identity);
  const int generation = ++_generation;
  const std::string roomCode = room.UTF8String, peer = identity.UTF8String;
  // LiveKit's URL when /lk-token gives none: the server's origin (nginx routes /rtc to LiveKit there).
  NSString* fallback = [NSString stringWithFormat:@"%@://%@%@", [origin.scheme isEqualToString:@"https"] ? @"wss" : @"ws",
                                                  origin.host, origin.port ? [@":" stringByAppendingString:origin.port.stringValue] : @""];
  __weak SMCallBridge* weakSelf = self;
  NSURLSession* session = _session;
  [[_session dataTaskWithURL:tokenURL
           completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
             NSInteger code = [response isKindOfClass:NSHTTPURLResponse.class] ? ((NSHTTPURLResponse*)response).statusCode : 0;
             NSDictionary* json = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
             NSString* token = [json isKindOfClass:NSDictionary.class] ? json[@"token"] : nil;
             NSString* url = [json isKindOfClass:NSDictionary.class] ? json[@"url"] : nil;
             NSLog(@"SmCall: GET /lk-token: HTTP %ld%@", (long)code, error ? error.localizedDescription : @"");
             if (![token isKindOfClass:NSString.class] || token.length == 0) {
               dispatch_async(dispatch_get_main_queue(), ^{
                 SMCallBridge* s = weakSelf;
                 if (s && generation == s->_generation) s->_status = [NSString stringWithFormat:@"no LiveKit token (HTTP %ld)", (long)code];
               });
               return;
             }
             NSURL* lk = [NSURL URLWithString:[url isKindOfClass:NSString.class] && url.length ? url : fallback];
             const std::string path = sm::apps::livekit::JoinPath(lk.path.length ? lk.path.UTF8String : "/",
                                                                  token.UTF8String, true);
             NSString* wsString = [NSString stringWithFormat:@"%@://%@%@%s", lk.scheme, lk.host,
                                   lk.port ? [@":" stringByAppendingString:lk.port.stringValue] : @"", path.c_str()];
             NSURL* wsURL = [NSURL URLWithString:wsString];
             // /turn too (coturn's REST credentials); LiveKit's JoinResponse adds its own ICE servers.
             [[session dataTaskWithURL:turnURL
                     completionHandler:^(NSData* turnData, NSURLResponse* turnResponse, NSError* turnError) {
                       NSInteger turnCode = [turnResponse isKindOfClass:NSHTTPURLResponse.class]
                                                ? ((NSHTTPURLResponse*)turnResponse).statusCode : 0;
                       std::string body;
                       if (!turnError && turnCode == 200 && turnData) {
                         body.assign(static_cast<const char*>(turnData.bytes), turnData.length);
                       }
                       dispatch_async(dispatch_get_main_queue(), ^{
                         SMCallBridge* s = weakSelf;
                         if (!s || !wsURL) return;
                         s->_group = YES;
                         s->_localIdentity = identity;
                         [s beginCall:roomCode turn:body ws:wsURL generation:generation livekit:true peer:peer];
                       });
                     }] resume];
           }] resume];
}

- (void)beginCall:(const std::string&)room turn:(const std::string&)turn ws:(NSURL*)wsURL generation:(int)generation {
  _group = NO;
  [self beginCall:room turn:turn ws:wsURL generation:generation livekit:false peer:""];
}

- (void)beginCall:(const std::string&)room
             turn:(const std::string&)turn
               ws:(NSURL*)wsURL
       generation:(int)generation
          livekit:(bool)livekit
             peer:(const std::string&)peer {
  if (generation != _generation || _client) return;  // Hung up or restarted meanwhile.
  _ws = [_session webSocketTaskWithURL:wsURL];
  NSURLSessionWebSocketTask* ws = _ws;
  sm::apps::CallClientConfig cfg;
  cfg.room = room;
  cfg.peer_id = peer;
  cfg.verbose = true;
  cfg.relay_only = self.relayOnly;
  // A LiveKit room (C20): the camera at the size the app captures, every participant received.
  cfg.livekit = livekit;
  cfg.livekit_video_width = 640;
  cfg.livekit_video_height = 480;
  // libwebrtc's iOS client (#1124): VoiceProcessingIO's echo canceller and AGC, the software AEC3 and AGC off.
  sm::apps::ApplyIosAudioOptions(&cfg);
  _client = std::make_unique<sm::apps::CallClient>(cfg, [ws, livekit](const std::string& m) {
    if (livekit) {  // LiveKit's signaling: one protobuf message per binary frame.
      NSData* bytes = [NSData dataWithBytes:m.data() length:m.size()];
      [ws sendMessage:[[NSURLSessionWebSocketMessage alloc] initWithData:bytes] completionHandler:^(NSError*) {}];
      return;
    }
    NSString* text = [[NSString alloc] initWithBytes:m.data() length:m.size() encoding:NSUTF8StringEncoding];
    [ws sendMessage:[[NSURLSessionWebSocketMessage alloc] initWithString:text] completionHandler:^(NSError*) {}];
  });
  if (!turn.empty()) _client->SetTurnResponse(turn);
  _client->SetMuted(_muted);
  _client->SetVideoEnabled(_videoEnabled);
  _wsClosed = false;
  _hangupRequested = false;
  _callDrawnBase = _remoteView.framesDrawn;
  _firstTime = _rateTime = 0;
  _renderFps = _decodedFps = 0;
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
    } else if (msg.type == NSURLSessionWebSocketMessageTypeData && msg.data) {  // LiveKit's protobuf frames.
      std::lock_guard<std::mutex> lock(s->_inboxMu);
      s->_inbox.emplace_back(static_cast<const char*>(msg.data.bytes), msg.data.length);
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
  __weak SMCallBridge* weakSelf = self;
  _camera->Start(cfg, [client, weakSelf](const sm::device::CapturedFrame& f) {
    if (f.format != sm::device::CapturePixelFormat::kI420) return;
    client->PushCameraFrame(f.i420, f.rotation);
    SMCallBridge* s = weakSelf;
    if (s && s->_group) {  // The group grid's preview tile.
      std::lock_guard<std::mutex> lock(s->_previewMu);
      s->_preview = f.i420;
      s->_previewRotation = f.rotation;
    }
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
  // With the app's rendered frames and rates (#1133) before the closing brace.
  if (_client) {
    const sm::apps::CallStats stats = _client->stats();
    std::string json = stats.ToJson();
    if (!json.empty() && json.back() == '}') json.insert(json.size() - 1, "," + [self renderJson:stats]);
    if (_group && !json.empty() && json.back() == '}') {  // Each tile's frames drawn on the GPU (C20).
      std::string tiles = "\"app_tiles_drawn\":{";
      bool first = true;
      for (NSString* identity in _tiles) {
        tiles += std::string(first ? "" : ",") + "\"" + identity.UTF8String + "\":" +
                 std::to_string(_tiles[identity].framesDrawn);
        first = false;
      }
      json.insert(json.size() - 1, "," + tiles + "}");
    }
    NSLog(@"SmCall: call-result %s", json.c_str());
    // Also appended to Documents/call-results.jsonl: the log cuts long lines (a group call's), and a test reads it
    // from the app's container.
    NSString* dir = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    if (dir) {
      const std::string path = std::string(dir.UTF8String) + "/call-results.jsonl";
      if (FILE* f = std::fopen(path.c_str(), "a")) {
        std::fprintf(f, "%s\n", json.c_str());
        std::fclose(f);
      }
    }
  }
  _camera.reset();
  _audio.reset();
  _client.reset();
  _audioAdapter.reset();
  [_tiles removeAllObjects];
  {
    std::lock_guard<std::mutex> lock(_previewMu);
    _preview.reset();
  }
  _group = NO;
}

@end
