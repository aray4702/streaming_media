#!/usr/bin/env python3
"""Merge sharded sm_compare results into one, as a single run would write them.

CI runs the comparison in N shards (.github/workflows/ci.yml, compare-webrtc):
shard k of N runs scenario files k, k + N, k + 2N, ... of the sorted list, so
scenario p of the full run is scenario p // N of shard p % N. This puts the
scenarios back in that order.

  tools/merge_compare.py --shards N --out-json compare-webrtc.json \\
      --out-txt compare-webrtc.txt shard-0 shard-1 ... shard-(N-1)

Each shard directory holds that shard's compare-webrtc.json and
compare-webrtc.txt. The merged JSON has the same keys as sm_compare's; the
merged text has the same header, scenario blocks in order, and the "better:"
line summed over the shards. Shards must agree on the libwebrtc version, the
seeds and the shown table.
"""
import argparse
import json
import os
import re
import sys

BETTER = re.compile(r"^better: sm (\d+), webrtc (\d+), tie (\d+) \(of the metrics shown\)$", re.M)


def interleave(parts):
    """parts[k] = shard k's items; returns them in the full run's order."""
    out, n = [], len(parts)
    total = sum(len(p) for p in parts)
    for pos in range(total):
        k, i = pos % n, pos // n
        if i >= len(parts[k]):
            raise SystemExit("shard %d has %d scenarios, expected more for round-robin order" % (k, len(parts[k])))
        out.append(parts[k][i])
    return out


def split_txt(text):
    """(header line, [scenario blocks], (sm, webrtc, tie), tail from 'better:')."""
    header, _, rest = text.partition("\n")
    m = BETTER.search(rest)
    if not m:
        raise SystemExit("no 'better:' line in a shard's text output")
    body, tail = rest[:m.start()], rest[m.start():]
    blocks = [b.strip("\n") for b in body.split("\n\n") if b.strip()]
    return header, blocks, tuple(int(x) for x in m.groups()), tail


def merge(jsons, txts):
    first = jsons[0]
    for j in jsons[1:]:
        for k in ("webrtc_version", "seeds", "shown"):
            if j.get(k) != first.get(k):
                raise SystemExit("shards disagree on %r" % k)
    merged = {"webrtc_version": first["webrtc_version"], "seeds": first["seeds"], "shown": first["shown"],
              "scenarios": interleave([j["scenarios"] for j in jsons])}
    traces = [t for j in jsons for t in j.get("traces", [])]
    if traces:
        merged["traces"] = traces
    parts = [split_txt(t) for t in txts]
    for (_, blocks, _, _), j in zip(parts, jsons):
        if len(blocks) != len(j["scenarios"]):
            raise SystemExit("a shard's text has %d scenario blocks for %d scenarios" % (len(blocks), len(j["scenarios"])))
    blocks = interleave([p[1] for p in parts])
    wins = [sum(p[2][i] for p in parts) for i in range(3)]
    tail = BETTER.sub("better: sm %d, webrtc %d, tie %d (of the metrics shown)" % tuple(wins), parts[0][3], count=1)
    text = parts[0][0] + "\n" + "".join("\n" + b + "\n" for b in blocks) + "\n" + tail
    return merged, text


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("shard_dirs", nargs="+")
    ap.add_argument("--shards", type=int, required=True)
    ap.add_argument("--out-json", required=True)
    ap.add_argument("--out-txt", required=True)
    a = ap.parse_args(argv)
    if len(a.shard_dirs) != a.shards:
        raise SystemExit("expected %d shard directories, got %d" % (a.shards, len(a.shard_dirs)))
    jsons, txts = [], []
    for d in a.shard_dirs:
        with open(os.path.join(d, "compare-webrtc.json")) as f:
            jsons.append(json.load(f))
        with open(os.path.join(d, "compare-webrtc.txt")) as f:
            txts.append(f.read())
    merged, text = merge(jsons, txts)
    with open(a.out_json, "w") as f:
        json.dump(merged, f, indent=1)
        f.write("\n")
    with open(a.out_txt, "w") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
