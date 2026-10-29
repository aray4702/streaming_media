#!/usr/bin/env python3
"""Requirements report: stage 1 progress measured in requirements closed.

The Owner measures progress by features and requirements closed, not by
issues or PRs (Owner, 2026-10-28; agents/program.md 0.4). This reads the
register (tools/requirements.json: one requirement per library feature in
tools/catalog.py, plus harness, platform and system requirements) and the
open GitHub items labelled `req:<id>`, and prints:

  - requirements done / total, overall and per area;
  - each requirement in progress with its open items;
  - inconsistencies: a requirement marked done with open items, an open item
    labelled with an unknown requirement, a catalog feature missing from the
    register.

  tools/requirements_report.py [--json out.json] [--markdown out.md] [--offline]

--offline skips GitHub (no open items) for a quick local look.
"""
import argparse
import json
import os
import subprocess
import sys
from collections import OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = "aray4702/streaming_media"
STATUSES = ("done", "in-progress", "open", "unassessed")


def load_register(path=None):
    with open(path or os.path.join(HERE, "requirements.json")) as f:
        return json.load(f)


def open_items(repo=REPO):
    """{requirement id: [(number, title, is_pr)]} from open issues and PRs labelled req:<id>."""
    out, page = {}, 1
    while True:
        r = subprocess.run(["gh", "api", f"repos/{repo}/issues?state=open&per_page=100&page={page}"],
                           capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(r.stderr.strip())
        batch = json.loads(r.stdout or "[]")
        for i in batch:
            for lab in i.get("labels", []):
                if lab["name"].startswith("req:"):
                    out.setdefault(lab["name"][4:], []).append((i["number"], i["title"], "pull_request" in i))
        if len(batch) < 100:
            return out
        page += 1


def report(reg, items, catalog_ids=None):
    reqs = reg["requirements"]
    ids = {r["id"] for r in reqs}
    areas = OrderedDict()
    for r in reqs:
        a = areas.setdefault(r["area"], {s: 0 for s in STATUSES})
        a[r["status"]] += 1
    problems = []
    for r in reqs:
        if r["status"] == "done" and items.get(r["id"]):
            problems.append(f"{r['id']} is marked done but has open items: "
                            + ", ".join(f"#{n}" for n, _, _ in items[r["id"]]))
    for rid in items:
        if rid not in ids:
            problems.append(f"label req:{rid} matches no requirement: "
                            + ", ".join(f"#{n}" for n, _, _ in items[rid]))
    if catalog_ids is not None:
        for cid in catalog_ids:
            if f"F{cid}" not in ids:
                problems.append(f"catalog feature {cid} is missing from the register")
    done = sum(r["status"] == "done" for r in reqs)
    return {"done": done, "total": len(reqs), "areas": areas, "problems": problems,
            "in_progress": [(r, items.get(r["id"], [])) for r in reqs if r["status"] == "in-progress"],
            "requirements": reqs, "items": items}


def markdown(rep):
    lines = [f"## Requirements: **{rep['done']} of {rep['total']} done**", "",
             "| area | done | in progress | open | unassessed |", "|---|---|---|---|---|"]
    for area, c in rep["areas"].items():
        lines.append(f"| {area} | {c['done']} | {c['in-progress']} | {c['open']} | {c['unassessed']} |")
    if rep["in_progress"]:
        lines += ["", "In progress:", ""]
        for r, its in rep["in_progress"]:
            what = ", ".join(f"#{n}" for n, _, _ in its) or (r.get("evidence") or "no open item labelled")
            lines.append(f"- **{r['id']}** {r['title']}: {what}")
    if rep["problems"]:
        lines += ["", "Register problems:", ""] + [f"- {p}" for p in rep["problems"]]
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json")
    ap.add_argument("--markdown")
    ap.add_argument("--offline", action="store_true")
    a = ap.parse_args(argv)
    reg = load_register()
    sys.path.insert(0, HERE)
    import catalog
    items = {} if a.offline else open_items()
    rep = report(reg, items, [x["id"] for x in catalog.ALGORITHMS])
    md = markdown(rep)
    if a.markdown:
        with open(a.markdown, "w") as f:
            f.write(md)
    if a.json:
        with open(a.json, "w") as f:
            json.dump({k: v for k, v in rep.items() if k != "areas"} | {"areas": dict(rep["areas"])}, f, indent=1)
    print(md, end="")
    return 1 if rep["problems"] else 0


if __name__ == "__main__":
    sys.exit(main())
