#!/bin/sh
# The simulator smoke run of the calling app (#1116), on this Mac against a local signaling service and AI server:
#  1. sm_call_smoke (the C++ call controller, built for the simulator as a plain executable) calls an `ai-` room;
#  2. the SmCall app bundle is installed and launched with -SMAutoCall, calls another `ai-` room and hangs up.
# Each passes when the AI server's metrics line for its room says connected and audio arrived; sm_call_smoke also
# needs a user turn and a reply (its microphone is scripted speech; the app's is the Mac's real one).
#
#   apps/ios/simulator_smoke.sh SIMULATOR_UDID   # after: cmake --preset native && cmake --build --preset native
#                                                #        --target sm_ai_server; cmake --preset ios-simulator &&
#                                                #        cmake --build out/ios-simulator --config Release
#                                                #        --target SmCall sm_call_smoke -- CODE_SIGNING_ALLOWED=NO
set -u
SIM=${1:?usage: simulator_smoke.sh SIMULATOR_UDID}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${SM_SMOKE_DIR:-$(mktemp -d)}
PORT=${SM_SMOKE_PORT:-8093}
APP=$ROOT/out/ios-simulator/apps/ios/Release-iphonesimulator/SmCall.app
SMOKE=$ROOT/out/ios-simulator/apps/call/Release-iphonesimulator/sm_call_smoke
xcrun simctl boot "$SIM" 2>/dev/null; xcrun simctl bootstatus "$SIM" -b >/dev/null
PORT=$PORT HOST=127.0.0.1 STUN_URLS= python3 "$ROOT/apps/signaling/server.py" >"$OUT/signaling.log" 2>&1 &
SIG=$!
sleep 1.5
"$ROOT/out/native/apps/ai_server/sm_ai_server" --signaling=ws://127.0.0.1:$PORT/ws --metrics="$OUT/calls.jsonl" \
  >"$OUT/ai_server.log" 2>&1 &
AI=$!
trap 'kill $AI $SIG 2>/dev/null' EXIT
sleep 1

xcrun simctl spawn "$SIM" "$SMOKE" --signaling=ws://127.0.0.1:$PORT/ws --room=ai-sim-smoke --seconds=8 \
  >"$OUT/smoke.out" 2>"$OUT/smoke.err"
echo "sm_call_smoke: exit $? ($(tail -1 "$OUT/smoke.out"))"

xcrun simctl install "$SIM" "$APP"
xcrun simctl privacy "$SIM" grant microphone io.github.aray4702.smcall
xcrun simctl launch --terminate-running-process "$SIM" io.github.aray4702.smcall -SMServer http://127.0.0.1:$PORT \
  -SMAutoCall ai-sim-app -SMAutoHangup 10 -SMVideo NO >/dev/null
sleep 8
xcrun simctl io "$SIM" screenshot "$OUT/app.png" >/dev/null 2>&1
sleep 6
xcrun simctl terminate "$SIM" io.github.aray4702.smcall 2>/dev/null
sleep 1

ok=0
check() {  # room, extra pattern that must not match
  line=$(grep "\"room\":\"$1\"" "$OUT/calls.jsonl" 2>/dev/null)
  echo "$1: ${line:-no call}" | cut -c1-300
  echo "$line" | grep -q '"connected":true' || ok=1
  echo "$line" | grep -q '"audio_packets_received":[1-9][0-9][0-9]' || ok=1
  [ -z "$2" ] || ! echo "$line" | grep -q -E "$2" || ok=1
}
check ai-sim-smoke '"user_turns":0,|"replies":0,'
check ai-sim-app ''
echo "logs and screenshot in $OUT"
[ $ok -eq 0 ] && echo "simulator smoke: ok" || echo "simulator smoke: FAILED"
exit $ok
