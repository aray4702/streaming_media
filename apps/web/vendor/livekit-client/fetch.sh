#!/bin/sh
# Downloads the pinned livekit-client tarball from npm, checks it against npm's sha512 integrity, and compares the
# vendored files with it (or, with --update, replaces them). See README.md.
set -eu
VERSION=2.23.0
INTEGRITY='sha512-j9Kg86nJjBjNFUD+ukp2jwMCGa0k+YD5gU//RzAM+ZY3pbG631QCte7fQjwqWMmtBzhDJL9nKYuOLMv/9IbAxg=='
HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
curl -fsSL --retry 3 -o "$TMP/pkg.tgz" "https://registry.npmjs.org/livekit-client/-/livekit-client-$VERSION.tgz"
GOT="sha512-$(openssl dgst -sha512 -binary "$TMP/pkg.tgz" | openssl base64 -A)"
if [ "$GOT" != "$INTEGRITY" ]; then
  echo "integrity mismatch: $GOT" >&2
  exit 1
fi
tar -xzf "$TMP/pkg.tgz" -C "$TMP"
if [ "${1:-}" = "--update" ]; then
  cp "$TMP/package/dist/livekit-client.umd.js" "$TMP/package/LICENSE" "$HERE/"
  (cd "$HERE" && shasum -a 256 livekit-client.umd.js LICENSE > SHA256SUMS)
  echo "updated to livekit-client $VERSION"
else
  cmp "$TMP/package/dist/livekit-client.umd.js" "$HERE/livekit-client.umd.js"
  cmp "$TMP/package/LICENSE" "$HERE/LICENSE"
  (cd "$HERE" && shasum -a 256 -c SHA256SUMS)
  echo "livekit-client $VERSION: the vendored files match npm's tarball"
fi
