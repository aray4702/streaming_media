sm_add_test_exe(sm_unit_video LABEL unit DEPS sm::video
  SOURCES
    unit/video/video_test.cc
  SUITES
    CodecFallback
    ContentPolicy
    FrameScaler
    FrameScheduler
    FreezeDetector
    Orientation
    QualityScaler
    RenderFrames
    VideoAdapter
    VideoStreamAdapter
)
# The video e2e scenarios also use the loss (keyframe requests) and timing
# (render-time estimation) sections when they are enabled.
set(SM_VIDEO_E2E_DEPS sm::video)
if(SM_WITH_TIMING)
  list(APPEND SM_VIDEO_E2E_DEPS sm::timing)
  sm_add_test_exe(sm_e2e_video LABEL e2e DEPS ${SM_VIDEO_E2E_DEPS}
    SOURCES
      e2e/video_e2e_test.cc
    SUITES
      E2E_CodecFallback
      E2E_ContentPolicy
      E2E_FrameScaler
      E2E_FrameScheduler
      E2E_FreezeDetector
      E2E_Orientation
      E2E_QualityScaler
  )
endif()
