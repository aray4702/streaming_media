// FineAudioBuffer (#1105): libwebrtc's modules/audio_device/fine_audio_buffer
// behaviour, as its fine_audio_buffer_unittest checks it: odd-sized device
// callbacks in, continuous 10 ms frames out, in both directions.
#include <vector>

#include "sm/device/fine_audio_buffer.h"
#include "sm_test.h"

using namespace sm::device;

namespace {

// Playout: a running counter, so gaps or repeats show. Record: keeps every frame.
class CountingTransport : public AudioTransport {
 public:
  void RecordedDataIsAvailable(const int16_t* samples, size_t samples_per_channel, int channels, int sample_rate_hz,
                               int total_delay_ms) override {
    record_frames.push_back(samples_per_channel);
    last_delay_ms = total_delay_ms;
    last_rate = sample_rate_hz;
    recorded.insert(recorded.end(), samples, samples + samples_per_channel * static_cast<size_t>(channels));
  }
  size_t NeedMorePlayData(int16_t* out, size_t samples_per_channel, int channels, int) override {
    ++play_requests;
    if (short_playout) return samples_per_channel / 2;
    for (size_t i = 0; i < samples_per_channel * static_cast<size_t>(channels); ++i) {
      out[i] = static_cast<int16_t>(next_play++ & 0x7fff);
    }
    return samples_per_channel;
  }
  std::vector<size_t> record_frames;
  std::vector<int16_t> recorded;
  int last_delay_ms = -1;
  int last_rate = 0;
  int play_requests = 0;
  int next_play = 0;
  bool short_playout = false;
};

}  // namespace

SM_TEST(DeviceFineAudioBuffer, PlayoutIsContinuousAcrossOddCallbackSizes) {
  CountingTransport t;
  FineAudioBuffer fb(48000, 1);
  fb.SetTransport(&t);
  SM_EXPECT_EQ(fb.samples_per_channel_10ms(), size_t{480});
  const size_t sizes[] = {470, 512, 256, 1024, 471, 480, 1};
  int expected = 0;
  size_t total = 0;
  for (int round = 0; round < 20; ++round) {
    for (size_t n : sizes) {
      std::vector<int16_t> out(n, -1);
      fb.GetPlayoutData(out.data(), n, 30);
      for (size_t i = 0; i < n; ++i) {
        SM_ASSERT_EQ(out[i], static_cast<int16_t>(expected & 0x7fff));
        ++expected;
      }
      total += n;
      // Never holds a full 10 ms more than it needs.
      SM_EXPECT_LT(fb.playout_buffered(), size_t{480});
    }
  }
  // Exactly the 10 ms chunks needed to cover the total.
  SM_EXPECT_EQ(static_cast<size_t>(t.play_requests), (total + 479) / 480);
}

SM_TEST(DeviceFineAudioBuffer, RecordDeliversExact10msFramesInOrder) {
  CountingTransport t;
  FineAudioBuffer fb(48000, 1);
  fb.SetTransport(&t);
  int16_t pout[512];
  fb.GetPlayoutData(pout, 512, 30);  // caches the playout delay
  const size_t sizes[] = {441, 512, 1, 1024, 479};
  std::vector<int16_t> sent;
  int16_t v = 0;
  for (int round = 0; round < 10; ++round) {
    for (size_t n : sizes) {
      std::vector<int16_t> in(n);
      for (auto& s : in) s = v++;
      sent.insert(sent.end(), in.begin(), in.end());
      fb.DeliverRecordedData(in.data(), n, 30);
    }
  }
  for (size_t f : t.record_frames) SM_EXPECT_EQ(f, size_t{480});
  SM_EXPECT_EQ(t.record_frames.size(), sent.size() / 480);
  SM_EXPECT_EQ(fb.record_buffered(), sent.size() % 480);
  SM_ASSERT_EQ(t.recorded.size(), (sent.size() / 480) * 480);
  for (size_t i = 0; i < t.recorded.size(); ++i) SM_ASSERT_EQ(t.recorded[i], sent[i]);
  SM_EXPECT_EQ(t.last_delay_ms, 60);  // playout + record estimates
  SM_EXPECT_EQ(t.last_rate, 48000);
}

SM_TEST(DeviceFineAudioBuffer, StereoAndOtherRates) {
  CountingTransport t;
  FineAudioBuffer fb(16000, 2);
  fb.SetTransport(&t);
  std::vector<int16_t> in(2 * 250, 7);
  for (int i = 0; i < 8; ++i) fb.DeliverRecordedData(in.data(), in.size(), 10);
  // 8 x 250 frames = 2000 frames = 12 full 160-frame chunks.
  SM_EXPECT_EQ(t.record_frames.size(), size_t{12});
  SM_EXPECT_EQ(t.recorded.size(), size_t{12 * 320});
  std::vector<int16_t> out(2 * 100);
  fb.GetPlayoutData(out.data(), out.size(), 10);
  SM_EXPECT_EQ(out[0], int16_t{0});
  SM_EXPECT_EQ(out[199], int16_t{199});
}

SM_TEST(DeviceFineAudioBuffer, SilenceWithoutTransportOrShortPlayout) {
  FineAudioBuffer fb(48000, 1);
  std::vector<int16_t> out(512, 99);
  fb.GetPlayoutData(out.data(), out.size(), 30);
  for (int16_t s : out) SM_ASSERT_EQ(s, int16_t{0});
  CountingTransport t;
  t.short_playout = true;
  fb.SetTransport(&t);
  out.assign(512, 99);
  fb.GetPlayoutData(out.data(), out.size(), 30);
  for (int16_t s : out) SM_ASSERT_EQ(s, int16_t{0});
  SM_EXPECT_EQ(fb.playout_buffered(), size_t{0});
  std::vector<int16_t> in(100, 1);
  FineAudioBuffer none(48000, 1);
  none.DeliverRecordedData(in.data(), in.size(), 30);  // no transport: dropped, no crash
  for (int i = 0; i < 10; ++i) none.DeliverRecordedData(in.data(), in.size(), 30);
  SM_EXPECT_EQ(none.record_buffered(), size_t{1100 % 480});
}

SM_TEST(DeviceFineAudioBuffer, ResetDropsBufferedAudio) {
  CountingTransport t;
  FineAudioBuffer fb(48000, 1);
  fb.SetTransport(&t);
  std::vector<int16_t> out(100);
  fb.GetPlayoutData(out.data(), out.size(), 30);
  SM_EXPECT_EQ(fb.playout_buffered(), size_t{380});
  fb.ResetPlayout();
  SM_EXPECT_EQ(fb.playout_buffered(), size_t{0});
  std::vector<int16_t> in(100);
  fb.DeliverRecordedData(in.data(), in.size(), 30);
  fb.ResetRecord();
  SM_EXPECT_EQ(fb.record_buffered(), size_t{0});
}
