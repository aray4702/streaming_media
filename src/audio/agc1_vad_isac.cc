// Ported from libwebrtc M157 (BSD license, see compare/webrtc/reference/):
// modules/audio_coding/codecs/isac/main/source/{filter_functions,isac_vad,
// pitch_estimator,pitch_filter}.c with settings.h and structs.h (the parts
// `VadAudioProc` reaches). Same types (float vs double) and operation order
// as the C; the C's `WebRtcIsac_lrint` is `lrint` on POSIX.
#include <cmath>
#include <cstring>

#include "sm/audio/agc1_vad.h"

namespace sm::audio::agc1::detail {

namespace isac {

namespace {

// settings.h.
constexpr int PITCH_FRAME_LEN = kPitchFrameLen;
constexpr int PITCH_MAX_LAG = kPitchMaxLag;
constexpr int PITCH_MIN_LAG = 20;
constexpr double PITCH_MAX_GAIN = 0.45;
constexpr double PITCH_MAX_GAIN_06 = 0.27;
constexpr int PITCH_LAG_SPAN2 = PITCH_MAX_LAG / 2 - PITCH_MIN_LAG / 2 + 5;
constexpr int PITCH_CORR_LEN2 = kPitchCorrLen2;
constexpr int PITCH_CORR_STEP2 = kPitchCorrStep2;
constexpr int PITCH_BW = 11;
constexpr int PITCH_SUBFRAMES = 4;
constexpr int PITCH_GRAN_PER_SUBFRAME = 5;
constexpr int PITCH_SUBFRAME_LEN = PITCH_FRAME_LEN / PITCH_SUBFRAMES;
constexpr int PITCH_UPDATE = PITCH_SUBFRAME_LEN / PITCH_GRAN_PER_SUBFRAME;
constexpr int PITCH_MAX_NUM_PEAKS = 10;
constexpr double PITCH_PEAK_DECAY = 0.85;
constexpr int PITCH_WLPCORDER = kPitchWlpcOrder;
constexpr int PITCH_WLPCWINLEN = kPitchWlpcWinLen;
constexpr double PITCH_WLPCASYM = 0.3;
constexpr int PITCH_WLPCBUFLEN = kPitchWlpcBufLen;
constexpr int PITCH_BUFFSIZE = kPitchBuffSize;
constexpr int PITCH_INTBUFFSIZE = PITCH_FRAME_LEN + PITCH_BUFFSIZE;
constexpr double PITCH_UPSTEP = 1.5;
constexpr double PITCH_DOWNSTEP = 0.67;
constexpr int PITCH_FRACS = 8;
constexpr int PITCH_FRACORDER = 9;
constexpr int PITCH_DAMPORDER = kPitchDampOrder;
constexpr float PITCH_FILTDELAY = 1.5f;
constexpr int QLOOKAHEAD = kQLookahead;
constexpr int ALLPASSSECTIONS = kAllpassSections;
constexpr int FRAMESAMPLES = kFrameSamples;
constexpr int FRAMESAMPLES_HALF = kFrameSamplesHalf;
constexpr int MAX_AR_MODEL_ORDER = 12;
constexpr int QORDER = kQOrder;

// ---- filter_functions.c ----------------------------------------------------

// The state of the filter is in InOut[-1] to InOut[-orderCoef].
void AllPoleFilter(double* InOut, const double* Coef, size_t lengthInOut, int orderCoef) {
  if ((Coef[0] > 0.9999) && (Coef[0] < 1.0001)) {
    for (size_t n = 0; n < lengthInOut; n++) {
      double sum = Coef[1] * InOut[-1];
      for (int k = 2; k <= orderCoef; k++) {
        sum += Coef[k] * InOut[-k];
      }
      *InOut++ -= sum;
    }
  } else {
    const double scal = 1.0 / Coef[0];
    for (size_t n = 0; n < lengthInOut; n++) {
      *InOut *= scal;
      for (int k = 1; k <= orderCoef; k++) {
        *InOut -= scal * Coef[k] * InOut[-k];
      }
      InOut++;
    }
  }
}

// The state of the filter is in In[-1] to In[-orderCoef].
void AllZeroFilter(const double* In, const double* Coef, size_t lengthInOut, int orderCoef, double* Out) {
  for (size_t n = 0; n < lengthInOut; n++) {
    double tmp = In[0] * Coef[0];
    for (int k = 1; k <= orderCoef; k++) {
      tmp += Coef[k] * In[-k];
    }
    *Out++ = tmp;
    In++;
  }
}

void ZeroPoleFilter(const double* In, const double* ZeroCoef, const double* PoleCoef, size_t lengthInOut,
                    int orderCoef, double* Out) {
  AllZeroFilter(In, ZeroCoef, lengthInOut, orderCoef, Out);
  AllPoleFilter(Out, PoleCoef, lengthInOut, orderCoef);
}

void BwExpand(double* out, const double* in, double coef, size_t length) {
  double chirp = coef;
  out[0] = in[0];
  for (size_t i = 1; i < length; i++) {
    out[i] = chirp * in[i];
    chirp *= coef;
  }
}

void WeightingFilter(const double* in, double* weiout, double* whiout, WeightFiltState* wfdata) {
  double tmpbuffer[PITCH_FRAME_LEN + PITCH_WLPCBUFLEN];
  double corr[PITCH_WLPCORDER + 1], rc[PITCH_WLPCORDER + 1];
  double apol[PITCH_WLPCORDER + 1], apolr[PITCH_WLPCORDER + 1];
  const double rho = 0.9;
  double whoutbuf[PITCH_WLPCBUFLEN + PITCH_WLPCORDER];
  double weoutbuf[PITCH_WLPCBUFLEN + PITCH_WLPCORDER];
  double opol[PITCH_WLPCORDER + 1], ext[PITCH_WLPCWINLEN];

  // Set up buffer and states.
  std::memcpy(tmpbuffer, wfdata->buffer, sizeof(double) * PITCH_WLPCBUFLEN);
  std::memcpy(tmpbuffer + PITCH_WLPCBUFLEN, in, sizeof(double) * PITCH_FRAME_LEN);
  std::memcpy(wfdata->buffer, tmpbuffer + PITCH_FRAME_LEN, sizeof(double) * PITCH_WLPCBUFLEN);

  double* dp = weoutbuf;
  double* dp2 = whoutbuf;
  for (int k = 0; k < PITCH_WLPCORDER; k++) {
    *dp++ = wfdata->weostate[k];
    *dp2++ = wfdata->whostate[k];
    opol[k] = 0.0;
  }
  opol[0] = 1.0;
  opol[PITCH_WLPCORDER] = 0.0;
  double* weo = dp;
  double* who = dp2;

  int endpos = PITCH_WLPCBUFLEN + PITCH_SUBFRAME_LEN;
  const double* inp = tmpbuffer + PITCH_WLPCBUFLEN;

  for (int n = 0; n < PITCH_SUBFRAMES; n++) {
    // Windowing.
    const int start = endpos - PITCH_WLPCWINLEN;
    for (int k = 0; k < PITCH_WLPCWINLEN; k++) {
      ext[k] = wfdata->window[k] * tmpbuffer[start + k];
    }

    // Get LPC polynomial.
    AutoCorr(corr, ext, PITCH_WLPCWINLEN, PITCH_WLPCORDER);
    corr[0] = 1.01 * corr[0] + 1.0;  // White noise correction.
    LevDurb(apol, rc, corr, PITCH_WLPCORDER);
    BwExpand(apolr, apol, rho, PITCH_WLPCORDER + 1);

    // Filtering.
    ZeroPoleFilter(inp, apol, apolr, PITCH_SUBFRAME_LEN, PITCH_WLPCORDER, weo);
    ZeroPoleFilter(inp, apolr, opol, PITCH_SUBFRAME_LEN, PITCH_WLPCORDER, who);

    inp += PITCH_SUBFRAME_LEN;
    endpos += PITCH_SUBFRAME_LEN;
    weo += PITCH_SUBFRAME_LEN;
    who += PITCH_SUBFRAME_LEN;
  }

  // Export filter states.
  for (int k = 0; k < PITCH_WLPCORDER; k++) {
    wfdata->weostate[k] = weoutbuf[PITCH_FRAME_LEN + k];
    wfdata->whostate[k] = whoutbuf[PITCH_FRAME_LEN + k];
  }

  // Export output data.
  std::memcpy(weiout, weoutbuf + PITCH_WLPCORDER, sizeof(double) * PITCH_FRAME_LEN);
  std::memcpy(whiout, whoutbuf + PITCH_WLPCORDER, sizeof(double) * PITCH_FRAME_LEN);
}

// ---- isac_vad.c ---------------------------------------------------------------

void InitPitchFilter(PitchFiltState* pitchfiltdata) {
  for (int k = 0; k < PITCH_BUFFSIZE; k++) {
    pitchfiltdata->ubuf[k] = 0.0;
  }
  pitchfiltdata->ystate[0] = 0.0;
  for (int k = 1; k < (PITCH_DAMPORDER); k++) {
    pitchfiltdata->ystate[k] = 0.0;
  }
  pitchfiltdata->oldlagp[0] = 50.0;
  pitchfiltdata->oldgainp[0] = 0.0;
}

void InitWeightingFilter(WeightFiltState* wfdata) {
  for (int k = 0; k < PITCH_WLPCBUFLEN; k++) wfdata->buffer[k] = 0.0;
  for (int k = 0; k < PITCH_WLPCORDER; k++) {
    wfdata->istate[k] = 0.0;
    wfdata->weostate[k] = 0.0;
    wfdata->whostate[k] = 0.0;
  }
  double t = 0.5;
  const double denum = 1.0 / (static_cast<double>(PITCH_WLPCWINLEN));
  const double denum2 = denum * denum;
  for (int k = 0; k < PITCH_WLPCWINLEN; k++) {
    double dtmp = PITCH_WLPCASYM * t * denum + (1 - PITCH_WLPCASYM) * t * t * denum2;
    dtmp *= 3.14159265;
    const double dtmp2 = std::sin(dtmp);
    wfdata->window[k] = dtmp2 * dtmp2;
    t++;
  }
}

// One cascade of first order all-pass sections over `InOut`, in place.
void AllPassFilter2Float(float* InOut, const float* APSectionFactors, int lengthInOut, int NumberOfSections,
                         float* FilterState) {
  for (int j = 0; j < NumberOfSections; j++) {
    for (int n = 0; n < lengthInOut; n++) {
      const float temp = FilterState[j] + APSectionFactors[j] * InOut[n];
      FilterState[j] = -APSectionFactors[j] * temp + InOut[n];
      InOut[n] = temp;
    }
  }
}

constexpr int NUMBEROFCHANNELAPSECTIONS = 2;
constexpr int NUMBEROFCOMPOSITEAPSECTIONS = 4;
// The upper and lower channel all-pass filter factors.
constexpr float kUpperApFactorsFloat[2] = {0.03470000000000f, 0.38260000000000f};
constexpr float kLowerApFactorsFloat[2] = {0.15440000000000f, 0.74400000000000f};

// ---- pitch_estimator.c --------------------------------------------------------

constexpr double kInterpolWin[8] = {-0.00067556028640, 0.02184247643159,  -0.12203175715679, 0.60086484101160,
                                    0.60086484101160,  -0.12203175715679, 0.02184247643159,  -0.00067556028640};

// Interpolation filter.
void IntrepolFilter(const double* data_ptr, double* intrp) {
  *intrp = kInterpolWin[0] * data_ptr[-3];
  *intrp += kInterpolWin[1] * data_ptr[-2];
  *intrp += kInterpolWin[2] * data_ptr[-1];
  *intrp += kInterpolWin[3] * data_ptr[0];
  *intrp += kInterpolWin[4] * data_ptr[1];
  *intrp += kInterpolWin[5] * data_ptr[2];
  *intrp += kInterpolWin[6] * data_ptr[3];
  *intrp += kInterpolWin[7] * data_ptr[4];
}

// 2D parabolic interpolation.
void Intrpol2D(double T[3][3], double* x, double* y, double* peak_val) {
  double b[2], A[2][2];
  const double c = T[1][1];
  b[0] = 0.5 * (T[1][2] + T[2][1] - T[0][1] - T[1][0]);
  b[1] = 0.5 * (T[1][0] + T[2][1] - T[0][1] - T[1][2]);
  A[0][1] = -0.5 * (T[0][1] + T[2][1] - T[1][0] - T[1][2]);
  double t1 = 0.5 * (T[0][0] + T[2][2]) - c;
  double t2 = 0.5 * (T[2][0] + T[0][2]) - c;
  const double d = (T[0][1] + T[1][2] + T[1][0] + T[2][1]) - 4.0 * c - t1 - t2;
  A[0][0] = -t1 - 0.5 * d;
  A[1][1] = -t2 - 0.5 * d;

  // Deal with singularities or ill-conditioned cases.
  if ((A[0][0] < 1e-7) || ((A[0][0] * A[1][1] - A[0][1] * A[0][1]) < 1e-7)) {
    *peak_val = T[1][1];
    return;
  }

  // Cholesky decomposition: replace A by upper-triangular factor.
  A[0][0] = std::sqrt(A[0][0]);
  A[0][1] = A[0][1] / A[0][0];
  A[1][1] = std::sqrt(A[1][1] - A[0][1] * A[0][1]);

  // [x; y] = -0.5 * inv(A) * b.
  t1 = b[0] / A[0][0];
  t2 = (b[1] - t1 * A[0][1]) / A[1][1];
  double delta2 = t2 / A[1][1];
  double delta1 = 0.5 * (t1 - delta2 * A[0][1]) / A[0][0];
  delta2 *= 0.5;

  // Limit norm.
  t1 = delta1 * delta1 + delta2 * delta2;
  if (t1 > 1.0) {
    delta1 /= t1;
    delta2 /= t1;
  }

  *peak_val = 0.5 * (b[0] * delta1 + b[1] * delta2) + c;

  *x += delta1;
  *y += delta2;
}

void PCorr(const double* in, double* outcorr) {
  double ysum = 1e-13;
  double sum = 0.0;
  const double* x = in + PITCH_MAX_LAG / 2 + 2;
  for (int n = 0; n < PITCH_CORR_LEN2; n++) {
    ysum += in[n] * in[n];
    sum += x[n] * in[n];
  }

  outcorr += PITCH_LAG_SPAN2 - 1;  // Index of last element in array.
  *outcorr = sum / std::sqrt(ysum);

  for (int k = 1; k < PITCH_LAG_SPAN2; k++) {
    ysum -= in[k - 1] * in[k - 1];
    ysum += in[PITCH_CORR_LEN2 + k - 1] * in[PITCH_CORR_LEN2 + k - 1];
    sum = 0.0;
    const double* inptr = &in[k];
    double prod = x[0] * inptr[0];
    for (int n = 1; n < PITCH_CORR_LEN2; n++) {
      sum += prod;
      prod = x[n] * inptr[n];
    }
    sum += prod;
    outcorr--;
    *outcorr = sum / std::sqrt(ysum);
  }
}

void AllpassFilterForDec(double* InOut, const double* APSectionFactors, size_t lengthInOut, double* FilterState) {
  for (size_t j = 0; j < ALLPASSSECTIONS; j++) {
    for (size_t n = 0; n < lengthInOut; n += 2) {
      const double temp = InOut[n];  // Store input.
      InOut[n] = FilterState[j] + APSectionFactors[j] * temp;
      FilterState[j] = -APSectionFactors[j] * InOut[n] + temp;
    }
  }
}

void DecimateAllpass(const double* in, double* state_in, size_t N, double* out) {
  static constexpr double APupper[ALLPASSSECTIONS] = {0.0347, 0.3826};
  static constexpr double APlower[ALLPASSSECTIONS] = {0.1544, 0.744};
  double data_vec[PITCH_FRAME_LEN];

  // Copy input.
  std::memcpy(data_vec + 1, in, sizeof(double) * (N - 1));

  data_vec[0] = state_in[2 * ALLPASSSECTIONS];  // The z^(-1) state.
  state_in[2 * ALLPASSSECTIONS] = in[N - 1];

  AllpassFilterForDec(data_vec + 1, APupper, N, state_in);
  AllpassFilterForDec(data_vec, APlower, N, state_in + ALLPASSSECTIONS);

  for (size_t n = 0; n < N / 2; n++) out[n] = data_vec[2 * n] + data_vec[2 * n + 1];
}

void InitializePitch(const double* in, const double old_lag, const double old_gain, PitchAnalysisState* State,
                     double* lags) {
  double buf_dec[PITCH_CORR_LEN2 + PITCH_CORR_STEP2 + PITCH_MAX_LAG / 2 + 2];
  double corrvec1[PITCH_LAG_SPAN2];
  double corrvec2[PITCH_LAG_SPAN2];
  // Allocating 10 extra entries at the begining of the CorrSurf.
  double corrSurfBuff[10 + (2 * PITCH_BW + 3) * (PITCH_LAG_SPAN2 + 4)];
  double* CorrSurf[2 * PITCH_BW + 3];
  double *CorrSurfPtr1, *CorrSurfPtr2;
  const double LagWin[3] = {0.2, 0.5, 0.98};
  int ind1, ind2, peaks_ind, peak, max_ind;
  int peaks[PITCH_MAX_NUM_PEAKS];
  double adj, gain_tmp;
  double corr, corr_max;
  double intrp_a, intrp_b, intrp_c, intrp_d;
  double peak_vals[PITCH_MAX_NUM_PEAKS];
  double lags1[PITCH_MAX_NUM_PEAKS];
  double lags2[PITCH_MAX_NUM_PEAKS];
  double T[3][3];
  int row;

  for (int k = 0; k < 2 * PITCH_BW + 3; k++) {
    CorrSurf[k] = &corrSurfBuff[10 + k * (PITCH_LAG_SPAN2 + 4)];
  }
  // Reset CorrSurf matrix.
  std::memset(corrSurfBuff, 0, sizeof(double) * (10 + (2 * PITCH_BW + 3) * (PITCH_LAG_SPAN2 + 4)));

  max_ind = 0;
  peak = 0;

  // Copy old values from state buffer.
  std::memcpy(buf_dec, State->dec_buffer,
              sizeof(double) * (PITCH_CORR_LEN2 + PITCH_CORR_STEP2 + PITCH_MAX_LAG / 2 - PITCH_FRAME_LEN / 2 + 2));

  // Decimation; put result after the old values.
  DecimateAllpass(in, State->decimator_state, PITCH_FRAME_LEN,
                  &buf_dec[PITCH_CORR_LEN2 + PITCH_CORR_STEP2 + PITCH_MAX_LAG / 2 - PITCH_FRAME_LEN / 2 + 2]);

  // Low-pass filtering.
  for (int k = PITCH_CORR_LEN2 + PITCH_CORR_STEP2 + PITCH_MAX_LAG / 2 - PITCH_FRAME_LEN / 2 + 2;
       k < PITCH_CORR_LEN2 + PITCH_CORR_STEP2 + PITCH_MAX_LAG / 2 + 2; k++)
    buf_dec[k] += 0.75 * buf_dec[k - 1] - 0.25 * buf_dec[k - 2];

  // Copy end part back into state buffer.
  std::memcpy(State->dec_buffer, buf_dec + PITCH_FRAME_LEN / 2,
              sizeof(double) * (PITCH_CORR_LEN2 + PITCH_CORR_STEP2 + PITCH_MAX_LAG / 2 - PITCH_FRAME_LEN / 2 + 2));

  // Compute correlation for first and second half of the frame.
  PCorr(buf_dec, corrvec1);
  PCorr(buf_dec + PITCH_CORR_STEP2, corrvec2);

  // Bias towards pitch lag of previous frame.
  const double log_lag = std::log(0.5 * old_lag);
  double gain_bias = 4.0 * old_gain * old_gain;
  if (gain_bias > 0.8) gain_bias = 0.8;
  for (int k = 0; k < PITCH_LAG_SPAN2; k++) {
    const double ratio = std::log(static_cast<double>(k + (PITCH_MIN_LAG / 2 - 2))) - log_lag;
    const double bias = 1.0 + gain_bias * std::exp(-5.0 * ratio * ratio);
    corrvec1[k] *= bias;
  }

  // Taper correlation functions.
  for (int k = 0; k < 3; k++) {
    gain_tmp = LagWin[k];
    corrvec1[k] *= gain_tmp;
    corrvec2[k] *= gain_tmp;
    corrvec1[PITCH_LAG_SPAN2 - 1 - k] *= gain_tmp;
    corrvec2[PITCH_LAG_SPAN2 - 1 - k] *= gain_tmp;
  }

  corr_max = 0.0;
  // Fill middle row of correlation surface.
  ind1 = 0;
  ind2 = 0;
  CorrSurfPtr1 = &CorrSurf[PITCH_BW][2];
  for (int k = 0; k < PITCH_LAG_SPAN2; k++) {
    corr = corrvec1[ind1++] + corrvec2[ind2++];
    CorrSurfPtr1[k] = corr;
    if (corr > corr_max) {
      corr_max = corr;  // Update maximum.
      max_ind = static_cast<int>(&CorrSurfPtr1[k] - &CorrSurf[0][0]);
    }
  }
  // Fill first and last rows of correlation surface.
  ind1 = 0;
  ind2 = PITCH_BW;
  CorrSurfPtr1 = &CorrSurf[0][2];
  CorrSurfPtr2 = &CorrSurf[2 * PITCH_BW][PITCH_BW + 2];
  for (int k = 0; k < PITCH_LAG_SPAN2 - PITCH_BW; k++) {
    const double ratio = (static_cast<double>(ind1 + 12)) / (static_cast<double>(ind2 + 12));
    adj = 0.2 * ratio * (2.0 - ratio);  // Inverse parabola as a function of ratio.
    corr = adj * (corrvec1[ind1] + corrvec2[ind2]);
    CorrSurfPtr1[k] = corr;
    if (corr > corr_max) {
      corr_max = corr;
      max_ind = static_cast<int>(&CorrSurfPtr1[k] - &CorrSurf[0][0]);
    }
    corr = adj * (corrvec1[ind2++] + corrvec2[ind1++]);
    CorrSurfPtr2[k] = corr;
    if (corr > corr_max) {
      corr_max = corr;
      max_ind = static_cast<int>(&CorrSurfPtr2[k] - &CorrSurf[0][0]);
    }
  }
  // Fill second and next to last rows of correlation surface.
  ind1 = 0;
  ind2 = PITCH_BW - 1;
  CorrSurfPtr1 = &CorrSurf[1][2];
  CorrSurfPtr2 = &CorrSurf[2 * PITCH_BW - 1][PITCH_BW + 1];
  for (int k = 0; k < PITCH_LAG_SPAN2 - PITCH_BW + 1; k++) {
    const double ratio = (static_cast<double>(ind1 + 12)) / (static_cast<double>(ind2 + 12));
    adj = 0.9 * ratio * (2.0 - ratio);
    corr = adj * (corrvec1[ind1] + corrvec2[ind2]);
    CorrSurfPtr1[k] = corr;
    if (corr > corr_max) {
      corr_max = corr;
      max_ind = static_cast<int>(&CorrSurfPtr1[k] - &CorrSurf[0][0]);
    }
    corr = adj * (corrvec1[ind2++] + corrvec2[ind1++]);
    CorrSurfPtr2[k] = corr;
    if (corr > corr_max) {
      corr_max = corr;
      max_ind = static_cast<int>(&CorrSurfPtr2[k] - &CorrSurf[0][0]);
    }
  }
  // Fill remainder of correlation surface.
  for (int m = 2; m < PITCH_BW; m++) {
    ind1 = 0;
    ind2 = PITCH_BW - m;  // Always larger than ind1.
    CorrSurfPtr1 = &CorrSurf[m][2];
    CorrSurfPtr2 = &CorrSurf[2 * PITCH_BW - m][PITCH_BW + 2 - m];
    for (int k = 0; k < PITCH_LAG_SPAN2 - PITCH_BW + m; k++) {
      const double ratio = (static_cast<double>(ind1 + 12)) / (static_cast<double>(ind2 + 12));
      adj = ratio * (2.0 - ratio);
      corr = adj * (corrvec1[ind1] + corrvec2[ind2]);
      CorrSurfPtr1[k] = corr;
      if (corr > corr_max) {
        corr_max = corr;
        max_ind = static_cast<int>(&CorrSurfPtr1[k] - &CorrSurf[0][0]);
      }
      corr = adj * (corrvec1[ind2++] + corrvec2[ind1++]);
      CorrSurfPtr2[k] = corr;
      if (corr > corr_max) {
        corr_max = corr;
        max_ind = static_cast<int>(&CorrSurfPtr2[k] - &CorrSurf[0][0]);
      }
    }
  }

  // Threshold value to qualify as a peak.
  corr_max *= 0.6;

  peaks_ind = 0;
  // Find peaks.
  for (int m = 1; m < PITCH_BW + 1; m++) {
    if (peaks_ind == PITCH_MAX_NUM_PEAKS) break;
    CorrSurfPtr1 = &CorrSurf[m][2];
    for (int k = 2; k < PITCH_LAG_SPAN2 - PITCH_BW - 2 + m; k++) {
      corr = CorrSurfPtr1[k];
      if (corr > corr_max) {
        if ((corr > CorrSurfPtr1[k - (PITCH_LAG_SPAN2 + 5)]) && (corr > CorrSurfPtr1[k - (PITCH_LAG_SPAN2 + 4)])) {
          if ((corr > CorrSurfPtr1[k + (PITCH_LAG_SPAN2 + 4)]) && (corr > CorrSurfPtr1[k + (PITCH_LAG_SPAN2 + 5)])) {
            // Found a peak; store index into matrix.
            peaks[peaks_ind++] = static_cast<int>(&CorrSurfPtr1[k] - &CorrSurf[0][0]);
            if (peaks_ind == PITCH_MAX_NUM_PEAKS) break;
          }
        }
      }
    }
  }
  for (int m = PITCH_BW + 1; m < 2 * PITCH_BW; m++) {
    if (peaks_ind == PITCH_MAX_NUM_PEAKS) break;
    CorrSurfPtr1 = &CorrSurf[m][2];
    for (int k = 2 + m - PITCH_BW; k < PITCH_LAG_SPAN2 - 2; k++) {
      corr = CorrSurfPtr1[k];
      if (corr > corr_max) {
        if ((corr > CorrSurfPtr1[k - (PITCH_LAG_SPAN2 + 5)]) && (corr > CorrSurfPtr1[k - (PITCH_LAG_SPAN2 + 4)])) {
          if ((corr > CorrSurfPtr1[k + (PITCH_LAG_SPAN2 + 4)]) && (corr > CorrSurfPtr1[k + (PITCH_LAG_SPAN2 + 5)])) {
            peaks[peaks_ind++] = static_cast<int>(&CorrSurfPtr1[k] - &CorrSurf[0][0]);
            if (peaks_ind == PITCH_MAX_NUM_PEAKS) break;
          }
        }
      }
    }
  }

  if (peaks_ind > 0) {
    // Examine each peak.
    CorrSurfPtr1 = &CorrSurf[0][0];
    for (int k = 0; k < peaks_ind; k++) {
      peak = peaks[k];

      // Compute four interpolated values around current peak.
      IntrepolFilter(&CorrSurfPtr1[peak - (PITCH_LAG_SPAN2 + 5)], &intrp_a);
      IntrepolFilter(&CorrSurfPtr1[peak - 1], &intrp_b);
      IntrepolFilter(&CorrSurfPtr1[peak], &intrp_c);
      IntrepolFilter(&CorrSurfPtr1[peak + (PITCH_LAG_SPAN2 + 4)], &intrp_d);

      // Determine maximum of the interpolated values.
      corr = CorrSurfPtr1[peak];
      corr_max = intrp_a;
      if (intrp_b > corr_max) corr_max = intrp_b;
      if (intrp_c > corr_max) corr_max = intrp_c;
      if (intrp_d > corr_max) corr_max = intrp_d;

      // Determine where the peak sits and fill a 3x3 matrix around it.
      row = peak / (PITCH_LAG_SPAN2 + 4);
      lags1[k] = static_cast<double>((peak - row * (PITCH_LAG_SPAN2 + 4)) + PITCH_MIN_LAG / 2 - 4);
      lags2[k] = static_cast<double>(lags1[k] + PITCH_BW - row);
      if (corr > corr_max) {
        T[0][0] = CorrSurfPtr1[peak - (PITCH_LAG_SPAN2 + 5)];
        T[2][0] = CorrSurfPtr1[peak - (PITCH_LAG_SPAN2 + 4)];
        T[1][1] = corr;
        T[0][2] = CorrSurfPtr1[peak + (PITCH_LAG_SPAN2 + 4)];
        T[2][2] = CorrSurfPtr1[peak + (PITCH_LAG_SPAN2 + 5)];
        T[1][0] = intrp_a;
        T[0][1] = intrp_b;
        T[2][1] = intrp_c;
        T[1][2] = intrp_d;
      } else {
        if (intrp_a == corr_max) {
          lags1[k] -= 0.5;
          lags2[k] += 0.5;
          IntrepolFilter(&CorrSurfPtr1[peak - 2 * (PITCH_LAG_SPAN2 + 5)], &T[0][0]);
          IntrepolFilter(&CorrSurfPtr1[peak - (2 * PITCH_LAG_SPAN2 + 9)], &T[2][0]);
          T[1][1] = intrp_a;
          T[0][2] = intrp_b;
          T[2][2] = intrp_c;
          T[1][0] = CorrSurfPtr1[peak - (2 * PITCH_LAG_SPAN2 + 9)];
          T[0][1] = CorrSurfPtr1[peak - (PITCH_LAG_SPAN2 + 5)];
          T[2][1] = CorrSurfPtr1[peak - (PITCH_LAG_SPAN2 + 4)];
          T[1][2] = corr;
        } else if (intrp_b == corr_max) {
          lags1[k] -= 0.5;
          lags2[k] -= 0.5;
          IntrepolFilter(&CorrSurfPtr1[peak - (PITCH_LAG_SPAN2 + 6)], &T[0][0]);
          T[2][0] = intrp_a;
          T[1][1] = intrp_b;
          IntrepolFilter(&CorrSurfPtr1[peak + (PITCH_LAG_SPAN2 + 3)], &T[0][2]);
          T[2][2] = intrp_d;
          T[1][0] = CorrSurfPtr1[peak - (PITCH_LAG_SPAN2 + 5)];
          T[0][1] = CorrSurfPtr1[peak - 1];
          T[2][1] = corr;
          T[1][2] = CorrSurfPtr1[peak + (PITCH_LAG_SPAN2 + 4)];
        } else if (intrp_c == corr_max) {
          lags1[k] += 0.5;
          lags2[k] += 0.5;
          T[0][0] = intrp_a;
          IntrepolFilter(&CorrSurfPtr1[peak - (PITCH_LAG_SPAN2 + 4)], &T[2][0]);
          T[1][1] = intrp_c;
          T[0][2] = intrp_d;
          IntrepolFilter(&CorrSurfPtr1[peak + (PITCH_LAG_SPAN2 + 5)], &T[2][2]);
          T[1][0] = CorrSurfPtr1[peak - (PITCH_LAG_SPAN2 + 4)];
          T[0][1] = corr;
          T[2][1] = CorrSurfPtr1[peak + 1];
          T[1][2] = CorrSurfPtr1[peak + (PITCH_LAG_SPAN2 + 5)];
        } else {
          lags1[k] += 0.5;
          lags2[k] -= 0.5;
          T[0][0] = intrp_b;
          T[2][0] = intrp_c;
          T[1][1] = intrp_d;
          IntrepolFilter(&CorrSurfPtr1[peak + 2 * (PITCH_LAG_SPAN2 + 4)], &T[0][2]);
          IntrepolFilter(&CorrSurfPtr1[peak + (2 * PITCH_LAG_SPAN2 + 9)], &T[2][2]);
          T[1][0] = corr;
          T[0][1] = CorrSurfPtr1[peak + (PITCH_LAG_SPAN2 + 4)];
          T[2][1] = CorrSurfPtr1[peak + (PITCH_LAG_SPAN2 + 5)];
          T[1][2] = CorrSurfPtr1[peak + (2 * PITCH_LAG_SPAN2 + 9)];
        }
      }

      // 2D parabolic interpolation gives more accurate lags and peak value.
      Intrpol2D(T, &lags1[k], &lags2[k], &peak_vals[k]);
    }

    // Determine the highest peak, after applying a bias towards short lags.
    corr_max = 0.0;
    for (int k = 0; k < peaks_ind; k++) {
      corr = peak_vals[k] * std::pow(PITCH_PEAK_DECAY, std::log(lags1[k] + lags2[k]));
      if (corr > corr_max) {
        corr_max = corr;
        peak = k;
      }
    }

    lags1[peak] *= 2.0;
    lags2[peak] *= 2.0;

    if (lags1[peak] < static_cast<double>(PITCH_MIN_LAG)) lags1[peak] = static_cast<double>(PITCH_MIN_LAG);
    if (lags2[peak] < static_cast<double>(PITCH_MIN_LAG)) lags2[peak] = static_cast<double>(PITCH_MIN_LAG);
    if (lags1[peak] > static_cast<double>(PITCH_MAX_LAG)) lags1[peak] = static_cast<double>(PITCH_MAX_LAG);
    if (lags2[peak] > static_cast<double>(PITCH_MAX_LAG)) lags2[peak] = static_cast<double>(PITCH_MAX_LAG);

    // Store lags of highest peak in output array.
    lags[0] = lags1[peak];
    lags[1] = lags1[peak];
    lags[2] = lags2[peak];
    lags[3] = lags2[peak];
  } else {
    row = max_ind / (PITCH_LAG_SPAN2 + 4);
    lags1[0] = static_cast<double>((max_ind - row * (PITCH_LAG_SPAN2 + 4)) + PITCH_MIN_LAG / 2 - 4);
    lags2[0] = static_cast<double>(lags1[0] + PITCH_BW - row);

    if (lags1[0] < static_cast<double>(PITCH_MIN_LAG)) lags1[0] = static_cast<double>(PITCH_MIN_LAG);
    if (lags2[0] < static_cast<double>(PITCH_MIN_LAG)) lags2[0] = static_cast<double>(PITCH_MIN_LAG);
    if (lags1[0] > static_cast<double>(PITCH_MAX_LAG)) lags1[0] = static_cast<double>(PITCH_MAX_LAG);
    if (lags2[0] > static_cast<double>(PITCH_MAX_LAG)) lags2[0] = static_cast<double>(PITCH_MAX_LAG);

    lags[0] = lags1[0];
    lags[1] = lags1[0];
    lags[2] = lags2[0];
    lags[3] = lags2[0];
  }
}

// Weighting matrix from orthogonalizing a basis of polynomials of increasing
// order.
constexpr double kWeight[5][5] = {
    {0.29714285714286, -0.30857142857143, -0.05714285714286, 0.05142857142857, 0.01714285714286},
    {-0.30857142857143, 0.67428571428571, -0.27142857142857, -0.14571428571429, 0.05142857142857},
    {-0.05714285714286, -0.27142857142857, 0.65714285714286, -0.27142857142857, -0.05714285714286},
    {0.05142857142857, -0.14571428571429, -0.27142857142857, 0.67428571428571, -0.30857142857143},
    {0.01714285714286, 0.05142857142857, -0.05714285714286, -0.30857142857143, 0.29714285714286}};

// Second order high-pass filter.
void Highpass(const double* in, double* out, double* state, size_t N) {
  static constexpr double a_coef[2] = {1.86864659625574, -0.88360000000000};
  static constexpr double b_coef[2] = {-1.99524591718270, 0.99600400000000};
  for (size_t k = 0; k < N; k++) {
    *out = *in + state[1];
    state[1] = state[0] + b_coef[0] * *in + a_coef[0] * *out;
    state[0] = b_coef[1] * *in++ + a_coef[1] * *out++;
  }
}

// ---- pitch_filter.c -----------------------------------------------------------

constexpr double kDampFilter[PITCH_DAMPORDER] = {-0.07, 0.25, 0.64, 0.25, -0.07};

// Interpolation coefficients; generated by design_pitch_filter.m.
constexpr double kIntrpCoef[PITCH_FRACS][PITCH_FRACORDER] = {
    {-0.02239172458614, 0.06653315052934, -0.16515880017569, 0.60701333734125, 0.64671399919202, -0.20249000396417,
     0.09926548334755, -0.04765933793109, 0.01754159521746},
    {-0.01985640750434, 0.05816126837866, -0.13991265473714, 0.44560418147643, 0.79117042386876, -0.20266133815188,
     0.09585268418555, -0.04533310458084, 0.01654127246314},
    {-0.01463300534216, 0.04229888475060, -0.09897034715253, 0.28284326017787, 0.90385267956632, -0.16976950138649,
     0.07704272393639, -0.03584218578311, 0.01295781500709},
    {-0.00764851320885, 0.02184035544377, -0.04985561057281, 0.13083306574393, 0.97545011664662, -0.10177807997561,
     0.04400901776474, -0.02010737175166, 0.00719783432422},
    {-0.00000000000000, 0.00000000000000, -0.00000000000001, 0.00000000000001, 0.99999999999999, 0.00000000000001,
     -0.00000000000001, 0.00000000000000, -0.00000000000000},
    {0.00719783432422, -0.02010737175166, 0.04400901776474, -0.10177807997562, 0.97545011664663, 0.13083306574393,
     -0.04985561057280, 0.02184035544377, -0.00764851320885},
    {0.01295781500710, -0.03584218578312, 0.07704272393640, -0.16976950138650, 0.90385267956634, 0.28284326017785,
     -0.09897034715252, 0.04229888475059, -0.01463300534216},
    {0.01654127246315, -0.04533310458085, 0.09585268418557, -0.20266133815190, 0.79117042386878, 0.44560418147640,
     -0.13991265473712, 0.05816126837865, -0.01985640750433}};

// The modes `VadAudioProc` uses (iSAC's decoder-side post filter is not
// ported).
enum PitchFilterOperation { kPitchFilterPre, kPitchFilterPreLa, kPitchFilterPreGain };

struct PitchFilterParam {
  double buffer[PITCH_INTBUFFSIZE + QLOOKAHEAD];
  double damper_state[PITCH_DAMPORDER];
  const double* interpol_coeff;
  double gain;
  double lag;
  int lag_offset;
  int sub_frame;
  PitchFilterOperation mode;
  int num_samples;
  int index;
  double damper_state_dg[4][PITCH_DAMPORDER];
  double gain_mult[4];
};

using OutDg = double[PITCH_FRAME_LEN + QLOOKAHEAD];

// Filters one segment, a quarter of a frame.
void FilterSegment(const double* in_data, PitchFilterParam* parameters, double* out_data, OutDg* out_dg) {
  // Index of `parameters->buffer` where the output is written to.
  int pos = parameters->index + PITCH_BUFFSIZE;
  // Index of `parameters->buffer` where samples are read for fractional-lag
  // computation.
  int pos_lag = pos - parameters->lag_offset;

  for (int n = 0; n < parameters->num_samples; ++n) {
    // Shift low pass filter states.
    for (int m = PITCH_DAMPORDER - 1; m > 0; --m) {
      parameters->damper_state[m] = parameters->damper_state[m - 1];
    }
    // Filter to get fractional pitch.
    double sum = 0.0;
    for (int m = 0; m < PITCH_FRACORDER; ++m) {
      sum += parameters->buffer[pos_lag + m] * parameters->interpol_coeff[m];
    }
    // Multiply with gain.
    parameters->damper_state[0] = parameters->gain * sum;

    if (parameters->mode == kPitchFilterPreGain) {
      const int lag_index = parameters->index - parameters->lag_offset;
      const int m_tmp = (lag_index < 0) ? -lag_index : 0;
      // Update the damper state for the new sample.
      for (int m = PITCH_DAMPORDER - 1; m > 0; --m) {
        for (int j = 0; j < 4; ++j) {
          parameters->damper_state_dg[j][m] = parameters->damper_state_dg[j][m - 1];
        }
      }

      for (int j = 0; j < parameters->sub_frame + 1; ++j) {
        // Filter for fractional pitch.
        double sum2 = 0.0;
        for (int m = PITCH_FRACORDER - 1; m >= m_tmp; --m) {
          // Samples outside `out_dg[j]` are taken as zero.
          sum2 += out_dg[j][lag_index + m] * parameters->interpol_coeff[m];
        }
        // Add the contribution of differential gain change.
        parameters->damper_state_dg[j][0] = parameters->gain_mult[j] * sum + parameters->gain * sum2;
      }

      // Filter with damping filter, and store the results.
      for (int j = 0; j < parameters->sub_frame + 1; ++j) {
        sum = 0.0;
        for (int m = 0; m < PITCH_DAMPORDER; ++m) {
          sum -= parameters->damper_state_dg[j][m] * kDampFilter[m];
        }
        out_dg[j][parameters->index] = sum;
      }
    }
    // Filter with damping filter.
    sum = 0.0;
    for (int m = 0; m < PITCH_DAMPORDER; ++m) {
      sum += parameters->damper_state[m] * kDampFilter[m];
    }

    // Subtract from input and update buffer.
    out_data[parameters->index] = in_data[parameters->index] - sum;
    parameters->buffer[pos] = in_data[parameters->index] + out_data[parameters->index];

    ++parameters->index;
    ++pos;
    ++pos_lag;
  }
}

// Update filter parameters based on the pitch-gains and pitch-lags.
void Update(PitchFilterParam* parameters) {
  // Compute integer lag-offset.
  parameters->lag_offset = static_cast<int>(std::lrint(parameters->lag + PITCH_FILTDELAY + 0.5));
  // Find correct set of coefficients for computing fractional pitch.
  const double fraction = parameters->lag_offset - (parameters->lag + PITCH_FILTDELAY);
  const int fraction_index = static_cast<int>(std::lrint(PITCH_FRACS * fraction - 0.5));
  parameters->interpol_coeff = kIntrpCoef[fraction_index];

  if (parameters->mode == kPitchFilterPreGain) {
    // If in this mode make a differential change to pitch gain.
    parameters->gain_mult[parameters->sub_frame] += 0.2;
    if (parameters->gain_mult[parameters->sub_frame] > 1.0) {
      parameters->gain_mult[parameters->sub_frame] = 1.0;
    }
    if (parameters->sub_frame > 0) {
      parameters->gain_mult[parameters->sub_frame - 1] -= 0.2;
    }
  }
}

// Filters a frame of 30 ms at 8 kHz, given 4 pitch lags and gains.
void FilterFrame(const double* in_data, PitchFiltState* filter_state, const double* lags, const double* gains,
                 PitchFilterOperation mode, double* out_data, OutDg* out_dg) {
  PitchFilterParam filter_parameters;

  // Set up buffer and states.
  filter_parameters.index = 0;
  filter_parameters.lag_offset = 0;
  filter_parameters.mode = mode;
  // Copy states to local variables.
  std::memcpy(filter_parameters.buffer, filter_state->ubuf, sizeof(filter_state->ubuf));
  static_assert(sizeof(filter_parameters.buffer) >= sizeof(filter_state->ubuf));
  std::memset(filter_parameters.buffer + sizeof(filter_state->ubuf) / sizeof(filter_state->ubuf[0]), 0,
              sizeof(filter_parameters.buffer) - sizeof(filter_state->ubuf));
  std::memcpy(filter_parameters.damper_state, filter_state->ystate, sizeof(filter_state->ystate));

  if (mode == kPitchFilterPreGain) {
    // Clear buffers.
    std::memset(filter_parameters.gain_mult, 0, sizeof(filter_parameters.gain_mult));
    std::memset(filter_parameters.damper_state_dg, 0, sizeof(filter_parameters.damper_state_dg));
    for (int n = 0; n < PITCH_SUBFRAMES; ++n) {
      std::memset(out_dg[n], 0, sizeof(out_dg[n]));
    }
  }

  double old_lag = *filter_state->oldlagp;
  double old_gain = *filter_state->oldgainp;

  // No interpolation if pitch lag step is big.
  if ((lags[0] > (PITCH_UPSTEP * old_lag)) || (lags[0] < (PITCH_DOWNSTEP * old_lag))) {
    old_lag = lags[0];
    old_gain = gains[0];
    if (mode == kPitchFilterPreGain) {
      filter_parameters.gain_mult[0] = 1.0;
    }
  }

  filter_parameters.num_samples = PITCH_UPDATE;
  for (int m = 0; m < PITCH_SUBFRAMES; ++m) {
    // Set the sub-frame value.
    filter_parameters.sub_frame = m;
    // Calculate interpolation steps for pitch-lag and pitch-gain.
    const double lag_delta = (lags[m] - old_lag) / PITCH_GRAN_PER_SUBFRAME;
    filter_parameters.lag = old_lag;
    const double gain_delta = (gains[m] - old_gain) / PITCH_GRAN_PER_SUBFRAME;
    filter_parameters.gain = old_gain;
    // Store for the next sub-frame.
    old_lag = lags[m];
    old_gain = gains[m];

    for (int n = 0; n < PITCH_GRAN_PER_SUBFRAME; ++n) {
      // Step-wise interpolation of pitch gains and lags.
      filter_parameters.gain += gain_delta;
      filter_parameters.lag += lag_delta;
      // Update parameters according to new lag value.
      Update(&filter_parameters);
      // Filter a segment of input.
      FilterSegment(in_data, &filter_parameters, out_data, out_dg);
    }
  }

  if (mode != kPitchFilterPreGain) {
    // Export buffer and states.
    std::memcpy(filter_state->ubuf, &filter_parameters.buffer[PITCH_FRAME_LEN], sizeof(filter_state->ubuf));
    std::memcpy(filter_state->ystate, filter_parameters.damper_state, sizeof(filter_state->ystate));
    // Store for the next frame.
    *filter_state->oldlagp = old_lag;
    *filter_state->oldgainp = old_gain;
  }

  if ((mode == kPitchFilterPreGain) || (mode == kPitchFilterPreLa)) {
    // Filter the lookahead segment, treated as the last sub-frame.
    filter_parameters.sub_frame = PITCH_SUBFRAMES - 1;
    filter_parameters.num_samples = QLOOKAHEAD;
    FilterSegment(in_data, &filter_parameters, out_data, out_dg);
  }
}

}  // namespace

// ---- filter_functions.c / isac_vad.c (public) ------------------------------------

void AutoCorr(double* r, const double* x, size_t N, size_t order) {
  for (size_t lag = 0; lag <= order; lag++) {
    double sum = 0.0f;
    const double* x_lag = &x[lag];
    double prod = x[0] * x_lag[0];
    for (size_t n = 1; n < N - lag; n++) {
      sum += prod;
      prod = x[n] * x_lag[n];
    }
    sum += prod;
    r[lag] = sum;
  }
}

void InitPitchAnalysis(PitchAnalysisState& State) {
  for (int k = 0; k < PITCH_CORR_LEN2 + PITCH_CORR_STEP2 + PITCH_MAX_LAG / 2 - PITCH_FRAME_LEN / 2 + 2; k++)
    State.dec_buffer[k] = 0.0;
  for (int k = 0; k < 2 * ALLPASSSECTIONS + 1; k++) State.decimator_state[k] = 0.0;
  for (int k = 0; k < 2; k++) State.hp_state[k] = 0.0;
  for (int k = 0; k < QLOOKAHEAD; k++) State.whitened_buf[k] = 0.0;
  for (int k = 0; k < QLOOKAHEAD; k++) State.inbuf[k] = 0.0;
  InitPitchFilter(&State.PFstr_wght);
  InitPitchFilter(&State.PFstr);
  InitWeightingFilter(&State.Wghtstr);
}

void InitPreFilterbank(PreFiltBankState& prefiltdata) {
  for (int k = 0; k < QLOOKAHEAD; k++) {
    prefiltdata.INLABUF1_float[k] = 0;
    prefiltdata.INLABUF2_float[k] = 0;
  }
  for (int k = 0; k < 2 * (QORDER - 1); k++) {
    prefiltdata.INSTAT1_float[k] = 0;
    prefiltdata.INSTAT2_float[k] = 0;
    prefiltdata.INSTATLA1_float[k] = 0;
    prefiltdata.INSTATLA2_float[k] = 0;
  }
  // High pass filter states.
  prefiltdata.HPstates_float[0] = 0.0f;
  prefiltdata.HPstates_float[1] = 0.0f;
}

double LevDurb(double* a, double* k, double* r, size_t order) {
  const double LEVINSON_EPS = 1.0e-10;
  double sum, alpha;
  alpha = 0;
  a[0] = 1.0;
  if (r[0] < LEVINSON_EPS) {  // If r[0] <= 0, set LPC coeff. to zero.
    for (size_t i = 0; i < order; i++) {
      k[i] = 0;
      a[i + 1] = 0;
    }
  } else {
    a[1] = k[0] = -r[1] / r[0];
    alpha = r[0] + r[1] * k[0];
    for (size_t m = 1; m < order; m++) {
      sum = r[m + 1];
      for (size_t i = 0; i < m; i++) {
        sum += a[i + 1] * r[m - i];
      }
      k[m] = -sum / alpha;
      alpha += k[m] * sum;
      const size_t m_h = (m + 1) >> 1;
      for (size_t i = 0; i < m_h; i++) {
        sum = a[i + 1] + k[m] * a[m - i];
        a[m - i] += k[m] * a[i + 1];
        a[i + 1] = sum;
      }
      a[m + 1] = k[m];
    }
  }
  return alpha;
}

void SplitAndFilterFloat(const float* pin, float* LP, float* HP, double* LP_la, double* HP_la,
                         PreFiltBankState& prefiltdata) {
  float CompositeAPFilterState[NUMBEROFCOMPOSITEAPSECTIONS];
  float ForTransform_CompositeAPFilterState[NUMBEROFCOMPOSITEAPSECTIONS];
  float ForTransform_CompositeAPFilterState2[NUMBEROFCOMPOSITEAPSECTIONS];
  float tempinoutvec[FRAMESAMPLES + MAX_AR_MODEL_ORDER];
  float tempin_ch1[FRAMESAMPLES + MAX_AR_MODEL_ORDER];
  float tempin_ch2[FRAMESAMPLES + MAX_AR_MODEL_ORDER];
  float in[FRAMESAMPLES];

  // HPstcoeff_in = {a1, a2, b1 - b0 * a1, b2 - b0 * a2};
  static constexpr float kHpStCoefInFloat[4] = {-1.94895953203325f, 0.94984516000000f, -0.05101826139794f,
                                                0.05015484000000f};
  // The composite all-pass filter factors.
  static constexpr float kCompositeApFactorsFloat[4] = {0.03470000000000f, 0.15440000000000f, 0.38260000000000f,
                                                        0.74400000000000f};
  // The matrix for transforming the backward composite state to upper
  // channel state.
  static constexpr float kTransform1Float[8] = {-0.00158678506084f, 0.00127157815343f, -0.00104805672709f,
                                                0.00084837248079f,  0.00134467983258f, -0.00107756549387f,
                                                0.00088814793277f,  -0.00071893072525f};
  // The matrix for transforming the backward composite state to lower
  // channel state.
  static constexpr float kTransform2Float[8] = {-0.00170686041697f, 0.00136780109829f, -0.00112736532350f,
                                                0.00091257055385f,  0.00103094281812f, -0.00082615076557f,
                                                0.00068092756088f,  -0.00055119165484f};

  // High pass filter.
  for (int k = 0; k < FRAMESAMPLES; k++) {
    in[k] = pin[k] + kHpStCoefInFloat[2] * prefiltdata.HPstates_float[0] +
            kHpStCoefInFloat[3] * prefiltdata.HPstates_float[1];
    const float ftmp = pin[k] - kHpStCoefInFloat[0] * prefiltdata.HPstates_float[0] -
                       kHpStCoefInFloat[1] * prefiltdata.HPstates_float[1];
    prefiltdata.HPstates_float[1] = prefiltdata.HPstates_float[0];
    prefiltdata.HPstates_float[0] = ftmp;
  }

  // First Channel. The initial state of the composite filter is zero.
  for (int k = 0; k < NUMBEROFCOMPOSITEAPSECTIONS; k++) {
    CompositeAPFilterState[k] = 0.0;
  }
  // Every other sample of input, in reverse (backward) order.
  for (int k = 0; k < FRAMESAMPLES_HALF; k++) {
    tempinoutvec[k] = in[FRAMESAMPLES - 1 - 2 * k];
  }
  // All-pass filter the backwards vector, in place.
  AllPassFilter2Float(tempinoutvec, kCompositeApFactorsFloat, FRAMESAMPLES_HALF, NUMBEROFCOMPOSITEAPSECTIONS,
                      CompositeAPFilterState);
  // Save the backwards filtered output for later forward filtering, in
  // forward order.
  for (int k = 0; k < FRAMESAMPLES_HALF; k++) {
    tempin_ch1[FRAMESAMPLES_HALF + QLOOKAHEAD - 1 - k] = tempinoutvec[k];
  }
  // Save the backwards filter state, to be transformed into a forward state.
  for (int k = 0; k < NUMBEROFCOMPOSITEAPSECTIONS; k++) {
    ForTransform_CompositeAPFilterState[k] = CompositeAPFilterState[k];
  }
  // Backwards filter the lookahead buffer from the previous frame.
  AllPassFilter2Float(prefiltdata.INLABUF1_float, kCompositeApFactorsFloat, QLOOKAHEAD, NUMBEROFCOMPOSITEAPSECTIONS,
                      CompositeAPFilterState);
  // Save the output in forward order, and the lookahead samples for the
  // next frame.
  for (int k = 0; k < QLOOKAHEAD; k++) {
    tempin_ch1[QLOOKAHEAD - 1 - k] = prefiltdata.INLABUF1_float[k];
    prefiltdata.INLABUF1_float[k] = in[FRAMESAMPLES - 1 - 2 * k];
  }

  // Second Channel, the even samples (lower channel).
  for (int k = 0; k < NUMBEROFCOMPOSITEAPSECTIONS; k++) {
    CompositeAPFilterState[k] = 0.0;
  }
  for (int k = 0; k < FRAMESAMPLES_HALF; k++) {
    tempinoutvec[k] = in[FRAMESAMPLES - 2 - 2 * k];
  }
  AllPassFilter2Float(tempinoutvec, kCompositeApFactorsFloat, FRAMESAMPLES_HALF, NUMBEROFCOMPOSITEAPSECTIONS,
                      CompositeAPFilterState);
  for (int k = 0; k < FRAMESAMPLES_HALF; k++) {
    tempin_ch2[FRAMESAMPLES_HALF + QLOOKAHEAD - 1 - k] = tempinoutvec[k];
  }
  for (int k = 0; k < NUMBEROFCOMPOSITEAPSECTIONS; k++) {
    ForTransform_CompositeAPFilterState2[k] = CompositeAPFilterState[k];
  }
  AllPassFilter2Float(prefiltdata.INLABUF2_float, kCompositeApFactorsFloat, QLOOKAHEAD, NUMBEROFCOMPOSITEAPSECTIONS,
                      CompositeAPFilterState);
  for (int k = 0; k < QLOOKAHEAD; k++) {
    tempin_ch2[QLOOKAHEAD - 1 - k] = prefiltdata.INLABUF2_float[k];
    prefiltdata.INLABUF2_float[k] = in[FRAMESAMPLES - 2 - 2 * k];
  }

  // Transform the backward filter states to forward states, added to each
  // channel's forward state (the C indexes the 2x4 matrices with row
  // stride NUMBEROFCHANNELAPSECTIONS; kept).
  for (int k = 0; k < NUMBEROFCHANNELAPSECTIONS; k++) {      // k is row variable
    for (int n = 0; n < NUMBEROFCOMPOSITEAPSECTIONS; n++) {  // n is column variable
      prefiltdata.INSTAT1_float[k] +=
          ForTransform_CompositeAPFilterState[n] * kTransform1Float[k * NUMBEROFCHANNELAPSECTIONS + n];
      prefiltdata.INSTAT2_float[k] +=
          ForTransform_CompositeAPFilterState2[n] * kTransform2Float[k * NUMBEROFCHANNELAPSECTIONS + n];
    }
  }

  // Forward filter the backward filtered samples with each channel's filter.
  AllPassFilter2Float(tempin_ch1, kUpperApFactorsFloat, FRAMESAMPLES_HALF, NUMBEROFCHANNELAPSECTIONS,
                      prefiltdata.INSTAT1_float);
  AllPassFilter2Float(tempin_ch2, kLowerApFactorsFloat, FRAMESAMPLES_HALF, NUMBEROFCHANNELAPSECTIONS,
                      prefiltdata.INSTAT2_float);

  // Low-pass and high-pass signals from the polyphase components.
  for (int k = 0; k < FRAMESAMPLES_HALF; k++) {
    LP[k] = 0.5f * (tempin_ch1[k] + tempin_ch2[k]);  // low pass signal
    HP[k] = 0.5f * (tempin_ch1[k] - tempin_ch2[k]);  // high pass signal
  }

  // Lookahead LP and HP signals, without phase equalization (for analysis).
  for (int k = 0; k < FRAMESAMPLES_HALF; k++) {
    tempin_ch1[k] = in[2 * k + 1];
    tempin_ch2[k] = in[2 * k];
  }
  AllPassFilter2Float(tempin_ch1, kUpperApFactorsFloat, FRAMESAMPLES_HALF, NUMBEROFCHANNELAPSECTIONS,
                      prefiltdata.INSTATLA1_float);
  AllPassFilter2Float(tempin_ch2, kLowerApFactorsFloat, FRAMESAMPLES_HALF, NUMBEROFCHANNELAPSECTIONS,
                      prefiltdata.INSTATLA2_float);
  for (int k = 0; k < FRAMESAMPLES_HALF; k++) {
    LP_la[k] = static_cast<float>(0.5f * (tempin_ch1[k] + tempin_ch2[k]));   // low pass
    HP_la[k] = static_cast<double>(0.5f * (tempin_ch1[k] - tempin_ch2[k]));  // high pass
  }
}

// ---- pitch_estimator.c (public) ---------------------------------------------------

void PitchAnalysis(const double* in, double* out, PitchAnalysisState& State, double* lags, double* gains) {
  double HPin[PITCH_FRAME_LEN];
  double Weighted[PITCH_FRAME_LEN];
  double Whitened[PITCH_FRAME_LEN + QLOOKAHEAD];
  double inbuf[PITCH_FRAME_LEN + QLOOKAHEAD];
  double out_G[PITCH_FRAME_LEN + QLOOKAHEAD];
  double out_dG[4][PITCH_FRAME_LEN + QLOOKAHEAD];
  double H[4][4];
  double grad[4];
  double dG[4];
  double tmp;

  // High pass filtering using second order pole-zero filter.
  Highpass(in, HPin, State.hp_state, PITCH_FRAME_LEN);

  // Copy from state into buffer.
  std::memcpy(Whitened, State.whitened_buf, sizeof(double) * QLOOKAHEAD);

  // Compute weighted and whitened signals.
  WeightingFilter(HPin, &Weighted[0], &Whitened[QLOOKAHEAD], &State.Wghtstr);

  // Copy from buffer into state.
  std::memcpy(State.whitened_buf, Whitened + PITCH_FRAME_LEN, sizeof(double) * QLOOKAHEAD);

  const double old_lag = State.PFstr_wght.oldlagp[0];
  const double old_gain = State.PFstr_wght.oldgainp[0];

  // Inital pitch estimate.
  InitializePitch(Weighted, old_lag, old_gain, &State, lags);

  // Energy of whitened signal.
  double nrg_wht = 0.0;
  for (int k = 0; k < PITCH_FRAME_LEN + QLOOKAHEAD; k++) nrg_wht += Whitened[k] * Whitened[k];

  // Weights for energy, gain fluctiation, and spectral gain penalty
  // functions.
  const double Wnrg = 1.0 / nrg_wht;
  const double Wgain = 0.005;
  const double Wfluct = 3.0;

  // Set initial gains.
  for (int k = 0; k < 4; k++) gains[k] = PITCH_MAX_GAIN_06;

  // Two iterations should be enough.
  for (int iter = 0; iter < 2; iter++) {
    // Jacobian of pre-filter output towards gains.
    FilterFrame(Whitened, &State.PFstr_wght, lags, gains, kPitchFilterPreGain, out_G, out_dG);

    // Gradient and approximate Hessian (lower triangle) for minimizing the
    // filter's output power.
    for (int k = 0; k < 4; k++) {
      tmp = 0.0;
      for (int n = 0; n < PITCH_FRAME_LEN + QLOOKAHEAD; n++) tmp += out_G[n] * out_dG[k][n];
      grad[k] = tmp * Wnrg;
    }
    for (int k = 0; k < 4; k++) {
      for (int m = 0; m <= k; m++) {
        tmp = 0.0;
        for (int n = 0; n < PITCH_FRAME_LEN + QLOOKAHEAD; n++) tmp += out_dG[m][n] * out_dG[k][n];
        H[k][m] = tmp * Wnrg;
      }
    }

    // Gradient and Hessian (lower triangle) for dampening fast gain changes.
    for (int k = 0; k < 4; k++) {
      tmp = kWeight[k + 1][0] * old_gain;
      for (int m = 0; m < 4; m++) tmp += kWeight[k + 1][m + 1] * gains[m];
      grad[k] += tmp * Wfluct;
    }
    for (int k = 0; k < 4; k++) {
      for (int m = 0; m <= k; m++) {
        H[k][m] += kWeight[k + 1][m + 1] * Wfluct;
      }
    }

    // Gradient and Hessian for dampening gain.
    for (int k = 0; k < 3; k++) {
      tmp = 1.0 / (1 - gains[k]);
      grad[k] += tmp * tmp * Wgain;
      H[k][k] += 2.0 * tmp * (tmp * tmp * Wgain);
    }
    tmp = 1.0 / (1 - gains[3]);
    grad[3] += 1.33 * (tmp * tmp * Wgain);
    H[3][3] += 2.66 * tmp * (tmp * tmp * Wgain);

    // Cholesky factorization of the Hessian, overwriting the upper
    // triangle; scale factors on the diagonal.
    H[0][1] = H[1][0] / H[0][0];
    H[0][2] = H[2][0] / H[0][0];
    H[0][3] = H[3][0] / H[0][0];
    H[1][1] -= H[0][0] * H[0][1] * H[0][1];
    H[1][2] = (H[2][1] - H[0][1] * H[2][0]) / H[1][1];
    H[1][3] = (H[3][1] - H[0][1] * H[3][0]) / H[1][1];
    H[2][2] -= H[0][0] * H[0][2] * H[0][2] + H[1][1] * H[1][2] * H[1][2];
    H[2][3] = (H[3][2] - H[0][2] * H[3][0] - H[1][2] * H[1][1] * H[1][3]) / H[2][2];
    H[3][3] -= H[0][0] * H[0][3] * H[0][3] + H[1][1] * H[1][3] * H[1][3] + H[2][2] * H[2][3] * H[2][3];

    // Update as delta_gains = -inv(H) * grad: copy and negate.
    for (int k = 0; k < 4; k++) dG[k] = -grad[k];
    // Back substitution.
    dG[1] -= dG[0] * H[0][1];
    dG[2] -= dG[0] * H[0][2] + dG[1] * H[1][2];
    dG[3] -= dG[0] * H[0][3] + dG[1] * H[1][3] + dG[2] * H[2][3];
    // Scale.
    for (int k = 0; k < 4; k++) dG[k] /= H[k][k];
    // Back substitution.
    dG[2] -= dG[3] * H[2][3];
    dG[1] -= dG[3] * H[1][3] + dG[2] * H[1][2];
    dG[0] -= dG[3] * H[0][3] + dG[2] * H[0][2] + dG[1] * H[0][1];

    // Update gains and check range.
    for (int k = 0; k < 4; k++) {
      gains[k] += dG[k];
      if (gains[k] > PITCH_MAX_GAIN)
        gains[k] = PITCH_MAX_GAIN;
      else if (gains[k] < 0.0)
        gains[k] = 0.0;
    }
  }

  // Update state for next frame.
  FilterFrame(Whitened, &State.PFstr_wght, lags, gains, kPitchFilterPre, out, nullptr);

  // Concatenate previous input's end and current input.
  std::memcpy(inbuf, State.inbuf, sizeof(double) * QLOOKAHEAD);
  std::memcpy(inbuf + QLOOKAHEAD, in, sizeof(double) * PITCH_FRAME_LEN);

  // Lookahead pitch filtering for masking analysis.
  FilterFrame(inbuf, &State.PFstr, lags, gains, kPitchFilterPreLa, out, nullptr);

  // Store last part of input.
  for (int k = 0; k < QLOOKAHEAD; k++) State.inbuf[k] = inbuf[k + PITCH_FRAME_LEN];
}

}  // namespace isac

}  // namespace sm::audio::agc1::detail
