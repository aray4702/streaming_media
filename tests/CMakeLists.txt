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
  if(SM_TLS_CROSS_TEST)
    # The same unit tests on the other backend (sm::tls_mbedtls, or sm::tls_boringssl when sm::tls is on mbedTLS;
    # without SrtpSession), and the BoringSSL <-> mbedTLS cross test (#998).
    add_executable(sm_unit_tls_${SM_TLS_OTHER_BACKEND} unit/tls/message_digest_test.cc unit/tls/ssl_identity_test.cc
                   unit/tls/dtls_transport_test.cc)
    target_link_libraries(sm_unit_tls_${SM_TLS_OTHER_BACKEND} PRIVATE sm_test_framework sm::tls_${SM_TLS_OTHER_BACKEND})
    sm_configure_target(sm_unit_tls_${SM_TLS_OTHER_BACKEND})
    foreach(suite TlsMessageDigest TlsCryptoRandom TlsSslIdentity TlsSslFingerprint TlsDtlsTransport)
      add_test(NAME unit.${suite}.${SM_TLS_OTHER_BACKEND}
               COMMAND sm_unit_tls_${SM_TLS_OTHER_BACKEND} --filter=${suite}.)
      set_tests_properties(unit.${suite}.${SM_TLS_OTHER_BACKEND} PROPERTIES LABELS unit)
    endforeach()
    # One tls_cross::Peer per backend: cross_peer.cc compiled against each sm::tls build.
    foreach(backend boringssl mbedtls)
      if(backend STREQUAL SM_TLS_OTHER_BACKEND)
        set(lib sm::tls_${backend})
      else()
        set(lib sm::tls)
      endif()
      add_library(sm_tls_cross_peer_${backend} STATIC unit/tls/cross_peer.cc)
      target_link_libraries(sm_tls_cross_peer_${backend} PRIVATE ${lib})
      target_include_directories(sm_tls_cross_peer_${backend} PUBLIC ${PROJECT_SOURCE_DIR}/include)
      sm_configure_target(sm_tls_cross_peer_${backend})
    endforeach()
    sm_add_test_exe(sm_unit_tls_cross LABEL unit DEPS sm_tls_cross_peer_boringssl sm_tls_cross_peer_mbedtls sm::core
      SOURCES unit/tls/cross_test.cc
      SUITES TlsCrossBackend)
  endif()
endif()

sm_add_test_exe(sm_unit_p2p LABEL unit DEPS sm::p2p
  SOURCES
    unit/p2p/connection_test.cc
    unit/p2p/ice_controller_test.cc
    unit/p2p/p2p_transport_channel_ping_test.cc
    unit/p2p/p2p_transport_channel_test.cc
    unit/p2p/dtls_utils_test.cc
    unit/p2p/packet_socket_test.cc
    unit/p2p/port_allocator_test.cc
    unit/p2p/port_test.cc
    unit/p2p/stun_request_test.cc
    unit/p2p/stun_test.cc
    unit/p2p/tcp_socket_test.cc
    unit/p2p/test_crypto.cc
    unit/p2p/test_crypto_test.cc
  SUITES
    AsyncStunTcpSocket
    AsyncTcpSocket
    DtlsUtils
    EmulatedSockets
    EmulatedTcp
    P2pBasicIceController
    P2pBasicPortAllocator
    P2pCandidate
    P2pConnection
    P2pConnectionCall
    P2pIceConfig
    P2pIceCredentialsIterator
    P2pIpAddress
    P2pNetwork
    P2pNetworkManager
    P2pPort
    P2pPortAllocator
    P2pSocketAddress
    P2pTransportChannel
    P2pTransportChannelPing
    P2pStunPort
    P2pUdpPort
    P2pWrappingActiveIceController
    Stun
    StunRequest
    StunRequestReentrance
    StunServer
    StunTestCrypto
)
if(SM_WITH_TLS)
  # The STUN crypto hooks on BoringSSL's crypto (sm::tls, #984).
  target_sources(sm_unit_p2p PRIVATE unit/p2p/stun_tls_test.cc)
  target_link_libraries(sm_unit_p2p PRIVATE sm::p2p_tls)
  add_test(NAME unit.StunTls COMMAND sm_unit_p2p --filter=StunTls.)
  set_tests_properties(unit.StunTls PROPERTIES LABELS unit)
  # DTLS over a fake ICE transport (C5 part 2, #1006).
  sm_add_test_exe(sm_unit_p2p_dtls LABEL unit DEPS sm::p2p_dtls
    SOURCES
      unit/p2p_dtls/dtls_transport_test.cc
    SUITES
      P2pDtlsRole
      P2pDtlsTransport
  )
  if(SM_WITH_SYNC)
    # SRTP and DTLS-SRTP transports over the fake ICE transport (C5 part 3, #1007).
    sm_add_test_exe(sm_unit_pc LABEL unit DEPS sm::pc
      SOURCES
        unit/pc/dtls_srtp_transport_test.cc
        unit/pc/rtp_transport_test.cc
        unit/pc/srtp_transport_test.cc
      SUITES
        PcDtlsSrtpTransport
        PcRtpTransport
        PcRtpUtils
        PcSrtpTransport
    )
  endif()
endif()

sm_add_test_exe(sm_unit_core LABEL unit DEPS sm::core
  SOURCES
    unit/core/audio_frame_operations_test.cc
    unit/core/core_test.cc
    unit/core/event_based_exponential_moving_average_test.cc
    unit/core/logging_test.cc
    unit/core/network_test.cc
  SUITES
    AudioFrameOperations
    Dsp
    EventBasedExponentialMovingAverage
    EventLog
    Fft
    Histogram
    Logging
    Metrics
    Nat
    NetworkLink
    Observability
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
