#!/bin/sh
# The simulator check of SmCall's remote video frame rate (#1133): the web app in headless Chrome (a fake 30 fps
# camera) and the SmCall app in the simulator join one room on a local signaling service; Chrome sends video, the app
# draws it (RemoteVideoView.mm) and logs its call-result JSON line at hang-up. Passes when the app drew at least 90% of
# the frames it decoded (app_frames_drawn >= 0.9 * video_frames_decoded) and decoded at least 20 fps.
#
#   apps/ios/simulator_video.sh SIMULATOR_UDID   # after: cmake --preset ios-simulator && cmake --build
#                                                #        out/ios-simulator --config Release --target SmCall
#                                                #        -- CODE_SIGNING_ALLOWED=NO
set -u
SIM=${1:?usage: simulator_video.sh SIMULATOR_UDID}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${SM_SMOKE_DIR:-$(mktemp -d)}
PORT=${SM_SMOKE_PORT:-8094}
SECONDS_CALL=${SM_VIDEO_SECONDS:-20}
APP=$ROOT/out/ios-simulator/apps/ios/Release-iphonesimulator/SmCall.app
CHROME=${CHROME:-"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"}
ROOM=sim-video-$$
xcrun simctl boot "$SIM" 2>/dev/null; xcrun simctl bootstatus "$SIM" -b >/dev/null
# A STUN URL nobody answers: an empty list gives the page an ICE server without urls, which RTCPeerConnection rejects.
PORT=$PORT HOST=127.0.0.1 STUN_URLS=stun:127.0.0.1:9 python3 "$ROOT/apps/signaling/server.py" >"$OUT/signaling.log" 2>&1 &
SIG=$!
sleep 1.5
PROFILE=$(mktemp -d)
"$CHROME" --headless=new --user-data-dir="$PROFILE" --no-first-run --no-default-browser-check --disable-gpu \
  --disable-extensions --disable-features=WebRtcHideLocalIpsWithMdns --use-fake-device-for-media-stream=fps=30 \
  --use-fake-ui-for-media-stream \
  --autoplay-policy=no-user-gesture-required --remote-debugging-port=0 \
  "http://127.0.0.1:$PORT/?room=$ROOM&auto=1" >"$OUT/chrome.log" 2>&1 &
CHR=$!
trap 'kill $CHR $SIG 2>/dev/null; sleep 1; rm -rf "$PROFILE"' EXIT
sleep 3

xcrun simctl install "$SIM" "$APP"
xcrun simctl privacy "$SIM" grant microphone io.github.aray4702.smcall
xcrun simctl launch --terminate-running-process "$SIM" io.github.aray4702.smcall \
  -SMServer "http://127.0.0.1:$PORT" -SMAutoCall "$ROOM" -SMAutoHangup "$SECONDS_CALL" -SMVideo NO >/dev/null
sleep $((SECONDS_CALL - 4))
xcrun simctl io "$SIM" screenshot "$OUT/app.png" >/dev/null 2>&1
sleep 7
xcrun simctl terminate "$SIM" io.github.aray4702.smcall 2>/dev/null
sleep 1

# The app's NSLog lines, from the simulator's log.
xcrun simctl spawn "$SIM" log show --last "$((SECONDS_CALL + 30))s" --style compact \
  --predicate 'process == "SmCall" AND eventMessage CONTAINS "SmCall"' >"$OUT/app.log" 2>/dev/null
line=$(grep -o 'call-result {.*}' "$OUT/app.log" | tail -1 | sed 's/^call-result //')
echo "app: ${line:-no call-result line}" | cut -c1-200
echo "$line" | python3 -c '
import json, sys
try:
    r = json.loads(sys.stdin.read())
except ValueError:
    sys.exit("no JSON line from the app")
drawn, decoded = r.get("app_frames_drawn", -1), r.get("video_frames_decoded", 0)
print("decoded %d frames (%.1f fps), drew %d (%.1f fps): %.0f%% drawn" % (
    decoded, r.get("app_decoded_fps", 0), drawn, r.get("app_render_fps", 0), 100.0 * drawn / max(decoded, 1)))
ok = decoded > 0 and drawn >= 0.9 * decoded and r.get("app_decoded_fps", 0) >= 20
sys.exit(0 if ok else 1)'
ok=$?
echo "logs and screenshot in $OUT"
[ $ok -eq 0 ] && echo "simulator video: ok" || echo "simulator video: FAILED"
exit $ok
