sm_add_test_exe(sm_unit_rtp LABEL unit DEPS sm::rtp
  SOURCES unit/rtp/rtcp_engine_test.cc unit/rtp/rtcp_sdes_bye_test.cc
          unit/rtp/rtp_sender_test.cc unit/rtp/send_side_controller_test.cc
  SUITES RtcpEngine RtcpSdesBye RtpSender SendSideController)
sm_add_test_exe(sm_e2e_rtp LABEL e2e DEPS sm::rtp
  SOURCES e2e/rtp_sender_e2e_test.cc
  SUITES E2E_RtpSender)
