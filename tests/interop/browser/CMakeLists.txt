# A call between headless Chrome and a peer (C6 part 4, #1088). Each test exits
# 77 (skipped, not failed) when no Chrome is found, so the local gate passes
# without one. run_browser_call.py documents the harness.
# Every call also checks that Chrome's send-side bandwidth estimate ramps up on
# loopback (#1125: Chrome -> Chrome reaches about 3.5 Mbps within 3 s; with
# feedback Chrome ignored, toward our endpoint it fell from 300 to 84 kbps).
# On a loaded CI Mac, Chrome's own ramp is sometimes slow (#1132), so the
# self-test records Chrome <-> Chrome's estimate and a call with our endpoint
# that did not reach 1500 kbps is judged against it (bwe_check in
# run_browser_call.py). The browser tests run serially (nothing else loads the
# host) and give the estimate 20 s. Our peer's arrival times must be the times
# it read each datagram (#1168: stamped with the time before poll(2), Chrome's
# probes toward it measured low and its estimate stalled at 957 kbps); when the
# check fails, the driver prints Chrome's probe results.
set(SM_BROWSER_BWE --bwe-kbps=1500 --bwe-seconds=20 --timeout=60
    --bwe-reference=${CMAKE_CURRENT_BINARY_DIR}/chrome_bwe_reference.json)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  set(SM_BROWSER_CALL ${CMAKE_CURRENT_SOURCE_DIR}/run_browser_call.py)
  # The harness self-test: Chrome calls a second RTCPeerConnection in the same
  # page through the signaling server, so the server, page and checks are tested
  # on their own.
  foreach(role offer answer)
    add_test(NAME interop.BrowserSelf.chrome_${role}s
             COMMAND ${Python3_EXECUTABLE} ${SM_BROWSER_CALL} --self --role=${role} ${SM_BROWSER_BWE})
    set_tests_properties(interop.BrowserSelf.chrome_${role}s PROPERTIES
      LABELS interop SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK sm_chrome RUN_SERIAL TRUE)
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
                     --role=${role} ${SM_BROWSER_DC} ${SM_BROWSER_BWE})
    # After the self-tests, whose Chrome <-> Chrome estimate is the reference.
    set_tests_properties(interop.Browser.chrome_${role}s PROPERTIES
      LABELS interop SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK sm_chrome RUN_SERIAL TRUE
      DEPENDS "interop.BrowserSelf.chrome_offers;interop.BrowserSelf.chrome_answers")
  endforeach()
  # Remote mDNS candidates (#1152): Chrome without a media permission hides its host IPs behind .local names (its
  # default); our endpoint resolves them (getaddrinfo) and connects host to host. Skipped (77) where .local names
  # don't resolve. No bandwidth check: the media are a canvas and a tone.
  add_test(NAME interop.Browser.mdns_chrome_offers
           COMMAND ${Python3_EXECUTABLE} ${SM_BROWSER_CALL} --peer=$<TARGET_FILE:sm_interop_browser_peer>
                   --role=offer --mdns ${SM_BROWSER_DC} --timeout=60)
  set_tests_properties(interop.Browser.mdns_chrome_offers PROPERTIES
    LABELS interop SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK sm_chrome RUN_SERIAL TRUE)
  # Multi-stream receive with renegotiation (C18, #1160): Chrome sends 5 participants' audio and video (10 tracks) on
  # one connection in three offers (2 participants, 3 more join, the first leaves); the endpoint answers each and
  # receives every track on its own stream. No bandwidth check: the media are canvases and tones.
  add_test(NAME interop.Browser.multistream_chrome_offers
           COMMAND ${Python3_EXECUTABLE} ${SM_BROWSER_CALL} --peer=$<TARGET_FILE:sm_interop_browser_peer>
                   --role=offer --multistream=5 --timeout=70)
  set_tests_properties(interop.Browser.multistream_chrome_offers PROPERTIES
    LABELS interop SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK sm_chrome RUN_SERIAL TRUE)
endif()
