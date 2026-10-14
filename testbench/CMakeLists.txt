# Call-level test bench: the whole library in one simulated call over
# scripted link impairments, measured as user-experience metrics.
add_library(sm_testbench_lib STATIC scenario.cc call.cc)
target_include_directories(sm_testbench_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sm_testbench_lib PUBLIC sm::loss sm::timing sm::cc sm::video)
sm_configure_target(sm_testbench_lib)

add_executable(sm_testbench sm_testbench_main.cc)
target_link_libraries(sm_testbench PRIVATE sm_testbench_lib)
sm_configure_target(sm_testbench)
if(EMSCRIPTEN)
  # Read scenario files from the host file system when run under Node.
  target_link_options(sm_testbench PRIVATE -sNODERAWFS=1)
endif()

if(SM_BUILD_TESTS)
  # One CTest entry per scenario; a failed expectation fails the test.
  file(GLOB SM_SCENARIOS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/*.scn)
  foreach(scn ${SM_SCENARIOS})
    get_filename_component(scn_name ${scn} NAME_WE)
    add_test(NAME scenario.${scn_name} COMMAND sm_testbench --quiet ${scn})
    set_tests_properties(scenario.${scn_name} PROPERTIES LABELS scenario)
  endforeach()
endif()

if(EMSCRIPTEN)
  add_subdirectory(${PROJECT_SOURCE_DIR}/web ${CMAKE_BINARY_DIR}/web)
endif()
