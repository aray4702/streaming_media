#!/usr/bin/env bash
# Refreshes the libwebrtc reference sources in this directory from
# webrtc.googlesource.com at the commit pinned in compare/webrtc/fetch.sh.
# Run it on a machine that can reach googlesource (the agents' cloud sandbox
# cannot), then commit the result.
#
#   compare/webrtc/reference/fetch.sh
set -euo pipefail

COMMIT="b321f634929af0af61a8964487fbd7894a008f12"  # M157, as compare/webrtc/fetch.sh.
BASE="https://webrtc.googlesource.com/src"
HERE="$(cd "$(dirname "$0")" && pwd)"

# Whole directories.
DIRS=(
  modules/congestion_controller/goog_cc
  modules/remote_bitrate_estimator
  modules/pacing
  modules/video_coding/utility
  video/adaptation
  modules/video_coding/timing
  video/timing
  video/config
  api/units
  modules/audio_coding/neteq
  modules/audio_coding/codecs/red
  modules/audio_processing/aec3
  modules/audio_processing/ns
  modules/audio_processing/agc2
  modules/audio_processing/agc
  modules/audio_processing/utility
  modules/audio_processing/vad
  modules/audio_processing/transient
  common_audio/vad
  common_audio/resampler
  common_audio/signal_processing
  common_audio/third_party/ooura/fft_size_256
  common_audio/third_party/ooura/fft_size_128
  modules/audio_coding/codecs/isac/main/source
  modules/third_party/fft
  modules/audio_processing/capture_levels_adjuster
  common_audio/third_party/spl_sqrt_floor
)

for d in "${DIRS[@]}"; do
  mkdir -p "$HERE/$d"
  curl -fsSL --retry 8 --retry-delay 10 "$BASE/+archive/$COMMIT/$d.tar.gz" | tar xz -C "$HERE/$d"
  echo "dir  $d"
done

# Single files, one per line in files.txt ('#' starts a comment).
grep -v '^\s*\(#\|$\)' "$HERE/files.txt" | while read -r f; do
  mkdir -p "$HERE/$(dirname "$f")"
  curl -fsSL --retry 8 --retry-delay 10 "$BASE/+/$COMMIT/$f?format=TEXT" | base64 -d > "$HERE/$f"
  echo "file $f"
  sleep 1  # googlesource rate-limits bursts (HTTP 429).
done

for f in LICENSE PATENTS AUTHORS; do
  curl -fsSL --retry 8 --retry-delay 10 "$BASE/+/$COMMIT/$f?format=TEXT" | base64 -d > "$HERE/$f"
done

# libopus, as libwebrtc builds it from third_party/opus/src. Revision from
# chromium/src/third_party@8a83e635 (libwebrtc DEPS at COMMIT) opus/README.chromium;
# Chromium's local changes there don't touch these files.
OPUS_REV="55513e81d8f606bd75d0ff773d2144e5f2a732f5"
for f in COPYING silk/control_codec.c src/opus_encoder.c silk/enc_API.c silk/fixed/encode_frame_FIX.c \
         silk/control.h silk/structs.h silk/define.h silk/tuning_parameters.h silk/fixed/structs_FIX.h src/opus_private.h; do
  mkdir -p "$HERE/third_party/opus/src/$(dirname "$f")"
  curl -fsSL --retry 8 --retry-delay 10 "https://gitlab.xiph.org/xiph/opus/-/raw/$OPUS_REV/$f" -o "$HERE/third_party/opus/src/$f"
  echo "opus $f"
done

# rnnoise (AGC2's RNN VAD weights), from chromium/src/third_party at the revision
# libwebrtc DEPS pins for src/third_party.
TP_REV="8a83e63525b495aa7f35648a3b4b676279deb864"
for f in rnnoise/src/rnn_vad_weights.h rnnoise/src/rnn_vad_weights.cc rnnoise/src/rnn_activations.h rnnoise/COPYING; do
  mkdir -p "$HERE/third_party/$(dirname "$f")"
  curl -fsSL --retry 8 --retry-delay 10 "https://chromium.googlesource.com/chromium/src/third_party/+/$TP_REV/$f?format=TEXT" | base64 -d > "$HERE/third_party/$f"
  echo "rnnoise $f"
done
