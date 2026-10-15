#!/usr/bin/env bash
# Downloads a prebuilt libwebrtc and the libc++ headers it was built with.
#
#   compare/webrtc/fetch.sh [dest]        (default: out/webrtc-deps)
#
# The binary comes from shiguredo-webrtc-build/webrtc-build, which publishes
# libwebrtc releases for macOS and Ubuntu with headers. It is built against
# Chromium's own libc++ (namespace std::__Cr) and ships only part of its
# headers, so the matching libc++ revision and Chromium's __config_site are
# fetched as well, at the commits the release's VERSIONS file names.
set -euo pipefail

RELEASE="m157.8103.0.0"  # libwebrtc M157, commit b321f634.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${1:-$ROOT/out/webrtc-deps}"

case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) ASSET="webrtc.macos_arm64.tar.gz" ;;
  Linux-x86_64) ASSET="webrtc.ubuntu-$(. /etc/os-release && echo "$VERSION_ID")_x86_64.tar.gz" ;;
  Linux-aarch64) ASSET="webrtc.ubuntu-$(. /etc/os-release && echo "$VERSION_ID")_armv8.tar.gz" ;;
  *) echo "unsupported platform: $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

mkdir -p "$DEST"
if [ ! -f "$DEST/webrtc/lib/libwebrtc.a" ]; then
  echo "downloading $ASSET ($RELEASE)"
  url="https://github.com/shiguredo-webrtc-build/webrtc-build/releases/download/$RELEASE/$ASSET"
  curl -fL --retry 3 -o "$DEST/$ASSET" "$url"
  tar -xzf "$DEST/$ASSET" -C "$DEST"
  rm "$DEST/$ASSET"
fi

versions="$DEST/webrtc/VERSIONS"
libcxx_commit=$(sed -n 's/^WEBRTC_SRC_THIRD_PARTY_LIBCXX_SRC_COMMIT=//p' "$versions")
buildtools_commit=$(sed -n 's/^WEBRTC_SRC_BUILDTOOLS_COMMIT=//p' "$versions")

if [ ! -f "$DEST/libcxx/include/cstdio" ]; then
  echo "fetching libc++ $libcxx_commit"
  rm -rf "$DEST/libcxx"
  git init -q "$DEST/libcxx"
  git -C "$DEST/libcxx" fetch -q --depth 1 \
    https://chromium.googlesource.com/external/github.com/llvm/llvm-project/libcxx.git "$libcxx_commit"
  git -C "$DEST/libcxx" checkout -q FETCH_HEAD
fi

mkdir -p "$DEST/libcxx-config"
for f in __config_site __assertion_handler; do
  if [ ! -s "$DEST/libcxx-config/$f" ]; then
    curl -fsL "https://chromium.googlesource.com/chromium/src/buildtools/+/$buildtools_commit/third_party/libc++/$f?format=TEXT" \
      | base64 -d > "$DEST/libcxx-config/$f"
  fi
done

echo "libwebrtc $(sed -n 's/^WEBRTC_READABLE_VERSION=//p' "$versions") ready in $DEST"
