// No audio or camera device on this platform (#1105: Apple platforms have them,
// src/device/apple/).
#include "sm/device/audio_device.h"
#include "sm/device/camera_capturer.h"

namespace sm::device {

std::unique_ptr<AudioDevice> CreatePlatformAudioDevice(const AudioDeviceConfig&) { return nullptr; }
std::vector<CameraInfo> ListCameras() { return {}; }
std::unique_ptr<CameraCapturer> CreatePlatformCameraCapturer() { return nullptr; }

}  // namespace sm::device
