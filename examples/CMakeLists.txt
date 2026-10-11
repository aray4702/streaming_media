add_executable(simulated_call simulated_call.cc)
target_link_libraries(simulated_call PRIVATE sm::sm)
sm_configure_target(simulated_call)
