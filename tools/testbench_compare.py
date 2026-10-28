#!/usr/bin/env python3
"""Compares test bench results against the baseline and flags regressions.

    testbench_compare.py BASELINE RESULTS.json [--markdown] [--update]
    testbench_compare.py --split BASELINE.json DIR

RESULTS is `sm_testbench --json` output (an array of scenario reports).
BASELINE is either such a file (`testbench/baseline.json`) or a directory
with one report per scenario, `<scenario>.json` (`testbench/baseline/`,
issue #498: a PR that adds a scenario or regenerates the baseline then
touches only its own scenarios' files, instead of putting every other open
PR in conflict on one shared file). Both forms load to the same reports.
A metric regresses when it moves in its "worse" direction by more than
max(relative tolerance x baseline, absolute floor). Exit status: 0 = no
regressions, 1 = regressions, 2 = usage error. `--update` rewrites BASELINE
with RESULTS (metrics only) after printing the comparison; in a directory it
rewrites each scenario's file and removes the files of scenarios not in
RESULTS, so files whose numbers did not change stay byte-identical.

While both `testbench/baseline.json` and `testbench/baseline/` exist (until
CI reads the directory), `--update` on either one writes both, and loading
one warns if the other holds different reports. `--split` writes the file's
reports to DIR as per-scenario files, the same as `--update` would.

The canonical baseline is produced on Linux x86_64 with GCC 13 (CI job
`testbench` and the cloud agents); other platforms may differ slightly.
"""
import json
import os
import sys

# Direction: +1 = higher is better, -1 = lower is better, 0 = informational.
HIGHER = {
    "video.frames_rendered", "video.render_fps", "video.render_ratio_pct", "video.avg_kbps",
    "cc.target_avg_kbps", "cc.goodput_kbps", "cc.utilization_pct",
}
INFO = {
    "net.loss_pct", "repair.nack_recovered", "repair.fec_recovered", "video.keyframes",
    "cc.rtt_p50_ms",
    # Per kind on the forward link (issue #117): information only.
    "net.audio_loss_pct", "net.video_loss_pct", "net.rtx_loss_pct", "net.padding_loss_pct",
    "net.audio_kbps", "net.video_kbps", "net.rtx_kbps", "net.padding_kbps", "net.fec_kbps",
    # Per kind packet sizes and queue drops (issue #119): information only.
    *(f"net.{k}_{m}" for k in ("audio", "video", "rtx", "padding")
      for m in ("size_p50_b", "size_p90_b", "size_max_b", "queue_drops")),
}
REL_TOL = 0.05


def direction(metric):
    if metric in INFO:
        return 0
    return 1 if metric in HIGHER else -1


def floor(metric):
    if metric.endswith("_ms"):
        return 20.0
    if metric.endswith("_pct"):
        return 1.0
    if metric.endswith("_kbps"):
        return 20.0
    if metric.endswith("_fps"):
        return 0.5
    return 1.0  # Counts.


def load_file(path):
    with open(path) as f:
        data = json.load(f)
    if isinstance(data, dict):
        data = data.get("scenarios", [data])
    return {r["scenario"]: r for r in data}


def load(path):
    """Reports by scenario, from a results/baseline file or a per-scenario directory."""
    if not os.path.isdir(path):
        return load_file(path)
    reports = {}
    for name in sorted(os.listdir(path)):
        if name.endswith(".json"):
            reports.update(load_file(os.path.join(path, name)))
    return reports


def sibling(path):
    """The other form of a baseline: `x/baseline.json` <-> `x/baseline/`, if it exists."""
    path = path.rstrip("/")
    other = path + ".json" if os.path.isdir(path) else (path[:-5] if path.endswith(".json") else None)
    return other if other and os.path.exists(other) and os.path.isdir(other) != os.path.isdir(path) else None


def baseline_entry(name, report):
    return {"scenario": name, "description": report.get("description", ""),
            "passed": report.get("passed", True), "metrics": report["metrics"]}


def dump(obj):
    return json.dumps(obj, indent=1, sort_keys=True) + "\n"


def write_baseline(path, reports):
    """Writes reports as a baseline file or, if `path` is a directory, one file per scenario."""
    if not os.path.isdir(path):
        with open(path, "w") as f:
            f.write(dump([baseline_entry(k, v) for k, v in sorted(reports.items())]))
        return
    keep = set()
    for name, report in sorted(reports.items()):
        if os.sep in name or name.startswith("."):
            raise ValueError(f"scenario name '{name}' is not a file name")
        file = name + ".json"
        keep.add(file)
        text = dump(baseline_entry(name, report))
        target = os.path.join(path, file)
        if os.path.exists(target):
            with open(target) as f:
                if f.read() == text:
                    continue
        with open(target, "w") as f:
            f.write(text)
    for file in os.listdir(path):
        if file.endswith(".json") and file not in keep:
            os.remove(os.path.join(path, file))


def entries(reports):
    return {k: baseline_entry(k, v) for k, v in reports.items()}


def compare(base, new):
    rows, regressions, missing = [], [], []
    for name in sorted(set(base) | set(new)):
        if name not in new:
            missing.append(name)
            continue
        if name not in base:
            rows.append((name, "(new scenario)", None, None, "new"))
            continue
        bm, nm = base[name]["metrics"], new[name]["metrics"]
        for metric in sorted(set(bm) | set(nm)):
            b, n = bm.get(metric), nm.get(metric)
            if b is None or n is None:
                rows.append((name, metric, b, n, "added" if b is None else "removed"))
                continue
            d = direction(metric)
            tol = max(REL_TOL * abs(b), floor(metric))
            worse = (n - b) * (-d)  # Positive = worse.
            if d != 0 and worse > tol:
                status = "REGRESSED"
                regressions.append((name, metric, b, n))
            elif d != 0 and -worse > tol:
                status = "improved"
            else:
                status = ""
            if status:
                rows.append((name, metric, b, n, status))
        if not new[name].get("passed", True):
            regressions.append((name, "expectations", None, None))
            rows.append((name, "expectations", None, None, "FAILED"))
    return rows, regressions, missing


def fmt(v):
    if v is None:
        return "-"
    return f"{v:.4g}"


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    if "--split" in argv:
        if os.path.isdir(args[0]):
            print("--split: BASELINE.json must be a file", file=sys.stderr)
            return 2
        os.makedirs(args[1], exist_ok=True)
        write_baseline(args[1], load(args[0]))
        print(f"baseline split: {args[0]} -> {args[1]}", file=sys.stderr)
        return 0
    markdown = "--markdown" in argv
    base, new = load(args[0]), load(args[1])
    other = sibling(args[0])
    if other:
        a, b = entries(base), entries(load(other))
        differ = sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k))
        if differ:
            print(f"warning: {args[0]} and {other} differ in {len(differ)} scenario(s) "
                  f"({', '.join(differ[:5])}{', ...' if len(differ) > 5 else ''}); "
                  f"`--update` writes both", file=sys.stderr)
    rows, regressions, missing = compare(base, new)
    if markdown:
        print("| scenario | metric | baseline | result | status |")
        print("|---|---|---|---|---|")
        for r in rows:
            print(f"| {r[0]} | {r[1]} | {fmt(r[2])} | {fmt(r[3])} | {r[4]} |")
        if not rows:
            print("| all | all | | | unchanged |")
    else:
        for r in rows:
            print(f"{r[4]:>10}  {r[0]:<28} {r[1]:<30} {fmt(r[2]):>10} -> {fmt(r[3])}")
    for name in missing:
        print(f"warning: scenario '{name}' is in the baseline but was not run", file=sys.stderr)
    print(f"\n{len(regressions)} regression(s), {sum(1 for r in rows if r[4] == 'improved')} improvement(s)",
          file=sys.stderr)
    if "--update" in argv:
        for path in [args[0]] + ([other] if other else []):
            write_baseline(path, new)
            print(f"baseline updated: {path}", file=sys.stderr)
    return 1 if regressions else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
