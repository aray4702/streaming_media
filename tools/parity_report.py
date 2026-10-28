#!/usr/bin/env python3
"""Parity report: where this library differs from libwebrtc, in either direction.

Reads compare-webrtc.json (written by `sm_compare --json`) and lists every
scenario and metric where the mean over seeds of this library and of libwebrtc
differ by more than the parity band. A metric that is better than libwebrtc's
is a gap too (program.md section 0.1).

Parity band (per scenario and metric), the largest of:
  1. the metric's tie floor from `sm_compare` (the JSON's "shown" table);
  2. 5% of the larger of the two means (`sm_compare`'s relative tie tolerance);
  3. SIGMA (default 1.0) standard deviations of libwebrtc's own per-seed
     values, so a difference smaller than libwebrtc's seed-to-seed spread
     is not a gap. Needs "per_seed" in the JSON; without it this term is 0.

Each gap also records `seeds_agree`: on how many seeds the single-seed
difference exceeds the same floor and tolerance in the gap's direction.

Usage:
  tools/parity_report.py compare-webrtc.json [--owners tools/parity_owners.json]
        [--base main-compare-webrtc.json] [--markdown out.md] [--json out.json]
        [--sigma 1.0] [--check-owned]

--base also reports the gaps closed and opened relative to that report (the
comparison of `main`, for a PR). --check-owned exits 1 if a gap has no owner
(an issue on the roadmap or an approved deviation); this is the stage 1 exit
test in program.md.

An owner counts only while someone works on the gap or it is explained: a
matching rule owns a gap through an issue that is open, or through any of its
issues when the rule carries a `note` (the recorded explanation of a closed,
explained difference or deviation). A rule without a note whose issues are all
closed owns nothing; the report lists those issues as stale owners, so a gap
"owned" only by finished work shows as unowned. Issue states come from `gh`
(GH_TOKEN or a logged-in gh; the repository from GITHUB_REPOSITORY or the
current checkout). Without them every listed issue counts, as before, and the
report says the states were unknown.
"""
import argparse
import fnmatch
import json
import math
import os
import subprocess
import sys

REL_TOL = 0.05


def load(path):
    with open(path) as f:
        return json.load(f)


def stdev(xs):
    if len(xs) < 2:
        return 0.0
    m = sum(xs) / len(xs)
    return math.sqrt(sum((x - m) ** 2 for x in xs) / (len(xs) - 1))


def signed(better, sm, rtc):
    """Positive when this library is better than libwebrtc."""
    d = sm - rtc
    return -d if better == "lower" else d


def gaps(data, sigma=1.0):
    shown = data.get("shown")
    if not shown:
        raise SystemExit("no 'shown' table in the input: regenerate it with a current sm_compare")
    out = []
    for sc in data["scenarios"]:
        per_seed = sc.get("per_seed", [])
        for metric, info in shown.items():
            if metric not in sc["sm"] or metric not in sc["webrtc"]:
                continue
            a, b = sc["sm"][metric], sc["webrtc"][metric]
            if a is None or b is None:
                continue
            rtc_vals = [s["webrtc"][metric] for s in per_seed if s["webrtc"].get(metric) is not None]
            tol = lambda x, y: max(info["floor"], REL_TOL * max(abs(x), abs(y)))
            band = max(tol(a, b), sigma * stdev(rtc_vals))
            if abs(a - b) <= band:
                continue
            s = signed(info["better"], a, b)
            agree = 0
            for ps in per_seed:
                x, y = ps["sm"].get(metric), ps["webrtc"].get(metric)
                if x is None or y is None:
                    continue
                d = signed(info["better"], x, y)
                if abs(x - y) > tol(x, y) and (d > 0) == (s > 0):
                    agree += 1
            out.append({
                "scenario": sc["scenario"], "metric": metric, "sm": a, "webrtc": b,
                "band": band, "direction": "ahead" if s > 0 else "behind",
                "seeds_agree": agree, "seeds": len(per_seed),
            })
    return out


def issue_states(numbers):
    """{issue: "open" | "closed"} from gh, or None when gh can't tell."""
    repo = os.environ.get("GITHUB_REPOSITORY")
    states = {}
    for n in sorted(numbers):
        cmd = ["gh", "issue", "view", str(n), "--json", "state", "--jq", ".state"] + (["-R", repo] if repo else [])
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        except (OSError, subprocess.TimeoutExpired):
            return None
        if r.returncode != 0:
            return None
        states[n] = r.stdout.strip().lower()
    return states


def attach_owners(gap_list, owners_path, states=None):
    """states: {issue: "open" | "closed"}; None counts every listed issue."""
    rules = load(owners_path)["rules"] if owners_path else []
    for g in gap_list:
        issues, stale = [], []
        for r in rules:
            if fnmatch.fnmatch(g["scenario"], r.get("scenario", "*")) and fnmatch.fnmatch(g["metric"], r["metric"]):
                for i in r.get("issues", []):
                    counts = states is None or "note" in r or states.get(i) == "open"
                    dest = issues if counts else stale
                    if i not in dest:
                        dest.append(i)
        g["owners"] = issues
        g["stale_owners"] = [i for i in stale if i not in issues]
    return gap_list


def key(g):
    return (g["scenario"], g["metric"])


def fmt(v):
    return "%.0f" % v if abs(v) >= 100 or v == int(v) else "%.1f" % v


def owner_text(g):
    text = ", ".join("#%d" % i for i in g["owners"]) if g["owners"] else "**none**"
    if g.get("stale_owners"):
        text += " (closed, no note: %s)" % ", ".join("#%d" % i for i in g["stale_owners"])
    return text


def table(rows):
    lines = ["| scenario | metric | ours | libwebrtc | band | vs libwebrtc | seeds | owner |", "|---|---|---|---|---|---|---|---|"]
    for g in rows:
        lines.append("| %s | `%s` | %s | %s | ±%s | %s | %d/%d | %s |" % (
            g["scenario"], g["metric"], fmt(g["sm"]), fmt(g["webrtc"]), fmt(g["band"]), g["direction"],
            g["seeds_agree"], g["seeds"], owner_text(g)))
    return lines


def markdown(data, gap_list, base_gaps=None, states_known=True):
    n_scn = len(data["scenarios"])
    behind = [g for g in gap_list if g["direction"] == "behind"]
    ahead = [g for g in gap_list if g["direction"] == "ahead"]
    unowned = [g for g in gap_list if not g["owners"]]
    lines = ["## Parity with libwebrtc %s" % data.get("webrtc_version", ""), "",
             "%d scenarios, mean of %d seeds. **%d gaps**: %d behind libwebrtc, %d ahead of it; %d without an owner." % (
                 n_scn, data.get("seeds", 0), len(gap_list), len(behind), len(ahead), len(unowned)),
             "",
             "A gap is a metric whose means differ by more than the band, in either direction: the larger of the "
             "`sm_compare` tie floor, 5% of the larger mean, and one standard deviation of libwebrtc's per-seed values. "
             "`seeds` is on how many seeds the single-seed difference is also outside the floor and 5%, in the gap's direction. "
             "An owner is an open issue, or a closed one whose rule carries a note (an explained difference); "
             "closed issues without a note are listed but own nothing.", ""]
    if not states_known:
        lines += ["Issue states were unknown (no `gh`), so every listed issue counts as an owner.", ""]
    if base_gaps is not None:
        bk = {key(g): g for g in base_gaps}
        hk = {key(g): g for g in gap_list}
        closed = [bk[k] for k in bk if k not in hk]
        opened = [hk[k] for k in hk if k not in bk]
        flipped = [hk[k] for k in hk if k in bk and bk[k]["direction"] != hk[k]["direction"]]
        lines += ["### Against `main`", "",
                  "%d gaps on `main`, %d here: **%d closed, %d opened**, %d changed direction." % (
                      len(base_gaps), len(gap_list), len(closed), len(opened), len(flipped)), ""]
        if closed:
            lines += ["Closed:", ""] + table(closed) + [""]
        if opened:
            lines += ["Opened:", ""] + table(opened) + [""]
        if flipped:
            lines += ["Changed direction:", ""] + table(flipped) + [""]
        # Gaps that stayed but moved relative to libwebrtc.
        moved = []
        for k in hk:
            if k in bk and bk[k]["direction"] == hk[k]["direction"]:
                d0 = abs(bk[k]["sm"] - bk[k]["webrtc"])
                d1 = abs(hk[k]["sm"] - hk[k]["webrtc"])
                if d0 > 0 and abs(d1 - d0) > 0.1 * d0:
                    moved.append((hk[k], d0, d1))
        if moved:
            lines += ["Still gaps, distance to libwebrtc changed by more than 10%:", "",
                      "| scenario | metric | distance on main | distance here |", "|---|---|---|---|"]
            for g, d0, d1 in moved:
                lines.append("| %s | `%s` | %s | %s (%s) |" % (g["scenario"], g["metric"], fmt(d0), fmt(d1),
                                                           "closer" if d1 < d0 else "further"))
            lines.append("")
    if not gap_list:
        lines += ["No gaps.", ""]
        return "\n".join(lines)
    by_scn = {}
    for g in gap_list:
        by_scn.setdefault(g["scenario"], []).append(g)
    lines += ["### Gaps", ""] + table(sorted(gap_list, key=lambda g: (g["scenario"], g["metric"]))) + [""]
    if unowned:
        lines += ["### Gaps without an owner", ""] + table(unowned) + [""]
    return "\n".join(lines)


def owner_issues(owners_path):
    rules = load(owners_path)["rules"] if owners_path else []
    return {i for r in rules for i in r.get("issues", [])}


def align_base(base, head):
    """Restrict main's report to the seeds the PR ran, scenario by scenario.

    On a PR, scenarios with `compare_seeds` run fewer seeds than on main
    (.github/workflows/ci.yml, compare-webrtc-shard). Comparing their 10-seed
    means with main's 80-seed means would open and close gaps from the seed
    count alone, so the base keeps only the seeds the head has, and its means
    are recomputed from them.
    """
    head_seeds = {s["scenario"]: {p["seed"] for p in s.get("per_seed", [])} for s in head["scenarios"]}
    out = dict(base, scenarios=[])
    for s in base["scenarios"]:
        want = head_seeds.get(s["scenario"])
        per = s.get("per_seed", [])
        if not want or not per or {p["seed"] for p in per} <= want:
            out["scenarios"].append(s)
            continue
        kept = [p for p in per if p["seed"] in want]
        if not kept:
            out["scenarios"].append(s)
            continue
        s = dict(s, per_seed=kept, sm=dict(s["sm"]), webrtc=dict(s["webrtc"]))
        for side in ("sm", "webrtc"):
            for m in s[side]:
                vals = [p[side][m] for p in kept if p[side].get(m) is not None]
                if vals:
                    s[side][m] = sum(vals) / len(vals)
        out["scenarios"].append(s)
    return out


def report(data, owners_path=None, sigma=1.0, states=None):
    return attach_owners(gaps(data, sigma), owners_path, states)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("compare_json")
    ap.add_argument("--owners")
    ap.add_argument("--base")
    ap.add_argument("--markdown")
    ap.add_argument("--json")
    ap.add_argument("--sigma", type=float, default=1.0)
    ap.add_argument("--check-owned", action="store_true")
    a = ap.parse_args(argv)
    data = load(a.compare_json)
    states = issue_states(owner_issues(a.owners)) if a.owners else None
    g = report(data, a.owners, a.sigma, states)
    base = None
    if a.base:
        try:
            base = report(align_base(load(a.base), data), a.owners, a.sigma, states)
        except (OSError, ValueError, SystemExit, KeyError) as e:
            print("base report unusable (%s); skipping the comparison with main" % e, file=sys.stderr)
    md = markdown(data, g, base, states_known=states is not None or not a.owners)
    if a.markdown:
        with open(a.markdown, "w") as f:
            f.write(md + "\n")
    else:
        print(md)
    if a.json:
        with open(a.json, "w") as f:
            json.dump({"webrtc_version": data.get("webrtc_version"), "seeds": data.get("seeds"),
                       "sigma": a.sigma, "issue_states": states, "gaps": g}, f, indent=1)
            f.write("\n")
    if a.check_owned and any(not x["owners"] for x in g):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
