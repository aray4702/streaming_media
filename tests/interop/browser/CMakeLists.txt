# A call between headless Chrome and a peer (C6 part 4, #1088). Each test exits
# 77 (skipped, not failed) when no Chrome is found, so the local gate passes
# without one. run_browser_call.py documents the harness.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  set(SM_BROWSER_CALL ${CMAKE_CURRENT_SOURCE_DIR}/run_browser_call.py)
  # The harness self-test: Chrome calls a second RTCPeerConnection in the same
  # page through the signaling server, so the server, page and checks are tested
  # on their own.
  foreach(role offer answer)
    add_test(NAME interop.BrowserSelf.chrome_${role}s
             COMMAND ${Python3_EXECUTABLE} ${SM_BROWSER_CALL} --self --role=${role})
    set_tests_properties(interop.BrowserSelf.chrome_${role}s PROPERTIES
      LABELS interop SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK sm_chrome)
  endforeach()
endif()
