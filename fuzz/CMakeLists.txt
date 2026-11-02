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
  transport_feedback
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
# The library each target links: sm::rtp (which brings the RTP, RTCP, loss
# and timing parsers) unless set here.
set(SM_FUZZ_DEPS_stun sm::p2p)
# Extra sources: the STUN tests' HMAC-SHA1 and MD5 for the crypto hooks.
set(SM_FUZZ_SOURCES_stun ${PROJECT_SOURCE_DIR}/tests/unit/p2p/test_crypto.cc)
set(SM_FUZZ_INCLUDES_stun ${PROJECT_SOURCE_DIR}/tests/unit/p2p)

foreach(t ${SM_FUZZ_TARGETS})
  if(DEFINED SM_FUZZ_DEPS_${t})
    set(deps ${SM_FUZZ_DEPS_${t}})
  else()
    set(deps sm::rtp)
  endif()
  if(SM_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(sm_fuzz_replay_${t} ${t}_fuzzer.cc replay_main.cc ${SM_FUZZ_SOURCES_${t}})
    target_include_directories(sm_fuzz_replay_${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${SM_FUZZ_INCLUDES_${t}})
    target_link_libraries(sm_fuzz_replay_${t} PRIVATE ${deps})
    sm_configure_target(sm_fuzz_replay_${t})
    add_test(NAME fuzz.${t} COMMAND sm_fuzz_replay_${t} ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${t})
    set_tests_properties(fuzz.${t} PROPERTIES LABELS fuzz)
  endif()
  if(SM_BUILD_FUZZERS)
    add_executable(${t}_fuzzer ${t}_fuzzer.cc ${SM_FUZZ_SOURCES_${t}})
    target_include_directories(${t}_fuzzer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${SM_FUZZ_INCLUDES_${t}})
    target_link_libraries(${t}_fuzzer PRIVATE ${deps})
    sm_configure_target(${t}_fuzzer)
    target_compile_options(${t}_fuzzer PRIVATE -fsanitize=fuzzer)
    target_link_options(${t}_fuzzer PRIVATE -fsanitize=fuzzer)
  endif()
endforeach()
