#!/bin/sh
# The simulator smoke run of a group call (C20, #1172), on this Mac: a local LiveKit server (livekit-server, its dev
# key pair on its own ports), the signaling service issuing tokens (/lk-token) and serving the web app, and the web app
# in headless Chrome in Group mode (a fake camera and microphone). Then in one LiveKit room:
#  1. sm_call_smoke, the C++ controller built for the simulator (as the app's controller), joins with --livekit and
#     publishes audio and video; it must receive the page's audio and video (its livekit_group stats);
#  2. the SmCall app joins the same room with -SMAutoGroup (its Group button) and hangs up after a while; its
#     call-result line (from the simulator's log) must show the page's audio played and video decoded, and the page's
#     tile drawn on the GPU (app_tiles_drawn).
# The page must receive both (its report: audio from each, video from sm_call_smoke).
#
#   apps/ios/simulator_group.sh SIMULATOR_UDID   # after: cmake --preset ios-simulator && cmake --build
#                                                #        out/ios-simulator --config Release --target SmCall
#                                                #        sm_call_smoke -- CODE_SIGNING_ALLOWED=NO
# livekit-server: SM_LIVEKIT_SERVER, else the PATH. Exits 77 without it or Chrome.
set -u
SIM=${1:?usage: simulator_group.sh SIMULATOR_UDID}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${SM_SMOKE_DIR:-$(mktemp -d)}
PORT=${SM_SMOKE_PORT:-8095}
LKPORT=${SM_LIVEKIT_PORT:-7895}
SECONDS_CALL=${SM_GROUP_SECONDS:-20}
APP=$ROOT/out/ios-simulator/apps/ios/Release-iphonesimulator/SmCall.app
SMOKE=$ROOT/out/ios-simulator/apps/call/Release-iphonesimulator/sm_call_smoke
CHROME=${CHROME:-"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"}
LK=${SM_LIVEKIT_SERVER:-$(command -v livekit-server)}
ROOM=sim-group-$$
if [ -z "$LK" ] || [ ! -x "$LK" ] || [ ! -x "$CHROME" ]; then
  echo "SKIPPED: no livekit-server (SM_LIVEKIT_SERVER) or Chrome"
  exit 77
fi
xcrun simctl boot "$SIM" 2>/dev/null; xcrun simctl bootstatus "$SIM" -b >/dev/null
# LiveKit's media address is this Mac's (the clients gather on its interfaces, not loopback), as test_group_call.py.
HOSTIP=$(python3 -c 'import socket; s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.connect(("192.0.2.1", 9)); print(s.getsockname()[0])' 2>/dev/null || echo 127.0.0.1)
"$LK" --config-body "$(printf 'port: %d\nbind_addresses: ["127.0.0.1"]\nrtc:\n  tcp_port: %d\n  udp_port: %d\n  node_ip: %s\n  use_external_ip: false\nkeys:\n  devkey: secret\nroom:\n  max_participants: 6\n' \
  "$LKPORT" $((LKPORT + 1)) $((LKPORT + 2)) "$HOSTIP")" >"$OUT/livekit.log" 2>&1 &
LKP=$!
# A STUN URL nobody answers: an empty list gives the page an ICE server without urls, which RTCPeerConnection rejects.
PORT=$PORT HOST=127.0.0.1 STUN_URLS=stun:127.0.0.1:9 LIVEKIT_API_KEY=devkey LIVEKIT_API_SECRET=secret \
  LIVEKIT_URL=ws://127.0.0.1:$LKPORT python3 "$ROOT/apps/signaling/server.py" >"$OUT/signaling.log" 2>&1 &
SIG=$!
sleep 2
PROFILE=$(mktemp -d)
"$CHROME" --headless=new --user-data-dir="$PROFILE" --no-first-run --no-default-browser-check --disable-gpu \
  --disable-extensions --use-fake-device-for-media-stream --use-fake-ui-for-media-stream \
  --autoplay-policy=no-user-gesture-required --remote-debugging-port=0 \
  "http://127.0.0.1:$PORT/?room=$ROOM&group=1&auto=1&report=1&dtx=0" >"$OUT/chrome.log" 2>&1 &
CHR=$!
trap 'kill $CHR $SIG $LKP 2>/dev/null; sleep 1; rm -rf "$PROFILE"' EXIT
sleep 4

# 1. The controller, built for the simulator.
xcrun simctl spawn "$SIM" "$SMOKE" --livekit=ws://127.0.0.1:$LKPORT --token=http://127.0.0.1:$PORT/lk-token \
  --room="$ROOM" --peer=sim-native-$$ --video --no-talk --seconds=15 >"$OUT/smoke.out" 2>"$OUT/smoke.err"
echo "sm_call_smoke: exit $? ($(tail -1 "$OUT/smoke.out"))"

# 2. The app.
xcrun simctl install "$SIM" "$APP"
xcrun simctl privacy "$SIM" grant microphone io.github.aray4702.smcall
xcrun simctl launch --terminate-running-process "$SIM" io.github.aray4702.smcall -SMServer "http://127.0.0.1:$PORT" \
  -SMAutoGroup "$ROOM" -SMAutoHangup "$SECONDS_CALL" -SMVideo NO >/dev/null
sleep $((SECONDS_CALL - 4))
xcrun simctl io "$SIM" screenshot "$OUT/app.png" >/dev/null 2>&1
curl -s "http://127.0.0.1:$PORT/report" >"$OUT/reports.json"
sleep 7
xcrun simctl terminate "$SIM" io.github.aray4702.smcall 2>/dev/null
sleep 1
xcrun simctl spawn "$SIM" log show --last "$((SECONDS_CALL + 30))s" --style compact \
  --predicate 'process == "SmCall" AND eventMessage CONTAINS "SmCall"' >"$OUT/app.log" 2>/dev/null
# The app's call-result line, from its Documents (the log cuts a group call's long line).
DATA=$(xcrun simctl get_app_container "$SIM" io.github.aray4702.smcall data 2>/dev/null)
tail -1 "$DATA/Documents/call-results.jsonl" >"$OUT/app.json" 2>/dev/null

python3 - "$OUT" <<'EOF'
import json, os, sys
out = sys.argv[1]
ok = True
def check(cond, what):
    global ok
    print(('ok: ' if cond else 'FAIL: ') + what)
    ok = ok and cond
def last_json(path, key):
    lines = [l for l in open(path, errors='replace') if l.startswith('{') and key in l]
    d = json.loads(lines[-1]) if lines else {}
    return d.get('stats', d)  # sm_call_smoke's last line is its call-result, with the stats inside.
reports = [r for r in json.load(open(os.path.join(out, 'reports.json'))) if 'group' in r]
page = reports[-1] if reports else {}
page_id = page.get('peer', '')
parts = page.get('group', {}).get('participants', {})
smoke = last_json(os.path.join(out, 'smoke.out'), '"livekit_group"')
g = {p['identity']: p for p in smoke.get('livekit_group', [])}.get(page_id, {})
check(g.get('audio_played_s', 0) > 1 and g.get('video_frames_decoded', 0) > 5,
      'sm_call_smoke (simulator) receives the page: audio %.1f s, video %d frames' % (
          g.get('audio_played_s', 0), g.get('video_frames_decoded', 0)))
try:
    app = json.load(open(os.path.join(out, 'app.json')))
except ValueError:
    app = {}
a = {p['identity']: p for p in app.get('livekit_group', [])}.get(page_id, {})
tiles = app.get('app_tiles_drawn', {})
check(a.get('audio_played_s', 0) > 1 and a.get('video_frames_decoded', 0) > 5,
      'the app receives the page: audio %.1f s, video %d frames' % (a.get('audio_played_s', 0),
                                                                   a.get('video_frames_decoded', 0)))
check(tiles.get(page_id, 0) > 5, "the app drew the page's tile (%d frames)" % tiles.get(page_id, 0))
app_id = app.get('livekit_identity', '')
check(app_id in parts and parts[app_id]['audioBytesIn'] > 2000,
      "the page receives the app's audio (%s)" % json.dumps(parts.get(app_id, {}).get('audioBytesIn')))
seen = page.get('group', {}).get('joined', [])
check(any(i.startswith('sim-native-') for i in seen) and app_id in seen,
      'the page saw both join (%s)' % seen)
sys.exit(0 if ok else 1)
EOF
ok=$?
echo "logs and screenshot in $OUT"
[ $ok -eq 0 ] && echo "simulator group: ok" || echo "simulator group: FAILED"
exit $ok
