# libopus for the test bench only (issue #457): the bench encodes its audio
# with the libopus revision libwebrtc M157 builds, instead of a size model.
# The core library stays dependency-free.
#
#   sm_add_opus()         defines the static library target sm_opus (call
#                         enable_language(C) in the directory first)
#   sm_opus_source_dir(v) sets v to the libopus source tree (headers only)
#
# The source is fetched at configure time from GitHub's xiph/opus mirror,
# pinned to the revision compare/webrtc/reference/fetch.sh names (OPUS_REV).
# Set SM_OPUS_SOURCE_DIR to use a checkout you already have.
#
# Built as libwebrtc builds it (float, not FIXED_POINT), but in plain C:
# without the SIMD paths and without contracting float expressions into FMA,
# so that every platform the bench runs on codes the same bits as the
# canonical one (Linux x86_64) and testbench/baseline.json holds everywhere.
include_guard(GLOBAL)

set(SM_OPUS_REV 55513e81d8f606bd75d0ff773d2144e5f2a732f5)
set(SM_OPUS_REPO https://github.com/xiph/opus.git)
set(SM_OPUS_SOURCE_DIR "" CACHE PATH "libopus source at ${SM_OPUS_REV} (fetched into the build tree when empty)")

function(sm_opus_source_dir out_var)
  if(SM_OPUS_SOURCE_DIR)
    set(dir ${SM_OPUS_SOURCE_DIR})
  else()
    set(dir ${CMAKE_BINARY_DIR}/_deps/opus-${SM_OPUS_REV})
  endif()
  if(NOT EXISTS ${dir}/include/opus.h)
    find_package(Git REQUIRED)
    message(STATUS "Fetching libopus ${SM_OPUS_REV}")
    file(REMOVE_RECURSE ${dir})
    file(MAKE_DIRECTORY ${dir})
    execute_process(COMMAND ${GIT_EXECUTABLE} init -q WORKING_DIRECTORY ${dir} RESULT_VARIABLE rc)
    foreach(attempt 1 2 3 4 5)
      execute_process(COMMAND ${GIT_EXECUTABLE} fetch -q --depth 1 ${SM_OPUS_REPO} ${SM_OPUS_REV}
                      WORKING_DIRECTORY ${dir} RESULT_VARIABLE rc)
      if(rc EQUAL 0)
        break()
      endif()
      message(STATUS "libopus fetch attempt ${attempt} failed")
      execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 10)
    endforeach()
    if(rc EQUAL 0)
      execute_process(COMMAND ${GIT_EXECUTABLE} -c advice.detachedHead=false checkout -q FETCH_HEAD
                      WORKING_DIRECTORY ${dir} RESULT_VARIABLE rc)
    endif()
    if(NOT rc EQUAL 0 OR NOT EXISTS ${dir}/include/opus.h)
      message(FATAL_ERROR "Could not fetch libopus ${SM_OPUS_REV} from ${SM_OPUS_REPO}; "
                          "set SM_OPUS_SOURCE_DIR to a checkout of that revision")
    endif()
  endif()
  set(${out_var} ${dir} PARENT_SCOPE)
endfunction()

# The file names listed under `name = \` in one of libopus's *_sources.mk.
function(_sm_opus_mk_sources dir mk name out_var)
  file(READ ${dir}/${mk} text)
  # The variable's line and its backslash-continued lines.
  string(REGEX MATCH "(^|\n)${name} = \\\\\n([^\n]*\\\\\n)*[^\n]*" block "${text}")
  string(REGEX MATCHALL "[A-Za-z0-9_/]+\\.c" files "${block}")
  if(NOT files)
    message(FATAL_ERROR "libopus: no ${name} in ${mk}")
  endif()
  list(TRANSFORM files PREPEND ${dir}/)
  set(${out_var} ${files} PARENT_SCOPE)
endfunction()

function(sm_add_opus)
  if(TARGET sm_opus)
    return()
  endif()
  sm_opus_source_dir(dir)
  set(all)
  foreach(spec "celt_sources.mk;CELT_SOURCES" "silk_sources.mk;SILK_SOURCES" "silk_sources.mk;SILK_SOURCES_FLOAT"
               "opus_sources.mk;OPUS_SOURCES" "opus_sources.mk;OPUS_SOURCES_FLOAT")
    list(GET spec 0 mk)
    list(GET spec 1 name)
    _sm_opus_mk_sources(${dir} ${mk} ${name} part)
    list(APPEND all ${part})
  endforeach()
  add_library(sm_opus STATIC ${all})
  target_include_directories(sm_opus SYSTEM PUBLIC ${dir}/include)
  target_include_directories(sm_opus PRIVATE ${dir} ${dir}/celt ${dir}/silk ${dir}/silk/float)
  # What libopus's own CMake defines for a float build without intrinsics.
  target_compile_definitions(sm_opus PRIVATE OPUS_BUILD
                             "PACKAGE_VERSION=\"${SM_OPUS_REV}\"")
  if(MSVC)
    target_compile_definitions(sm_opus PRIVATE USE_ALLOCA)
  else()
    target_compile_definitions(sm_opus PRIVATE VAR_ARRAYS HAVE_LRINT HAVE_LRINTF)
    # Optimized in every configuration: the Debug + sanitizer build runs the
    # whole scenario suite through it too.
    target_compile_options(sm_opus PRIVATE -O2 -ffp-contract=off -w)
  endif()
  set_target_properties(sm_opus PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
endfunction()
