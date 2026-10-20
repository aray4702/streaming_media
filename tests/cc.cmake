sm_add_test_exe(sm_unit_cc LABEL unit DEPS sm::cc
  SOURCES unit/cc/transport_feedback_test.cc unit/cc/bwe_test.cc unit/cc/aimd_rate_control_test.cc unit/cc/pacer_probe_test.cc unit/cc/pacing_controller_test.cc
          unit/cc/allocation_adaptation_test.cc unit/cc/bitrate_allocator_v2_test.cc unit/cc/encoder_bitrate_adjuster_test.cc unit/cc/congestion_window_pushback_test.cc unit/cc/probe_v2_test.cc unit/cc/loss_based_bwe_v2_test.cc unit/cc/delay_based_bwe_libwebrtc_test.cc unit/cc/robust_throughput_estimator_test.cc unit/cc/acknowledged_bitrate_estimator_test.cc unit/cc/prioritized_packet_queue_test.cc
  SUITES TransportFeedback DelayBwe DelayBweV2 DelayBweLibwebrtc RobustThroughputEstimator AcknowledgedBitrateEstimator PrioritizedPacketQueue AimdRateControl BitrateEstimatorV2 LossBwe SendSideBwe Pacer PacingController Probing BitrateAllocator BitrateAllocatorV2 FrameDropper
         VideoAdaptation Outage CongestionWindowPushback EncoderOvershootDetector EncoderBitrateAdjuster
         BitrateProber PacingControllerProbing AlrDetector ProbeControllerV2 ProbeBitrateEstimatorV2 GccV2Probing StartUpProbing LossBweV2 SendSideBweV2 GccV2Loss)
sm_add_test_exe(sm_e2e_cc LABEL e2e DEPS sm::cc
  SOURCES e2e/cc_e2e_test.cc
  SUITES E2E_TransportFeedback E2E_DelayBwe E2E_LossBwe E2E_SendSideBwe E2E_Pacer E2E_Probing
         E2E_BitrateAllocator E2E_FrameDropper E2E_VideoAdaptation E2E_Outage)
