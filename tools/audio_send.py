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
  encode,...  every packet the Opus encoder completed (issue #547; traces
              written before it have none): its index (seq), an FNV-1a hash
              of the PCM it took (payload_hash), the last fraction lost it was
              given (times 256), and bitrate_bps, allocation_bps and
              overhead_bytes: the bitrate it had and the last allocation and
              per-packet overhead it was given (-1: never given one)

libopus is deterministic, so two frames with the same payload hash were
encoded from the same samples with the same settings. For each side the tool
also replays the report blocks through libwebrtc's packet loss smoother
(audio_encoder_opus.cc PacketLossFractionSmoother: ExpFilter alpha 0.9999 per
ms, clamped to 0.2, a whole percent to libopus), so a frame that differs can
be told apart from a frame whose loss input differed.

  tools/audio_send.py sm.audiosend.csv webrtc.audiosend.csv
  tools/audio_send.py compare-webrtc.json --scenario audio_only_lossy

Exit status 0 when every frame has the same RTP timestamp on both sides and
every frame up to the first one whose loss percentage differs has the same
payload (libopus keeps state, so later frames may differ once the inputs
did); 1 otherwise. Send time differences are listed: they are times offered
to the link, after each stack's pacer.
"""
import argparse
import csv
import io
import json
import sys

ALPHA = 0.9999           # kAlphaForPacketLossFractionSmoother, per ms
MAX_LOSS_FRACTION = 0.2  # kMaxPacketLossFraction


ENCODE_INPUTS = ("pcm", "loss_given", "bitrate_bps", "allocation_bps", "overhead_bytes")


def parse(text, encodes=None):
    frames, reports = [], []
    for r in csv.DictReader(io.StringIO(text)):
        if r["kind"] == "frame":
            frames.append({"t": int(r["time_us"]), "ts": int(r["rtp_ts"]), "pt": int(r["pt"]),
                           "bytes": int(r["payload_bytes"]), "hash": r["payload_hash"]})
        elif r["kind"] == "report":
            reports.append((int(r["time_us"]), int(r["fraction_lost"]) / 256.0))
        elif r["kind"] == "encode" and encodes is not None:
            encodes.append({"k": int(r["seq"]), "pcm": r["payload_hash"], "loss_given": int(r["fraction_lost"]),
                            "bitrate_bps": int(r["bitrate_bps"]), "allocation_bps": int(r["allocation_bps"]),
                            "overhead_bytes": int(r["overhead_bytes"])})
    return frames, reports


def compare_encoder_inputs(ea, eb, names, out):
    """The first packet whose encoder inputs differ, input by input (issue #547)."""
    if not ea or not eb:
        print("encoder inputs: not traced", file=out)
        return
    ea, eb = sorted(ea, key=lambda e: e["k"]), sorted(eb, key=lambda e: e["k"])
    n = min(len(ea), len(eb))
    print(f"encoder inputs: {names[0]} {len(ea)} packets, {names[1]} {len(eb)}", file=out)
    for key in ENCODE_INPUTS:
        ks = [k for k in range(n) if ea[k][key] != eb[k][key]]
        if not ks:
            print(f"  {key}: same on all {n}", file=out)
            continue
        k = ks[0]
        print(f"  {key}: differs on {len(ks)}, first at packet {k}: {names[0]} {ea[k][key]}  |  {names[1]} {eb[k][key]}",
              file=out)


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
    ea, eb = [], []
    fa, ra = parse(a_text, ea)
    fb, rb = parse(b_text, eb)
    la, lb = loss_percent(fa, ra), loss_percent(fb, rb)
    n = min(len(fa), len(fb))
    print(f"frames: {names[0]} {len(fa)}, {names[1]} {len(fb)}", file=out)
    print(f"report blocks at the sender (ms): {names[0]} {[r[0] // 1000 for r in ra]}", file=out)
    print(f"                                  {names[1]} {[r[0] // 1000 for r in rb]}", file=out)
    ts_diff = [k for k in range(n) if fa[k]["ts"] != fb[k]["ts"]]
    t_diff = [k for k in range(n) if fa[k]["t"] != fb[k]["t"]]
    loss_diff = [k for k in range(n) if la[k] != lb[k]]
    # libopus carries state from frame to frame, so once the two encoders had different loss inputs their
    # payloads may differ for good: payloads are compared up to the first frame whose loss % differs.
    same_input = range(loss_diff[0] if loss_diff else n)
    hash_diff = [k for k in same_input if fa[k]["hash"] != fb[k]["hash"]]
    print(f"RTP timestamp differs: {len(ts_diff)} frames", file=out)
    print(f"send time differs: {len(t_diff)} frames (offered to the link: includes each stack's pacer)", file=out)
    print(f"loss % differs (different report blocks): {len(loss_diff)} frames", file=out)
    print(f"payload differs before the loss inputs diverge: {len(hash_diff)} of {len(same_input)} frames", file=out)
    for label, ks in (("RTP timestamp", ts_diff), ("send time", t_diff), ("payload", hash_diff), ("loss %", loss_diff)):
        if not ks:
            continue
        k = ks[0]
        print(f"first {label} difference: frame {k}", file=out)
        for j in range(max(0, k - 1), min(n, k + 2)):
            print(f"  frame {j}: " + "  |  ".join(
                f"{nm} t={f[j]['t']} ts={f[j]['ts']} pt={f[j]['pt']} bytes={f[j]['bytes']} hash={f[j]['hash']} loss={l[j]}%"
                for nm, f, l in ((names[0], fa, la), (names[1], fb, lb))), file=out)
    compare_encoder_inputs(ea, eb, names, out)
    ok = len(fa) == len(fb) and not ts_diff and not hash_diff
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
