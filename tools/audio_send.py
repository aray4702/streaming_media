#!/usr/bin/env python3
"""Compare the two stacks' audio send paths frame by frame (issue #477).

Both `sm_testbench --link-trace=DIR` and `sm_compare` write the same audio
send trace for this library and for libwebrtc, read from the packet bytes
(testbench/call.h AudioSendRow):

  kind,time_us,seq,rtp_ts,pt,payload_bytes,payload_hash,fraction_lost
  frame,...   every audio packet offered to the forward link: its RTP timestamp
              (relative to the first frame's), payload type, payload size and an
              FNV-1a hash of the payload (RED header included)
  report,...  every report block about the audio SSRC delivered to the sender,
              with its 8-bit fraction lost

libopus is deterministic, so two frames with the same payload hash were
encoded from the same samples with the same settings. For each side the tool
also replays the report blocks through libwebrtc's packet loss smoother
(audio_encoder_opus.cc PacketLossFractionSmoother: ExpFilter alpha 0.9999 per
ms, clamped to 0.2, a whole percent to libopus), so a frame that differs can
be told apart from a frame whose loss input differed.

  tools/audio_send.py sm.audiosend.csv webrtc.audiosend.csv
  tools/audio_send.py compare-webrtc.json --scenario audio_only_lossy

Exit status 0 when every frame matches in RTP timestamp and send time, and
every frame encoded with the same loss percentage on both sides has the same
payload; 1 otherwise.
"""
import argparse
import csv
import io
import json
import sys

ALPHA = 0.9999           # kAlphaForPacketLossFractionSmoother, per ms
MAX_LOSS_FRACTION = 0.2  # kMaxPacketLossFraction


def parse(text):
    frames, reports = [], []
    for r in csv.DictReader(io.StringIO(text)):
        if r["kind"] == "frame":
            frames.append({"t": int(r["time_us"]), "ts": int(r["rtp_ts"]), "pt": int(r["pt"]),
                           "bytes": int(r["payload_bytes"]), "hash": r["payload_hash"]})
        else:
            reports.append((int(r["time_us"]), int(r["fraction_lost"]) / 256.0))
    return frames, reports


def loss_percent(frames, reports):
    """The percentage libopus had for each frame: report blocks delivered before the frame went out."""
    out, filtered, last_t, i = [], None, 0, 0
    pct = 0
    for f in frames:
        while i < len(reports) and reports[i][0] <= f["t"]:
            t, frac = reports[i]
            if filtered is None:
                filtered = frac
            else:
                a = ALPHA ** ((t - last_t) / 1000.0)
                filtered = a * filtered + (1 - a) * frac
            last_t = t
            pct = int(min(max(filtered, 0.0), MAX_LOSS_FRACTION) * 100 + 0.5)
            i += 1
        out.append(pct)
    return out


def compare(a_text, b_text, names=("sm", "webrtc"), out=sys.stdout):
    fa, ra = parse(a_text)
    fb, rb = parse(b_text)
    la, lb = loss_percent(fa, ra), loss_percent(fb, rb)
    n = min(len(fa), len(fb))
    print(f"frames: {names[0]} {len(fa)}, {names[1]} {len(fb)}", file=out)
    print(f"report blocks at the sender (ms): {names[0]} {[r[0] // 1000 for r in ra]}", file=out)
    print(f"                                  {names[1]} {[r[0] // 1000 for r in rb]}", file=out)
    ts_diff = [k for k in range(n) if fa[k]["ts"] != fb[k]["ts"]]
    t_diff = [k for k in range(n) if fa[k]["t"] != fb[k]["t"]]
    same_loss = [k for k in range(n) if la[k] == lb[k]]
    hash_diff = [k for k in same_loss if fa[k]["hash"] != fb[k]["hash"]]
    loss_diff = [k for k in range(n) if la[k] != lb[k]]
    print(f"RTP timestamp differs: {len(ts_diff)} frames", file=out)
    print(f"send time differs: {len(t_diff)} frames", file=out)
    print(f"loss % differs (different report blocks): {len(loss_diff)} frames", file=out)
    print(f"payload differs with the same loss %: {len(hash_diff)} of {len(same_loss)} frames", file=out)
    for label, ks in (("RTP timestamp", ts_diff), ("send time", t_diff), ("payload", hash_diff), ("loss %", loss_diff)):
        if not ks:
            continue
        k = ks[0]
        print(f"first {label} difference: frame {k}", file=out)
        for j in range(max(0, k - 1), min(n, k + 2)):
            print(f"  frame {j}: " + "  |  ".join(
                f"{nm} t={f[j]['t']} ts={f[j]['ts']} pt={f[j]['pt']} bytes={f[j]['bytes']} hash={f[j]['hash']} loss={l[j]}%"
                for nm, f, l in ((names[0], fa, la), (names[1], fb, lb))), file=out)
    ok = len(fa) == len(fb) and not ts_diff and not t_diff and not hash_diff
    print("MATCH" if ok else "DIFFERENT", file=out)
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="two .audiosend.csv files, or compare-webrtc.json")
    ap.add_argument("--scenario", help="the traced scenario to read from the JSON")
    args = ap.parse_args()
    if len(args.inputs) == 2:
        texts = [open(p).read() for p in args.inputs]
        return 0 if compare(*texts) else 1
    data = json.load(open(args.inputs[0]))
    ok = True
    for t in data.get("traces", []):
        if args.scenario and t["scenario"] != args.scenario:
            continue
        if "sm_audiosend" not in t:
            continue
        print(f"== {t['scenario']} seed {t['seed']}")
        ok = compare(t["sm_audiosend"], t["webrtc_audiosend"]) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
