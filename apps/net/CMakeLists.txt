# OS sockets for the library's socket interfaces (#1037), the small WebSocket, HTTP and JSON helpers the apps'
# signaling uses (#1103), and the getaddrinfo resolver for remote mDNS candidates (#1152, on worker threads the
# host owns). POSIX only; not part of the library, which reads no clock, opens no socket and starts no thread.
find_package(Threads REQUIRED)
add_library(sm_os_sockets STATIC os_sockets.cc os_dns_resolver.cc websocket.cc)
target_include_directories(sm_os_sockets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sm_os_sockets PUBLIC sm::p2p_tls Threads::Threads)
sm_configure_target(sm_os_sockets)
