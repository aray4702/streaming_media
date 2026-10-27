#!/usr/bin/env python3
"""Mechanical dispatcher for the agent loop (agents/coordinator.md).

Applies the coordinator's rule-based duties without an LLM, so they run on
every GitHub event and every 15 minutes instead of on a coordinator tick:

  AUTOMERGE      turn on squash auto-merge for an approved, green PR (parity
                 closed > opened, or the Owner approved it: owner-approved)
  HOLD           turn auto-merge off on a held PR (blocked or needs-human), so
                 a merge freeze or an Owner question can't be merged past
  UPDATE-BRANCH  bring one approved, green, behind PR up to date (oldest first)
  RUN fixer      start a Fixer run when an eligible #64 item has waited
  RUN fixer_b    start Fixer B, the on-demand second Fixer, when a Fixer is
                 already working and another eligible #64 item is free
  RUN reviewer   start a Reviewer run when a green PR has waited for review, or
                 when needs-human was removed after its last review on the head
  RUN coordinator  start the cloud Coordinator when something needs judgment
                 (a new unplaced issue, a needs-human or needs-coordinator
                 label, a Fixer comment on a work-list item)
  RUN builder    start the Builder when an issue or PR carries needs-builder
  RUN tester     start a Tester run when fixes wait to be verified (an
                 agent-found or bug issue closed) or main moved a lot (3+ merges)
                 since its last run, or when a green PR waits for its probe
                 (needs-probe)
  STALE          report an in-progress item with no Fixer activity
  NOTIFY         forward each new "NEEDS YOU:" comment on the log issue to the
                 Owner's Discord channel (GitHub doesn't notify the Owner about
                 posts from their own account, which the agents post as)

Judgment (placement, sign-offs, re-planning, escalation) stays with the
coordinator. By default this only prints what it would do (dry run); --live
acts. Uses `gh api` (REST), so it runs locally and in GitHub Actions.

  python3 agents/tools/dispatch.py [--live] [--repo owner/name]
"""
import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys

BOT = "aray4702-reviewer-app[bot]"  # the Reviewer App (App ID 5320656); renamed from streaming-media-reviewer on 2026-10-26
BOT_ID = 342667563  # its bot user id, which survives a rename; match on this
ROADMAP = 64
LOG_ISSUE = 111
PROTECTED = (".github/", "agents/")
OWNER_APPROVED = "owner-approved"  # the Owner approved a measurement fix whose parity got worse
NEEDS_BUILDER = "needs-builder"
NEEDS_COORDINATOR = "needs-coordinator"  # a question the Coordinator decides; holds the item meanwhile
NEEDS_PROBE = "needs-probe"  # a risky PR the Tester probes before the Reviewer reviews it
RECORD_LABELS = {"record", "goal", "agent-log"}  # records, not work items
MAX_IN_PROGRESS = 5
FIXER_WAIT_MIN = 60        # pace rule: extra Fixer run only after an item waited this long
REVIEW_WAIT_MIN = 30       # green PR without a review on its head
STALE_MIN = 90             # in-progress item without Fixer activity
CLAIM_WINDOW_MIN = 30      # a Fixer "Work started" this recent means a run is active
DISPATCH_GAP_MIN = 30      # at most one dispatch per routine in this window
FIXER_B_WAIT_MIN = 10      # a free item waits this long before Fixer B takes it (lets a claim settle)
FIXER_B_GAP_MIN = 45       # at most one Fixer B start in this window (a run takes about that long)
COORDINATOR_POSTS = ("<!-- sm-agent: coordinator -->",
                     "Owner (coordinator)", "Owner (local coordinator)")  # the last two: before 2026-10-26
TESTER_GAP_MIN = 180       # a Tester run takes up to an hour; keep runs apart
TESTER_MERGES = 3          # merges to main since its last run that call for a new one
TESTER_RUN_MIN = 90        # a run that started this recently may not have posted its log yet
MERGE_FIXER_MIN = 20       # a closed PR starts a Fixer run by itself (GitHub event trigger)
PRODUCT_PATHS = ("src/", "include/", "testbench/", "tests/", "compare/")  # merges that change the product or its measurement
MARKER = "dispatcher-last-{}"  # <!-- dispatcher-last-fixer: ISO --> in #111's body
FAIL_MARKER = "dispatcher-fail-{}"  # <!-- dispatcher-fail-tester: ISO error --> while starts fail

NOW = dt.datetime.now(dt.timezone.utc)


def load_routines():
    """agents/routines.json: the cloud routines' ids, schedules and triggers (one source for this
    script, the console and smctl; the Builder keeps it in step with claude.ai/code/routines)."""
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "routines.json")) as f:
        return json.load(f)["routines"]


def cron_fields(cron):
    """Minutes and UTC hours of a 'M H * * *' cron: M a number or list, H '*', '*/N', a number or a list."""
    m, h = cron.split()[:2]
    minutes = tuple(int(x) for x in m.split(","))
    hours = tuple(range(24)) if h == "*" else tuple(range(0, 24, int(h[2:]))) if h.startswith("*/") \
        else tuple(int(x) for x in h.split(","))
    return minutes, hours


ROUTINE_CONFIG = load_routines()
SCHEDULED_FIXER_MINUTES = cron_fields(ROUTINE_CONFIG["fixer"]["cron"])[0]  # Fixer B has no cron: the Dispatcher starts it
SCHEDULED_COORDINATOR_MINUTE = cron_fields(ROUTINE_CONFIG["coordinator"]["cron"])[0][0]
TESTER_SCHEDULE = (cron_fields(ROUTINE_CONFIG["tester"]["cron"])[0][0], cron_fields(ROUTINE_CONFIG["tester"]["cron"])[1])


def gh(path, method="GET", fields=None, paginate=False):
    cmd = ["gh", "api", "-X", method, path]
    if paginate:
        cmd.append("--paginate")
    for k, v in (fields or {}).items():
        cmd += ["-f", f"{k}={v}"]
    out = subprocess.run(cmd, capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError(f"gh api {method} {path}: {out.stderr.strip()}")
    text = out.stdout.strip()
    if not text:
        return None
    if paginate and text.startswith("[") and "][" in text:
        text = text.replace("][", ",")
    return json.loads(text)


def ts(s):
    return dt.datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=dt.timezone.utc)


def minutes_ago(s):
    return (NOW - ts(s)).total_seconds() / 60


def is_bot(user):
    """True for the Reviewer App's bot user."""
    return (user or {}).get("id") == BOT_ID or (user or {}).get("login") == BOT


def labels(item):
    return {l["name"] for l in item.get("labels", [])}


class Dispatcher:
    def __init__(self, repo, live, verbose=False):
        self.r, self.live, self.verbose = repo, live, verbose
        self.actions = []
        self.failures = []
        self.pulls = gh(f"repos/{repo}/pulls?state=open&per_page=100") or []
        self.log_body = gh(f"repos/{repo}/issues/{LOG_ISSUE}")["body"] or ""

    # ---- helpers --------------------------------------------------------
    def note(self, target, why):
        if self.verbose:
            print(f"{'skip':13} {target:12} {why}")

    def say(self, kind, target, why):
        self.actions.append((kind, target, why))
        print(f"{kind:13} {target:12} {why}")

    def ci_ok(self, sha):
        runs = gh(f"repos/{self.r}/commits/{sha}/check-runs?check_name=ci-ok")["check_runs"]
        if not runs:
            return None, None
        run = runs[0]
        return (run.get("conclusion") or run.get("status")), run.get("completed_at")

    def bot_review(self, number):
        revs = [x for x in gh(f"repos/{self.r}/pulls/{number}/reviews?per_page=100") or []
                if is_bot(x["user"])]
        return revs[-1] if revs else None

    def needs_human_cleared_after(self, number, when):
        events = gh(f"repos/{self.r}/issues/{number}/events?per_page=100", paginate=True) or []
        return any(e.get("event") == "unlabeled" and (e.get("label") or {}).get("name") == "needs-human"
                   and e["created_at"] > when for e in events)

    def parity_ok(self, pr):
        """closed > opened (or nothing opened) on this head's published report."""
        base = f"repos/{self.r}/contents/pr/{pr['number']}"
        try:
            head = self._content(f"{base}/HEAD")
            report = self._content(f"{base}/parity.md")
        except RuntimeError:
            return False, "no parity report yet"
        if pr["head"]["sha"] not in head:
            return False, "parity report is for an older head"
        m = re.search(r"\*\*(\d+) closed, (\d+) opened\*\*", report)
        if not m:
            return False, "parity count not found"
        closed, opened = int(m[1]), int(m[2])
        return (closed > opened or opened == 0), f"parity {closed} closed / {opened} opened"

    def _content(self, path):
        import base64
        data = gh(f"{path}?ref=parity-data-pr")
        return base64.b64decode(data["content"]).decode()

    def last_dispatch(self, routine):
        m = re.search(rf"<!-- {MARKER.format(routine)}: ([0-9TZ:-]+) -->", self.log_body)
        return m[1] if m else None

    def record_dispatch(self, routine):
        line = f"<!-- {MARKER.format(routine)}: {NOW.strftime('%Y-%m-%dT%H:%M:%SZ')} -->"
        pat = rf"<!-- {MARKER.format(routine)}: [^>]* -->"
        body = re.sub(pat, line, self.log_body) if re.search(pat, self.log_body) \
            else self.log_body.rstrip("\n") + "\n" + line
        gh(f"repos/{self.r}/issues/{LOG_ISSUE}", "PATCH", {"body": body})
        self.log_body = body

    def start(self, routine):
        """Start a routine run. A failed start (a revoked token, an API error) is reported and
        recorded, and the other rules still run: one bad token mustn't stop the Dispatcher."""
        try:
            start_routine(routine.upper())
        except Exception as e:  # urllib's HTTPError, a missing token, a timeout
            err = re.sub(r"\s+", " ", str(e))[:120]
            self.say("FAIL", routine, f"start failed: {err}")
            print(f"::error::starting the {routine} routine failed: {err}")
            self.failures.append(routine)
            self.record_failure(routine, err)
            return False
        self.record_dispatch(routine)
        self.clear_failure(routine)
        return True

    def record_failure(self, routine, err):
        """Mark #111 with the failure (kept from its first occurrence) and alert the Owner once."""
        pat = rf"<!-- {FAIL_MARKER.format(routine)}: [^>]* -->"
        if re.search(pat, self.log_body):
            return  # already reported; the console shows it until a start succeeds
        stamp = NOW.strftime("%Y-%m-%dT%H:%M:%SZ")
        body = self.log_body.rstrip("\n") + f"\n<!-- {FAIL_MARKER.format(routine)}: {stamp} {err.replace('--', '-')} -->"
        gh(f"repos/{self.r}/issues/{LOG_ISSUE}", "PATCH", {"body": body})
        self.log_body = body
        try:
            post_discord(f"**streaming_media: the Dispatcher can't start the {routine} routine** ({err}). "
                         f"If it's a 401, regenerate the routine's API token at claude.ai/code/routines and "
                         f"update the secret SM_ROUTINE_{routine.upper()}_TOKEN.")
        except Exception as e:
            print(f"::warning::Discord alert failed: {e}")

    def clear_failure(self, routine):
        pat = rf"\n?<!-- {FAIL_MARKER.format(routine)}: [^>]* -->"
        if re.search(pat, self.log_body):
            body = re.sub(pat, "", self.log_body)
            gh(f"repos/{self.r}/issues/{LOG_ISSUE}", "PATCH", {"body": body})
            self.log_body = body

    # ---- rules ----------------------------------------------------------
    def stop_switch(self):
        issues = gh(f"repos/{self.r}/issues?state=open&per_page=100") or []
        return any(i["title"].strip() == "STOP AGENTS" for i in issues)

    def merges(self):
        update_done = False
        for pr in sorted(self.pulls, key=lambda p: p["created_at"]):
            n, sha = pr["number"], pr["head"]["sha"]
            held = labels(pr) & {"needs-human", "blocked"}  # blocked: a merge freeze (agents/coordinator.md)
            if held and pr["auto_merge"] is not None:
                # Auto-merge turned on before the hold would still merge the PR once
                # ci-ok passes (#448 during #450's freeze). The AUTOMERGE rule below
                # turns it back on once the hold lifts.
                self.say("HOLD", f"PR #{n}", f"{', '.join(sorted(held))}: auto-merge off")
                if self.live:
                    subprocess.run(["gh", "pr", "merge", str(n), "-R", self.r, "--disable-auto"], check=True)
                continue
            if pr["draft"] or held:
                self.note(f"PR #{n}", "draft" if pr["draft"] else ", ".join(sorted(held)))
                continue
            ci, _ = self.ci_ok(sha)
            rev = self.bot_review(n)
            approved = rev and rev["state"] == "APPROVED" and rev["commit_id"] == sha
            if ci != "success" or not approved:
                self.note(f"PR #{n}", f"ci-ok {ci}, " + (f"review {rev['state']} on {rev['commit_id'][:7]}" if rev else "no review"))
                continue
            files = [f["filename"] for f in gh(f"repos/{self.r}/pulls/{n}/files?per_page=100") or []]
            if any(f.startswith(PROTECTED) for f in files):
                continue  # the Owner merges protected paths
            if pr["auto_merge"] is None:
                ok, why = self.parity_ok(pr)
                if not ok and OWNER_APPROVED in labels(pr):
                    ok, why = True, f"{why}; Owner-approved ({OWNER_APPROVED})"
                if not ok:
                    self.say("WAIT", f"PR #{n}", f"approved and green, but {why}")
                    continue
                self.say("AUTOMERGE", f"PR #{n}", f"approved and green on {sha[:7]}; {why}")
                if self.live:
                    subprocess.run(["gh", "pr", "merge", str(n), "-R", self.r, "--auto", "--squash"], check=True)
            elif not update_done:
                state = gh(f"repos/{self.r}/pulls/{n}")["mergeable_state"]
                if state == "behind":
                    self.say("UPDATE-BRANCH", f"PR #{n}", "approved, green, auto-merge on, behind main")
                    if self.live:
                        gh(f"repos/{self.r}/pulls/{n}/update-branch", "PUT")
                    update_done = True

    def roadmap_candidate(self):
        body = gh(f"repos/{self.r}/issues/{ROADMAP}")["body"]
        section = body.split("## Work list", 1)[1].split("\n## ", 1)[0] if "## Work list" in body else ""
        items = [int(m) for m in re.findall(r"^- \[ \] #(\d+)", section, re.M)]
        refs = {}
        for pr in self.pulls:
            for m in re.findall(r"(?:Fixes|Refs|Closes) #(\d+)\b", pr.get("body") or ""):
                refs.setdefault(int(m), []).append(pr["number"])
        # The Fixer's own selection rule (agents/fixer.md step 3): the first open
        # item; while it is busy, only parallel-ok items below it, stopping at
        # the first free item without parallel-ok.
        in_progress, cand, walking, first = 0, None, True, True
        for n in items:
            issue = gh(f"repos/{self.r}/issues/{n}")
            if issue["state"] != "open":
                continue
            lab = labels(issue)
            in_progress += "in-progress" in lab
            busy = bool(lab & {"in-progress", "needs-human", NEEDS_BUILDER, NEEDS_COORDINATOR, "blocked", "wontfix", "cannot-reproduce"}) or n in refs
            if walking:
                if not busy and (first or "parallel-ok" in lab):
                    cand, walking = issue, False
                elif not busy:
                    walking = False  # free but not parallel-ok: later items wait
            first = False
        return (cand or None), in_progress

    def closed_pulls(self):
        if not hasattr(self, "_closed"):
            self._closed = gh(f"repos/{self.r}/pulls?state=closed&sort=updated&direction=desc&per_page=50") or []
        return self._closed

    def recent_fixer_claim(self):
        since = (NOW - dt.timedelta(minutes=CLAIM_WINDOW_MIN)).strftime("%Y-%m-%dT%H:%M:%SZ")
        comments = gh(f"repos/{self.r}/issues/comments?since={since}&per_page=100") or []
        return any("sm-agent: fixer" in (c["body"] or "") and
                   re.search(r"Work (started|resumed)", c["body"] or "") for c in comments)

    def fixer(self):
        cand, in_progress = self._candidate = self.roadmap_candidate()
        if not cand:
            return self.note("fixer", f"no free eligible #{ROADMAP} item ({in_progress} in progress)")
        n = cand["number"]
        # Free since: the issue's last update, or the moment a PR on it closed
        # (merging a PR doesn't touch the issue, but it frees the item).
        free_since = max([cand["updated_at"]] + [
            p["closed_at"] for p in self.closed_pulls() if p.get("closed_at") and
            re.search(rf"(?:Fixes|Refs|Closes|For) #{n}\b", p.get("body") or "")])
        waited = minutes_ago(free_since)
        recent_close = [p for p in self.closed_pulls()
                        if p.get("closed_at") and minutes_ago(p["closed_at"]) < MERGE_FIXER_MIN]
        last = self.last_dispatch("fixer")
        if in_progress >= MAX_IN_PROGRESS:
            return self.say("HOLD", "fixer", f"#{n} free but {in_progress} items in progress")
        if waited < FIXER_WAIT_MIN:
            return self.say("HOLD", "fixer", f"#{n} free for {waited:.0f} min (< {FIXER_WAIT_MIN})")
        if recent_close:
            return self.say("HOLD", "fixer", f"PR #{recent_close[0]['number']} closed {minutes_ago(recent_close[0]['closed_at']):.0f} min ago; its event already starts a Fixer run")
        if self.recent_fixer_claim():
            return self.say("HOLD", "fixer", "a Fixer claimed work in the last 30 min")
        if last and minutes_ago(last) < DISPATCH_GAP_MIN:
            return self.say("HOLD", "fixer", f"dispatched {minutes_ago(last):.0f} min ago")
        if any(0 <= (m - NOW.minute) % 60 <= 10 for m in SCHEDULED_FIXER_MINUTES):
            return self.say("HOLD", "fixer", "the scheduled Fixer run is due within 10 min")
        self.say("RUN", "fixer", f"#{n} free for {waited:.0f} min")
        if self.live:
            self.start("fixer")

    def fixer_b(self):
        # The second Fixer (Owner, 2026-10-26: on demand only). fixer() covers an
        # idle loop; Fixer B covers the case it holds: one Fixer is working and
        # another eligible item is free.
        cand, in_progress = getattr(self, "_candidate", None) or self.roadmap_candidate()
        if not cand:
            return self.note("fixer_b", "no free eligible item")
        n = cand["number"]
        if not self.recent_fixer_claim():
            return self.note("fixer_b", f"#{n} free, but no Fixer is working; the main Fixer takes it")
        if in_progress >= MAX_IN_PROGRESS:
            return self.say("HOLD", "fixer_b", f"#{n} free but {in_progress} items in progress")
        waited = minutes_ago(cand["updated_at"])
        if waited < FIXER_B_WAIT_MIN:
            return self.say("HOLD", "fixer_b", f"#{n} free for {waited:.0f} min (< {FIXER_B_WAIT_MIN})")
        closed = [p for p in self.closed_pulls() if p.get("closed_at") and minutes_ago(p["closed_at"]) < MERGE_FIXER_MIN]
        if closed:
            return self.say("HOLD", "fixer_b", f"PR #{closed[0]['number']} closed {minutes_ago(closed[0]['closed_at']):.0f} min ago; its event starts a Fixer run")
        if any(0 <= (m - NOW.minute) % 60 <= 10 for m in SCHEDULED_FIXER_MINUTES):
            return self.say("HOLD", "fixer_b", "the scheduled Fixer run is due within 10 min")
        last = self.last_dispatch("fixer_b")
        if last and minutes_ago(last) < FIXER_B_GAP_MIN:
            return self.say("HOLD", "fixer_b", f"started {minutes_ago(last):.0f} min ago")
        if self.live and not os.environ.get("SM_ROUTINE_FIXER_B_TOKEN"):
            return self.say("HOLD", "fixer_b", "SM_ROUTINE_FIXER_B_TOKEN not set")
        self.say("RUN", "fixer_b", f"#{n} free for {waited:.0f} min while a Fixer works")
        if self.live:
            self.start("fixer_b")

    def reviewer(self):
        for pr in sorted(self.pulls, key=lambda p: p["created_at"]):
            n, sha = pr["number"], pr["head"]["sha"]
            if pr["draft"] or labels(pr) & {"needs-human", NEEDS_PROBE}:
                continue
            ci, done = self.ci_ok(sha)
            if ci != "success" or not done:
                continue
            rev = self.bot_review(n)
            cleared = False
            if rev and rev["commit_id"] == sha:
                # Reviewed this head already. Review again only if it wasn't an
                # approval and needs-human was removed after that review (the
                # Owner or the Coordinator answered what the review waited on).
                if rev["state"] == "APPROVED" or not self.needs_human_cleared_after(n, rev["submitted_at"]):
                    continue
                cleared = True
            waited = minutes_ago(done)
            if waited < REVIEW_WAIT_MIN and not cleared:
                continue
            last = self.last_dispatch("reviewer")
            if last and minutes_ago(last) < DISPATCH_GAP_MIN:
                return self.say("HOLD", "reviewer", f"PR #{n} waiting, but dispatched {minutes_ago(last):.0f} min ago")
            self.say("RUN", "reviewer", f"PR #{n}: needs-human cleared after its {rev['state']} review on {sha[:7]}"
                     if cleared else f"PR #{n} green for {waited:.0f} min with no review on {sha[:7]}")
            if self.live:
                self.start("reviewer")
            return

    def coordinator(self):
        # Events since the coordinator last looked: its last start by this
        # dispatcher, or its last comment on the log issue.
        marks = [self.last_dispatch("coordinator")]
        log = gh(f"repos/{self.r}/issues/{LOG_ISSUE}/comments?per_page=100", paginate=True) or []
        marks += [c["created_at"] for c in log if (c["body"] or "").startswith(COORDINATOR_POSTS)]
        marks = [m for m in marks if m]
        since = max(marks) if marks else (NOW - dt.timedelta(hours=1)).strftime("%Y-%m-%dT%H:%M:%SZ")
        roadmap = gh(f"repos/{self.r}/issues/{ROADMAP}")["body"]
        listed = {int(m) for m in re.findall(r"#(\d+)", roadmap)}
        reasons = []
        for i in gh(f"repos/{self.r}/issues?state=open&since={since}&per_page=100") or []:
            n, lab = i["number"], labels(i)
            if i["created_at"] > since and "pull_request" not in i and n not in listed \
                    and n not in (ROADMAP, LOG_ISSUE) and not lab & RECORD_LABELS:
                reasons.append(f"#{n} is new and not on #{ROADMAP}")
            if "needs-human" in lab and n != LOG_ISSUE:
                reasons.append(f"#{n} carries needs-human")
            if NEEDS_COORDINATOR in lab:
                reasons.append(f"#{n} carries {NEEDS_COORDINATOR}")
        comments = gh(f"repos/{self.r}/issues/comments?since={since}&per_page=100") or []
        for c in comments:
            n = int(c["issue_url"].rsplit("/", 1)[1])
            if "/pull/" in c["html_url"]:
                continue  # PR threads are the Reviewer's and the Fixer's; work-list issues only
            if "sm-agent: fixer" in (c["body"] or "") and n in listed:
                reasons.append(f"Fixer commented on #{n}")
        reasons = sorted(set(reasons))
        if not reasons:
            return self.note("coordinator", f"nothing new since {since}")
        last = self.last_dispatch("coordinator")
        if last and minutes_ago(last) < DISPATCH_GAP_MIN:
            return self.say("HOLD", "coordinator", f"{len(reasons)} events, but dispatched {minutes_ago(last):.0f} min ago")
        if 0 <= (SCHEDULED_COORDINATOR_MINUTE - NOW.minute) % 60 <= 10:
            return self.say("HOLD", "coordinator", "the scheduled Coordinator run is due within 10 min")
        self.say("RUN", "coordinator", "; ".join(reasons[:4]) + (" ..." if len(reasons) > 4 else ""))
        if self.live:
            self.start("coordinator")

    def probe_waiting(self):
        """The oldest green, non-draft needs-probe PR without a probe report on its head."""
        for pr in sorted(self.pulls, key=lambda p: p["created_at"]):
            if pr["draft"] or NEEDS_PROBE not in labels(pr):
                continue
            sha = pr["head"]["sha"]
            if self.ci_ok(sha)[0] != "success":
                continue
            comments = gh(f"repos/{self.r}/issues/{pr['number']}/comments?per_page=100", paginate=True) or []
            if not any("sm-agent: tester" in (c["body"] or "") and f"Probe report @{sha[:7]}" in (c["body"] or "")
                       for c in comments):
                return pr["number"]
        return None

    def tester(self):
        probe = self.probe_waiting()
        if probe:
            last = self.last_dispatch("tester")
            if last and minutes_ago(last) < TESTER_RUN_MIN:
                return self.say("HOLD", "tester", f"PR #{probe} waits for its probe, but a Tester run started {minutes_ago(last):.0f} min ago")
            self.say("RUN", "tester", f"PR #{probe} waits for its probe ({NEEDS_PROBE})")
            if self.live:
                self.start("tester")
            return
        logs = [i for i in gh(f"repos/{self.r}/issues?state=open&labels=agent-log&per_page=100") or []
                if i["title"].strip().lower() == "tester run log"]
        marks = [self.last_dispatch("tester")]
        if logs:
            comments = gh(f"repos/{self.r}/issues/{logs[0]['number']}/comments?per_page=100", paginate=True) or []
            marks += [c["created_at"] for c in comments if "sm-agent: tester" in (c["body"] or "")]
        # A scheduled run posts its log only when it ends, so count a slot
        # that started in the last TESTER_RUN_MIN minutes as a run.
        minute, hours = TESTER_SCHEDULE
        slots = [NOW.replace(hour=h, minute=minute, second=0, microsecond=0) - dt.timedelta(days=d)
                 for d in (0, 1) for h in hours]
        started = [t for t in slots if dt.timedelta(0) <= NOW - t < dt.timedelta(minutes=TESTER_RUN_MIN)]
        marks += [t.strftime("%Y-%m-%dT%H:%M:%SZ") for t in started]
        marks = [m for m in marks if m]
        if not marks:
            return self.note("tester", "no previous Tester run found")
        last = max(marks)
        if minutes_ago(last) < TESTER_GAP_MIN:
            return self.note("tester", f"last run {minutes_ago(last):.0f} min ago (< {TESTER_GAP_MIN})")
        nxt = min((NOW.replace(hour=h, minute=minute, second=0, microsecond=0) + dt.timedelta(days=d)
                   for d in (0, 1) for h in hours
                   if NOW.replace(hour=h, minute=minute, second=0, microsecond=0) + dt.timedelta(days=d) > NOW))
        if (nxt - NOW).total_seconds() < 45 * 60:
            return self.note("tester", f"scheduled run due at {nxt:%H:%M}Z")
        fixed = [i["number"] for i in gh(f"repos/{self.r}/issues?state=closed&since={last}&per_page=100") or []
                 if "pull_request" not in i and (i.get("closed_at") or "") > last
                 and labels(i) & {"agent-found", "bug"}]
        merged = []
        for p in self.closed_pulls():
            if (p.get("merged_at") or "") > last:
                files = [f["filename"] for f in gh(f"repos/{self.r}/pulls/{p['number']}/files?per_page=100") or []]
                if any(f.startswith(PRODUCT_PATHS) for f in files):
                    merged.append(p["number"])  # system-only merges (agents/, .github/, docs) don't count
        why = []
        if fixed:
            why.append("fixes to verify: " + ", ".join(f"#{n}" for n in fixed))
        if len(merged) >= TESTER_MERGES:
            why.append(f"{len(merged)} product merges since its last run")
        if not why:
            return self.note("tester", f"nothing new since {last} ({len(merged)} product merges)")
        self.say("RUN", "tester", "; ".join(why))
        if self.live:
            self.start("tester")

    def builder(self):
        waiting = [i["number"] for i in gh(f"repos/{self.r}/issues?state=open&labels={NEEDS_BUILDER}&per_page=100",
                                           paginate=True) or []]
        if not waiting:
            return self.note("builder", f"nothing labelled {NEEDS_BUILDER}")
        last = self.last_dispatch("builder")
        if last and minutes_ago(last) < DISPATCH_GAP_MIN:
            return self.say("HOLD", "builder", f"{len(waiting)} waiting, but dispatched {minutes_ago(last):.0f} min ago")
        if self.live and not os.environ.get("SM_ROUTINE_BUILDER_TOKEN"):
            return self.say("HOLD", "builder", "SM_ROUTINE_BUILDER_TOKEN not set")
        self.say("RUN", "builder", f"{NEEDS_BUILDER} on " + ", ".join(f"#{n}" for n in waiting[:5]))
        if self.live:
            self.start("builder")

    def notify(self):
        last = self.last_dispatch("notify")
        since = last or (NOW - dt.timedelta(hours=1)).strftime("%Y-%m-%dT%H:%M:%SZ")
        new = [c for c in gh(f"repos/{self.r}/issues/{LOG_ISSUE}/comments?since={since}&per_page=100") or []
               if "NEEDS YOU" in (c["body"] or "").split("\n", 1)[0] and c["created_at"] > since]
        if not new:
            return self.note("notify", f"no new NEEDS YOU since {since}")
        for c in new:
            first = (c["body"] or "").split("\n", 1)[0][:300]
            self.say("NOTIFY", "discord", f"{c['html_url']} {first[:80]}")
            if self.live:
                post_discord(f"**streaming_media needs you**\n{first}\n{c['html_url']}")
        if self.live:
            self.record_dispatch("notify")

    def stale(self):
        for issue in gh(f"repos/{self.r}/issues?state=open&labels=in-progress&per_page=100") or []:
            if "pull_request" in issue:
                continue
            n = issue["number"]
            comments = gh(f"repos/{self.r}/issues/{n}/comments?per_page=100") or []
            fixer = [c for c in comments if "sm-agent: fixer" in (c["body"] or "")]
            age = minutes_ago(fixer[-1]["created_at"]) if fixer else None
            if age is None or age > STALE_MIN:
                self.say("STALE", f"#{n}", "no Fixer activity" + (f" for {age:.0f} min" if age else ""))

    def run(self):
        if self.stop_switch():
            print("STOP AGENTS issue is open; doing nothing.")
            return
        self.merges()
        self.fixer()
        self.fixer_b()
        self.reviewer()
        self.coordinator()
        self.tester()
        self.builder()
        self.notify()
        self.stale()
        if not self.actions:
            print("nothing to do")


ROUTINES = {name.upper(): r["id"] for name, r in ROUTINE_CONFIG.items()}  # FIXER, FIXER_B, ...


def start_routine(name):
    """Fire a routine run (https://platform.claude.com/docs/en/api/claude-code/routines-fire).
    The per-routine token comes from SM_ROUTINE_<NAME>_TOKEN (an Actions secret).
    Limits: 30 fires per hour per routine, 100 per account; retries create duplicates."""
    token = os.environ.get(f"SM_ROUTINE_{name}_TOKEN")
    if not token:
        raise RuntimeError(f"routine {name}: SM_ROUTINE_{name}_TOKEN not set")
    import urllib.request
    url = f"https://api.anthropic.com/v1/claude_code/routines/{ROUTINES[name]}/fire"
    req = urllib.request.Request(url, method="POST", data=b"{}",
                                 headers={"Authorization": f"Bearer {token}",
                                          "anthropic-version": "2023-06-01",
                                          "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        body = json.loads(resp.read() or b"{}")
    print(f"  started {name.lower()}: {body.get('claude_code_session_url', '?')}")


def post_discord(text):
    """Post to the Owner's Discord channel through a webhook (SM_DISCORD_WEBHOOK)."""
    url = os.environ.get("SM_DISCORD_WEBHOOK")
    if not url:
        raise RuntimeError("SM_DISCORD_WEBHOOK not set")
    import urllib.request
    req = urllib.request.Request(url, method="POST",
                                 data=json.dumps({"content": text[:1900],
                                                  "allowed_mentions": {"parse": []}}).encode(),
                                 headers={"Content-Type": "application/json",
                                          "User-Agent": "streaming-media-dispatcher"})
    urllib.request.urlopen(req, timeout=30).close()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--live", action="store_true", help="act (default: dry run)")
    ap.add_argument("--verbose", action="store_true", help="say why each PR and item is skipped")
    ap.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", "aray4702/streaming_media"))
    args = ap.parse_args()
    print(f"dispatcher {'LIVE' if args.live else 'dry run'} at {NOW:%Y-%m-%d %H:%M}Z on {args.repo}")
    Dispatcher(args.repo, args.live, args.verbose).run()


if __name__ == "__main__":
    sys.exit(main())
