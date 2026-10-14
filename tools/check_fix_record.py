#!/usr/bin/env python3
"""CI check (job `fix-record`): a PR that fixes a found problem must carry a
regression test and a fix record. See program.md section 6.5.

For every issue the PR body closes ("Fixes #N", "Closes #N", "Resolves #N")
or partially fixes ("Refs #N", "Part of #N") that is labelled agent-found,
bug, ux or perf, the PR must:
  1. add testbench/scenarios/issue_<N>_*.scn, which must FAIL when run with
     the sm_testbench built from the PR's base commit (proving it catches
     the problem; the normal suite proves it passes with the fix);
  2. add or update docs/fixes/<N>-*.md with front matter `issue: N` and the
     sections Issue, Root cause, Fix, Regression tests, Results (plus
     Remaining and `status: partial` for a partial fix, else `status: fixed`), and with
     every file listed in `regression_tests` present in the PR head;
  3. keep docs/fixes/README.md in sync (tools/fixes_index.py --check).
Outside pull_request events, or for PRs that close no such issue, it only
runs check 3.

Env: GITHUB_EVENT_NAME, GITHUB_EVENT_PATH, GITHUB_REPOSITORY, GH_TOKEN.
Local use: check_fix_record.py --body-file body.md --base origin/main
"""
import json
import os
import pathlib
import re
import subprocess
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
FOUND_LABELS = {"agent-found", "bug", "ux", "perf"}
SECTIONS = ["## Issue", "## Root cause", "## Fix", "## Regression tests", "## Results"]
CLOSES = re.compile(r"(?i)\b(?:fix(?:es|ed)?|close[sd]?|resolve[sd]?)\s+#(\d+)\b")
# Partial fixes reference the issue without closing it (program.md 6.5).
PARTIAL = re.compile(r"(?i)\b(?:refs|part of)\s+#(\d+)\b")


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)


def issue_labels(repo, n):
    req = urllib.request.Request(f"https://api.github.com/repos/{repo}/issues/{n}",
                                 headers={"Accept": "application/vnd.github+json"})
    if os.environ.get("GH_TOKEN"):
        req.add_header("Authorization", f"Bearer {os.environ['GH_TOKEN']}")
    with urllib.request.urlopen(req, timeout=30) as r:
        return {l["name"] for l in json.load(r).get("labels", [])}


def changed_files(base):
    out = run(["git", "diff", "--name-status", f"{base}...HEAD"]).stdout
    files = {}
    for line in out.splitlines():
        parts = line.split("\t")
        files[parts[-1]] = parts[0][0]
    return files


def front_matter(text):
    m = re.match(r"---\n(.*?)\n---\n", text, re.S)
    fields = {}
    if m:
        for line in m.group(1).splitlines():
            if ":" in line:
                k, v = line.split(":", 1)
                fields[k.strip()] = v.strip()
    return fields


def build_base_testbench(base):
    """Builds sm_testbench at the base commit; returns its path or None."""
    wt = pathlib.Path(os.environ.get("RUNNER_TEMP", "/tmp")) / "sm-base"
    run(["git", "worktree", "remove", "--force", str(wt)])
    if run(["git", "worktree", "add", "--detach", str(wt), base]).returncode != 0:
        return None
    build = wt / "out" / "base"
    cfg = subprocess.run(["cmake", "-S", str(wt), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release",
                          "-DSM_BUILD_TESTS=OFF", "-DSM_BUILD_BENCH=OFF", "-DSM_BUILD_EXAMPLES=OFF"],
                         capture_output=True, text=True)
    if cfg.returncode != 0:
        return None
    b = subprocess.run(["cmake", "--build", str(build), "-j", "4", "--target", "sm_testbench"],
                       capture_output=True, text=True)
    exe = build / "testbench" / "sm_testbench"
    return exe if b.returncode == 0 and exe.exists() else None


def main(argv):
    problems, notes = [], []
    body, base, repo = "", None, os.environ.get("GITHUB_REPOSITORY", "aray4702/streaming_media")
    if "--body-file" in argv:
        body = pathlib.Path(argv[argv.index("--body-file") + 1]).read_text()
        base = argv[argv.index("--base") + 1] if "--base" in argv else "origin/main"
    elif os.environ.get("GITHUB_EVENT_NAME") == "pull_request":
        event = json.loads(pathlib.Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
        body = event["pull_request"].get("body") or ""
        base = event["pull_request"]["base"]["sha"]

    if run([sys.executable, "tools/fixes_index.py", "--check"]).returncode != 0:
        problems.append("docs/fixes/README.md is out of date: run `python3 tools/fixes_index.py` and commit it")

    closing = {int(n) for n in CLOSES.findall(body)}
    partial = {int(n) for n in PARTIAL.findall(body)} - closing
    issues = sorted(closing | partial)
    found = []
    for n in issues:
        try:
            labels = issue_labels(repo, n)
        except Exception as e:  # Network trouble must not silently pass a fix.
            problems.append(f"#{n}: could not read the issue's labels ({e})")
            continue
        if labels & FOUND_LABELS:
            found.append(n)
        else:
            notes.append(f"#{n}: not a found problem ({', '.join(sorted(labels)) or 'no labels'}); no fix record required")

    files = changed_files(base) if found else {}
    base_exe = None
    for n in found:
        scenarios = [f for f, st in files.items()
                     if re.fullmatch(rf"testbench/scenarios/issue_{n}_[\w-]+\.scn", f) and st in "AM"]
        if not scenarios:
            problems.append(f"#{n}: add a regression scenario testbench/scenarios/issue_{n}_<slug>.scn")
        records = [f for f, st in files.items() if re.fullmatch(rf"docs/fixes/{n}-[\w-]+\.md", f) and st in "AM"]
        if not records:
            problems.append(f"#{n}: add a fix record docs/fixes/{n}-<slug>.md (copy docs/fixes/TEMPLATE.md)")
        for rec in records:
            text = (ROOT / rec).read_text()
            fm = front_matter(text)
            if fm.get("issue") != str(n):
                problems.append(f"{rec}: front matter must say `issue: {n}`")
            want = "partial" if n in partial else "fixed"
            if fm.get("status") != want:
                problems.append(f"{rec}: front matter must say `status: {want}` "
                                f"({'Refs/Part of' if want == 'partial' else 'Fixes'} #{n} in the PR body)")
            if want == "partial" and not re.search(r"^## Remaining\s*$", text, re.M):
                problems.append(f"{rec}: a partial fix needs a '## Remaining' section (what is left and why)")
            for sec in SECTIONS:
                if not re.search(rf"^{re.escape(sec)}\s*$", text, re.M):
                    problems.append(f"{rec}: missing section '{sec}'")
            for t in [t.strip() for t in fm.get("regression_tests", "").split(",") if t.strip()]:
                if not (ROOT / t).exists():
                    problems.append(f"{rec}: regression test '{t}' does not exist")
            if "<" in fm.get("title", "<") or "TODO" in text:
                problems.append(f"{rec}: still contains template placeholders or TODO")
        # The scenario must catch the problem: it has to fail before the fix.
        for scn in scenarios:
            if base_exe is None:
                base_exe = build_base_testbench(base) or False
            if not base_exe:
                problems.append(f"{scn}: could not build sm_testbench at the base commit to confirm it fails there")
                continue
            r = subprocess.run([str(base_exe), "--quiet", str(ROOT / scn)], capture_output=True, text=True)
            if r.returncode == 0:
                problems.append(f"{scn}: passes on the base commit, so it does not catch the problem "
                                "(its expect lines must fail before the fix)")
            else:
                notes.append(f"{scn}: fails on the base commit as required (exit {r.returncode})")

    for line in notes:
        print(f"ok: {line}")
    for line in problems:
        print(f"MISSING: {line}")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as f:
            f.write("## Regression test and fix record\n\n")
            for line in notes:
                f.write(f"- ok: {line}\n")
            for line in problems:
                f.write(f"- **missing:** {line}\n")
            if not notes and not problems:
                f.write("- no found problem is fixed by this change\n")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
