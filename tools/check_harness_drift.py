#!/usr/bin/env python3
"""Harness drift check: both stacks must send the same audio where they do today.

Both sides of `sm_compare` encode the same signal with the same libopus and
settings (agents/program.md 0.2, "One test environment for both stacks";
docs/design/harness-fidelity.md). On a link without loss, nothing feeds back
into the encoder differently, so every audio packet must have the same size
on both stacks, in the same order. This reads the seed-1 forward-link traces
that compare-webrtc.json carries (sm_compare's kDefaultTraced) and fails if
any REQUIRED scenario's audio differs, so a harness change that breaks the
match is caught on the PR that makes it.

Scenarios with loss are reported but not required: their audio still parts
at the first loss report, because our receiver sends report blocks more often
than libwebrtc's (#484). Move them to REQUIRED once that is fixed.

It also checks the captured media (harness-fidelity row 1) on EVERY traced
scenario, lossy or not, because capture does not depend on the stack: each
audio frame must have the same RTP timestamp on both sides and the same payload
until the encoders' loss inputs part (tools/audio_send.py, the per-frame send
trace), and each second of video must have the same number of captured frames
(the video trace's `captured` column).

  tools/check_harness_drift.py compare-webrtc.json [--markdown out.md]
"""
import argparse
import csv
import io
import json
import sys

import audio_send

REQUIRED = ["issue_124_clean_link_v2", "issue_124_bufferbloat_v2", "issue_61_googcc_glue_congestive",
            "probe_v2_cross_traffic"]


def audio_sizes(trace_csv):
    """Sizes of the audio packets in the order first sent (retransmissions left out)."""
    seen, out = set(), []
    for r in csv.DictReader(io.StringIO(trace_csv)):
        if r["kind"] == "audio" and r["seq"] not in seen:
            seen.add(r["seq"])
            out.append(int(r["size"]))
    return out


def compare(trace):
    a, b = audio_sizes(trace["sm"]), audio_sizes(trace["webrtc"])
    n = min(len(a), len(b))
    first = next((i for i in range(n) if a[i] != b[i]), None)
    same = sum(a[i] == b[i] for i in range(n))
    ok = len(a) == len(b) and first is None and n > 0
    return {"scenario": trace["scenario"], "seed": trace.get("seed"), "ours": len(a), "webrtc": len(b),
            "same": same, "first_diff": first, "ok": ok}


def captured_per_second(video_csv):
    return [int(r["captured"]) for r in csv.DictReader(io.StringIO(video_csv))]


def check_capture(trace):
    """Row 1: the same captured audio and video on both sides. Returns a list of failures."""
    name, failed = trace["scenario"], []
    if trace.get("sm_audiosend") or trace.get("webrtc_audiosend"):
        if not audio_send.compare(trace.get("sm_audiosend", ""), trace.get("webrtc_audiosend", ""), out=io.StringIO()):
            failed.append("%s: captured audio differs (an audio frame's RTP timestamp, or its payload before the "
                          "loss inputs part; tools/audio_send.py shows where)" % name)
    a, b = captured_per_second(trace.get("sm_video", "")), captured_per_second(trace.get("webrtc_video", ""))
    if any(a) or any(b):
        first = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), None)
        if len(a) != len(b) or first is not None:
            failed.append("%s: captured video differs (%d vs %d frames, first different second %s)" % (
                name, sum(a), sum(b), first if first is not None else min(len(a), len(b)) + 1))
    return failed


def check(data, required=REQUIRED):
    traces = {t["scenario"]: t for t in data.get("traces", [])}
    rows, failed = [], []
    for name in required:
        if name not in traces:
            failed.append("%s: no trace in the JSON (is it still in sm_compare's kDefaultTraced?)" % name)
            continue
        r = dict(compare(traces[name]), required=True)
        rows.append(r)
        if not r["ok"]:
            failed.append("%s: audio differs (%d vs %d packets, %d same, first difference at packet %s)" % (
                name, r["ours"], r["webrtc"], r["same"], r["first_diff"]))
    for name, t in sorted(traces.items()):
        if name not in required:
            rows.append(dict(compare(t), required=False))
    for name, t in sorted(traces.items()):
        failed += check_capture(t)
    return rows, failed


def markdown(rows, failed):
    lines = ["## Harness drift check (audio packet sizes, seed 1)", "",
             "| scenario | required | audio packets ours / libwebrtc | same size | first difference |",
             "|---|---|---|---|---|"]
    for r in rows:
        lines.append("| %s | %s | %d / %d | %d | %s |" % (r["scenario"], "yes" if r["required"] else "no (#484)",
                                                     r["ours"], r["webrtc"], r["same"],
                                                     "none" if r["first_diff"] is None else "packet %d" % r["first_diff"]))
    lines += ["", "**FAILED:** " + "; ".join(failed) if failed else
              "All required scenarios match packet for packet, and every traced scenario has the same captured "
              "audio and video on both sides.", ""]
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("compare_json")
    ap.add_argument("--markdown")
    a = ap.parse_args(argv)
    with open(a.compare_json) as f:
        rows, failed = check(json.load(f))
    md = markdown(rows, failed)
    if a.markdown:
        with open(a.markdown, "w") as f:
            f.write(md + "\n")
    print(md)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
