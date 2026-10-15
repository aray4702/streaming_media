sm_add_library(core SOURCES
  core/metrics.cc
  core/event_log.cc
  core/fft.cc
  core/dsp.cc
  core/rtp_packet.cc)

set(SM_SECTIONS core)

if(SM_WITH_LOSS)
  sm_add_library(loss DEPS sm::core SOURCES
    loss/nack_tracker.cc
    loss/rtx.cc
    loss/fec.cc
    loss/red.cc
    loss/plc.cc
    loss/keyframe_request.cc
    loss/temporal_layers.cc
    loss/protection_controller.cc)
  list(APPEND SM_SECTIONS loss)
endif()

if(SM_WITH_TIMING)
  sm_add_library(timing DEPS sm::loss SOURCES
    timing/packet_buffer.cc
    timing/rtcp_stats.cc
    timing/delay_manager.cc
    timing/time_stretch.cc
    timing/audio_jitter_buffer.cc
    timing/video_jitter_estimator.cc
    timing/clock_drift.cc)
  list(APPEND SM_SECTIONS timing)
endif()

if(SM_WITH_CC)
  sm_add_library(cc DEPS sm::core SOURCES
    cc/transport_feedback.cc
    cc/delay_based_bwe.cc
    cc/loss_based_bwe.cc
    cc/send_side_bwe.cc
    cc/pacer.cc
    cc/probing.cc
    cc/bitrate_allocator.cc
    cc/frame_dropper.cc
    cc/video_adaptation.cc
    cc/outage_monitor.cc)
  list(APPEND SM_SECTIONS cc)
endif()

if(SM_WITH_AUDIO)
  sm_add_library(audio DEPS sm::core SOURCES
    audio/high_pass_filter.cc
    audio/vad.cc
    audio/noise_suppressor.cc
    audio/gain_control.cc
    audio/echo_delay_estimator.cc
    audio/echo_canceller.cc
    audio/transient_suppressor.cc
    audio/comfort_noise.cc
    audio/audio_level.cc
    audio/resampler.cc)
  list(APPEND SM_SECTIONS audio)
endif()

if(SM_WITH_VIDEO)
  sm_add_library(video DEPS sm::core SOURCES
    video/frame.cc
    video/freeze_detector.cc
    video/codec_fallback.cc
    video/orientation.cc
    video/content_policy.cc
    video/codec_negotiation.cc
    video/frame_scaler.cc
    video/frame_scheduler.cc
    video/quality_scaler.cc)
  list(APPEND SM_SECTIONS video)
endif()

if(SM_WITH_SYNC)
  sm_add_library(sync DEPS sm::timing SOURCES
    sync/rtp_to_ntp.cc
    sync/stream_sync.cc
    sync/capture_time.cc
    sync/header_extensions.cc)
  list(APPEND SM_SECTIONS sync)
endif()

# Umbrella target: link sm::sm to get every enabled section.
add_library(sm_all INTERFACE)
add_library(sm::sm ALIAS sm_all)
foreach(s ${SM_SECTIONS})
  target_link_libraries(sm_all INTERFACE sm::${s})
endforeach()
set(SM_SECTIONS ${SM_SECTIONS} PARENT_SCOPE)

# Install + package export: consumers use
#   find_package(streaming_media CONFIG REQUIRED)
#   target_link_libraries(app PRIVATE sm::sm)   # or sm::audio, sm::cc, ...
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set_target_properties(sm_all PROPERTIES EXPORT_NAME sm)
# sm/sim (network emulator, test signals) is header-only and installed too,
# so applications can reuse it in their own integration tests.
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/sm DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
set(_sm_targets sm_all)
foreach(s ${SM_SECTIONS})
  list(APPEND _sm_targets sm_${s})
endforeach()
install(TARGETS ${_sm_targets} EXPORT streaming_mediaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT streaming_mediaTargets NAMESPACE sm::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/streaming_media)
configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/streaming_mediaConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/streaming_mediaConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/streaming_media)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/streaming_mediaConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/streaming_mediaConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/streaming_mediaConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/streaming_media)
