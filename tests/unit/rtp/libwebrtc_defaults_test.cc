// Issue #580: the library defaults the test bench used to override are
// libwebrtc M157's, so a library user gets libwebrtc's behaviour without
// setting them (docs/design/config-inventory.md, row 14).
#include "sm/cc/send_side_bwe.h"
#include "sm/loss/keyframe_request.h"
#include "sm/rtp/rtp_receiver.h"
#include "sm/rtp/rtp_sender.h"
#include "sm/rtp/send_side_controller.h"
#include "sm/rtp/session.h"
#include "sm/rtp/video_send_controller.h"
#include "sm/timing/packet_buffer.h"
#include "sm_test.h"

using namespace sm;
using namespace sm::rtp;

// The audio channel always runs RtcpMode::kCompound (audio/channel_send.cc:551).
SM_TEST(LibwebrtcDefaults, TheAudioSendStreamSendsItsOwnRtcp) {
  SM_EXPECT_TRUE(SessionConfig{}.audio_rtcp);
}

// LNTF only with `lntf.enabled`, off by default (video/rtp_video_stream_receiver2.cc:373).
SM_TEST(LibwebrtcDefaults, NoLossNotifications) {
  SM_EXPECT_TRUE(!loss::KeyframeRequestConfig{}.send_lntf);
  SM_EXPECT_TRUE(!VideoStreamConfig{}.keyframe.send_lntf);
}

// kMinKeyframeSendIntervalMs (video/encoder_rtcp_feedback.cc:37).
SM_TEST(LibwebrtcDefaults, KeyframeRequestsAreFilteredFor300Ms) {
  SM_EXPECT_EQ(VideoSendControllerConfig{}.min_keyframe_request_interval.ms(), 300);
}

// H264SpsPpsTracker and the packet buffer's missing_packets_ run on every H.264 packet.
SM_TEST(LibwebrtcDefaults, TheH264ReceiveRulesAreOn) {
  SM_EXPECT_TRUE(RtpReceiverConfig{}.sps_pps_tracker);
  SM_EXPECT_TRUE(timing::PacketBufferConfig{}.missing_packets);
  SM_EXPECT_TRUE(VideoStreamConfig{}.sps_pps_tracker);
  SM_EXPECT_TRUE(VideoStreamConfig{}.packet_buffer.missing_packets);
}

// No max_bitrate_bps: ConvertConstraints gives DataRate::Infinity()
// (call/rtp_transport_controller_send.cc:68-83).
SM_TEST(LibwebrtcDefaults, TheCongestionControllerHasNoMaximum) {
  SM_EXPECT_TRUE(cc::GccConfig{}.max_rate.IsPlusInfinity());
  SM_EXPECT_TRUE(SessionConfig{}.send_side.gcc.max_rate.IsPlusInfinity());
}

// ModuleRtpRtcpImpl2: IP_PACKET_SIZE less the TCP/IPv4 header (rtp_rtcp_impl2.cc:146-150).
SM_TEST(LibwebrtcDefaults, PacketsAreUpTo1460Bytes) {
  SM_EXPECT_EQ(SendSideConfig{}.max_packet_size, 1460);
  SM_EXPECT_EQ(VideoStreamConfig{}.max_payload_size + 12, 1460u);
  SM_EXPECT_EQ(RtpSenderConfig{}.max_payload_size + 12, 1460u);
}

// Issue #576: RTCP is split at 1460 bytes (rtp_rtcp_impl2.cc:146-150, 463); a
// video sender at min(max_packet_size, kPathMTU - overhead) (rtp_video_sender.cc:272, 860-865).
SM_TEST(LibwebrtcDefaults, RtcpIsSplitAtTheRtpPacketSize) {
  SM_EXPECT_EQ(RtcpConfig{}.max_datagram_bytes, 1460u);
  Session receive_only{SessionConfig{}};
  SM_EXPECT_EQ(receive_only.rtcp().max_datagram_bytes(), 1460u);
  Session video{SessionConfig{}};
  video.AddVideoSend(VideoStreamConfig{});
  SM_EXPECT_EQ(video.rtcp().max_datagram_bytes(), 1460u);
  SessionConfig small;
  small.send_side.max_packet_size = 1200;
  Session v1200{small};
  v1200.AddVideoSend(VideoStreamConfig{});
  SM_EXPECT_EQ(v1200.rtcp().max_datagram_bytes(), 1200u);
  SessionConfig overhead;
  overhead.send_side.transport_overhead_bytes = 100;
  Session v_overhead{overhead};
  v_overhead.AddVideoSend(VideoStreamConfig{});
  SM_EXPECT_EQ(v_overhead.rtcp().max_datagram_bytes(), 1400u);
}

// The codec maximum is the stream's max_bitrate_bps, which the app sets; no cap of the library's own.
SM_TEST(LibwebrtcDefaults, TheEncoderAllocationHasNoMaximumOfItsOwn) {
  SM_EXPECT_TRUE(VideoSendControllerConfig{}.allocation_max_bitrate.IsPlusInfinity());
}

// Issue #637: VideoStreamEncoder re-runs the bitrate adjuster only in UpdateBitrateAllocation (an
// allocation update, a reconfiguration, every kParameterUpdateIntervalMs), not per captured frame
// (video/video_stream_encoder.cc).
SM_TEST(LibwebrtcDefaults, TheBitrateAdjusterRunsOnAllocationUpdates) {
  SM_EXPECT_TRUE(VideoSendControllerConfig{}.adjuster_on_update);
}

// enable_prerenderer_smoothing defaults to true (call/video_receive_stream.h:291,
// media/base/media_config.h:62 at M157), so decoded frames pass IncomingVideoStream /
// VideoRenderFrames (video/video_receive_stream2.cc:400-403). Issue #437.
SM_TEST(LibwebrtcDefaults, DecodedFramesPassThePreRenderQueue) {
  SM_EXPECT_TRUE(RtpReceiverConfig{}.render_smoothing);
  SM_EXPECT_TRUE(VideoStreamConfig{}.render_smoothing);
}

// A keyframe's flush clears the RTX SSRC too, where padding is queued
// (RemovePacketsForSsrc(rtx_ssrc), modules/pacing/pacing_controller.cc:227-231). Issue #437.
SM_TEST(LibwebrtcDefaults, AKeyframeFlushAlsoDropsQueuedPadding) {
  SM_EXPECT_TRUE(cc::PacingControllerConfig{}.keyframe_flush_padding);
  SM_EXPECT_TRUE(SessionConfig{}.send_side.pacer.controller.keyframe_flush_padding);
}
