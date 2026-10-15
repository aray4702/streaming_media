sm_add_test_exe(sm_unit_rtp LABEL unit DEPS sm::rtp
  SOURCES unit/rtp/rtcp_engine_test.cc unit/rtp/rtcp_sdes_bye_test.cc
  SUITES RtcpEngine RtcpSdesBye)
