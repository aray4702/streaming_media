// The audio session and the platform audio device on Apple platforms (#1105).
//
// iOS: the AVAudioSession as libwebrtc's RTCAudioSession sets it up for a call
// (sdk/objc/components/audio/RTCAudioSessionConfiguration.m), from Apple's
// documented API: category playAndRecord, mode voiceChat, Bluetooth HFP
// allowed, 48 kHz and a 20 ms I/O buffer preferred, one input channel. Its
// interruption, route-change and media-services-reset notifications go to
// AppleAudioDevice. macOS has no audio session: the unit runs at 48 kHz on
// the default devices.
#import <Foundation/Foundation.h>
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <AVFAudio/AVFAudio.h>
#endif

#include "sm/device/apple_audio_device.h"
#include "voice_processing_audio_unit.h"

#if !__has_feature(objc_arc)
#error "build with -fobjc-arc"
#endif

namespace sm::device {
namespace {

#if TARGET_OS_IPHONE
constexpr double kPreferredSampleRate = 48000.0;
constexpr double kPreferredIOBufferDuration = 0.02;

class IosAudioSession : public AudioSessionControl {
 public:
  ~IosAudioSession() override {
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    for (id o in observers_) [center removeObserver:o];
  }

  void Attach(AppleAudioDevice* device) {
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    AVAudioSession* session = [AVAudioSession sharedInstance];
    NSMutableArray* list = [NSMutableArray array];
    [list addObject:[center addObserverForName:AVAudioSessionInterruptionNotification
                                        object:session
                                         queue:nil
                                    usingBlock:^(NSNotification* n) {
                                      NSNumber* type = n.userInfo[AVAudioSessionInterruptionTypeKey];
                                      if (type.unsignedIntegerValue == AVAudioSessionInterruptionTypeBegan) {
                                        device->OnInterruptionBegan();
                                      } else {
                                        device->OnInterruptionEnded();
                                      }
                                    }]];
    [list addObject:[center addObserverForName:AVAudioSessionRouteChangeNotification
                                        object:session
                                         queue:nil
                                    usingBlock:^(NSNotification*) {
                                      device->OnRouteChanged();
                                    }]];
    [list addObject:[center addObserverForName:AVAudioSessionMediaServicesWereResetNotification
                                        object:session
                                         queue:nil
                                    usingBlock:^(NSNotification*) {
                                      device->OnMediaServicesReset();
                                    }]];
    observers_ = list;
  }

  bool Activate(const AudioDeviceConfig& config) override {
    AVAudioSession* session = [AVAudioSession sharedInstance];
    NSError* error = nil;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    // AllowBluetooth is AllowBluetoothHFP (0x4) in newer SDKs.
    const AVAudioSessionCategoryOptions options = AVAudioSessionCategoryOptionAllowBluetooth;
#pragma clang diagnostic pop
    if (![session setCategory:AVAudioSessionCategoryPlayAndRecord
                         mode:AVAudioSessionModeVoiceChat
                      options:options
                        error:&error]) {
      NSLog(@"sm audio session: setCategory failed: %@", error);
      return false;
    }
    const double rate = config.sample_rate_hz > 0 ? config.sample_rate_hz : kPreferredSampleRate;
    if (![session setPreferredSampleRate:rate error:&error]) {
      NSLog(@"sm audio session: setPreferredSampleRate failed: %@", error);
    }
    if (![session setPreferredIOBufferDuration:kPreferredIOBufferDuration error:&error]) {
      NSLog(@"sm audio session: setPreferredIOBufferDuration failed: %@", error);
    }
    if (![session setActive:YES error:&error]) {
      NSLog(@"sm audio session: setActive failed: %@", error);
      return false;
    }
    // The channel count can only be set on an active session.
    if (session.maximumInputNumberOfChannels >= 1) {
      [session setPreferredInputNumberOfChannels:1 error:nil];
    }
    return true;
  }

  void Deactivate() override {
    [[AVAudioSession sharedInstance] setActive:NO
                                   withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                                         error:nil];
  }

  int SampleRateHz() const override {
    return static_cast<int>([AVAudioSession sharedInstance].sampleRate + 0.5);
  }

  bool CanPlayOrRecord() const override { return true; }

 private:
  NSArray* observers_ = nil;
};
#else
class MacAudioSession : public AudioSessionControl {
 public:
  bool Activate(const AudioDeviceConfig&) override { return true; }
  void Deactivate() override {}
  int SampleRateHz() const override { return 0; }  // the configured rate; the unit converts
  bool CanPlayOrRecord() const override { return true; }
};
#endif

}  // namespace

std::unique_ptr<AudioDevice> CreatePlatformAudioDevice(const AudioDeviceConfig& config) {
#if TARGET_OS_IPHONE
  auto session = std::make_unique<IosAudioSession>();
  IosAudioSession* raw = session.get();
  auto device = std::make_unique<AppleAudioDevice>(config, &VoiceProcessingAudioUnit::Create, std::move(session));
  raw->Attach(device.get());
  return device;
#else
  return std::make_unique<AppleAudioDevice>(config, &VoiceProcessingAudioUnit::Create,
                                            std::make_unique<MacAudioSession>());
#endif
}

}  // namespace sm::device
