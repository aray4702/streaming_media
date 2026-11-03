# The AI server (C12 part 1, #1103): answers the signaling service's `ai-` rooms with the library. It reuses the test
# bench's libopus encoder and decoder, turn detector and AI source (testbench/), and the OS sockets (apps/net).
add_library(sm_ai_server_lib STATIC ai_server.cc)
target_include_directories(sm_ai_server_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sm_ai_server_lib PUBLIC sm_testbench_lib sm_os_sockets sm::pc_endpoint sm::p2p_tls)
sm_configure_target(sm_ai_server_lib)

add_executable(sm_ai_server main.cc)
target_link_libraries(sm_ai_server PRIVATE sm_ai_server_lib)
sm_configure_target(sm_ai_server)

if(SM_BUILD_TESTS)
  # A native caller (PeerEndpoint) calls the server through an in-test signaling stub of #1102's protocol, over
  # loopback in real time: it connects, gets a scripted reply after its turn, and a barge-in stops the reply.
  sm_add_test_exe(sm_ai_server_test LABEL e2e DEPS sm_ai_server_lib
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ai_server_test.cc
    SUITES AiServerNet AiServer)
  set_tests_properties(e2e.AiServer PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
endif()
add_test(NAME unit.ai_server.usage COMMAND sm_ai_server --help)
set_tests_properties(unit.ai_server.usage PROPERTIES LABELS unit PASS_REGULAR_EXPRESSION "usage: sm_ai_server")
