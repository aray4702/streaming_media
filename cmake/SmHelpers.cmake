# Shared compile settings for every sm target.
function(sm_configure_target target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow
                           -Wno-unused-parameter)
    if(SM_NO_EXCEPTIONS)
      target_compile_options(${target} PRIVATE -fno-exceptions -fno-rtti)
    endif()
    if(SM_SANITIZE)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined
                             -fno-omit-frame-pointer -fno-sanitize-recover=all)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
    if(SM_BUILD_FUZZERS)
      # Coverage feedback for libFuzzer in the library code too (fuzz/).
      target_compile_options(${target} PRIVATE -fsanitize=fuzzer-no-link)
    endif()
  endif()
  if(EMSCRIPTEN)
    # Test and benchmark executables hold seconds of audio/video in memory;
    # let the Wasm heap grow beyond Emscripten's 16 MB default.
    get_target_property(_type ${target} TYPE)
    if(_type STREQUAL "EXECUTABLE")
      target_link_options(${target} PRIVATE -sALLOW_MEMORY_GROWTH=1)
    endif()
  endif()
  target_compile_definitions(${target} PUBLIC
    SM_ENABLE_METRICS=$<BOOL:${SM_ENABLE_METRICS}>
    SM_ENABLE_EVENTS=$<BOOL:${SM_ENABLE_EVENTS}>)
endfunction()

# sm_add_library(<section> SOURCES ... DEPS ...)
# Creates sm_<section> and the alias sm::<section>.
function(sm_add_library name)
  cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})
  add_library(sm_${name} STATIC ${ARG_SOURCES})
  add_library(sm::${name} ALIAS sm_${name})
  # Installed as sm::<section> too (see cmake/streaming_mediaConfig.cmake).
  set_target_properties(sm_${name} PROPERTIES EXPORT_NAME ${name})
  target_include_directories(sm_${name} PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
  target_link_libraries(sm_${name} PUBLIC ${ARG_DEPS})
  sm_configure_target(sm_${name})
endfunction()

# sm_add_test_exe(<name> SOURCES ... DEPS ... SUITES ...)
# Builds a test executable and registers one CTest entry per suite so every
# algorithm reports pass/fail on its own line.
# Callers list one source and one suite per line, sorted, so that two
# changes that each add a test touch different lines (issue #286).
function(sm_add_test_exe name)
  cmake_parse_arguments(ARG "" "LABEL" "SOURCES;DEPS;SUITES" ${ARGN})
  add_executable(${name} ${ARG_SOURCES})
  target_link_libraries(${name} PRIVATE sm_test_framework ${ARG_DEPS})
  sm_configure_target(${name})
  foreach(suite ${ARG_SUITES})
    add_test(NAME ${ARG_LABEL}.${suite} COMMAND ${name} --filter=${suite}.)
    set_tests_properties(${ARG_LABEL}.${suite} PROPERTIES LABELS ${ARG_LABEL})
  endforeach()
endfunction()
