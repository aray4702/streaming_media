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
add_library(sm_all STATIC ${SM_SOURCES} ${SM_ROOT}/testbench/scenario.cc ${SM_ROOT}/testbench/call.cc)
target_include_directories(sm_all PUBLIC ${SM_ROOT}/include ${SM_ROOT}/testbench)

add_executable(sm_compare sm_compare_main.cc sim_time.cc webrtc_call.cc)
target_include_directories(sm_compare PRIVATE ${WEBRTC_DIR}/include ${WEBRTC_DIR}/include/third_party/abseil-cpp)
target_compile_definitions(sm_compare PRIVATE WEBRTC_POSIX)
file(WRITE ${CMAKE_BINARY_DIR}/gen/webrtc_version.h "#define SM_WEBRTC_VERSION \"${WEBRTC_VERSION}\"\n")
target_include_directories(sm_compare PRIVATE ${CMAKE_BINARY_DIR}/gen)
target_link_libraries(sm_compare PRIVATE sm_all ${WEBRTC_DIR}/lib/libwebrtc.a)
# libwebrtc.a contains the libc++ runtime.
target_link_options(sm_compare PRIVATE -nostdlib++)
if(APPLE)
  target_compile_definitions(sm_compare PRIVATE WEBRTC_MAC)
  foreach(fw Foundation CoreFoundation Security AudioToolbox CoreAudio CoreMedia CoreVideo VideoToolbox
             AVFoundation AppKit Metal MetalKit IOSurface QuartzCore ScreenCaptureKit OpenGL ApplicationServices)
    target_link_libraries(sm_compare PRIVATE "-framework ${fw}")
  endforeach()
else()
  target_compile_definitions(sm_compare PRIVATE WEBRTC_LINUX)
  target_link_libraries(sm_compare PRIVATE pthread dl rt)
endif()
