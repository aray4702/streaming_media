#!/usr/bin/env python3
"""Flow report for the system audit (agents/builder.md, "Audits").

Measures how work flowed through the agent system over a window (default
the last 24 h, compared with the last 7 days), from GitHub's PR, label and
Actions history:

  throughput   PRs merged, work-list items closed
  lead time    PR opened -> merged (median, 90th percentile)
  waiting      hours items spent labelled needs-owner, needs-builder,
               needs-coordinator, needs-human or blocked
  CI           ci runs, cancelled share, median run time, median wait for
               the first job (slots), runs per merged PR
  churn        update-branch merges of main per merged PR

  agents/tools/flow_report.py [--repo R] [--hours 24] [--compare-days 7] [--markdown out.md]

Read-only; uses `gh api`. The Builder posts the result on the audit issue.
"""
import argparse
import datetime as dt
import json
import statistics
import subprocess
import sys

WAIT_LABELS = ("needs-owner", "needs-builder", "needs-coordinator", "needs-human", "blocked")
NOW = dt.datetime.now(dt.timezone.utc)


def gh(path, paginate=False):
    cmd = ["gh", "api", path] + (["--paginate"] if paginate else [])
    out = subprocess.run(cmd, capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError(f"gh api {path}: {out.stderr.strip()}")
    text = out.stdout.strip()
    if paginate and text.startswith("[") and "][" in text:
        text = text.replace("][", ",")
    return json.loads(text) if text else None


def t(s):
    return dt.datetime.fromisoformat(s.replace("Z", "+00:00")) if s else None


def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(round(p * (len(xs) - 1))))] if xs else None


def hours(x):
    return "-" if x is None else f"{x:.1f} h"


def window(repo, since):
    """The measures for activity since `since`."""
    pulls = gh(f"repos/{repo}/pulls?state=closed&sort=updated&direction=desc&per_page=100", paginate=True) or []
    merged = [p for p in pulls if p.get("merged_at") and t(p["merged_at"]) >= since]
    lead = [(t(p["merged_at"]) - t(p["created_at"])).total_seconds() / 3600 for p in merged]
    closed_items = gh(f"repos/{repo}/issues?state=closed&since={since:%Y-%m-%dT%H:%M:%SZ}&per_page=100", paginate=True) or []
    closed_items = [i for i in closed_items if "pull_request" not in i and i.get("closed_at") and t(i["closed_at"]) >= since]

    # Waiting time per label, from label events on items active in the window.
    active = gh(f"repos/{repo}/issues?state=all&since={since:%Y-%m-%dT%H:%M:%SZ}&per_page=100", paginate=True) or []
    waiting = {label: 0.0 for label in WAIT_LABELS}
    for item in active:
        if not any(lab["name"] in waiting for lab in item.get("labels", [])) and item.get("state") == "closed" \
                and t(item.get("closed_at")) and t(item["closed_at"]) < since:
            continue
        events = gh(f"repos/{repo}/issues/{item['number']}/events?per_page=100", paginate=True) or []
        on = {}
        for e in events:
            name = (e.get("label") or {}).get("name")
            if name not in waiting:
                continue
            at = t(e["created_at"])
            if e["event"] == "labeled":
                on[name] = at
            elif e["event"] == "unlabeled" and name in on:
                start = max(on.pop(name), since)
                waiting[name] += max(0.0, (at - start).total_seconds() / 3600)
        for name, start in on.items():  # still labelled
            waiting[name] += max(0.0, (NOW - max(start, since)).total_seconds() / 3600)

    # CI runs of the ci workflow.
    runs, page = [], 1
    while True:  # the ci workflow's own runs (the Dispatcher's would crowd them out of /actions/runs)
        batch = (gh(f"repos/{repo}/actions/workflows/ci.yml/runs?created=>={since:%Y-%m-%dT%H:%M:%SZ}"
                    f"&per_page=100&page={page}") or {}).get("workflow_runs", [])
        runs += batch
        if len(batch) < 100 or page >= 10:
            break
        page += 1
    runs = [r for r in runs if r.get("status") == "completed"]
    cancelled = sum(r.get("conclusion") == "cancelled" for r in runs)
    durations = [(t(r["updated_at"]) - t(r["run_started_at"])).total_seconds() / 60
                 for r in runs if r.get("run_started_at") and r.get("conclusion") != "cancelled"]
    slot_wait = []
    for r in runs[:30]:  # first-job wait, sampled: one API call per run
        jobs = (gh(f"repos/{repo}/actions/runs/{r['id']}/jobs?per_page=60") or {}).get("jobs", [])
        starts = [t(j["started_at"]) for j in jobs if j.get("started_at")]
        if starts:
            slot_wait.append((min(starts) - t(r["created_at"])).total_seconds() / 60)

    # Update-branch merges of main per merged PR (each one re-runs CI).
    updates = []
    for p in merged:
        commits = gh(f"repos/{repo}/pulls/{p['number']}/commits?per_page=100") or []
        updates.append(sum(c["commit"]["message"].startswith("Merge branch 'main' into ") for c in commits))
    pr_runs = {}
    for r in runs:
        if r.get("event") == "pull_request":
            pr_runs[r["head_branch"]] = pr_runs.get(r["head_branch"], 0) + 1
    runs_per_merge = [pr_runs.get(p["head"]["ref"], 0) for p in merged]

    return {
        "merged": len(merged), "items_closed": len(closed_items),
        "lead_median_h": statistics.median(lead) if lead else None, "lead_p90_h": pct(lead, 0.9),
        "waiting_h": waiting,
        "ci_runs": len(runs), "ci_cancelled": cancelled,
        "ci_median_min": statistics.median(durations) if durations else None,
        "slot_wait_median_min": statistics.median(slot_wait) if slot_wait else None,
        "runs_per_merged_pr": statistics.mean(runs_per_merge) if runs_per_merge else None,
        "updates_per_merged_pr": statistics.mean(updates) if updates else None,
    }


def markdown(day, week, hours_back, days_back):
    def row(name, a, b, fmt):
        return f"| {name} | {fmt(a)} | {fmt(b)} |"
    num = lambda x: "-" if x is None else (f"{x:.1f}" if isinstance(x, float) else str(x))
    mins = lambda x: "-" if x is None else f"{x:.0f} min"
    per_day = lambda w: {k: (v / days_back if isinstance(v, (int, float)) and k in ("merged", "items_closed", "ci_runs", "ci_cancelled") else v)
                         for k, v in w.items()}
    wk = per_day(week)
    lines = [f"## Flow report ({NOW:%Y-%m-%d %H:%M} UTC)", "",
             f"| measure | last {hours_back} h | 7-day daily average or median |", "|---|---|---|",
             row("PRs merged", day["merged"], wk["merged"], num),
             row("work items closed", day["items_closed"], wk["items_closed"], num),
             row("PR lead time, median", day["lead_median_h"], week["lead_median_h"], hours),
             row("PR lead time, 90th pct", day["lead_p90_h"], week["lead_p90_h"], hours),
             row("ci runs", day["ci_runs"], wk["ci_runs"], num),
             row("ci runs cancelled", day["ci_cancelled"], wk["ci_cancelled"], num),
             row("ci run time, median", day["ci_median_min"], week["ci_median_min"], mins),
             row("wait for a CI slot, median", day["slot_wait_median_min"], week["slot_wait_median_min"], mins),
             row("ci runs per merged PR", day["runs_per_merged_pr"], week["runs_per_merged_pr"], num),
             row("update-branch merges per merged PR", day["updates_per_merged_pr"], week["updates_per_merged_pr"], num),
             "", f"Waiting (label-hours in the last {hours_back} h; 7-day total in brackets):", ""]
    for label in WAIT_LABELS:
        lines.append(f"- `{label}`: {day['waiting_h'][label]:.1f} h ({week['waiting_h'][label]:.1f} h)")
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", default="aray4702/streaming_media")
    ap.add_argument("--hours", type=int, default=24)
    ap.add_argument("--compare-days", type=int, default=7)
    ap.add_argument("--markdown")
    a = ap.parse_args(argv)
    day = window(a.repo, NOW - dt.timedelta(hours=a.hours))
    week = window(a.repo, NOW - dt.timedelta(days=a.compare_days))
    md = markdown(day, week, a.hours, a.compare_days)
    if a.markdown:
        with open(a.markdown, "w") as f:
            f.write(md)
    print(md)
    return 0


if __name__ == "__main__":
    sys.exit(main())
