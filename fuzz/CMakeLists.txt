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
  transport_feedback
  ulpfec
)

foreach(t ${SM_FUZZ_TARGETS})
  if(SM_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_executable(sm_fuzz_replay_${t} ${t}_fuzzer.cc replay_main.cc)
    target_include_directories(sm_fuzz_replay_${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(sm_fuzz_replay_${t} PRIVATE sm::rtp)
    sm_configure_target(sm_fuzz_replay_${t})
    add_test(NAME fuzz.${t} COMMAND sm_fuzz_replay_${t} ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${t})
    set_tests_properties(fuzz.${t} PROPERTIES LABELS fuzz)
  endif()
  if(SM_BUILD_FUZZERS)
    add_executable(${t}_fuzzer ${t}_fuzzer.cc)
    target_include_directories(${t}_fuzzer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${t}_fuzzer PRIVATE sm::rtp)
    sm_configure_target(${t}_fuzzer)
    target_compile_options(${t}_fuzzer PRIVATE -fsanitize=fuzzer)
    target_link_options(${t}_fuzzer PRIVATE -fsanitize=fuzzer)
  endif()
endforeach()
