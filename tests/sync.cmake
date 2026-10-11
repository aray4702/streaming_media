sm_add_test_exe(sm_unit_sync LABEL unit DEPS sm::sync
  SOURCES unit/sync/sync_test.cc
  SUITES RtpToNtp StreamSync CaptureTime HeaderExtensions)
sm_add_test_exe(sm_e2e_sync LABEL e2e DEPS sm::sync
  SOURCES e2e/sync_e2e_test.cc
  SUITES E2E_RtpToNtp E2E_StreamSync E2E_CaptureTime E2E_HeaderExtensions)
