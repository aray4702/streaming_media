sm_add_test_exe(sm_unit_timing LABEL unit DEPS sm::timing
  SOURCES unit/timing/packet_buffer_test.cc unit/timing/rtcp_stats_test.cc
          unit/timing/delay_manager_test.cc unit/timing/time_stretch_test.cc
          unit/timing/audio_jitter_buffer_test.cc unit/timing/video_jitter_test.cc
          unit/timing/clock_drift_test.cc unit/timing/vcm_timing_test.cc
          unit/timing/neteq_delay_test.cc
  SUITES PacketBuffer RtcpStats DelayManager TimeStretch AudioJitterBuffer VideoJitter ClockDrift VcmTiming
         NetEqHistogram UnderrunOptimizer ReorderOptimizer PacketArrivalHistory NetEqDelayManager
         AudioJitterBufferNetEqV2 NetEqBufferLevelFilter NetEqDecisionLogic)
sm_add_test_exe(sm_e2e_timing LABEL e2e DEPS sm::timing
  SOURCES e2e/timing_e2e_test.cc
  SUITES E2E_PacketBuffer E2E_RtcpStats E2E_DelayManager E2E_TimeStretch E2E_AudioJitterBuffer
         E2E_VideoJitter E2E_ClockDrift)
