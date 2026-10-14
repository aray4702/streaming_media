# Browser port: an ES module (sm_web.js + sm_web.wasm) exposing the test
# bench and library components to JavaScript. Built only with Emscripten.
add_executable(sm_web sm_web.cc)
target_link_libraries(sm_web PRIVATE sm_testbench_lib)
sm_configure_target(sm_web)
target_link_options(sm_web PRIVATE -lembind -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createSmModule
                    -sENVIRONMENT=web,worker,node -sALLOW_MEMORY_GROWTH=1)
set_target_properties(sm_web PROPERTIES SUFFIX ".js")

# Demo page next to the module, with the scenario suite.
add_custom_command(TARGET sm_web POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/index.html $<TARGET_FILE_DIR:sm_web>/index.html
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/testbench/scenarios $<TARGET_FILE_DIR:sm_web>/scenarios
  COMMAND ${CMAKE_COMMAND} -E echo_append "" > $<TARGET_FILE_DIR:sm_web>/.nojekyll)

if(SM_BUILD_TESTS)
  find_program(NODE_EXE NAMES node nodejs)
  if(NODE_EXE)
    add_test(NAME web.smoke COMMAND ${NODE_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/test/smoke.mjs
             $<TARGET_FILE:sm_web> ${PROJECT_SOURCE_DIR}/testbench/scenarios/clean_link.scn)
    set_tests_properties(web.smoke PROPERTIES LABELS web)
  endif()
endif()
