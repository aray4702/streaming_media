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

# The call with our endpoint (pc::PeerEndpoint, C6 part 3) over OS sockets.
if(Python3_FOUND AND TARGET sm::pc_endpoint)
  add_executable(sm_interop_browser_peer browser_peer.cc)
  target_link_libraries(sm_interop_browser_peer PRIVATE sm::pc_endpoint sm_os_sockets)
  sm_configure_target(sm_interop_browser_peer)
  # With data channels built (C7 part 5, #1091), the call carries one each way; without, no data checks.
  set(SM_BROWSER_DC)
  if(NOT TARGET sm::pc_datachannel)
    set(SM_BROWSER_DC --no-data-channel)
  endif()
  foreach(role offer answer)
    add_test(NAME interop.Browser.chrome_${role}s
             COMMAND ${Python3_EXECUTABLE} ${SM_BROWSER_CALL} --peer=$<TARGET_FILE:sm_interop_browser_peer>
                     --role=${role} ${SM_BROWSER_DC})
    set_tests_properties(interop.Browser.chrome_${role}s PROPERTIES
      LABELS interop SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK sm_chrome)
  endforeach()
endif()
