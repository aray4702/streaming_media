#!/usr/bin/env python3
"""Mechanical dispatcher for the agent loop (agents/coordinator.md).

Applies the coordinator's rule-based duties without an LLM, so they run on
every GitHub event and every 15 minutes instead of on a coordinator tick:

  AUTOMERGE      turn on squash auto-merge for an approved, green PR
  UPDATE-BRANCH  bring one approved, green, behind PR up to date (oldest first)
  RUN fixer      start a Fixer run when an eligible #64 item has waited
  RUN reviewer   start a Reviewer run when a green PR has waited for review
  RUN coordinator  start the cloud Coordinator when something needs judgment
                 (a new unplaced issue, a needs-human label, a Fixer comment on
                 a work-list item) and no local coordinator holds the lease
  RUN tester     start a Tester run when fixes wait to be verified (an
                 agent-found or bug issue closed) or main moved a lot (3+ merges)
                 since its last run
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

BOT = "streaming-media-reviewer[bot]"
ROADMAP = 64
LOG_ISSUE = 111
PROTECTED = (".github/", "agents/")
MAX_IN_PROGRESS = 5
FIXER_WAIT_MIN = 60        # pace rule: extra Fixer run only after an item waited this long
REVIEW_WAIT_MIN = 30       # green PR without a review on its head
STALE_MIN = 90             # in-progress item without Fixer activity
CLAIM_WINDOW_MIN = 30      # a Fixer "Work started" this recent means a run is active
DISPATCH_GAP_MIN = 30      # at most one dispatch per routine in this window
SCHEDULED_FIXER_MINUTE = 15  # the Fixer routine's own cron minute
SCHEDULED_COORDINATOR_MINUTE = 5  # the Coordinator routine's own cron minute
TESTER_GAP_MIN = 180       # a Tester run takes up to an hour; keep runs apart
TESTER_MERGES = 3          # merges to main since its last run that call for a new one
TESTER_SCHEDULE = (34, (0, 6, 12, 18))  # its own cron: minute, UTC hours
MARKER = "dispatcher-last-{}"  # <!-- dispatcher-last-fixer: ISO --> in #111's body

NOW = dt.datetime.now(dt.timezone.utc)


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


def labels(item):
    return {l["name"] for l in item.get("labels", [])}


class Dispatcher:
    def __init__(self, repo, live, verbose=False):
        self.r, self.live, self.verbose = repo, live, verbose
        self.actions = []
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
                if x["user"]["login"] == BOT]
        return revs[-1] if revs else None

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

    # ---- rules ----------------------------------------------------------
    def stop_switch(self):
        issues = gh(f"repos/{self.r}/issues?state=open&per_page=100") or []
        return any(i["title"].strip() == "STOP AGENTS" for i in issues)

    def merges(self):
        update_done = False
        for pr in sorted(self.pulls, key=lambda p: p["created_at"]):
            n, sha = pr["number"], pr["head"]["sha"]
            if pr["draft"] or "needs-human" in labels(pr):
                self.note(f"PR #{n}", "draft" if pr["draft"] else "needs-human")
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
            busy = bool(lab & {"in-progress", "needs-human", "wontfix", "cannot-reproduce"}) or n in refs
            if walking:
                if not busy and (first or "parallel-ok" in lab):
                    cand, walking = issue, False
                elif not busy:
                    walking = False  # free but not parallel-ok: later items wait
            first = False
        return (cand or None), in_progress

    def recent_fixer_claim(self):
        since = (NOW - dt.timedelta(minutes=CLAIM_WINDOW_MIN)).strftime("%Y-%m-%dT%H:%M:%SZ")
        comments = gh(f"repos/{self.r}/issues/comments?since={since}&per_page=100") or []
        return any("sm-agent: fixer" in (c["body"] or "") and
                   re.search(r"Work (started|resumed)", c["body"] or "") for c in comments)

    def fixer(self):
        cand, in_progress = self.roadmap_candidate()
        if not cand:
            return self.note("fixer", f"no free eligible #{ROADMAP} item ({in_progress} in progress)")
        n = cand["number"]
        waited = minutes_ago(cand["updated_at"])
        last = self.last_dispatch("fixer")
        if in_progress >= MAX_IN_PROGRESS:
            return self.say("HOLD", "fixer", f"#{n} free but {in_progress} items in progress")
        if waited < FIXER_WAIT_MIN:
            return self.say("HOLD", "fixer", f"#{n} free for {waited:.0f} min (< {FIXER_WAIT_MIN})")
        if self.recent_fixer_claim():
            return self.say("HOLD", "fixer", "a Fixer claimed work in the last 30 min")
        if last and minutes_ago(last) < DISPATCH_GAP_MIN:
            return self.say("HOLD", "fixer", f"dispatched {minutes_ago(last):.0f} min ago")
        if 0 <= (SCHEDULED_FIXER_MINUTE - NOW.minute) % 60 <= 10:
            return self.say("HOLD", "fixer", "the scheduled Fixer run is due within 10 min")
        self.say("RUN", "fixer", f"#{n} free for {waited:.0f} min")
        if self.live:
            start_routine("FIXER")
            self.record_dispatch("fixer")

    def reviewer(self):
        for pr in sorted(self.pulls, key=lambda p: p["created_at"]):
            n, sha = pr["number"], pr["head"]["sha"]
            if pr["draft"] or "needs-human" in labels(pr):
                continue
            ci, done = self.ci_ok(sha)
            if ci != "success" or not done:
                continue
            rev = self.bot_review(n)
            if rev and rev["commit_id"] == sha:
                continue
            waited = minutes_ago(done)
            if waited < REVIEW_WAIT_MIN:
                continue
            last = self.last_dispatch("reviewer")
            if last and minutes_ago(last) < DISPATCH_GAP_MIN:
                return self.say("HOLD", "reviewer", f"PR #{n} waiting, but dispatched {minutes_ago(last):.0f} min ago")
            self.say("RUN", "reviewer", f"PR #{n} green for {waited:.0f} min with no review on {sha[:7]}")
            if self.live:
                start_routine("REVIEWER")
                self.record_dispatch("reviewer")
            return

    def coordinator(self):
        lease = re.search(r"local-coordinator-lease: ([0-9TZ:-]+)", self.log_body)
        if lease and ts(lease[1]) > NOW:
            return self.note("coordinator", f"local coordinator holds the lease until {lease[1]}")
        # Events since the coordinator last looked: its last start by this
        # dispatcher, or its last comment on the log issue.
        marks = [self.last_dispatch("coordinator")]
        log = gh(f"repos/{self.r}/issues/{LOG_ISSUE}/comments?per_page=100", paginate=True) or []
        marks += [c["created_at"] for c in log if (c["body"] or "").startswith(
            ("Owner (coordinator):", "Owner (local coordinator):"))]
        marks = [m for m in marks if m]
        since = max(marks) if marks else (NOW - dt.timedelta(hours=1)).strftime("%Y-%m-%dT%H:%M:%SZ")
        roadmap = gh(f"repos/{self.r}/issues/{ROADMAP}")["body"]
        listed = {int(m) for m in re.findall(r"#(\d+)", roadmap)}
        reasons = []
        for i in gh(f"repos/{self.r}/issues?state=open&since={since}&per_page=100") or []:
            n, lab = i["number"], labels(i)
            if i["created_at"] > since and "pull_request" not in i and n not in listed \
                    and n not in (ROADMAP, LOG_ISSUE) and "agent-log" not in lab:
                reasons.append(f"#{n} is new and not on #{ROADMAP}")
            if "needs-human" in lab and n != LOG_ISSUE:
                reasons.append(f"#{n} carries needs-human")
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
            start_routine("COORDINATOR")
            self.record_dispatch("coordinator")

    def tester(self):
        logs = [i for i in gh(f"repos/{self.r}/issues?state=open&labels=agent-log&per_page=100") or []
                if i["title"].strip().lower() == "tester run log"]
        marks = [self.last_dispatch("tester")]
        if logs:
            comments = gh(f"repos/{self.r}/issues/{logs[0]['number']}/comments?per_page=100", paginate=True) or []
            marks += [c["created_at"] for c in comments if "sm-agent: tester" in (c["body"] or "")]
        marks = [m for m in marks if m]
        if not marks:
            return self.note("tester", "no previous Tester run found")
        last = max(marks)
        if minutes_ago(last) < TESTER_GAP_MIN:
            return self.note("tester", f"last run {minutes_ago(last):.0f} min ago (< {TESTER_GAP_MIN})")
        minute, hours = TESTER_SCHEDULE
        nxt = min((NOW.replace(hour=h, minute=minute, second=0, microsecond=0) + dt.timedelta(days=d)
                   for d in (0, 1) for h in hours
                   if NOW.replace(hour=h, minute=minute, second=0, microsecond=0) + dt.timedelta(days=d) > NOW))
        if (nxt - NOW).total_seconds() < 45 * 60:
            return self.note("tester", f"scheduled run due at {nxt:%H:%M}Z")
        fixed = [i["number"] for i in gh(f"repos/{self.r}/issues?state=closed&since={last}&per_page=100") or []
                 if "pull_request" not in i and (i.get("closed_at") or "") > last
                 and labels(i) & {"agent-found", "bug"}]
        merged = [p["number"] for p in gh(f"repos/{self.r}/pulls?state=closed&sort=updated&direction=desc&per_page=50") or []
                  if (p.get("merged_at") or "") > last]
        why = []
        if fixed:
            why.append("fixes to verify: " + ", ".join(f"#{n}" for n in fixed))
        if len(merged) >= TESTER_MERGES:
            why.append(f"{len(merged)} merges since its last run")
        if not why:
            return self.note("tester", f"nothing new since {last} ({len(merged)} merges)")
        self.say("RUN", "tester", "; ".join(why))
        if self.live:
            start_routine("TESTER")
            self.record_dispatch("tester")

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
        self.reviewer()
        self.coordinator()
        self.tester()
        self.notify()
        self.stale()
        if not self.actions:
            print("nothing to do")


ROUTINES = {"FIXER": "trig_01J3mfRuNYRkTJjDyPPmDMXT", "REVIEWER": "trig_01QtG1eghRppxXD1p9tYcW5p",
            "COORDINATOR": "trig_01QaBh8maaHLypPUE9y9sD6f", "TESTER": "trig_01NrjibmvNHPiNfYp4YC8YFB"}


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
