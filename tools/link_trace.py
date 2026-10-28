#!/usr/bin/env python3
"""Summarise and compare the forward-link packet traces (issue #125).

`sm_compare` records every packet either stack offers to the forward link,
for the first seed of the scenarios named by --trace (default
very_low_capacity_allocator_v2), in the same CSV for this library and for
libwebrtc: time_us,size,kind,seq,queue_bytes,outcome,arrival_us,rtp_ts,marker,osn,key
(the last five from issue #437; tools/link_frames.py rebuilds frames from them). CI publishes them in
compare-webrtc.json under "traces"; with --trace-dir they are also written as
<scenario>.<seed>.{sm,webrtc}.trace.csv.

For each kind (audio, video, rtx, padding, fec, other) the summary shows the
offered rate, the wire size (p50, p90, max and a histogram), the gap between
consecutive sends, and the drops by cause, so two stacks' traces can be read
side by side:

  tools/link_trace.py compare-webrtc.json [--scenario NAME]
  tools/link_trace.py sm.trace.csv webrtc.trace.csv [--duration SECONDS]
  tools/link_trace.py compare-webrtc.json --csv-dir DIR   # extract the CSVs

Rates divide by --duration (default: the time of the last packet).
"""
import argparse
import csv
import io
import json
import math
import os
import sys

KINDS = ["audio", "video", "rtx", "padding", "fec", "other"]
SIZE_BINS = [100, 150, 200, 250, 300, 500, 800, 1100, 1300]  # upper bounds; the last bucket is open
FIELDS = ["time_us", "size", "kind", "seq", "queue_bytes", "outcome", "arrival_us", "rtp_ts", "marker", "osn", "key"]


def parse(text):
    """Rows of a trace CSV as dicts with int fields."""
    rows = []
    for r in csv.DictReader(io.StringIO(text)):
        rows.append({
            "time_us": int(r["time_us"]), "size": int(r["size"]), "kind": r["kind"],
            "seq": int(r["seq"]), "queue_bytes": int(r["queue_bytes"]), "outcome": r["outcome"],
        })
    return rows


def percentile(xs, pct):
    """Nearest-rank percentile, as the test bench computes sizes."""
    if not xs:
        return 0
    xs = sorted(xs)
    rank = max(1, math.ceil(pct / 100.0 * len(xs) - 1e-9))
    return xs[rank - 1]


def size_histogram(sizes):
    counts = [0] * (len(SIZE_BINS) + 1)
    for s in sizes:
        for i, hi in enumerate(SIZE_BINS):
            if s <= hi:
                counts[i] += 1
                break
        else:
            counts[-1] += 1
    labels = ["<=%d" % hi for hi in SIZE_BINS] + [">%d" % SIZE_BINS[-1]]
    return dict(zip(labels, counts))


def summarize(rows, duration_s=None):
    """Per-kind summary of a trace, plus the totals."""
    if duration_s is None:
        duration_s = max((r["time_us"] for r in rows), default=0) / 1e6
    duration_s = max(duration_s, 1e-3)
    out = {"duration_s": duration_s, "packets": len(rows), "kinds": {}}
    for kind in KINDS:
        k = [r for r in rows if r["kind"] == kind]
        if not k:
            continue
        sizes = [r["size"] for r in k]
        times = [r["time_us"] for r in k]
        gaps = [(b - a) / 1000.0 for a, b in zip(times, times[1:])]
        drops = {o: sum(1 for r in k if r["outcome"] == o) for o in ("queue_drop", "lost", "down")}
        out["kinds"][kind] = {
            "packets": len(k),
            "offered_kbps": sum(sizes) * 8 / 1000.0 / duration_s,
            "size_p50": percentile(sizes, 50), "size_p90": percentile(sizes, 90), "size_max": max(sizes),
            "size_hist": size_histogram(sizes),
            "gap_ms_p50": percentile(gaps, 50), "gap_ms_p90": percentile(gaps, 90), "gap_ms_max": max(gaps, default=0),
            "queue_bytes_mean_at_arrival": sum(r["queue_bytes"] for r in k) / len(k),
            "drops": drops,
            "drop_pct": 100.0 * sum(drops.values()) / len(k),
        }
    total_drops = sum(sum(v["drops"].values()) for v in out["kinds"].values())
    for v in out["kinds"].values():
        # Share of all drops that landed on this kind.
        v["share_of_drops_pct"] = 100.0 * sum(v["drops"].values()) / total_drops if total_drops else 0.0
    out["drops"] = total_drops
    return out


def fmt(v):
    if isinstance(v, float):
        return "%.1f" % v
    return str(v)


def render(named):
    """Text table of {name: summary}, one column per stack."""
    names = list(named)
    kinds = [k for k in KINDS if any(k in named[n]["kinds"] for n in names)]
    lines = ["%-34s" % "" + "".join("%14s" % n for n in names)]
    lines.append("%-34s" % "packets offered" + "".join("%14s" % named[n]["packets"] for n in names))
    lines.append("%-34s" % "link drops" + "".join("%14s" % named[n]["drops"] for n in names))
    rows = [("packets", "packets"), ("offered kbps", "offered_kbps"), ("size p50 B", "size_p50"),
            ("size p90 B", "size_p90"), ("size max B", "size_max"), ("send gap p50 ms", "gap_ms_p50"),
            ("send gap p90 ms", "gap_ms_p90"), ("send gap max ms", "gap_ms_max"),
            ("queue bytes at arrival (mean)", "queue_bytes_mean_at_arrival"),
            ("queue drops", ("drops", "queue_drop")), ("random/burst losses", ("drops", "lost")),
            ("drop %", "drop_pct"), ("share of all drops %", "share_of_drops_pct")]
    for kind in kinds:
        lines.append("")
        lines.append(kind)
        for label, key in rows:
            vals = []
            for n in names:
                k = named[n]["kinds"].get(kind)
                if k is None:
                    vals.append("-")
                elif isinstance(key, tuple):
                    vals.append(fmt(k[key[0]][key[1]]))
                else:
                    vals.append(fmt(k[key]))
            lines.append("  %-32s" % label + "".join("%14s" % v for v in vals))
        bins = list(next(named[n]["kinds"][kind] for n in names if kind in named[n]["kinds"])["size_hist"])
        lines.append("  size histogram (packets)")
        for b in bins:
            vals = [fmt(named[n]["kinds"][kind]["size_hist"][b]) if kind in named[n]["kinds"] else "-" for n in names]
            lines.append("    %-30s" % b + "".join("%14s" % v for v in vals))
    return "\n".join(lines)


def traces_in(data, scenario=None):
    """The traces entries of a compare-webrtc.json, optionally for one scenario."""
    found = [t for t in data.get("traces", []) if scenario in (None, t["scenario"])]
    if not found:
        raise SystemExit("no traces%s in the JSON (sm_compare --trace=NAME records them)" %
                         (" for " + scenario if scenario else ""))
    return found


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="compare-webrtc.json, or one or two trace CSVs (sm, then webrtc)")
    ap.add_argument("--scenario", help="only this scenario's trace (JSON input)")
    ap.add_argument("--duration", type=float, help="seconds to divide rates by (default: last packet time)")
    ap.add_argument("--csv-dir", help="write the JSON's traces as CSVs to this directory and exit")
    ap.add_argument("--json", dest="json_out", help="also write the summaries as JSON")
    a = ap.parse_args(argv)

    sets = []  # (title, {name: rows})
    if a.inputs[0].endswith(".json"):
        with open(a.inputs[0]) as f:
            data = json.load(f)
        for t in traces_in(data, a.scenario):
            if a.csv_dir:
                os.makedirs(a.csv_dir, exist_ok=True)
                for who in ("sm", "webrtc"):
                    path = os.path.join(a.csv_dir, "%s.%d.%s.trace.csv" % (t["scenario"], t["seed"], who))
                    with open(path, "w") as f:
                        f.write(t[who])
                    print("wrote", path)
            sets.append(("%s, seed %d" % (t["scenario"], t["seed"]),
                         {"sm": parse(t["sm"]), "webrtc": parse(t["webrtc"])}))
        if a.csv_dir:
            return 0
    else:
        named = {}
        for path, name in zip(a.inputs, ("sm", "webrtc")):
            with open(path) as f:
                named[name if len(a.inputs) > 1 else os.path.basename(path)] = parse(f.read())
        sets.append((", ".join(a.inputs), named))

    result = {}
    for title, named in sets:
        summaries = {n: summarize(rows, a.duration) for n, rows in named.items()}
        print(title)
        print(render(summaries))
        print()
        result[title] = summaries
    if a.json_out:
        with open(a.json_out, "w") as f:
            json.dump(result, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
