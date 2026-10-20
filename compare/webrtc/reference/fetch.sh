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
  modules/audio_processing/vad
  modules/audio_processing/transient
  common_audio/vad
  common_audio/resampler
  common_audio/signal_processing
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
