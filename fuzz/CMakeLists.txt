# Fuzz targets for the parsers that read network input (#989, requirement
# C10). Each target is one LLVMFuzzerTestOneInput in <name>_fuzzer.cc.
#
# - SM_BUILD_FUZZERS=ON (clang only): <name>_fuzzer, a libFuzzer binary built
#   with -fsanitize=fuzzer,address,undefined, the library instrumented for
#   coverage. See fuzz/README.md.
# - Always (with tests, not on wasm): sm_fuzz_replay_<name>, the same target
#   with replay_main.cc, and a ctest `fuzz.<name>` that runs it over
#   fuzz/corpus/<name>/, so a parser regression shows in the ordinary gate.
set(SM_FUZZ_TARGETS
  h264_depacketizer
  packet_buffer
  red
  rtcp
  rtp_packet
  stun
  tcp_framing
  transport_feedback
  turn
  ulpfec
)
# dcsctp's packet layer (#1032) and socket (#1060), only when data channels are compiled in.
if(SM_WITH_DATACHANNEL)
  list(APPEND SM_FUZZ_TARGETS dcsctp_packet dcsctp_socket)
  set(SM_FUZZ_DEPS_dcsctp_packet sm::dcsctp)
  # The socket (#1060): net/dcsctp/fuzzers/dcsctp_fuzzers.cc's FuzzSocket, ported as dcsctp_fuzzers.cc.
  set(SM_FUZZ_DEPS_dcsctp_socket sm::dcsctp)
  set(SM_FUZZ_SOURCES_dcsctp_socket ${CMAKE_CURRENT_SOURCE_DIR}/dcsctp_fuzzers.cc)
endif()
# DTLS (#1058), only with sm::tls: dtls_record on sm::tls's backend, and on the
# other one too where both are built (SM_TLS_CROSS_TEST), and dtls_transport
# (p2p::DtlsTransport over the fake ICE transport). All three replay
# corpus/dtls_record/.
if(SM_WITH_TLS)
  list(APPEND SM_FUZZ_TARGETS dtls_record dtls_transport)
  set(SM_FUZZ_DEPS_dtls_record sm::tls)
  set(SM_FUZZ_DEPS_dtls_transport sm::p2p_dtls)
  set(SM_FUZZ_INCLUDES_dtls_transport ${PROJECT_SOURCE_DIR}/tests/unit/p2p_dtls)
  set(SM_FUZZ_CORPUS_dtls_transport dtls_record)
  if(SM_BUILD_TESTS AND SM_TLS_CROSS_TEST AND SM_TLS_OTHER_BACKEND)
    set(t dtls_record_${SM_TLS_OTHER_BACKEND})
    list(APPEND SM_FUZZ_TARGETS ${t})
    set(SM_FUZZ_SOURCE_${t} dtls_record_fuzzer.cc)
    set(SM_FUZZ_DEPS_${t} sm::tls_${SM_TLS_OTHER_BACKEND})
    set(SM_FUZZ_CORPUS_${t} dtls_record)
  endif()
endif()
# The library each target links: sm::rtp (which brings the RTP, RTCP, loss
# and timing parsers) unless set here.
# Extra sources: the STUN tests' HMAC-SHA1 and MD5 for the crypto hooks (the
# TURN and TCP framing targets use them for TURN's credentials).
foreach(t stun turn tcp_framing)
  set(SM_FUZZ_DEPS_${t} sm::p2p)
  set(SM_FUZZ_SOURCES_${t} ${PROJECT_SOURCE_DIR}/tests/unit/p2p/test_crypto.cc)
  set(SM_FUZZ_INCLUDES_${t} ${PROJECT_SOURCE_DIR}/tests/unit/p2p)
endforeach()

foreach(t ${SM_FUZZ_TARGETS})
  if(DEFINED SM_FUZZ_DEPS_${t})
    set(deps ${SM_FUZZ_DEPS_${t}})
  else()
    set(deps sm::rtp)
  endif()
  # The source and the corpus: <name>_fuzzer.cc and corpus/<name>/ unless set.
  set(src ${t}_fuzzer.cc)
  if(DEFINED SM_FUZZ_SOURCE_${t})
    set(src ${SM_FUZZ_SOURCE_${t}})
  endif()
  set(corpus ${t})
  if(DEFINED SM_FUZZ_CORPUS_${t})
    set(corpus ${SM_FUZZ_CORPUS_${t}})
  endif()
  if(SM_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(sm_fuzz_replay_${t} ${src} replay_main.cc ${SM_FUZZ_SOURCES_${t}})
    target_include_directories(sm_fuzz_replay_${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${SM_FUZZ_INCLUDES_${t}})
    target_link_libraries(sm_fuzz_replay_${t} PRIVATE ${deps})
    sm_configure_target(sm_fuzz_replay_${t})
    add_test(NAME fuzz.${t} COMMAND sm_fuzz_replay_${t} ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${corpus})
    set_tests_properties(fuzz.${t} PROPERTIES LABELS fuzz)
  endif()
  if(SM_BUILD_FUZZERS)
    add_executable(${t}_fuzzer ${src} ${SM_FUZZ_SOURCES_${t}})
    target_include_directories(${t}_fuzzer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${SM_FUZZ_INCLUDES_${t}})
    target_link_libraries(${t}_fuzzer PRIVATE ${deps})
    sm_configure_target(${t}_fuzzer)
    target_compile_options(${t}_fuzzer PRIVATE -fsanitize=fuzzer)
    target_link_options(${t}_fuzzer PRIVATE -fsanitize=fuzzer)
  endif()
endforeach()
