#!/usr/bin/env python3
"""Summarise and compare the sender's per-second video decisions (issue #131).

`sm_compare` records, for the first seed of the scenarios named by --trace
(default very_low_capacity_allocator_v2), one row per simulated second of the
video send path, in the same CSV for this library and for libwebrtc:

  t_s,encoder_target_bps,alloc_target_bps,captured,encoded,dropped_capturer,
  dropped_encoder_queue,dropped_rate_limiter,dropped_congestion_window,
  dropped_encoder,paused,encoded_bytes,pacer_queue_ms,qp_mean,encode_ms_mean,
  encode_width,encode_height,keyframes,keyframe_bytes,rendered,max_render_gap_ms,
  height_mean,qs_checks,qs_qp_high,qs_qp_low,qs_drop_pct,qs_signal

The drop columns are libwebrtc's VideoSendStream::Stats reasons. This library
maps its drop points onto them (frame dropper -> dropped_rate_limiter,
congestion-window pushback -> dropped_congestion_window, paused encoder ->
dropped_encoder_queue) and has no counterpart for dropped_capturer, the
DropDueToSize path or dropped_encoder, which stay 0. qp_mean and
encode_ms_mean (issue #84) come from the encoder model both stacks share
(testbench/encoder_model.h); traces written before it lack them and read as
0. encode_width/encode_height (the last frame encoded in the second, 0 when
none was), keyframes, keyframe_bytes, rendered and max_render_gap_ms (the
largest gap from the previous rendered frame to one rendered in the second)
line up resolution switches and keyframes with freezes (issue #333); older
traces read them as 0. height_mean is the mean encoded frame height
(quality scaling, #84); traces written before it read it as 0. The qs_*
columns (issue #347) are the quality
scaler's checks that observed enough frames, as libwebrtc's QualityScaler::
CheckQp logs them: how many fell in the second, and the last one's smoothed
QP (high and low filter, as int), frame-drop percent when that reported high
QP, and signal (-1 adapt down, +1 adapt up); -1 where there was none, and
older traces read them so. CI publishes the traces
in compare-webrtc.json under "traces" ("sm_video", "webrtc_video"); with
--trace-dir they are also written as <scenario>.<seed>.{sm,webrtc}.video.csv.

  tools/video_trace.py compare-webrtc.json [--scenario NAME] [--series]
  tools/video_trace.py sm.video.csv webrtc.video.csv
"""
import argparse
import csv
import io
import json
import os
import sys

DROPS = ["dropped_capturer", "dropped_encoder_queue", "dropped_rate_limiter",
         "dropped_congestion_window", "dropped_encoder"]
LATER_INTS = ["encode_width", "encode_height", "keyframes", "keyframe_bytes", "rendered"]  # Issue #333.
QS_INTS = ["qs_qp_high", "qs_qp_low", "qs_drop_pct"]  # Issue #347; -1 when absent.
INTS = ["t_s", "encoder_target_bps", "alloc_target_bps", "captured", "encoded", *DROPS, "paused", "encoded_bytes"]


def parse(text):
    """Rows of a video trace CSV as dicts of numbers."""
    rows = []
    for r in csv.DictReader(io.StringIO(text)):
        row = {k: int(r[k]) for k in INTS}
        row["pacer_queue_ms"] = float(r["pacer_queue_ms"])
        for k in ("qp_mean", "encode_ms_mean", "max_render_gap_ms", "height_mean"):
            row[k] = float(r.get(k) or 0)
        for k in LATER_INTS:
            row[k] = int(r.get(k) or 0)
        row["qs_checks"] = int(r.get("qs_checks") or 0)
        row["qs_signal"] = int(r.get("qs_signal") or 0)
        for k in QS_INTS:
            row[k] = int(r.get(k) or -1)
        rows.append(row)
    return rows


def summarize(rows):
    """Totals over the call, drops by reason, and rates per second."""
    secs = max(len(rows), 1)
    total = {k: sum(r[k] for r in rows) for k in ["captured", "encoded", "encoded_bytes", "keyframes", "keyframe_bytes",
                                                   "rendered", *DROPS]}
    heights = [r["encode_height"] for r in rows if r["encode_height"] > 0]
    dropped = sum(total[k] for k in DROPS)
    return {
        "seconds": len(rows),
        "captured": total["captured"],
        "encoded": total["encoded"],
        "dropped": dropped,
        "drops": {k: total[k] for k in DROPS},
        "encoded_fps": total["encoded"] / secs,
        "encoded_kbps": total["encoded_bytes"] * 8 / 1000.0 / secs,
        "encoder_target_kbps_mean": sum(r["encoder_target_bps"] for r in rows) / 1000.0 / secs,
        "alloc_target_kbps_mean": sum(r["alloc_target_bps"] for r in rows) / 1000.0 / secs,
        "paused_seconds": sum(r["paused"] for r in rows),
        "pacer_queue_ms_max": max((r["pacer_queue_ms"] for r in rows), default=0.0),
        # Means over encoded frames, weighting each second by its frames encoded.
        "qp_mean": sum(r["qp_mean"] * r["encoded"] for r in rows) / max(total["encoded"], 1),
        "encode_ms_mean": sum(r["encode_ms_mean"] * r["encoded"] for r in rows) / max(total["encoded"], 1),
        "height_mean": sum(r["height_mean"] * r["encoded"] for r in rows) / max(total["encoded"], 1),
        "keyframes": total["keyframes"],
        "keyframe_bytes": total["keyframe_bytes"],
        # Seconds whose last encoded height differs from the previous encoded second's.
        "resolution_switches": sum(1 for a, b in zip(heights, heights[1:]) if a != b),
        "rendered": total["rendered"],
        "max_render_gap_ms": max((r["max_render_gap_ms"] for r in rows), default=0.0),
        # Quality scaler (issue #347): its checks, and the seconds whose last check signalled each way.
        "qs_checks": sum(r["qs_checks"] for r in rows),
        "qs_up_seconds": sum(1 for r in rows if r["qs_signal"] > 0),
        "qs_down_seconds": sum(1 for r in rows if r["qs_signal"] < 0),
    }


def fmt(v):
    return "%.1f" % v if isinstance(v, float) else str(v)


def render(named):
    names = list(named)
    lines = ["%-32s" % "" + "".join("%14s" % n for n in names)]
    rows = [("seconds", "seconds"), ("frames captured", "captured"), ("frames encoded", "encoded"),
            ("frames dropped", "dropped"), ("encoded fps", "encoded_fps"), ("encoded kbps", "encoded_kbps"),
            ("encoder target kbps (mean)", "encoder_target_kbps_mean"),
            ("allocator video target kbps (mean)", "alloc_target_kbps_mean"),
            ("paused/suspended seconds", "paused_seconds"), ("pacer queue max ms", "pacer_queue_ms_max"),
            ("QP (mean)", "qp_mean"), ("encode ms (mean)", "encode_ms_mean"), ("keyframes", "keyframes"),
            ("keyframe bytes", "keyframe_bytes"), ("resolution switches", "resolution_switches"),
            ("frames rendered", "rendered"), ("longest render gap ms", "max_render_gap_ms"),
            ("encoded height (mean)", "height_mean"),
            ("quality scaler checks", "qs_checks"), ("scaler adapt-up seconds", "qs_up_seconds"),
            ("scaler adapt-down seconds", "qs_down_seconds")]
    for label, key in rows:
        lines.append("%-32s" % label + "".join("%14s" % fmt(named[n][key]) for n in names))
    lines.append("dropped by reason")
    for k in DROPS:
        lines.append("  %-30s" % k[len("dropped_"):] + "".join("%14s" % named[n]["drops"][k] for n in names))
    return "\n".join(lines)


def series(named_rows):
    """Per-second encoder target, frames encoded, height, keyframes and rendering, one set of columns per stack."""
    names = list(named_rows)
    n = max(len(r) for r in named_rows.values())
    head = "%4s" % "t_s" + "".join("%12s%8s%8s%8s%7s%5s%7s%8s" % (nm + " kbps", "enc", "drop", "pace", "height", "key",
                                                                 "rend", "gap")
                                   for nm in names)
    lines = [head]
    for i in range(n):
        cols = []
        for nm in names:
            rows = named_rows[nm]
            if i < len(rows):
                r = rows[i]
                cols.append("%12.0f%8d%8d%8.0f%7d%5d%7d%8.0f" % (
                    r["encoder_target_bps"] / 1000.0, r["encoded"], sum(r[k] for k in DROPS), r["pacer_queue_ms"],
                    r["encode_height"], r["keyframes"], r["rendered"], r["max_render_gap_ms"]))
            else:
                cols.append("%12s%8s%8s%8s%7s%5s%7s%8s" % (("-",) * 8))
        lines.append("%4d" % (i + 1) + "".join(cols))
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="compare-webrtc.json, or one or two video trace CSVs (sm, then webrtc)")
    ap.add_argument("--scenario", help="only this scenario (JSON input)")
    ap.add_argument("--series", action="store_true", help="also print the per-second series")
    ap.add_argument("--json", dest="json_out", help="also write the summaries as JSON")
    a = ap.parse_args(argv)

    sets = []
    if a.inputs[0].endswith(".json"):
        with open(a.inputs[0]) as f:
            data = json.load(f)
        found = [t for t in data.get("traces", []) if "sm_video" in t and a.scenario in (None, t["scenario"])]
        if not found:
            raise SystemExit("no video traces in the JSON (sm_compare --trace=NAME records them)")
        for t in found:
            sets.append(("%s, seed %d" % (t["scenario"], t["seed"]),
                         {"sm": parse(t["sm_video"]), "webrtc": parse(t["webrtc_video"])}))
    else:
        named = {}
        for path, name in zip(a.inputs, ("sm", "webrtc")):
            with open(path) as f:
                named[name if len(a.inputs) > 1 else os.path.basename(path)] = parse(f.read())
        sets.append((", ".join(a.inputs), named))

    result = {}
    for title, named in sets:
        summaries = {n: summarize(rows) for n, rows in named.items()}
        print(title)
        print(render(summaries))
        if a.series:
            print()
            print(series(named))
        print()
        result[title] = summaries
    if a.json_out:
        with open(a.json_out, "w") as f:
            json.dump(result, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
