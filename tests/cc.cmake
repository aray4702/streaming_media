sm_add_test_exe(sm_unit_cc LABEL unit DEPS sm::cc
  SOURCES unit/cc/transport_feedback_test.cc unit/cc/bwe_test.cc unit/cc/pacer_probe_test.cc
          unit/cc/allocation_adaptation_test.cc unit/cc/congestion_window_pushback_test.cc
  SUITES TransportFeedback DelayBwe LossBwe SendSideBwe Pacer Probing BitrateAllocator FrameDropper
         VideoAdaptation Outage CongestionWindowPushback)
sm_add_test_exe(sm_e2e_cc LABEL e2e DEPS sm::cc
  SOURCES e2e/cc_e2e_test.cc
  SUITES E2E_TransportFeedback E2E_DelayBwe E2E_LossBwe E2E_SendSideBwe E2E_Pacer E2E_Probing
         E2E_BitrateAllocator E2E_FrameDropper E2E_VideoAdaptation E2E_Outage)
