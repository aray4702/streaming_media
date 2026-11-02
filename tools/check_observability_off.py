#!/usr/bin/env python3
"""Observability-off check (issue #988, requirement C8).

Builds the test bench with every observability output compiled out
(SM_ENABLE_METRICS, SM_ENABLE_EVENTS and SM_ENABLE_STATS off, SM_LOG_MIN_LEVEL
NONE), runs the scenario suite with it and with the default build, and fails
unless every scenario's media results are bit-identical: the same `metrics`
(exact values, the same keys), the same `checks` and the same verdict. The
observability outputs themselves (the `events` counts, `--library` metrics)
are absent in the off build and are not compared.

    tools/check_observability_off.py --default-build out/native \\
        [--off-build out/obs-off] [--jobs 4] [scenarios...]

`--default-build` must already be built (its testbench/sm_testbench). The off
build is configured and built here (only the sm_testbench target, with the
same build type) unless `--no-build` is given. With no scenarios, it runs
testbench/scenarios/*.scn. Exit code 0 when identical, 1 on a difference, 2 on
a setup error.

CI: add a step to the test bench job in .github/workflows/ci.yml, after the
native build, e.g.
    python3 tools/check_observability_off.py --default-build out/native --jobs 4
(`.github/` is a protected path, so the Owner adds it.)
"""

import argparse
import concurrent.futures
import glob
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

OFF_OPTIONS = [
    "-DSM_ENABLE_METRICS=OFF",
    "-DSM_ENABLE_EVENTS=OFF",
    "-DSM_ENABLE_STATS=OFF",
    "-DSM_LOG_MIN_LEVEL=NONE",
    "-DSM_BUILD_TESTS=OFF",
    "-DSM_BUILD_BENCH=OFF",
    "-DSM_BUILD_EXAMPLES=OFF",
]

# The keys of a scenario's result that are media results. Everything else (`events`, `library`,
# `timeseries`) is an observability output.
COMPARED = ("passed", "metrics", "checks")


def build_type_of(build_dir):
    try:
        with open(os.path.join(build_dir, "CMakeCache.txt")) as f:
            for line in f:
                if line.startswith("CMAKE_BUILD_TYPE:"):
                    return line.split("=", 1)[1].strip() or "Release"
    except OSError:
        pass
    return "Release"


def build_off(default_build, off_build, jobs):
    cfg = ["cmake", "-S", ROOT, "-B", off_build, "-G", "Ninja",
           "-DCMAKE_BUILD_TYPE=" + build_type_of(default_build)] + OFF_OPTIONS
    subprocess.run(cfg, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["cmake", "--build", off_build, "-j", str(jobs), "--target", "sm_testbench"],
                   check=True, stdout=subprocess.DEVNULL)


def run_one(exe, scn, tmp):
    out = os.path.join(tmp, os.path.basename(exe + "." + os.path.basename(scn)) + ".json")
    proc = subprocess.run([exe, "--quiet", "--json=" + out, scn],
                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    try:
        with open(out) as f:
            data = json.load(f)
    except (OSError, ValueError):
        return {"error": "no result (exit %d): %s" % (proc.returncode, proc.stderr.strip()[-300:])}
    return {r["scenario"]: {k: r.get(k) for k in COMPARED} for r in data}


def diff(name, a, b):
    lines = []
    for k in ("passed", "checks"):
        if a.get(k) != b.get(k):
            lines.append("  %s: default %s, off %s" % (k, json.dumps(a.get(k))[:200], json.dumps(b.get(k))[:200]))
    ma, mb = a.get("metrics") or {}, b.get("metrics") or {}
    for key in sorted(set(ma) | set(mb)):
        if key not in ma or key not in mb:
            lines.append("  metric %s: %s" % (key, "only in the default build" if key in ma else "only in the off build"))
        elif ma[key] != mb[key]:
            lines.append("  metric %s: default %r, off %r" % (key, ma[key], mb[key]))
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--default-build", required=True)
    ap.add_argument("--off-build", default=os.path.join(ROOT, "out", "obs-off"))
    ap.add_argument("--no-build", action="store_true", help="use --off-build as it is")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("scenarios", nargs="*")
    args = ap.parse_args()

    scenarios = args.scenarios or sorted(glob.glob(os.path.join(ROOT, "testbench", "scenarios", "*.scn")))
    on_exe = os.path.join(args.default_build, "testbench", "sm_testbench")
    off_exe = os.path.join(args.off_build, "testbench", "sm_testbench")
    if not os.path.exists(on_exe):
        print("missing %s: build the default tree first" % on_exe, file=sys.stderr)
        return 2
    if not args.no_build:
        try:
            build_off(args.default_build, args.off_build, args.jobs)
        except subprocess.CalledProcessError as e:
            print("building the observability-off tree failed: %s" % e, file=sys.stderr)
            return 2
    if not os.path.exists(off_exe):
        print("missing %s" % off_exe, file=sys.stderr)
        return 2

    failures = 0
    with tempfile.TemporaryDirectory() as tmp, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        jobs = {scn: (pool.submit(run_one, on_exe, scn, tmp), pool.submit(run_one, off_exe, scn, tmp))
                for scn in scenarios}
        for scn, (fa, fb) in jobs.items():
            ra, rb = fa.result(), fb.result()
            label = os.path.basename(scn)
            if "error" in ra or "error" in rb:
                print("ERROR %s: default %s; off %s" % (label, ra.get("error", "ok"), rb.get("error", "ok")))
                failures += 1
                continue
            lines = []
            for name in sorted(set(ra) | set(rb)):
                if name not in ra or name not in rb:
                    lines.append("  run %s only in the %s build" % (name, "default" if name in ra else "off"))
                    continue
                lines += diff(name, ra[name], rb[name])
            if lines:
                failures += 1
                print("DIFF %s" % label)
                print("\n".join(lines[:40]))
    print("%d scenario files, %d with a difference between the default and the observability-off build"
          % (len(scenarios), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
