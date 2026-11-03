# Audio and camera devices (C11 part 2, #1105). The unit tests need no hardware (a fake voice-processing unit and
# a fake audio session); they run on every platform.
sm_add_test_exe(sm_unit_device LABEL unit DEPS sm::device
  SOURCES
    unit/device/apple_audio_device_test.cc
    unit/device/fine_audio_buffer_test.cc
    unit/device/video_capture_util_test.cc
  SUITES
    DeviceAppleAudio
    DeviceCaptureFormat
    DeviceFineAudioBuffer
    DeviceFramerateController
    DeviceNv12
    DeviceScale
)
# The device check the Owner runs on a Mac or an iPhone (--list, --loopback, --camera): real microphone, speaker
# and camera, so it is built but not registered with CTest.
if(SM_WITH_APPLE_MEDIA)
  add_executable(sm_device_check device/sm_device_check.cc)
  target_link_libraries(sm_device_check PRIVATE sm::device)
  sm_configure_target(sm_device_check)
endif()
