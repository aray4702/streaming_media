# OS sockets for the library's socket interfaces (#1037), and the small WebSocket, HTTP and JSON helpers the apps'
# signaling uses (#1103). POSIX only; not part of the library, which reads no clock and opens no socket.
add_library(sm_os_sockets STATIC os_sockets.cc websocket.cc)
target_include_directories(sm_os_sockets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sm_os_sockets PUBLIC sm::p2p_tls)
sm_configure_target(sm_os_sockets)
