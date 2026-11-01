// #879: libwebrtc's AudioFrameOperations::Mute, against its own unit tests
// (audio/utility/audio_frame_operations_unittest.cc:314-449, the same frames and expected samples).
#include <vector>

#include "sm/core/audio_frame_operations.h"
#include "sm_test.h"

using namespace sm;

namespace {

// InitFrame (audio_frame_operations_unittest.cc:82-96): `left` on channel 0, `right` on channel 1.
std::vector<int16_t> Frame(size_t channels, size_t samples_per_channel, int16_t left, int16_t right) {
  std::vector<int16_t> f(channels * samples_per_channel);
  for (size_t i = 0; i < f.size(); i += channels) {
    f[i] = left;
    if (channels == 2) f[i + 1] = right;
  }
  return f;
}

int16_t At(const std::vector<int16_t>& f, size_t channels, size_t channel, size_t index) {
  return f[index * channels + channel];
}

// VerifyFrameDataBounds (audio_frame_operations_unittest.cc:104-113).
bool InBounds(const std::vector<int16_t>& f, size_t channels, size_t channel, int16_t max, int16_t min) {
  for (size_t i = channel; i < f.size(); i += channels) {
    if (f[i] < min || f[i] > max) return false;
  }
  return true;
}

}  // namespace

SM_TEST(AudioFrameOperations, MuteDisabledLeavesTheFrame) {
  auto f = Frame(2, 320, 1000, -1000);
  dsp::MuteAudioFrame(f, 2, false, false);
  SM_EXPECT_TRUE(f == Frame(2, 320, 1000, -1000));
}

SM_TEST(AudioFrameOperations, MuteEnabledZeroesTheFrame) {
  auto f = Frame(2, 320, 1000, -1000);
  dsp::MuteAudioFrame(f, 2, true, true);
  SM_EXPECT_TRUE(f == std::vector<int16_t>(640, 0));
}

// Beginning to mute ramps the end of the frame down to zero (MuteBegin*).
SM_TEST(AudioFrameOperations, MuteBeginMonoLong) {
  auto f = Frame(1, 228, 1000, -1000);
  dsp::MuteAudioFrame(f, 1, false, true);
  SM_EXPECT_TRUE(InBounds(f, 1, 0, 1000, 0));
  SM_EXPECT_EQ(At(f, 1, 0, 99), 1000);
  SM_EXPECT_EQ(At(f, 1, 0, 100), 992);
  SM_EXPECT_EQ(At(f, 1, 0, 226), 7);
  SM_EXPECT_EQ(At(f, 1, 0, 227), 0);
}

SM_TEST(AudioFrameOperations, MuteBeginMonoShort) {
  auto f = Frame(1, 93, 1000, -1000);
  dsp::MuteAudioFrame(f, 1, false, true);
  SM_EXPECT_TRUE(InBounds(f, 1, 0, 1000, 0));
  SM_EXPECT_EQ(At(f, 1, 0, 0), 989);
  SM_EXPECT_EQ(At(f, 1, 0, 1), 978);
  SM_EXPECT_EQ(At(f, 1, 0, 91), 10);
  SM_EXPECT_EQ(At(f, 1, 0, 92), 0);
}

SM_TEST(AudioFrameOperations, MuteBeginStereoLong) {
  auto f = Frame(2, 228, 1000, -1000);
  dsp::MuteAudioFrame(f, 2, false, true);
  SM_EXPECT_TRUE(InBounds(f, 2, 0, 1000, 0));
  SM_EXPECT_TRUE(InBounds(f, 2, 1, 0, -1000));
  SM_EXPECT_EQ(At(f, 2, 0, 99), 1000);
  SM_EXPECT_EQ(At(f, 2, 1, 99), -1000);
  SM_EXPECT_EQ(At(f, 2, 0, 100), 992);
  SM_EXPECT_EQ(At(f, 2, 1, 100), -992);
  SM_EXPECT_EQ(At(f, 2, 0, 226), 7);
  SM_EXPECT_EQ(At(f, 2, 1, 226), -7);
  SM_EXPECT_EQ(At(f, 2, 0, 227), 0);
  SM_EXPECT_EQ(At(f, 2, 1, 227), 0);
}

SM_TEST(AudioFrameOperations, MuteBeginStereoShort) {
  auto f = Frame(2, 93, 1000, -1000);
  dsp::MuteAudioFrame(f, 2, false, true);
  SM_EXPECT_TRUE(InBounds(f, 2, 0, 1000, 0));
  SM_EXPECT_TRUE(InBounds(f, 2, 1, 0, -1000));
  SM_EXPECT_EQ(At(f, 2, 0, 0), 989);
  SM_EXPECT_EQ(At(f, 2, 1, 0), -989);
  SM_EXPECT_EQ(At(f, 2, 0, 1), 978);
  SM_EXPECT_EQ(At(f, 2, 1, 1), -978);
  SM_EXPECT_EQ(At(f, 2, 0, 91), 10);
  SM_EXPECT_EQ(At(f, 2, 1, 91), -10);
  SM_EXPECT_EQ(At(f, 2, 0, 92), 0);
  SM_EXPECT_EQ(At(f, 2, 1, 92), 0);
}

// Ending a mute ramps the start of the frame up from zero (MuteEnd*).
SM_TEST(AudioFrameOperations, MuteEndMonoLong) {
  auto f = Frame(1, 228, 1000, -1000);
  dsp::MuteAudioFrame(f, 1, true, false);
  SM_EXPECT_TRUE(InBounds(f, 1, 0, 1000, 0));
  SM_EXPECT_EQ(At(f, 1, 0, 0), 7);
  SM_EXPECT_EQ(At(f, 1, 0, 1), 15);
  SM_EXPECT_EQ(At(f, 1, 0, 127), 1000);
  SM_EXPECT_EQ(At(f, 1, 0, 128), 1000);
}

SM_TEST(AudioFrameOperations, MuteEndMonoShort) {
  auto f = Frame(1, 93, 1000, -1000);
  dsp::MuteAudioFrame(f, 1, true, false);
  SM_EXPECT_TRUE(InBounds(f, 1, 0, 1000, 0));
  SM_EXPECT_EQ(At(f, 1, 0, 0), 10);
  SM_EXPECT_EQ(At(f, 1, 0, 1), 21);
  SM_EXPECT_EQ(At(f, 1, 0, 91), 989);
  SM_EXPECT_EQ(At(f, 1, 0, 92), 999);
}

SM_TEST(AudioFrameOperations, MuteEndStereoLong) {
  auto f = Frame(2, 228, 1000, -1000);
  dsp::MuteAudioFrame(f, 2, true, false);
  SM_EXPECT_TRUE(InBounds(f, 2, 0, 1000, 0));
  SM_EXPECT_TRUE(InBounds(f, 2, 1, 0, -1000));
  SM_EXPECT_EQ(At(f, 2, 0, 0), 7);
  SM_EXPECT_EQ(At(f, 2, 1, 0), -7);
  SM_EXPECT_EQ(At(f, 2, 0, 1), 15);
  SM_EXPECT_EQ(At(f, 2, 1, 1), -15);
  SM_EXPECT_EQ(At(f, 2, 0, 127), 1000);
  SM_EXPECT_EQ(At(f, 2, 1, 127), -1000);
  SM_EXPECT_EQ(At(f, 2, 0, 128), 1000);
  SM_EXPECT_EQ(At(f, 2, 1, 128), -1000);
}

SM_TEST(AudioFrameOperations, MuteEndStereoShort) {
  auto f = Frame(2, 93, 1000, -1000);
  dsp::MuteAudioFrame(f, 2, true, false);
  SM_EXPECT_TRUE(InBounds(f, 2, 0, 1000, 0));
  SM_EXPECT_TRUE(InBounds(f, 2, 1, 0, -1000));
  SM_EXPECT_EQ(At(f, 2, 0, 0), 10);
  SM_EXPECT_EQ(At(f, 2, 1, 0), -10);
  SM_EXPECT_EQ(At(f, 2, 0, 1), 21);
  SM_EXPECT_EQ(At(f, 2, 1, 1), -21);
  SM_EXPECT_EQ(At(f, 2, 0, 91), 989);
  SM_EXPECT_EQ(At(f, 2, 1, 91), -989);
  SM_EXPECT_EQ(At(f, 2, 0, 92), 999);
  SM_EXPECT_EQ(At(f, 2, 1, 92), -999);
}

// MuteBeginAlreadyMuted / MuteEndAlreadyMuted: a fade leaves a silent frame silent.
SM_TEST(AudioFrameOperations, FadeOfASilentFrameStaysSilent) {
  auto f = Frame(2, 320, 0, 0);
  dsp::MuteAudioFrame(f, 2, false, true);
  SM_EXPECT_TRUE(f == std::vector<int16_t>(640, 0));
  dsp::MuteAudioFrame(f, 2, true, false);
  SM_EXPECT_TRUE(f == std::vector<int16_t>(640, 0));
}
