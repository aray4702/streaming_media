// Ooura's fft4g real DFT, as libwebrtc M157's WebRtc_rdft
// (compare/webrtc/reference/common_audio/third_party/ooura/fft_size_256/).
// libwebrtc's noise suppressor (NrFft, ns_fft.cc) runs on it.
#pragma once

#include <stddef.h>

namespace sm::audio::ooura {

// WebRtc_rdft: the in-place real DFT of a[0..n-1] (n a power of 2).
// isgn >= 0: forward, a[2k] = sum_j a[j] cos(2 pi j k / n),
// a[2k+1] = sum_j a[j] sin(2 pi j k / n), with a[1] the bin n/2.
// isgn < 0: the inverse of that, scaled by n/2.
// ip (at least 2 + sqrt(n/2) entries) and w (n/2 entries) are the work
// tables; set ip[0] = 0 before the first call so that they are built.
void Rdft(size_t n, int isgn, float* a, size_t* ip, float* w);

}  // namespace sm::audio::ooura
