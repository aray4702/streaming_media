# sm_compare: the test bench run through this library and through a prebuilt
# libwebrtc, side by side. A separate project because everything in it is
# compiled against the libc++ the libwebrtc binary was built with
# (Chromium's, namespace std::__Cr), including this library's sources.
#
#   compare/webrtc/fetch.sh
#   cmake -S compare/webrtc -B out/compare -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build out/compare
#   out/compare/sm_compare testbench/scenarios/*.scn
cmake_minimum_required(VERSION 3.20)
project(sm_compare_webrtc CXX)

set(SM_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(WEBRTC_DEPS_DIR ${SM_ROOT}/out/webrtc-deps CACHE PATH "Directory filled by fetch.sh")
set(WEBRTC_DIR ${WEBRTC_DEPS_DIR}/webrtc)
if(NOT EXISTS ${WEBRTC_DIR}/lib/libwebrtc.a)
  message(FATAL_ERROR "libwebrtc not found in ${WEBRTC_DEPS_DIR}; run compare/webrtc/fetch.sh first")
endif()

file(STRINGS ${WEBRTC_DIR}/VERSIONS webrtc_version_line REGEX "^WEBRTC_READABLE_VERSION=")
string(REPLACE "WEBRTC_READABLE_VERSION=" "" WEBRTC_VERSION "${webrtc_version_line}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Chromium's libc++ instead of the toolchain's.
add_compile_options(
  -nostdinc++
  "SHELL:-isystem ${WEBRTC_DEPS_DIR}/libcxx-config"
  "SHELL:-isystem ${WEBRTC_DEPS_DIR}/libcxx/include"
  "SHELL:-isystem ${WEBRTC_DIR}/include/third_party/libc++abi/src/include"
  -Wno-nullability-completeness)
add_compile_definitions(
  _LIBCPP_DISABLE_AVAILABILITY
  _LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS
  _LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS
  _LIBCPP_ENABLE_NODISCARD
  _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE
  SM_ENABLE_METRICS=1
  SM_ENABLE_EVENTS=1)

# This library and the test bench, built with the same standard library.
file(GLOB SM_SOURCES ${SM_ROOT}/src/*/*.cc)
# The connectivity layer's TLS module (sm::tls, #984) links BoringSSL. This build doesn't link a BoringSSL of its own:
# libwebrtc.a carries the one libwebrtc M157 builds, at the revision cmake/SmBoringSSL.cmake pins, so the part of
# sm::tls that TURN over TLS needs (#1055: TlsSslAdapter for our side's client and the TURN server) is compiled
# against libwebrtc's BoringSSL headers and links its symbols. DTLS, SRTP and Mbed TLS are left out.
list(FILTER SM_SOURCES EXCLUDE REGEX "/src/tls/")
list(FILTER SM_SOURCES EXCLUDE REGEX "/src/p2p/(stun_crypto_tls|dtls_transport)\\.cc$")
# Apple-only (VideoToolbox, #1104): the comparison builds on Linux.
list(FILTER SM_SOURCES EXCLUDE REGEX "/src/video/videotoolbox_[^/]*\\.cc$")
set(SM_COMPARE_TLS_SOURCES message_digest.cc crypto_random.cc ssl_fingerprint.cc boringssl_digest.cc
    boringssl_identity.cc boringssl_tls.cc)
list(TRANSFORM SM_COMPARE_TLS_SOURCES PREPEND ${SM_ROOT}/src/tls/)
# Same test bench sources as sm_testbench_lib (testbench/CMakeLists.txt); ice_transport.cc without SM_TESTBENCH_ICE: `transport ice`
# scenarios run as direct calls here, and say `compare off` (#1087).
add_library(sm_all STATIC ${SM_SOURCES} ${SM_COMPARE_TLS_SOURCES} ${SM_ROOT}/testbench/scenario.cc
            ${SM_ROOT}/testbench/call.cc ${SM_ROOT}/testbench/turn_detector.cc ${SM_ROOT}/testbench/ai_source.cc
            ${SM_ROOT}/testbench/ice_transport.cc)
target_include_directories(sm_all PUBLIC ${SM_ROOT}/include ${SM_ROOT}/testbench)
target_include_directories(sm_all SYSTEM PRIVATE ${WEBRTC_DIR}/include/third_party/boringssl/src/include)
# The test bench encodes its audio with libopus (issue #457). libwebrtc.a
# carries the same revision (55513e81, compare/webrtc/reference/fetch.sh), so
# this library's side links libwebrtc's copy and needs only the headers.
include(${SM_ROOT}/cmake/SmOpus.cmake)
sm_opus_source_dir(SM_OPUS_DIR)
target_include_directories(sm_all SYSTEM PUBLIC ${SM_OPUS_DIR}/include)

add_executable(sm_compare sm_compare_main.cc sim_time.cc webrtc_call.cc)
target_include_directories(sm_compare PRIVATE ${WEBRTC_DIR}/include ${WEBRTC_DIR}/include/third_party/abseil-cpp)
target_compile_definitions(sm_compare PRIVATE WEBRTC_POSIX)
file(WRITE ${CMAKE_BINARY_DIR}/gen/webrtc_version.h "#define SM_WEBRTC_VERSION \"${WEBRTC_VERSION}\"\n")
target_include_directories(sm_compare PRIVATE ${CMAKE_BINARY_DIR}/gen)
# sm_compare runs this library's call on a second thread (sm_compare_main.cc).
find_package(Threads REQUIRED)
target_link_libraries(sm_compare PRIVATE sm_all ${WEBRTC_DIR}/lib/libwebrtc.a Threads::Threads)
# libwebrtc.a contains the libc++ runtime.
target_link_options(sm_compare PRIVATE -nostdlib++)
# RTCP interop check (NACK, PLI, FIR, loss notification, compound): this
# library's wire formats against libwebrtc's parsers and back. It runs after
# every build, so the compare job fails if a format stops interoperating.
add_executable(sm_rtcp_interop rtcp_interop.cc)
target_include_directories(sm_rtcp_interop PRIVATE ${WEBRTC_DIR}/include ${WEBRTC_DIR}/include/third_party/abseil-cpp)
target_compile_definitions(sm_rtcp_interop PRIVATE WEBRTC_POSIX)
target_link_libraries(sm_rtcp_interop PRIVATE sm_all ${WEBRTC_DIR}/lib/libwebrtc.a)
target_link_options(sm_rtcp_interop PRIVATE -nostdlib++)
add_custom_command(TARGET sm_rtcp_interop POST_BUILD COMMAND sm_rtcp_interop VERBATIM)
# APM comparison (#63, section 6): this library's audio processing modules
# and libwebrtc's APM on the section 6 e2e test signals.
add_executable(sm_apm_compare apm_compare.cc)
target_include_directories(sm_apm_compare PRIVATE ${SM_ROOT}/tests/e2e ${WEBRTC_DIR}/include
                           ${WEBRTC_DIR}/include/third_party/abseil-cpp)
target_compile_definitions(sm_apm_compare PRIVATE WEBRTC_POSIX)
target_link_libraries(sm_apm_compare PRIVATE sm_all ${WEBRTC_DIR}/lib/libwebrtc.a)
target_link_options(sm_apm_compare PRIVATE -nostdlib++)
# Prints the table after every build, so the compare job's log carries it.
add_custom_command(TARGET sm_apm_compare POST_BUILD COMMAND sm_apm_compare VERBATIM)
# Group-call mixing (#1175, F6.13): this library's AudioMixerImpl port and libwebrtc's, bit for bit on the
# E2E_AudioMixer scenarios. It runs after every build and fails it on any difference.
add_executable(sm_mixer_compare mixer_compare.cc)
target_include_directories(sm_mixer_compare PRIVATE ${SM_ROOT}/tests/e2e ${WEBRTC_DIR}/include
                           ${WEBRTC_DIR}/include/third_party/abseil-cpp)
target_compile_definitions(sm_mixer_compare PRIVATE WEBRTC_POSIX)
target_link_libraries(sm_mixer_compare PRIVATE sm_all ${WEBRTC_DIR}/lib/libwebrtc.a)
target_link_options(sm_mixer_compare PRIVATE -nostdlib++)
add_custom_command(TARGET sm_mixer_compare POST_BUILD COMMAND sm_mixer_compare VERBATIM)
# The limiter's float arithmetic as libwebrtc's (Chromium builds with -ffp-contract=off), as src/CMakeLists.txt sets it.
set_source_files_properties(${SM_ROOT}/src/audio/agc2.cc ${SM_ROOT}/src/audio/audio_mixer.cc PROPERTIES
                            COMPILE_OPTIONS -ffp-contract=off)
# Video comparison (#63, section 7): freeze detection, quality scaling and
# frame timing, this library's modules and libwebrtc's on the same inputs.
add_executable(sm_video_compare video_compare.cc sim_time.cc)
target_include_directories(sm_video_compare PRIVATE ${WEBRTC_DIR}/include ${WEBRTC_DIR}/include/third_party/abseil-cpp)
target_compile_definitions(sm_video_compare PRIVATE WEBRTC_POSIX)
target_link_libraries(sm_video_compare PRIVATE sm_all ${WEBRTC_DIR}/lib/libwebrtc.a)
target_link_options(sm_video_compare PRIVATE -nostdlib++)
add_custom_command(TARGET sm_video_compare POST_BUILD COMMAND sm_video_compare VERBATIM)
# ICE comparison (#1004, C3 part 8): this library's P2PTransportChannel and libwebrtc's, set up as PeerConnection
# sets them up, on the same emulated network with NATs. Our side's STUN integrity uses the p2p tests' SHA-1/HMAC.
add_executable(sm_ice_compare ice_compare.cc sim_time.cc ${SM_ROOT}/tests/unit/p2p/test_crypto.cc)
target_include_directories(sm_ice_compare PRIVATE ${SM_ROOT}/tests/unit/p2p ${WEBRTC_DIR}/include
                           ${WEBRTC_DIR}/include/third_party/abseil-cpp)
target_compile_definitions(sm_ice_compare PRIVATE WEBRTC_POSIX)
target_link_libraries(sm_ice_compare PRIVATE sm_all ${WEBRTC_DIR}/lib/libwebrtc.a)
target_link_options(sm_ice_compare PRIVATE -nostdlib++ -rdynamic)
add_custom_command(TARGET sm_ice_compare POST_BUILD COMMAND sm_ice_compare --verbose VERBATIM)
if(APPLE)
  target_compile_definitions(sm_compare PRIVATE WEBRTC_MAC)
  target_compile_definitions(sm_rtcp_interop PRIVATE WEBRTC_MAC)
  target_compile_definitions(sm_apm_compare PRIVATE WEBRTC_MAC)
  target_compile_definitions(sm_mixer_compare PRIVATE WEBRTC_MAC)
  target_compile_definitions(sm_video_compare PRIVATE WEBRTC_MAC)
  target_compile_definitions(sm_ice_compare PRIVATE WEBRTC_MAC)
  foreach(fw Foundation CoreFoundation Security AudioToolbox CoreAudio CoreMedia CoreVideo VideoToolbox
             AVFoundation AppKit Metal MetalKit IOSurface QuartzCore ScreenCaptureKit OpenGL ApplicationServices)
    target_link_libraries(sm_compare PRIVATE "-framework ${fw}")
    target_link_libraries(sm_rtcp_interop PRIVATE "-framework ${fw}")
    target_link_libraries(sm_apm_compare PRIVATE "-framework ${fw}")
    target_link_libraries(sm_mixer_compare PRIVATE "-framework ${fw}")
    target_link_libraries(sm_video_compare PRIVATE "-framework ${fw}")
    target_link_libraries(sm_ice_compare PRIVATE "-framework ${fw}")
  endforeach()
else()
  target_compile_definitions(sm_compare PRIVATE WEBRTC_LINUX)
  target_compile_definitions(sm_rtcp_interop PRIVATE WEBRTC_LINUX)
  target_compile_definitions(sm_apm_compare PRIVATE WEBRTC_LINUX)
  target_compile_definitions(sm_mixer_compare PRIVATE WEBRTC_LINUX)
  target_compile_definitions(sm_video_compare PRIVATE WEBRTC_LINUX)
  target_compile_definitions(sm_ice_compare PRIVATE WEBRTC_LINUX)
  target_link_libraries(sm_compare PRIVATE pthread dl rt)
  target_link_libraries(sm_rtcp_interop PRIVATE pthread dl rt)
  target_link_libraries(sm_apm_compare PRIVATE pthread dl rt)
  target_link_libraries(sm_mixer_compare PRIVATE pthread dl rt)
  target_link_libraries(sm_video_compare PRIVATE pthread dl rt)
  target_link_libraries(sm_ice_compare PRIVATE pthread dl rt)
endif()
