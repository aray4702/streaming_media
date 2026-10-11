add_library(sm_test_framework STATIC framework/sm_test_main.cc)
target_include_directories(sm_test_framework PUBLIC framework)
sm_configure_target(sm_test_framework)

# One executable per section; one CTest entry per suite (= per algorithm).
sm_add_test_exe(sm_unit_core LABEL unit DEPS sm::core
  SOURCES unit/core/core_test.cc
  SUITES Units SeqNum Histogram Metrics EventLog Fft Dsp RtpPacket NetworkLink)

if(SM_WITH_LOSS)
  sm_add_test_exe(sm_unit_loss LABEL unit DEPS sm::loss
    SOURCES unit/loss/nack_rtx_test.cc unit/loss/fec_test.cc unit/loss/red_test.cc
            unit/loss/plc_test.cc unit/loss/keyframe_test.cc unit/loss/temporal_layers_test.cc
            unit/loss/protection_test.cc
    SUITES NackTracker Rtx Fec Red Plc KeyframeRequest TemporalLayers Protection)
  sm_add_test_exe(sm_e2e_loss LABEL e2e DEPS sm::loss
    SOURCES e2e/loss_e2e_test.cc
    SUITES E2E_NackRtx E2E_Fec E2E_Red E2E_Plc E2E_Keyframe E2E_TemporalLayers E2E_Protection)
endif()

if(SM_WITH_TIMING)
  include(${CMAKE_CURRENT_SOURCE_DIR}/timing.cmake)
endif()
if(SM_WITH_CC)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cc.cmake)
endif()
if(SM_WITH_AUDIO)
  include(${CMAKE_CURRENT_SOURCE_DIR}/audio.cmake)
endif()
if(SM_WITH_VIDEO)
  include(${CMAKE_CURRENT_SOURCE_DIR}/video.cmake)
endif()
if(SM_WITH_SYNC)
  include(${CMAKE_CURRENT_SOURCE_DIR}/sync.cmake)
endif()
