# Mbed TLS for the connectivity layer on microcontrollers (C1 part 3, #998, as decided on #944): the second sm::tls
# backend, beside BoringSSL (cmake/SmBoringSSL.cmake). The 3.6 LTS line, at a pinned release tag.
#
#   sm_add_mbedtls()   defines the target sm_mbedtls (libmbedcrypto, libmbedx509 and libmbedtls in one archive)
#
# The source is fetched at configure time from GitHub, as cmake/SmBoringSSL.cmake fetches BoringSSL. Set
# SM_MBEDTLS_SOURCE_DIR to use a checkout you already have. Release tags carry their generated sources, so the build
# needs neither Python nor Perl. Mbed TLS's own CMake needs its `framework` submodule (test code), so the library's C
# files are compiled here directly, as cmake/SmLibsrtp.cmake does for libsrtp.
#
# Configuration: Mbed TLS's default mbedtls_config.h, plus cmake/mbedtls/sm_mbedtls_user_config.h (DTLS-SRTP, which
# the default leaves off).
include_guard(GLOBAL)
enable_language(C)

set(SM_MBEDTLS_REV 068ff080b369adfac81509f9b57b2afabaf82dc5)  # tag v3.6.7
set(SM_MBEDTLS_REPO https://github.com/Mbed-TLS/mbedtls.git)
set(SM_MBEDTLS_SOURCE_DIR "" CACHE PATH "Mbed TLS source at ${SM_MBEDTLS_REV} (fetched into the build tree when empty)")

function(sm_add_mbedtls)
  if(TARGET sm_mbedtls)
    return()
  endif()
  if(SM_MBEDTLS_SOURCE_DIR)
    set(dir ${SM_MBEDTLS_SOURCE_DIR})
  else()
    set(dir ${CMAKE_BINARY_DIR}/_deps/mbedtls-${SM_MBEDTLS_REV})
  endif()
  if(NOT EXISTS ${dir}/include/mbedtls/ssl.h)
    find_package(Git REQUIRED)
    message(STATUS "Fetching Mbed TLS ${SM_MBEDTLS_REV}")
    file(REMOVE_RECURSE ${dir})
    file(MAKE_DIRECTORY ${dir})
    execute_process(COMMAND ${GIT_EXECUTABLE} init -q WORKING_DIRECTORY ${dir} RESULT_VARIABLE rc)
    foreach(attempt 1 2 3 4 5)
      execute_process(COMMAND ${GIT_EXECUTABLE} fetch -q --depth 1 ${SM_MBEDTLS_REPO} ${SM_MBEDTLS_REV}
                      WORKING_DIRECTORY ${dir} RESULT_VARIABLE rc)
      if(rc EQUAL 0)
        break()
      endif()
      message(STATUS "Mbed TLS fetch attempt ${attempt} failed")
      execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 10)
    endforeach()
    if(rc EQUAL 0)
      execute_process(COMMAND ${GIT_EXECUTABLE} -c advice.detachedHead=false checkout -q FETCH_HEAD
                      WORKING_DIRECTORY ${dir} RESULT_VARIABLE rc)
    endif()
    if(NOT rc EQUAL 0 OR NOT EXISTS ${dir}/include/mbedtls/ssl.h)
      message(FATAL_ERROR "Could not fetch Mbed TLS ${SM_MBEDTLS_REV} from ${SM_MBEDTLS_REPO}; "
                          "set SM_MBEDTLS_SOURCE_DIR to a checkout of that revision")
    endif()
  endif()
  file(GLOB srcs ${dir}/library/*.c)
  add_library(sm_mbedtls STATIC ${srcs})
  set_target_properties(sm_mbedtls PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
  target_include_directories(sm_mbedtls PRIVATE ${dir}/library)
  target_include_directories(sm_mbedtls SYSTEM PUBLIC ${dir}/include ${PROJECT_SOURCE_DIR}/cmake/mbedtls)
  target_compile_definitions(sm_mbedtls PUBLIC MBEDTLS_USER_CONFIG_FILE="sm_mbedtls_user_config.h")
endfunction()
