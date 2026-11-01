// libwebrtc's `AudioFrameOperations::Mute` (audio/utility/audio_frame_operations.cc:146-198,
// #879): the send path's mute with a fade. In core because the RTP send path (`RtpSender`, as
// `ChannelSend::ProcessAndEncodeAudio`, channel_send.cc:909-911) applies it to every capture frame.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sm::dsp {

// Mutes one frame of interleaved int16 samples (`num_channels` channels) in place, as
// AudioFrameOperations::Mute(frame, previous_frame_muted, current_frame_muted):
// - not muted before or now: the frame is left alone;
// - muted before and now: every sample becomes 0;
// - mute starts on this frame: its last 128 samples per channel (all of them in a shorter
//   frame) ramp down to 0, and the samples before them are kept;
// - mute ends on this frame: its first 128 samples per channel ramp up from 0.
// The ramp steps by 1/128 (1/length in a shorter frame) in float, and each sample is scaled
// and truncated toward zero as libwebrtc's `int16_t *= float`.
void MuteAudioFrame(std::span<int16_t> interleaved, size_t num_channels, bool previous_frame_muted,
                    bool current_frame_muted);

}  // namespace sm::dsp
