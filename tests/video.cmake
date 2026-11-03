sm_add_test_exe(sm_unit_video LABEL unit DEPS sm::video
  SOURCES
    unit/video/bitrate_adjuster_test.cc
    unit/video/frame_cadence_adapter_test.cc
    unit/video/resource_adaptation_processor_test.cc
    unit/video/video_test.cc
  SUITES
    BitrateAdjuster
    BitrateConstraint
    CodecFallback
    ContentPolicy
    DecoderFallback
    FrameCadenceAdapter
    FrameScheduler
    FreezeDetector
    Orientation
    QualityConvergence
    QualityScaler
    RenderFrames
    ResourceAdaptationProcessor
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
      E2E_FrameScheduler
      E2E_FreezeDetector
      E2E_Orientation
      E2E_QualityScaler
  )
endif()

# VideoToolbox H.264 (#1104): encode/decode, rate tracking, keyframes and the H.264 RTP path, on macOS.
if(SM_WITH_VIDEOTOOLBOX AND SM_WITH_RTP)
  sm_add_test_exe(sm_unit_videotoolbox LABEL unit DEPS sm::videotoolbox sm::rtp
    SOURCES
      unit/video/videotoolbox_h264_test.cc
    SUITES
      VideoToolboxH264
  )
endif()
