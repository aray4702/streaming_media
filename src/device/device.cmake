# Audio and camera devices (C11 part 2, #1105). sm::device holds the plain C++ parts (FineAudioBuffer, the Apple
# audio device's state machine, camera format selection, frame pacing and NV12/I420 conversion), built and tested
# on every platform. With SM_WITH_APPLE_MEDIA (on for Apple platforms) the platform devices come from Audio Units
# (VoiceProcessingIO), the iOS AVAudioSession and AVCaptureSession; elsewhere CreatePlatform*() return nothing.
# Not in the sm::sm umbrella: an app on a platform links it itself.
set(_sm_device_sources
  device/fine_audio_buffer.cc
  device/apple_audio_device.cc
  device/video_capture_util.cc)
if(SM_WITH_APPLE_MEDIA)
  list(APPEND _sm_device_sources
    device/apple/voice_processing_audio_unit.cc
    device/apple/audio_device_apple.mm
    device/apple/camera_capturer_avf.mm)
else()
  list(APPEND _sm_device_sources device/platform_none.cc)
endif()
list(TRANSFORM _sm_device_sources PREPEND ${PROJECT_SOURCE_DIR}/src/)
sm_add_library(device DEPS sm::core sm::video SOURCES ${_sm_device_sources})
if(SM_WITH_APPLE_MEDIA)
  set_source_files_properties(${PROJECT_SOURCE_DIR}/src/device/apple/audio_device_apple.mm
                              ${PROJECT_SOURCE_DIR}/src/device/apple/camera_capturer_avf.mm
                              PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(sm_device PUBLIC "-framework AudioToolbox" "-framework AVFoundation" "-framework CoreMedia"
                        "-framework CoreVideo" "-framework Foundation")
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_link_libraries(sm_device PUBLIC "-framework AVFAudio" "-framework UIKit")
  endif()
  target_compile_definitions(sm_device PUBLIC SM_WITH_APPLE_MEDIA=1)
endif()
