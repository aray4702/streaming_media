#!/usr/bin/env python3
"""Compares test bench results against the baseline and flags regressions.

    testbench_compare.py BASELINE.json RESULTS.json [--markdown] [--update]

Both files are `sm_testbench --json` output (an array of scenario reports).
A metric regresses when it moves in its "worse" direction by more than
max(relative tolerance x baseline, absolute floor). Exit status: 0 = no
regressions, 1 = regressions, 2 = usage error. `--update` rewrites BASELINE
with RESULTS (metrics only) after printing the comparison.

The canonical baseline is produced on Linux x86_64 with GCC 13 (CI job
`testbench` and the cloud agents); other platforms may differ slightly.
"""
import json
import sys

# Direction: +1 = higher is better, -1 = lower is better, 0 = informational.
HIGHER = {
    "video.frames_rendered", "video.render_fps", "video.render_ratio_pct", "video.avg_kbps",
    "cc.target_avg_kbps", "cc.goodput_kbps", "cc.utilization_pct",
}
INFO = {
    "net.loss_pct", "repair.nack_recovered", "repair.fec_recovered", "video.keyframes",
    "cc.rtt_p50_ms",
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


def load(path):
    with open(path) as f:
        data = json.load(f)
    if isinstance(data, dict):
        data = data.get("scenarios", [data])
    return {r["scenario"]: r for r in data}


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
    markdown = "--markdown" in argv
    base, new = load(args[0]), load(args[1])
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
        out = [{"scenario": k, "description": v.get("description", ""), "passed": v.get("passed", True),
                "metrics": v["metrics"]} for k, v in sorted(new.items())]
        with open(args[0], "w") as f:
            json.dump(out, f, indent=1, sort_keys=True)
            f.write("\n")
        print(f"baseline updated: {args[0]}", file=sys.stderr)
    return 1 if regressions else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
