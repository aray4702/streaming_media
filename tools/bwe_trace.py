#!/usr/bin/env python3
"""Lay the GoogCC estimates of this library and of libwebrtc side by side (issue #149).

`sm_compare` records, for the first seed of the scenarios named by --trace (and
of every scenario with --trace-all-dir), the sender's bandwidth estimates in the
same CSV for both stacks:

  time_us,event,bitrate_bps,detector,fraction_loss,packets,loss_state,
  loss_limit_bps,instant_ub_bps

  delay_based  libwebrtc's RtcEventBweUpdateDelayBased (delay_based_bwe.cc): the
               AIMD estimate and the trendline detector state, logged when either
               changes.
  loss_based   RtcEventBweUpdateLossBased (send_side_bandwidth_estimation.cc): the
               send-side target (the minimum of the delay-based estimate, the
               loss-based estimate and the other limits), the loss fraction (0-255)
               and the packets of the report, logged when the target or the
               fraction changes, and at least every 5 s.
  target       the target the call reports (libwebrtc: Call::Stats::send_bandwidth_bps
               every 100 ms; this library: GccController's target), when it changes.

loss_state, loss_limit_bps and instant_ub_bps are LossBasedBweV2's state and are
filled for this library only: the prebuilt libwebrtc has no event or public
accessor for them. CI publishes the traces in compare-webrtc.json under
"traces" ("sm_bwe", "webrtc_bwe") and, on main, as
<scenario>.<seed>.{sm,webrtc}.bwe.csv in parity-data/traces/.

Per second, the table shows the last value of each stream and which one the
target is held by: "delay" when the send-side target is within 1 kbps of the
delay-based estimate (the loss-based estimator and the other limits do not bind),
"loss" when it is below it.

  tools/bwe_trace.py compare-webrtc.json [--scenario NAME] [--from S] [--to S]
  tools/bwe_trace.py sm.bwe.csv webrtc.bwe.csv [--from S] [--to S]
"""
import argparse
import csv
import io
import json
import sys

TOLERANCE_BPS = 1000


def _num(v):
    return int(v) if v != "" else None


def parse(text):
    """Rows of a BWE trace CSV as dicts; empty columns are None."""
    rows = []
    for r in csv.DictReader(io.StringIO(text)):
        rows.append({
            "time_us": int(r["time_us"]), "event": r["event"], "bitrate_bps": int(r["bitrate_bps"]),
            "detector": r["detector"] or None, "fraction_loss": _num(r["fraction_loss"]),
            "packets": _num(r["packets"]), "loss_state": r["loss_state"] or None,
            "loss_limit_bps": _num(r["loss_limit_bps"]), "instant_ub_bps": _num(r["instant_ub_bps"]),
        })
    return rows


def per_second(rows, first_s, last_s):
    """One dict per second: the last value of each event at or before the end of that second."""
    out = []
    i = 0
    latest = {}
    rows = sorted(rows, key=lambda r: r["time_us"])
    for sec in range(first_s, last_s + 1):
        end_us = (sec + 1) * 1000000
        while i < len(rows) and rows[i]["time_us"] < end_us:
            latest[rows[i]["event"]] = rows[i]
            i += 1
        d, l, t = latest.get("delay_based"), latest.get("loss_based"), latest.get("target")
        row = {"t_s": sec, "delay_bps": d["bitrate_bps"] if d else None, "detector": d["detector"] if d else None,
               "loss_bps": l["bitrate_bps"] if l else None, "fraction_loss": l["fraction_loss"] if l else None,
               "loss_state": l["loss_state"] if l else None, "loss_limit_bps": l["loss_limit_bps"] if l else None,
               "target_bps": t["bitrate_bps"] if t else None}
        row["bound"] = bound(row)
        out.append(row)
    return out


def bound(row):
    """"delay" or "loss": what holds the send-side target; None when a stream is missing."""
    if row["delay_bps"] is None or row["loss_bps"] is None:
        return None
    return "delay" if row["loss_bps"] >= row["delay_bps"] - TOLERANCE_BPS else "loss"


def _k(v):
    return "-" if v is None else "%d" % round(v / 1000.0)


def _cell(r):
    det = {"normal": "N", "underusing": "U", "overusing": "O"}.get(r["detector"], "-")
    state = r["loss_state"]
    state = {"increasing": "inc", "increase_using_padding": "pad", "decreasing": "dec",
             "delay_based_estimate": "dly"}.get(state, "") if state else ""
    return "%6s %s %6s %3s %-4s %6s %-5s" % (_k(r["delay_bps"]), det, _k(r["loss_bps"]),
                                             "-" if r["fraction_loss"] is None else r["fraction_loss"], state,
                                             _k(r["target_bps"]), r["bound"] or "-")


def side_by_side(sm_rows, rtc_rows, first_s=None, last_s=None):
    end = max([r["time_us"] for r in sm_rows + rtc_rows] or [0]) // 1000000
    first_s = 0 if first_s is None else first_s
    last_s = end if last_s is None else min(last_s, end)
    a = per_second(sm_rows, first_s, last_s)
    b = per_second(rtc_rows, first_s, last_s)
    head = "%6s %s %6s %3s %-4s %6s %-5s" % ("delay", "d", "loss", "fr", "lbst", "target", "bound")
    lines = ["kbps per second (last value); d = detector N/U/O, fr = fraction lost /256, lbst = LossBasedBweV2 state",
             "%4s | %-40s | %-40s" % ("t", "sm   " + head, "webrtc " + head)]
    for x, y in zip(a, b):
        lines.append("%4d | %-40s | %-40s" % (x["t_s"], _cell(x), _cell(y)))
    for name, rows in (("sm", a), ("webrtc", b)):
        known = [r for r in rows if r["bound"]]
        if known:
            n = sum(1 for r in known if r["bound"] == "delay")
            lines.append("%-7s target held by the delay-based estimate in %d of %d seconds, by the loss side in %d"
                         % (name, n, len(known), len(known) - n))
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="compare-webrtc.json, or sm.bwe.csv and webrtc.bwe.csv")
    ap.add_argument("--scenario", help="scenario in the JSON (default: the first with a BWE trace)")
    ap.add_argument("--from", dest="first", type=int, help="first second")
    ap.add_argument("--to", dest="last", type=int, help="last second")
    args = ap.parse_args(argv)
    if len(args.inputs) == 2:
        sm_text, rtc_text = (open(p, encoding="utf-8").read() for p in args.inputs)
        title = None
    else:
        with open(args.inputs[0], encoding="utf-8") as f:
            traces = [t for t in json.load(f).get("traces", []) if "sm_bwe" in t]
        if args.scenario:
            traces = [t for t in traces if t["scenario"] == args.scenario]
        if not traces:
            sys.exit("no BWE trace in %s%s" % (args.inputs[0], " for " + args.scenario if args.scenario else ""))
        t = traces[0]
        sm_text, rtc_text, title = t["sm_bwe"], t["webrtc_bwe"], "%s seed %s" % (t["scenario"], t["seed"])
    if title:
        print(title)
    print(side_by_side(parse(sm_text), parse(rtc_text), args.first, args.last))


if __name__ == "__main__":
    main()
