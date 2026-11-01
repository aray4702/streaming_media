# Call-level test bench: the whole library in one simulated call over
# scripted link impairments, measured as user-experience metrics.
# Audio is encoded and decoded with libopus (cmake/SmOpus.cmake, issues #457,
# #476); the library itself has no dependencies.
enable_language(C)
include(${PROJECT_SOURCE_DIR}/cmake/SmOpus.cmake)
sm_add_opus()
add_library(sm_testbench_lib STATIC scenario.cc call.cc turn_detector.cc ai_source.cc)
target_include_directories(sm_testbench_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sm_testbench_lib PUBLIC sm::loss sm::timing sm::cc sm::audio sm::video sm::rtp sm_opus)
sm_configure_target(sm_testbench_lib)

add_executable(sm_testbench sm_testbench_main.cc)
target_link_libraries(sm_testbench PRIVATE sm_testbench_lib)
sm_configure_target(sm_testbench)
if(EMSCRIPTEN)
  # Read scenario files from the host file system when run under Node.
  target_link_options(sm_testbench PRIVATE -sNODERAWFS=1)
endif()

if(SM_BUILD_TESTS)
  # Unit tests of the bench's own metric computations.
  sm_add_test_exe(sm_unit_testbench LABEL unit DEPS sm_testbench_lib
    SOURCES ${PROJECT_SOURCE_DIR}/tests/unit/testbench/av_sync_rtcp_test.cc
            ${PROJECT_SOURCE_DIR}/tests/unit/testbench/testbench_metrics_test.cc
            ${PROJECT_SOURCE_DIR}/tests/unit/testbench/opus_encoder_test.cc
            ${PROJECT_SOURCE_DIR}/tests/unit/testbench/opus_decoder_test.cc
            ${PROJECT_SOURCE_DIR}/tests/unit/testbench/encoder_model_test.cc
            ${PROJECT_SOURCE_DIR}/tests/unit/testbench/rtcp_seed_test.cc
            ${PROJECT_SOURCE_DIR}/tests/unit/testbench/event_timing_test.cc
            ${PROJECT_SOURCE_DIR}/tests/unit/testbench/call_mode_test.cc
            ${PROJECT_SOURCE_DIR}/tests/unit/testbench/turn_detector_test.cc
            ${PROJECT_SOURCE_DIR}/tests/unit/testbench/ai_source_test.cc
    SUITES TestbenchMetrics OpusEncoder OpusDecoder EncoderModel TestbenchRtcpSeed TestbenchAvSyncRtcp TestbenchEventTiming
           TestbenchCallMode TurnDetector TestbenchAiSource)
  # sm_testbench refuses `two_way_video` in a one-way call with a clear error (issues #743, #747).
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/two_way_rejected.scn "name two_way_rejected\ntwo_way_video up\nduration 2s\n")
  add_test(NAME unit.testbench.two_way_rejected
    COMMAND sm_testbench --quiet ${CMAKE_CURRENT_BINARY_DIR}/two_way_rejected.scn)
  set_tests_properties(unit.testbench.two_way_rejected PROPERTIES LABELS unit
    PASS_REGULAR_EXPRESSION "two_way_video needs call two-way")
  # One CTest entry per scenario; a failed expectation fails the test.
  file(GLOB SM_SCENARIOS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/*.scn)
  foreach(scn ${SM_SCENARIOS})
    get_filename_component(scn_name ${scn} NAME_WE)
    add_test(NAME scenario.${scn_name} COMMAND sm_testbench --quiet ${scn})
    set_tests_properties(scenario.${scn_name} PROPERTIES LABELS scenario)
  endforeach()
  # The baseline comparison tool's file and per-scenario forms (issue #498).
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND AND NOT EMSCRIPTEN)
    add_test(NAME unit.tools.testbench_compare
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/tools/testbench_compare_test.py)
    set_tests_properties(unit.tools.testbench_compare PROPERTIES LABELS unit)
    # A two-way call writes each direction's traces and time series, and the tools read them (issue #749).
    add_test(NAME unit.tools.two_way_traces
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/tools/two_way_traces_test.py
              $<TARGET_FILE:sm_testbench> ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/two_way_video_clean.scn)
    set_tests_properties(unit.tools.two_way_traces PROPERTIES LABELS unit)
  endif()
endif()

if(EMSCRIPTEN)
  add_subdirectory(${PROJECT_SOURCE_DIR}/web ${CMAKE_BINARY_DIR}/web)
endif()
