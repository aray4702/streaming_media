# Web app and signaling service (C15, #1102). The service's unit tests check /turn against
# CreateTurnRestCredentials; the browser tests exit 77 (skipped) when no Chrome is found.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_executable(sm_turn_credentials signaling/turn_credentials_tool.cc)
  target_link_libraries(sm_turn_credentials PRIVATE sm::p2p_tls)
  sm_configure_target(sm_turn_credentials)
  add_test(NAME unit.SignalingService
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/signaling/test_signaling.py)
  set_tests_properties(unit.SignalingService PROPERTIES LABELS unit TIMEOUT 60
    ENVIRONMENT "SM_TURN_TOOL=$<TARGET_FILE:sm_turn_credentials>")

  # The real-network calls' runner (#1121): its table and parsing; the live calls only with SM_SIGNALING_URL set.
  add_test(NAME unit.RealCalls COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/test_real_calls.py)
  set_tests_properties(unit.RealCalls PROPERTIES LABELS unit TIMEOUT 400)

  set(SM_WEB_CALL ${CMAKE_CURRENT_SOURCE_DIR}/web/test_web_call.py)
  add_test(NAME interop.WebApp.two_tabs COMMAND ${Python3_EXECUTABLE} ${SM_WEB_CALL})
  set_tests_properties(interop.WebApp.two_tabs PROPERTIES
    LABELS interop SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK sm_chrome)
  # Group calls through a local LiveKit server (C19, #1164): two pages in Group mode; skipped without Chrome or
  # livekit-server (SM_LIVEKIT_SERVER). The native LiveKit client's test is in apps/call.
  set(SM_GROUP_CALL ${CMAKE_CURRENT_SOURCE_DIR}/web/test_group_call.py)
  add_test(NAME interop.WebApp.group COMMAND ${Python3_EXECUTABLE} ${SM_GROUP_CALL})
  set_tests_properties(interop.WebApp.group PROPERTIES
    LABELS interop SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK sm_chrome)
  if(TARGET sm_interop_browser_peer)
    add_test(NAME interop.WebApp.native COMMAND ${Python3_EXECUTABLE} ${SM_WEB_CALL}
             --peer=$<TARGET_FILE:sm_interop_browser_peer>)
    set_tests_properties(interop.WebApp.native PROPERTIES
      LABELS interop SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK sm_chrome)
  endif()
endif()
