#!/usr/bin/env python3
"""Lay the repair and keyframe traces of this library and of libwebrtc side by side (issue #151).

`sm_compare` records, for the first seed of the scenarios named by --trace (and of every
scenario with --trace-all-dir), one row per 100 ms in the same CSV for both stacks:

  t_ms,rtx_packets,key_frames_encoded,nack_requests,unique_nack_requests,plis,
  key_frames_decoded,frames_rendered

Counts are for the 100 ms ending at t_ms. libwebrtc's side is read from
VideoSendStream::Stats (rtp_stats.retransmitted.packets, frame_counts.key_frames) and
VideoReceiveStreamInterface::Stats (rtcp_packet_type_counts.nack_requests / unique_nack_requests /
pli+fir, key_frames_decoded, frames_rendered). Here: RtpSender::stats().rtx_packets, the
NackTracker's nack_requests and missing_detected, the KeyframeRequester's pli+fir, and the
test bench's own keyframe and render counts. CI publishes the traces in compare-webrtc.json
under "traces" ("sm_repair", "webrtc_repair") and, on main, as
<scenario>.<seed>.{sm,webrtc}.repair.csv in parity-data/traces/.

The table groups the rows into steps (default 500 ms) and shows ours / libwebrtc per column,
with a total over the window at the end.

  tools/repair_trace.py compare-webrtc.json [--scenario NAME] [--from S] [--to S] [--step MS]
  tools/repair_trace.py sm.repair.csv webrtc.repair.csv [--from S] [--to S] [--step MS]
"""
import argparse
import csv
import io
import json
import sys

COLUMNS = ["rtx_packets", "key_frames_encoded", "nack_requests", "unique_nack_requests", "plis",
           "key_frames_decoded", "frames_rendered"]


def parse(text):
    """Rows of a repair trace CSV as dicts of ints."""
    return [{k: int(v) for k, v in r.items()} for r in csv.DictReader(io.StringIO(text))]


def group(rows, lo_ms, hi_ms, step_ms):
    """Sum the rows into steps; returns {step_end_ms: {column: total}}."""
    out = {}
    for r in rows:
        if r["t_ms"] <= lo_ms or r["t_ms"] > hi_ms:
            continue
        end = ((r["t_ms"] - lo_ms + step_ms - 1) // step_ms) * step_ms + lo_ms
        acc = out.setdefault(end, {c: 0 for c in COLUMNS})
        for c in COLUMNS:
            acc[c] += r[c]
    return out


def render(sm_rows, rtc_rows, lo_ms, hi_ms, step_ms):
    a, b = group(sm_rows, lo_ms, hi_ms, step_ms), group(rtc_rows, lo_ms, hi_ms, step_ms)
    lines = ["t_s    " + "  ".join(f"{c[:14]:>14}" for c in COLUMNS)]
    total = {c: [0, 0] for c in COLUMNS}
    for end in sorted(set(a) | set(b)):
        x, y = a.get(end, {}), b.get(end, {})
        cells = []
        for c in COLUMNS:
            total[c][0] += x.get(c, 0)
            total[c][1] += y.get(c, 0)
            cells.append(f"{x.get(c, 0):>6} /{y.get(c, 0):>6}")
        if any(x.get(c, 0) or y.get(c, 0) for c in COLUMNS):
            lines.append(f"{end / 1000:<6.1f} " + "  ".join(cells))
    lines.append("total  " + "  ".join(f"{total[c][0]:>6} /{total[c][1]:>6}" for c in COLUMNS))
    lines.append("(ours / libwebrtc; steps with no activity on either side are omitted)")
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--scenario")
    ap.add_argument("--from", dest="lo", type=float, default=0.0)
    ap.add_argument("--to", dest="hi", type=float, default=1e9)
    ap.add_argument("--step", type=int, default=500)
    args = ap.parse_args(argv)
    if len(args.files) == 1:
        with open(args.files[0]) as f:
            traces = json.load(f).get("traces", [])
        picked = [t for t in traces if args.scenario in (None, t["scenario"]) and "sm_repair" in t]
        if not picked:
            sys.exit("no repair trace in " + args.files[0])
        t = picked[0]
        sm_text, rtc_text = t["sm_repair"], t["webrtc_repair"]
        print(f"# {t['scenario']} seed {t['seed']}")
    else:
        with open(args.files[0]) as f1, open(args.files[1]) as f2:
            sm_text, rtc_text = f1.read(), f2.read()
    print(render(parse(sm_text), parse(rtc_text), int(args.lo * 1000), int(min(args.hi, 1e6) * 1000), args.step))


if __name__ == "__main__":
    main()
