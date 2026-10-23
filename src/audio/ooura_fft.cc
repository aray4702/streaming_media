// libwebrtc M157's 128-point Ooura FFT (OouraFft), which AEC3's Aec3Fft runs
// on.
//
// Ported from
// compare/webrtc/reference/common_audio/third_party/ooura/fft_size_128/
// {ooura_fft.cc,ooura_fft_tables_common.h} (Copyright Takuya OOURA,
// 1996-2001; "You may use, copy, modify and distribute this code for any
// purpose (include commercial use) and without fee. Please refer to this
// package when you modify this code."), with the WebRTC authors' changes:
// the rdft of length 128 only, with stored tables. This is the portable C
// path (cft1st_128_C, cftmdl_128_C, rftfsub_128_C, rftbsub_128_C), which
// libwebrtc builds where there is no SSE2, NEON or MIPS FPU; the SIMD paths
// do the same arithmetic in another summation order. Built with
// -ffp-contract=off, as Chromium builds it. Only the namespace, the class
// (the dispatch to SIMD is gone) and the const tables' linkage differ.
#include "sm/audio/ooura_fft.h"

namespace sm::audio::ooura {

namespace {

// ooura_fft_tables_common.h: constants shared by all paths (C, SSE2, NEON).
constexpr float rdft_w[64] = {
    1.0000000000f, 0.0000000000f, 0.7071067691f, 0.7071067691f, 0.9238795638f,
    0.3826834559f, 0.3826834559f, 0.9238795638f, 0.9807852507f, 0.1950903237f,
    0.5555702448f, 0.8314695954f, 0.8314695954f, 0.5555702448f, 0.1950903237f,
    0.9807852507f, 0.9951847196f, 0.0980171412f, 0.6343933344f, 0.7730104327f,
    0.8819212914f, 0.4713967443f, 0.2902846634f, 0.9569403529f, 0.9569403529f,
    0.2902846634f, 0.4713967443f, 0.8819212914f, 0.7730104327f, 0.6343933344f,
    0.0980171412f, 0.9951847196f, 0.7071067691f, 0.4993977249f, 0.4975923598f,
    0.4945882559f, 0.4903926253f, 0.4850156307f, 0.4784701765f, 0.4707720280f,
    0.4619397819f, 0.4519946277f, 0.4409606457f, 0.4288643003f, 0.4157347977f,
    0.4016037583f, 0.3865052164f, 0.3704755902f, 0.3535533845f, 0.3357794881f,
    0.3171966672f, 0.2978496552f, 0.2777851224f, 0.2570513785f, 0.2356983721f,
    0.2137775421f, 0.1913417280f, 0.1684449315f, 0.1451423317f, 0.1214900985f,
    0.0975451618f, 0.0733652338f, 0.0490085706f, 0.0245338380f,
};

// Constants used by the C and MIPS paths.
constexpr float rdft_wk3ri_first[16] = {
    1.000000000f, 0.000000000f, 0.382683456f,  0.923879564f,
    0.831469536f, 0.555570245f, -0.195090353f, 0.980785251f,
    0.956940353f, 0.290284693f, 0.098017156f,  0.995184720f,
    0.634393334f, 0.773010492f, -0.471396863f, 0.881921172f,
};
constexpr float rdft_wk3ri_second[16] = {
    -0.707106769f, 0.707106769f,  -0.923879564f, -0.382683456f,
    -0.980785251f, 0.195090353f,  -0.555570245f, -0.831469536f,
    -0.881921172f, 0.471396863f,  -0.773010492f, -0.634393334f,
    -0.995184720f, -0.098017156f, -0.290284693f, -0.956940353f,
};

void cft1st_128_C(float* a) {
  const int n = 128;
  int j, k1, k2;
  float wk1r, wk1i, wk2r, wk2i, wk3r, wk3i;
  float x0r, x0i, x1r, x1i, x2r, x2i, x3r, x3i;

  // The processing of the first set of elements was simplified in C to avoid
  // some operations (multiplication by zero or one, addition of two elements
  // multiplied by the same weight, ...).
  x0r = a[0] + a[2];
  x0i = a[1] + a[3];
  x1r = a[0] - a[2];
  x1i = a[1] - a[3];
  x2r = a[4] + a[6];
  x2i = a[5] + a[7];
  x3r = a[4] - a[6];
  x3i = a[5] - a[7];
  a[0] = x0r + x2r;
  a[1] = x0i + x2i;
  a[4] = x0r - x2r;
  a[5] = x0i - x2i;
  a[2] = x1r - x3i;
  a[3] = x1i + x3r;
  a[6] = x1r + x3i;
  a[7] = x1i - x3r;
  wk1r = rdft_w[2];
  x0r = a[8] + a[10];
  x0i = a[9] + a[11];
  x1r = a[8] - a[10];
  x1i = a[9] - a[11];
  x2r = a[12] + a[14];
  x2i = a[13] + a[15];
  x3r = a[12] - a[14];
  x3i = a[13] - a[15];
  a[8] = x0r + x2r;
  a[9] = x0i + x2i;
  a[12] = x2i - x0i;
  a[13] = x0r - x2r;
  x0r = x1r - x3i;
  x0i = x1i + x3r;
  a[10] = wk1r * (x0r - x0i);
  a[11] = wk1r * (x0r + x0i);
  x0r = x3i + x1r;
  x0i = x3r - x1i;
  a[14] = wk1r * (x0i - x0r);
  a[15] = wk1r * (x0i + x0r);
  k1 = 0;
  for (j = 16; j < n; j += 16) {
    k1 += 2;
    k2 = 2 * k1;
    wk2r = rdft_w[k1 + 0];
    wk2i = rdft_w[k1 + 1];
    wk1r = rdft_w[k2 + 0];
    wk1i = rdft_w[k2 + 1];
    wk3r = rdft_wk3ri_first[k1 + 0];
    wk3i = rdft_wk3ri_first[k1 + 1];
    x0r = a[j + 0] + a[j + 2];
    x0i = a[j + 1] + a[j + 3];
    x1r = a[j + 0] - a[j + 2];
    x1i = a[j + 1] - a[j + 3];
    x2r = a[j + 4] + a[j + 6];
    x2i = a[j + 5] + a[j + 7];
    x3r = a[j + 4] - a[j + 6];
    x3i = a[j + 5] - a[j + 7];
    a[j + 0] = x0r + x2r;
    a[j + 1] = x0i + x2i;
    x0r -= x2r;
    x0i -= x2i;
    a[j + 4] = wk2r * x0r - wk2i * x0i;
    a[j + 5] = wk2r * x0i + wk2i * x0r;
    x0r = x1r - x3i;
    x0i = x1i + x3r;
    a[j + 2] = wk1r * x0r - wk1i * x0i;
    a[j + 3] = wk1r * x0i + wk1i * x0r;
    x0r = x1r + x3i;
    x0i = x1i - x3r;
    a[j + 6] = wk3r * x0r - wk3i * x0i;
    a[j + 7] = wk3r * x0i + wk3i * x0r;
    wk1r = rdft_w[k2 + 2];
    wk1i = rdft_w[k2 + 3];
    wk3r = rdft_wk3ri_second[k1 + 0];
    wk3i = rdft_wk3ri_second[k1 + 1];
    x0r = a[j + 8] + a[j + 10];
    x0i = a[j + 9] + a[j + 11];
    x1r = a[j + 8] - a[j + 10];
    x1i = a[j + 9] - a[j + 11];
    x2r = a[j + 12] + a[j + 14];
    x2i = a[j + 13] + a[j + 15];
    x3r = a[j + 12] - a[j + 14];
    x3i = a[j + 13] - a[j + 15];
    a[j + 8] = x0r + x2r;
    a[j + 9] = x0i + x2i;
    x0r -= x2r;
    x0i -= x2i;
    a[j + 12] = -wk2i * x0r - wk2r * x0i;
    a[j + 13] = -wk2i * x0i + wk2r * x0r;
    x0r = x1r - x3i;
    x0i = x1i + x3r;
    a[j + 10] = wk1r * x0r - wk1i * x0i;
    a[j + 11] = wk1r * x0i + wk1i * x0r;
    x0r = x1r + x3i;
    x0i = x1i - x3r;
    a[j + 14] = wk3r * x0r - wk3i * x0i;
    a[j + 15] = wk3r * x0i + wk3i * x0r;
  }
}

void cftmdl_128_C(float* a) {
  const int l = 8;
  const int n = 128;
  const int m = 32;
  int j0, j1, j2, j3, k, k1, k2, m2;
  float wk1r, wk1i, wk2r, wk2i, wk3r, wk3i;
  float x0r, x0i, x1r, x1i, x2r, x2i, x3r, x3i;

  for (j0 = 0; j0 < l; j0 += 2) {
    j1 = j0 + 8;
    j2 = j0 + 16;
    j3 = j0 + 24;
    x0r = a[j0 + 0] + a[j1 + 0];
    x0i = a[j0 + 1] + a[j1 + 1];
    x1r = a[j0 + 0] - a[j1 + 0];
    x1i = a[j0 + 1] - a[j1 + 1];
    x2r = a[j2 + 0] + a[j3 + 0];
    x2i = a[j2 + 1] + a[j3 + 1];
    x3r = a[j2 + 0] - a[j3 + 0];
    x3i = a[j2 + 1] - a[j3 + 1];
    a[j0 + 0] = x0r + x2r;
    a[j0 + 1] = x0i + x2i;
    a[j2 + 0] = x0r - x2r;
    a[j2 + 1] = x0i - x2i;
    a[j1 + 0] = x1r - x3i;
    a[j1 + 1] = x1i + x3r;
    a[j3 + 0] = x1r + x3i;
    a[j3 + 1] = x1i - x3r;
  }
  wk1r = rdft_w[2];
  for (j0 = m; j0 < l + m; j0 += 2) {
    j1 = j0 + 8;
    j2 = j0 + 16;
    j3 = j0 + 24;
    x0r = a[j0 + 0] + a[j1 + 0];
    x0i = a[j0 + 1] + a[j1 + 1];
    x1r = a[j0 + 0] - a[j1 + 0];
    x1i = a[j0 + 1] - a[j1 + 1];
    x2r = a[j2 + 0] + a[j3 + 0];
    x2i = a[j2 + 1] + a[j3 + 1];
    x3r = a[j2 + 0] - a[j3 + 0];
    x3i = a[j2 + 1] - a[j3 + 1];
    a[j0 + 0] = x0r + x2r;
    a[j0 + 1] = x0i + x2i;
    a[j2 + 0] = x2i - x0i;
    a[j2 + 1] = x0r - x2r;
    x0r = x1r - x3i;
    x0i = x1i + x3r;
    a[j1 + 0] = wk1r * (x0r - x0i);
    a[j1 + 1] = wk1r * (x0r + x0i);
    x0r = x3i + x1r;
    x0i = x3r - x1i;
    a[j3 + 0] = wk1r * (x0i - x0r);
    a[j3 + 1] = wk1r * (x0i + x0r);
  }
  k1 = 0;
  m2 = 2 * m;
  for (k = m2; k < n; k += m2) {
    k1 += 2;
    k2 = 2 * k1;
    wk2r = rdft_w[k1 + 0];
    wk2i = rdft_w[k1 + 1];
    wk1r = rdft_w[k2 + 0];
    wk1i = rdft_w[k2 + 1];
    wk3r = rdft_wk3ri_first[k1 + 0];
    wk3i = rdft_wk3ri_first[k1 + 1];
    for (j0 = k; j0 < l + k; j0 += 2) {
      j1 = j0 + 8;
      j2 = j0 + 16;
      j3 = j0 + 24;
      x0r = a[j0 + 0] + a[j1 + 0];
      x0i = a[j0 + 1] + a[j1 + 1];
      x1r = a[j0 + 0] - a[j1 + 0];
      x1i = a[j0 + 1] - a[j1 + 1];
      x2r = a[j2 + 0] + a[j3 + 0];
      x2i = a[j2 + 1] + a[j3 + 1];
      x3r = a[j2 + 0] - a[j3 + 0];
      x3i = a[j2 + 1] - a[j3 + 1];
      a[j0 + 0] = x0r + x2r;
      a[j0 + 1] = x0i + x2i;
      x0r -= x2r;
      x0i -= x2i;
      a[j2 + 0] = wk2r * x0r - wk2i * x0i;
      a[j2 + 1] = wk2r * x0i + wk2i * x0r;
      x0r = x1r - x3i;
      x0i = x1i + x3r;
      a[j1 + 0] = wk1r * x0r - wk1i * x0i;
      a[j1 + 1] = wk1r * x0i + wk1i * x0r;
      x0r = x1r + x3i;
      x0i = x1i - x3r;
      a[j3 + 0] = wk3r * x0r - wk3i * x0i;
      a[j3 + 1] = wk3r * x0i + wk3i * x0r;
    }
    wk1r = rdft_w[k2 + 2];
    wk1i = rdft_w[k2 + 3];
    wk3r = rdft_wk3ri_second[k1 + 0];
    wk3i = rdft_wk3ri_second[k1 + 1];
    for (j0 = k + m; j0 < l + (k + m); j0 += 2) {
      j1 = j0 + 8;
      j2 = j0 + 16;
      j3 = j0 + 24;
      x0r = a[j0 + 0] + a[j1 + 0];
      x0i = a[j0 + 1] + a[j1 + 1];
      x1r = a[j0 + 0] - a[j1 + 0];
      x1i = a[j0 + 1] - a[j1 + 1];
      x2r = a[j2 + 0] + a[j3 + 0];
      x2i = a[j2 + 1] + a[j3 + 1];
      x3r = a[j2 + 0] - a[j3 + 0];
      x3i = a[j2 + 1] - a[j3 + 1];
      a[j0 + 0] = x0r + x2r;
      a[j0 + 1] = x0i + x2i;
      x0r -= x2r;
      x0i -= x2i;
      a[j2 + 0] = -wk2i * x0r - wk2r * x0i;
      a[j2 + 1] = -wk2i * x0i + wk2r * x0r;
      x0r = x1r - x3i;
      x0i = x1i + x3r;
      a[j1 + 0] = wk1r * x0r - wk1i * x0i;
      a[j1 + 1] = wk1r * x0i + wk1i * x0r;
      x0r = x1r + x3i;
      x0i = x1i - x3r;
      a[j3 + 0] = wk3r * x0r - wk3i * x0i;
      a[j3 + 1] = wk3r * x0i + wk3i * x0r;
    }
  }
}

void rftfsub_128_C(float* a) {
  const float* c = rdft_w + 32;
  int j1, j2, k1, k2;
  float wkr, wki, xr, xi, yr, yi;

  for (j1 = 1, j2 = 2; j2 < 64; j1 += 1, j2 += 2) {
    k2 = 128 - j2;
    k1 = 32 - j1;
    wkr = 0.5f - c[k1];
    wki = c[j1];
    xr = a[j2 + 0] - a[k2 + 0];
    xi = a[j2 + 1] + a[k2 + 1];
    yr = wkr * xr - wki * xi;
    yi = wkr * xi + wki * xr;
    a[j2 + 0] -= yr;
    a[j2 + 1] -= yi;
    a[k2 + 0] += yr;
    a[k2 + 1] -= yi;
  }
}

void rftbsub_128_C(float* a) {
  const float* c = rdft_w + 32;
  int j1, j2, k1, k2;
  float wkr, wki, xr, xi, yr, yi;

  a[1] = -a[1];
  for (j1 = 1, j2 = 2; j2 < 64; j1 += 1, j2 += 2) {
    k2 = 128 - j2;
    k1 = 32 - j1;
    wkr = 0.5f - c[k1];
    wki = c[j1];
    xr = a[j2 + 0] - a[k2 + 0];
    xi = a[j2 + 1] + a[k2 + 1];
    yr = wkr * xr + wki * xi;
    yi = wkr * xi - wki * xr;
    a[j2 + 0] = a[j2 + 0] - yr;
    a[j2 + 1] = yi - a[j2 + 1];
    a[k2 + 0] = yr + a[k2 + 0];
    a[k2 + 1] = yi - a[k2 + 1];
  }
  a[65] = -a[65];
}

void cftbsub_128(float* a) {
  int j, j1, j2, j3, l;
  float x0r, x0i, x1r, x1i, x2r, x2i, x3r, x3i;

  cft1st_128_C(a);
  cftmdl_128_C(a);
  l = 32;

  for (j = 0; j < l; j += 2) {
    j1 = j + l;
    j2 = j1 + l;
    j3 = j2 + l;
    x0r = a[j] + a[j1];
    x0i = -a[j + 1] - a[j1 + 1];
    x1r = a[j] - a[j1];
    x1i = -a[j + 1] + a[j1 + 1];
    x2r = a[j2] + a[j3];
    x2i = a[j2 + 1] + a[j3 + 1];
    x3r = a[j2] - a[j3];
    x3i = a[j2 + 1] - a[j3 + 1];
    a[j] = x0r + x2r;
    a[j + 1] = x0i - x2i;
    a[j2] = x0r - x2r;
    a[j2 + 1] = x0i + x2i;
    a[j1] = x1r - x3i;
    a[j1 + 1] = x1i - x3r;
    a[j3] = x1r + x3i;
    a[j3 + 1] = x1i + x3r;
  }
}

void cftfsub_128(float* a) {
  int j, j1, j2, j3, l;
  float x0r, x0i, x1r, x1i, x2r, x2i, x3r, x3i;

  cft1st_128_C(a);
  cftmdl_128_C(a);
  l = 32;
  for (j = 0; j < l; j += 2) {
    j1 = j + l;
    j2 = j1 + l;
    j3 = j2 + l;
    x0r = a[j] + a[j1];
    x0i = a[j + 1] + a[j1 + 1];
    x1r = a[j] - a[j1];
    x1i = a[j + 1] - a[j1 + 1];
    x2r = a[j2] + a[j3];
    x2i = a[j2 + 1] + a[j3 + 1];
    x3r = a[j2] - a[j3];
    x3i = a[j2 + 1] - a[j3 + 1];
    a[j] = x0r + x2r;
    a[j + 1] = x0i + x2i;
    a[j2] = x0r - x2r;
    a[j2 + 1] = x0i - x2i;
    a[j1] = x1r - x3i;
    a[j1 + 1] = x1i + x3r;
    a[j3] = x1r + x3i;
    a[j3 + 1] = x1i - x3r;
  }
}

void bitrv2_128(float* a) {
  /*
      Following things have been attempted but are no faster:
      (a) Storing the swap indexes in a LUT (index calculations are done
          for 'free' while waiting on memory/L1).
      (b) Consolidate the load/store of two consecutive floats by a 64 bit
          integer (execution is memory/L1 bound).
      (c) Do a mix of floats and 64 bit integer to maximize register
          utilization (execution is memory/L1 bound).
      (d) Replacing ip[i] by ((k<<31)>>25) + ((k >> 1)<<5).
      (e) Hard-coding of the offsets to completely eliminates index
          calculations.
  */

  unsigned int j, j1, k, k1;
  float xr, xi, yr, yi;

  const int ip[4] = {0, 64, 32, 96};
  for (k = 0; k < 4; k++) {
    for (j = 0; j < k; j++) {
      j1 = 2 * j + ip[k];
      k1 = 2 * k + ip[j];
      xr = a[j1 + 0];
      xi = a[j1 + 1];
      yr = a[k1 + 0];
      yi = a[k1 + 1];
      a[j1 + 0] = yr;
      a[j1 + 1] = yi;
      a[k1 + 0] = xr;
      a[k1 + 1] = xi;
      j1 += 8;
      k1 += 16;
      xr = a[j1 + 0];
      xi = a[j1 + 1];
      yr = a[k1 + 0];
      yi = a[k1 + 1];
      a[j1 + 0] = yr;
      a[j1 + 1] = yi;
      a[k1 + 0] = xr;
      a[k1 + 1] = xi;
      j1 += 8;
      k1 -= 8;
      xr = a[j1 + 0];
      xi = a[j1 + 1];
      yr = a[k1 + 0];
      yi = a[k1 + 1];
      a[j1 + 0] = yr;
      a[j1 + 1] = yi;
      a[k1 + 0] = xr;
      a[k1 + 1] = xi;
      j1 += 8;
      k1 += 16;
      xr = a[j1 + 0];
      xi = a[j1 + 1];
      yr = a[k1 + 0];
      yi = a[k1 + 1];
      a[j1 + 0] = yr;
      a[j1 + 1] = yi;
      a[k1 + 0] = xr;
      a[k1 + 1] = xi;
    }
    j1 = 2 * k + 8 + ip[k];
    k1 = j1 + 8;
    xr = a[j1 + 0];
    xi = a[j1 + 1];
    yr = a[k1 + 0];
    yi = a[k1 + 1];
    a[j1 + 0] = yr;
    a[j1 + 1] = yi;
    a[k1 + 0] = xr;
    a[k1 + 1] = xi;
  }
}

}  // namespace

void OouraFft128::Fft(float* a) const {
  float xi;
  bitrv2_128(a);
  cftfsub_128(a);
  rftfsub_128_C(a);
  xi = a[0] - a[1];
  a[0] += a[1];
  a[1] = xi;
}
void OouraFft128::InverseFft(float* a) const {
  a[1] = 0.5f * (a[0] - a[1]);
  a[0] -= a[1];
  rftbsub_128_C(a);
  bitrv2_128(a);
  cftbsub_128(a);
}

}  // namespace sm::audio::ooura
