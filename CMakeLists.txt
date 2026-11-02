cmake_minimum_required(VERSION 3.20)
project(streaming_media VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options. Every section can be compiled out so a constrained target (MCU-class
# Linux, wearable, Wasm) links only what it uses.
# ---------------------------------------------------------------------------
option(SM_WITH_LOSS     "Section 3: packet-loss resilience"        ON)
option(SM_WITH_TIMING   "Section 4: jitter, reordering and timing" ON)
option(SM_WITH_CC       "Section 5: bandwidth and congestion"      ON)
option(SM_WITH_AUDIO    "Section 6: audio processing"              ON)
option(SM_WITH_VIDEO    "Section 7: video quality and processing"  ON)
option(SM_WITH_SYNC     "Section 8: synchronization"               ON)
option(SM_WITH_RTP      "RTP/RTCP session layer (needs loss, timing, cc, sync, video)" ON)
# Milestone 3 (#944, #984): TLS/DTLS for the connectivity layer, through BoringSSL (cmake/SmBoringSSL.cmake).
# Not for wasm: in a browser the browser's own WebRTC does TLS and DTLS.
if(EMSCRIPTEN)
  set(_sm_tls_default OFF)
else()
  set(_sm_tls_default ON)
endif()
option(SM_WITH_TLS      "Connectivity: TLS, DTLS and crypto through BoringSSL (sm::tls)" ${_sm_tls_default})
option(SM_ENABLE_METRICS "Compile metrics export (off = no-op)"    ON)
option(SM_ENABLE_EVENTS  "Compile the structured event log"        ON)
# Issue #988 (C8): the getStats-style snapshot (rtp::Session::GetStats), and the text log's
# compiled-in minimum severity (sm/core/logging.h; lower severities are compiled out).
option(SM_ENABLE_STATS   "Compile the stats snapshot API (off = GetStats returns not supported)" ON)
set(SM_LOG_MIN_LEVEL "VERBOSE" CACHE STRING "Lowest log severity compiled in: VERBOSE, INFO, WARNING, ERROR or NONE")
set_property(CACHE SM_LOG_MIN_LEVEL PROPERTY STRINGS VERBOSE INFO WARNING ERROR NONE)
set(_sm_log_levels VERBOSE INFO WARNING ERROR NONE)
string(TOUPPER "${SM_LOG_MIN_LEVEL}" _sm_log_level_upper)
list(FIND _sm_log_levels "${_sm_log_level_upper}" SM_LOG_MIN_LEVEL_INDEX)
if(SM_LOG_MIN_LEVEL_INDEX LESS 0)
  message(FATAL_ERROR "SM_LOG_MIN_LEVEL must be one of ${_sm_log_levels}, not '${SM_LOG_MIN_LEVEL}'")
endif()
option(SM_BUILD_TESTS   "Build unit and end-to-end tests"          ${PROJECT_IS_TOP_LEVEL})
option(SM_BUILD_BENCH   "Build benchmarks"                         ${PROJECT_IS_TOP_LEVEL})
option(SM_BUILD_TESTBENCH "Build the call-level test bench"   ${PROJECT_IS_TOP_LEVEL})
option(SM_BUILD_EXAMPLES "Build examples (needs all sections)"     ${PROJECT_IS_TOP_LEVEL})
option(SM_NO_EXCEPTIONS "Build with -fno-exceptions -fno-rtti"     OFF)
option(SM_SANITIZE      "Build with ASan + UBSan"                  OFF)
# libFuzzer targets for the network parsers (fuzz/, #989). Clang only; turns on
# ASan + UBSan and coverage instrumentation for every sm target.
option(SM_BUILD_FUZZERS "Build the libFuzzer targets in fuzz/ (clang)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(SM_BUILD_FUZZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "SM_BUILD_FUZZERS needs clang (libFuzzer); configure with -DCMAKE_CXX_COMPILER=clang++")
endif()
if(SM_BUILD_FUZZERS)
  set(SM_SANITIZE ON CACHE BOOL "" FORCE)
endif()

include(cmake/SmHelpers.cmake)

# Section dependencies (timing uses loss/PLC and audio DSP, sync uses timing).
if(SM_WITH_TIMING AND NOT SM_WITH_LOSS)
  message(FATAL_ERROR "SM_WITH_TIMING requires SM_WITH_LOSS (the video e2e tests use its keyframe requests with timing)")
endif()
if(SM_WITH_SYNC AND NOT SM_WITH_TIMING)
  message(FATAL_ERROR "SM_WITH_SYNC requires SM_WITH_TIMING")
endif()

if(SM_WITH_RTP AND NOT (SM_WITH_LOSS AND SM_WITH_TIMING AND SM_WITH_CC AND SM_WITH_SYNC AND SM_WITH_VIDEO))
  message(FATAL_ERROR "SM_WITH_RTP requires SM_WITH_LOSS, SM_WITH_TIMING, SM_WITH_CC, SM_WITH_SYNC and SM_WITH_VIDEO")
endif()

add_subdirectory(src)

if(SM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(SM_BUILD_BENCH)
  add_subdirectory(bench)
endif()
if(SM_BUILD_EXAMPLES AND SM_WITH_LOSS AND SM_WITH_TIMING AND SM_WITH_CC AND SM_WITH_VIDEO AND SM_WITH_RTP)
  add_subdirectory(examples)
endif()
if(SM_WITH_RTP AND (SM_BUILD_TESTS OR SM_BUILD_FUZZERS))
  add_subdirectory(fuzz)
endif()
if(SM_BUILD_TESTBENCH AND SM_WITH_LOSS AND SM_WITH_TIMING AND SM_WITH_CC AND SM_WITH_AUDIO AND SM_WITH_VIDEO AND SM_WITH_RTP)
  add_subdirectory(testbench)
endif()
