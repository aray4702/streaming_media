add_library(sm_test_framework STATIC framework/sm_test_main.cc)
target_include_directories(sm_test_framework PUBLIC framework)
sm_configure_target(sm_test_framework)

# One executable per section; one CTest entry per suite (= per algorithm).
if(SM_WITH_TLS)
  sm_add_test_exe(sm_unit_tls LABEL unit DEPS sm::tls
    SOURCES unit/tls/message_digest_test.cc
            unit/tls/ssl_identity_test.cc
            unit/tls/dtls_transport_test.cc
            unit/tls/srtp_session_test.cc
    SUITES TlsMessageDigest TlsCryptoRandom TlsSslIdentity TlsSslFingerprint TlsDtlsTransport TlsSrtpSession
           TlsDtlsSrtp)
endif()

sm_add_test_exe(sm_unit_core LABEL unit DEPS sm::core
  SOURCES
    unit/core/audio_frame_operations_test.cc
    unit/core/core_test.cc
    unit/core/network_test.cc
  SUITES
    AudioFrameOperations
    Dsp
    EventLog
    Fft
    Histogram
    Metrics
    Nat
    NetworkLink
    RateStatistics
    RateTracker
    RtpPacket
    SeqNum
    SimNetwork
    Units
)

if(SM_WITH_LOSS)
  sm_add_test_exe(sm_unit_loss LABEL unit DEPS sm::loss
    SOURCES
      unit/loss/audio_nack_tracker_test.cc
      unit/loss/fec_controller_test.cc
      unit/loss/keyframe_test.cc
      unit/loss/nack_rtx_test.cc
      unit/loss/red_test.cc
      unit/loss/rtcp_feedback_test.cc
      unit/loss/ulpfec_test.cc
    SUITES
      AudioNackTracker
      FecController
      KeyframeRequest
      NackTracker
      Red
      RtcpFeedback
      Rtx
      Ulpfec
  )
  sm_add_test_exe(sm_e2e_loss LABEL e2e DEPS sm::loss
    SOURCES
      e2e/loss_e2e_test.cc
    SUITES
      E2E_Fec
      E2E_Keyframe
      E2E_NackRtx
      E2E_Protection
      E2E_Red
  )
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
# The browser binding's native code (web/), without Emscripten.
if(SM_WITH_TIMING AND SM_WITH_AUDIO)
  sm_add_test_exe(sm_unit_web LABEL unit DEPS sm::timing sm::audio
    SOURCES
      unit/web/audio_jitter_buffer_binding_test.cc
    SUITES
      WebAudioJitterBuffer
  )
  target_include_directories(sm_unit_web PRIVATE ${PROJECT_SOURCE_DIR}/web)
endif()
if(SM_WITH_RTP)
  include(${CMAKE_CURRENT_SOURCE_DIR}/rtp.cmake)
endif()
