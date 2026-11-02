# BoringSSL for the connectivity layer (milestone 3, #944, #984): the TLS library libwebrtc M157 builds,
# at the revision its DEPS pins (src/third_party/boringssl/src). Only sm::tls links it; the rest of the
# library stays dependency-free.
#
#   sm_add_boringssl()   defines the targets sm_boringssl_crypto and sm_boringssl_ssl
#
# The source is fetched at configure time from GitHub's google/boringssl mirror, as cmake/SmOpus.cmake
# fetches libopus. Set SM_BORINGSSL_SOURCE_DIR to use a checkout you already have. BoringSSL ships its
# generated sources (gen/), so the build needs neither Go nor Perl.
include_guard(GLOBAL)

set(SM_BORINGSSL_REV 5fbad2285b096858fc9afa3e4c949fde39452070)
set(SM_BORINGSSL_REPO https://github.com/google/boringssl.git)
set(SM_BORINGSSL_SOURCE_DIR "" CACHE PATH "BoringSSL source at ${SM_BORINGSSL_REV} (fetched into the build tree when empty)")

function(sm_add_boringssl)
  if(TARGET sm_boringssl_crypto)
    return()
  endif()
  if(SM_BORINGSSL_SOURCE_DIR)
    set(dir ${SM_BORINGSSL_SOURCE_DIR})
  else()
    set(dir ${CMAKE_BINARY_DIR}/_deps/boringssl-${SM_BORINGSSL_REV})
  endif()
  if(NOT EXISTS ${dir}/include/openssl/ssl.h)
    find_package(Git REQUIRED)
    message(STATUS "Fetching BoringSSL ${SM_BORINGSSL_REV}")
    file(REMOVE_RECURSE ${dir})
    file(MAKE_DIRECTORY ${dir})
    execute_process(COMMAND ${GIT_EXECUTABLE} init -q WORKING_DIRECTORY ${dir} RESULT_VARIABLE rc)
    foreach(attempt 1 2 3 4 5)
      execute_process(COMMAND ${GIT_EXECUTABLE} fetch -q --depth 1 ${SM_BORINGSSL_REPO} ${SM_BORINGSSL_REV}
                      WORKING_DIRECTORY ${dir} RESULT_VARIABLE rc)
      if(rc EQUAL 0)
        break()
      endif()
      message(STATUS "BoringSSL fetch attempt ${attempt} failed")
      execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 10)
    endforeach()
    if(rc EQUAL 0)
      execute_process(COMMAND ${GIT_EXECUTABLE} -c advice.detachedHead=false checkout -q FETCH_HEAD
                      WORKING_DIRECTORY ${dir} RESULT_VARIABLE rc)
    endif()
    if(NOT rc EQUAL 0 OR NOT EXISTS ${dir}/include/openssl/ssl.h)
      message(FATAL_ERROR "Could not fetch BoringSSL ${SM_BORINGSSL_REV} from ${SM_BORINGSSL_REPO}; "
                          "set SM_BORINGSSL_SOURCE_DIR to a checkout of that revision")
    endif()
  endif()
  # BoringSSL's own tests and tools are not built; its targets are built only when linked.
  set(BUILD_TESTING OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  # Our sanitizer and warning flags are per target (sm_configure_target), so they don't reach BoringSSL.
  add_subdirectory(${dir} ${CMAKE_BINARY_DIR}/_deps/boringssl-build EXCLUDE_FROM_ALL SYSTEM)
  add_library(sm_boringssl_crypto INTERFACE)
  target_link_libraries(sm_boringssl_crypto INTERFACE crypto)
  target_include_directories(sm_boringssl_crypto SYSTEM INTERFACE ${dir}/include)
  add_library(sm_boringssl_ssl INTERFACE)
  target_link_libraries(sm_boringssl_ssl INTERFACE ssl sm_boringssl_crypto)
endfunction()
