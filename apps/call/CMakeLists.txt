# The calling app's C++ call controller (C14, #1116), shared by the iOS app (apps/ios/) and the tests: the AI server's
# endpoint wiring (apps/ai_server) and OS sockets (apps/net), the library's audio processing, and VideoToolbox H.264
# on Apple platforms.
add_library(sm_call_client STATIC call_client.cc livekit_proto.cc)
target_include_directories(sm_call_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sm_call_client PUBLIC sm_ai_server_lib sm::audio)
if(TARGET sm::videotoolbox)
  target_link_libraries(sm_call_client PUBLIC sm::videotoolbox)
endif()
sm_configure_target(sm_call_client)

# The controller from the command line; the simulator smoke run (see call_smoke.cc).
add_executable(sm_call_smoke call_smoke.cc)
target_link_libraries(sm_call_smoke PRIVATE sm_call_client)
sm_configure_target(sm_call_smoke)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set_target_properties(sm_call_smoke PROPERTIES MACOSX_BUNDLE OFF)
else()
  add_test(NAME unit.call_smoke.usage COMMAND sm_call_smoke)
  set_tests_properties(unit.call_smoke.usage PROPERTIES LABELS unit WILL_FAIL TRUE)
  # The native LiveKit client (C19, #1164) publishes to a local LiveKit server and a Chrome page receives it; skipped
  # without Chrome or livekit-server (SM_LIVEKIT_SERVER).
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME interop.WebApp.group_native
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/apps/web/test_group_call.py
                     --native=$<TARGET_FILE:sm_call_smoke>)
    set_tests_properties(interop.WebApp.group_native PROPERTIES
      LABELS interop SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK sm_chrome)
  endif()
endif()

if(SM_BUILD_TESTS)
  # Two calls through apps/signaling/server.py on loopback: the app calls the AI server, and two apps join one room.
  sm_add_test_exe(sm_call_client_test LABEL e2e DEPS sm_call_client
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/call_client_test.cc
    SUITES CallClient)
  set_tests_properties(e2e.CallClient PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
  # The native LiveKit client (C19, #1164) without a server: the protobuf codec and the publisher's signaling.
  sm_add_test_exe(sm_livekit_test LABEL unit DEPS sm_call_client
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/livekit_test.cc
    SUITES LiveKitProto LiveKitClient)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  target_compile_definitions(sm_call_client_test PRIVATE
    SM_SIGNALING_SERVER_PY="${PROJECT_SOURCE_DIR}/apps/signaling/server.py"
    SM_PYTHON="$<$<BOOL:${Python3_Interpreter_FOUND}>:${Python3_EXECUTABLE}>")
endif()
