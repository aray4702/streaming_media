sm_add_test_exe(sm_unit_audio LABEL unit DEPS sm::audio
  SOURCES unit/audio/filters_test.cc unit/audio/echo_test.cc unit/audio/dtx_level_mixer_test.cc
          unit/audio/agc2_test.cc unit/audio/splitting_filter_test.cc unit/audio/ns_test.cc
  SUITES HighPassFilter Vad NoiseSuppressor GainControl GainController2 TransientSuppressor Resampler EchoDelay
         EchoCanceller ComfortNoise AudioLevel SplittingFilter Ns)
sm_add_test_exe(sm_e2e_audio LABEL e2e DEPS sm::audio
  SOURCES e2e/audio_e2e_test.cc
  SUITES E2E_HighPassFilter E2E_Vad E2E_NoiseSuppressor E2E_GainControl E2E_EchoDelay E2E_EchoCanceller
         E2E_TransientSuppressor E2E_ComfortNoise E2E_AudioLevel E2E_Resampler)
