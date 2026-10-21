#!/usr/bin/env python3
"""Summarise and compare the per-frame receive traces (issue #230).

`sm_compare` records every rendered video frame of a traced scenario's first
seed, in the same CSV for this library and for libwebrtc:
rtp_ts,capture_ms,complete_ms,render_ms,render_time_ms (ms from call start;
complete is the arrival of the frame's last media or RTX packet before it
rendered, render_time is the render time the timing model gave it). CI
publishes them in compare-webrtc.json under "traces" ("sm_frames",
"webrtc_frames"); with --trace-dir they are also written as
<scenario>.<seed>.{sm,webrtc}.frames.csv, and `sm_testbench --link-trace=DIR`
writes ours.

For each stack it shows the freezes (video::FreezeDetector's rule over the
render times), how long the receiver held a complete frame (render - complete),
how late the frame rendered against its render time (render - render_time),
and capture to render, then each freeze with the frame that ended it:

  tools/frame_trace.py compare-webrtc.json [--scenario NAME] [--from S] [--to S]
  tools/frame_trace.py sm.frames.csv webrtc.frames.csv [--from S] [--to S]
"""
import argparse
import csv
import io
import json
import math
import sys


def parse(text):
    """Rows of a frames CSV as dicts; a missing time is None."""
    rows = []
    for r in csv.DictReader(io.StringIO(text)):
        row = {"rtp_ts": int(r["rtp_ts"])}
        for k in ("capture_ms", "complete_ms", "render_ms", "render_time_ms"):
            row[k] = float(r[k]) if r[k] != "" else None
        rows.append(row)
    return rows


def percentile(xs, pct):
    """Nearest-rank percentile."""
    if not xs:
        return None
    xs = sorted(xs)
    rank = max(1, math.ceil(pct / 100.0 * len(xs) - 1e-9))
    return xs[rank - 1]


def freezes(rows):
    """FreezeDetector::OnFrameRendered over the render times: (index, gap) of each freeze."""
    out, recent, last = [], [], None
    for i, r in enumerate(rows):
        t = r["render_ms"]
        if last is None:
            last = t
            continue
        d, last = t - last, t
        if d >= 5000:
            continue  # A pause.
        if len(recent) >= 5:
            avg = sum(recent) / len(recent)
            if d >= max(3 * avg, avg + 150):
                out.append((i, d))
                continue
        recent.append(d)
        if len(recent) > 30:
            recent.pop(0)
    return out


def window(rows, first, last):
    return [r for r in rows if (first is None or r["render_ms"] >= first * 1000) and
            (last is None or r["render_ms"] < (last + 1) * 1000)]


def stats(rows):
    def diff(a, b):
        return [r[a] - r[b] for r in rows if r[a] is not None and r[b] is not None]
    hold, late, e2e = diff("render_ms", "complete_ms"), diff("render_ms", "render_time_ms"), diff("render_ms", "capture_ms")
    fz = freezes(rows)
    out = {"frames rendered": len(rows), "freezes": len(fz), "freeze total ms": sum(d for _, d in fz)}
    for name, xs in (("held after complete", hold), ("render - render_time", late), ("capture to render", e2e)):
        for p in (5, 50, 95):
            out["%s p%d ms" % (name, p)] = percentile(xs, p)
    return out, fz


def fmt(v):
    if v is None:
        return "-"
    return "%.0f" % v if isinstance(v, float) else str(v)


def side_by_side(sm, rtc, first=None, last=None):
    sm, rtc = window(sm, first, last), window(rtc, first, last)
    (a, fa), (b, fb) = stats(sm), stats(rtc)
    lines = ["%-34s %10s %10s" % ("", "sm", "webrtc")]
    for k in a:
        lines.append("%-34s %10s %10s" % (k, fmt(a[k]), fmt(b[k])))
    for name, rows, fz in (("sm", sm, fa), ("webrtc", rtc, fb)):
        lines.append("")
        lines.append("%s freezes: render_ms gap_ms | frame that ended it: capture complete render render_time" % name)
        for i, d in fz:
            r = rows[i]
            lines.append("  %9.0f %6.0f | %9s %9s %9s %9s" % (r["render_ms"], d, fmt(r["capture_ms"]), fmt(r["complete_ms"]),
                                                          fmt(r["render_ms"]), fmt(r["render_time_ms"])))
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="compare-webrtc.json, or sm.frames.csv and webrtc.frames.csv")
    ap.add_argument("--scenario", help="scenario in the JSON (default: the first with a frame trace)")
    ap.add_argument("--from", dest="first", type=int, help="first second")
    ap.add_argument("--to", dest="last", type=int, help="last second")
    args = ap.parse_args(argv)
    if len(args.inputs) == 2:
        sm_text, rtc_text = (open(p, encoding="utf-8").read() for p in args.inputs)
        title = None
    else:
        with open(args.inputs[0], encoding="utf-8") as f:
            traces = [t for t in json.load(f).get("traces", []) if "sm_frames" in t]
        if args.scenario:
            traces = [t for t in traces if t["scenario"] == args.scenario]
        if not traces:
            sys.exit("no frame trace in %s%s" % (args.inputs[0], " for " + args.scenario if args.scenario else ""))
        t = traces[0]
        sm_text, rtc_text, title = t["sm_frames"], t["webrtc_frames"], "%s seed %s" % (t["scenario"], t["seed"])
    if title:
        print(title)
    print(side_by_side(parse(sm_text), parse(rtc_text), args.first, args.last))


if __name__ == "__main__":
    main()
