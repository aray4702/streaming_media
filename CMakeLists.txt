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
option(SM_BUILD_TESTS   "Build unit and end-to-end tests"          ${PROJECT_IS_TOP_LEVEL})
option(SM_BUILD_BENCH   "Build benchmarks"                         ${PROJECT_IS_TOP_LEVEL})
option(SM_BUILD_TESTBENCH "Build the call-level test bench"   ${PROJECT_IS_TOP_LEVEL})
option(SM_BUILD_EXAMPLES "Build examples (needs all sections)"     ${PROJECT_IS_TOP_LEVEL})
option(SM_NO_EXCEPTIONS "Build with -fno-exceptions -fno-rtti"     OFF)
option(SM_SANITIZE      "Build with ASan + UBSan"                  OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
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
if(SM_BUILD_TESTBENCH AND SM_WITH_LOSS AND SM_WITH_TIMING AND SM_WITH_CC AND SM_WITH_AUDIO AND SM_WITH_VIDEO AND SM_WITH_RTP)
  add_subdirectory(testbench)
endif()
