#!/usr/bin/env python3
"""Rebuild video frames from a forward-link trace (issue #437).

The trace (tools/link_trace.py; `sm_testbench --link-trace=DIR`, and
`sm_compare` for libwebrtc) has, for each RTP packet, its arrival time, RTP
timestamp, marker bit and, for an RTX packet, the original sequence number it
carries. From these this tool works out, for each video frame the sender put
on the link:

- `complete_ms`: when the last of its packets first reached the receiver,
  as the original or as an RTX copy (empty if one never did);
- `in_order_ms`: when it could leave an H.264 packet buffer that holds a
  delta frame while an earlier sequence number is missing
  (`packet_buffer.cc` FindFrames, `missing_packets_`): the later of its
  completion and the moment every earlier sequence number had arrived. A
  frame counts as a keyframe when `--keyframe-bytes` is given and its
  payload is at least that large; a keyframe resets the chain;
- `max_repair_ms`: the longest time one of its packets took from its first
  send to its first arrival.

It prints a per-second table (frames sent, complete, released in order) and
the repair-delay percentiles, so this library's trace and libwebrtc's can be
read side by side:

  tools/link_frames.py ours.trace.csv [theirs.trace.csv] [--keyframe-bytes N] [--frames]
"""
import argparse
import csv
import math
import sys


def load(path):
    rows = list(csv.DictReader(open(path)))
    if rows and "rtp_ts" not in rows[0]:
        sys.exit("%s has no rtp_ts column: it predates issue #437's frame trace" % path)
    return rows


def rebuild(rows, keyframe_bytes=0):
    """Frames in send order, each a dict, and every media packet's repair delay in ms."""
    first_send = {}   # original seq -> first time it was offered (us)
    first_arrival = {}  # original seq -> first arrival as original or RTX (us)
    frames = {}       # rtp_ts -> {"seqs": set, "bytes": int, "marker_seq": int}
    order = []
    for r in rows:
        kind = r["kind"]
        seq = int(r["seq"])
        arr = int(r["arrival_us"])
        if kind == "video":
            ts = int(r["rtp_ts"])
            if ts not in frames:
                frames[ts] = {"ts": ts, "seqs": set(), "bytes": 0, "marker_seq": None, "first_send_us": int(r["time_us"])}
                order.append(ts)
            f = frames[ts]
            if seq not in f["seqs"]:
                f["seqs"].add(seq)
                f["bytes"] += int(r["size"])
            if int(r["marker"]) == 1:
                f["marker_seq"] = seq
            first_send.setdefault(seq, int(r["time_us"]))
            if arr >= 0 and (seq not in first_arrival or arr < first_arrival[seq]):
                first_arrival[seq] = arr
        elif kind == "rtx" and int(r["osn"]) >= 0:
            osn = int(r["osn"])
            if arr >= 0 and (osn not in first_arrival or arr < first_arrival[osn]):
                first_arrival[osn] = arr

    # Unwrap sequence numbers in send order so holes can be ordered.
    unwrap = {}
    last = None
    base = 0
    for ts in order:
        for s in sorted(frames[ts]["seqs"]):
            if s in unwrap:
                continue
            if last is not None:
                d = (s - last) % 65536
                u = unwrap_last + (d if d < 32768 else d - 65536)
            else:
                u = s
            unwrap[s] = u
            unwrap_last, last = u, s

    repair = []
    for s, t0 in first_send.items():
        if s in first_arrival:
            repair.append((first_arrival[s] - t0) / 1000.0)

    # Arrival time of each unwrapped media seq (inf if never).
    arrival_u = {unwrap[s]: first_arrival.get(s, math.inf) for s in unwrap}
    out = []
    for ts in order:
        f = frames[ts]
        seqs = sorted(unwrap[s] for s in f["seqs"])
        times = [arrival_u[u] for u in seqs]
        complete = max(times) if f["marker_seq"] is not None else math.inf
        rep = [(first_arrival[s] - first_send[s]) / 1000.0 for s in f["seqs"] if s in first_arrival]
        out.append({
            "ts": ts, "first_u": seqs[0], "last_u": seqs[-1], "packets": len(seqs), "bytes": f["bytes"],
            "key": keyframe_bytes > 0 and f["bytes"] >= keyframe_bytes,
            "send_ms": f["first_send_us"] / 1000.0, "complete_us": complete,
            "max_repair_ms": max(rep) if len(rep) == len(seqs) else math.inf,
        })

    # In-order release: a delta frame leaves once everything before it has arrived.
    # Walk the media seqs in order and keep the time by which all of them up to u arrived.
    all_u = sorted(arrival_u)
    prefix = {}
    running = -math.inf
    for u in all_u:
        running = max(running, arrival_u[u])
        prefix[u] = running
    last_key_u = None
    for f in out:
        if f["key"]:
            last_key_u = f["first_u"]
            f["in_order_us"] = f["complete_us"]
            continue
        before = [u for u in all_u if u < f["first_u"] and (last_key_u is None or u >= last_key_u)]
        gate = max((arrival_u[u] for u in before), default=-math.inf)
        f["in_order_us"] = max(f["complete_us"], gate)
    return out, repair


def pct(xs, p):
    if not xs:
        return float("nan")
    xs = sorted(xs)
    return xs[max(0, math.ceil(p / 100.0 * len(xs)) - 1)]


def per_second(frames, duration):
    sent = [0] * duration
    comp = [0] * duration
    rel = [0] * duration
    for f in frames:
        s = int(f["send_ms"] // 1000)
        if 0 <= s < duration:
            sent[s] += 1
        for key, arr in (("complete_us", comp), ("in_order_us", rel)):
            t = f[key]
            if math.isfinite(t) and 0 <= int(t // 1e6) < duration:
                arr[int(t // 1e6)] += 1
    return sent, comp, rel


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("traces", nargs="+", help="one or two *.trace.csv files (ours, then libwebrtc's)")
    ap.add_argument("--keyframe-bytes", type=int, default=0, help="a frame this large or larger is a keyframe")
    ap.add_argument("--frames", action="store_true", help="also list every frame")
    a = ap.parse_args(argv)
    results = []
    for path in a.traces[:2]:
        frames, repair = rebuild(load(path), a.keyframe_bytes)
        results.append((path, frames, repair))
    duration = 1 + int(max(f["send_ms"] for _, fr, _ in results for f in fr) // 1000)
    print("# frames: sent / complete / released in order, per second")
    cols = [per_second(fr, duration) for _, fr, _ in results]
    print("t_s  " + "  ".join("%-20s" % p.split("/")[-1][:20] for p, _, _ in results))
    for t in range(duration):
        print("%3d  " % (t + 1) + "  ".join("%4d %4d %4d          " % (c[0][t], c[1][t], c[2][t]) for c in cols))
    for (path, frames, repair), c in zip(results, cols):
        print("\n%s: %d frames sent, %d complete, %d released in order; packet repair p50/p90/p99/max %.0f/%.0f/%.0f/%.0f ms"
              % (path, len(frames), sum(c[1]), sum(c[2]), pct(repair, 50), pct(repair, 90), pct(repair, 99),
                 max(repair) if repair else float("nan")))
        late = [f for f in frames if math.isfinite(f["in_order_us"]) and f["in_order_us"] - f["complete_us"] > 0]
        print("  frames held behind an earlier hole: %d; their extra wait p50/max %.0f/%.0f ms"
              % (len(late), pct([(f["in_order_us"] - f["complete_us"]) / 1000 for f in late], 50),
                 max([(f["in_order_us"] - f["complete_us"]) / 1000 for f in late], default=float("nan"))))
        if a.frames:
            for f in frames:
                print("  ts=%d seq=%d..%d n=%d bytes=%d key=%d send=%.0f complete=%s in_order=%s max_repair=%s" % (
                    f["ts"], f["first_u"], f["last_u"], f["packets"], f["bytes"], f["key"], f["send_ms"],
                    "%.0f" % (f["complete_us"] / 1000) if math.isfinite(f["complete_us"]) else "-",
                    "%.0f" % (f["in_order_us"] / 1000) if math.isfinite(f["in_order_us"]) else "-",
                    "%.0f" % f["max_repair_ms"] if math.isfinite(f["max_repair_ms"]) else "-"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
