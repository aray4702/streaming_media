#!/usr/bin/env python3
"""Local console for the agent system: status in tabs, and the Owner's controls.

Serves a page on 127.0.0.1 that reads GitHub through `gh api` (REST, the
Owner's own login) and shows the system in tabs: overview, the #64 work
order, PRs, issues, agents, the Dispatcher and the #111 log. The controls
act as the Owner: comment, add or remove a label, close an issue, merge a
PR, stop or resume the agents (the `STOP AGENTS` issue), set the
Dispatcher's mode (the SM_DISPATCH_MODE variable), run the Dispatcher now,
and run a local dry run of agents/tools/dispatch.py. Every control asks for
confirmation in the page.

  python3 agents/tools/console.py [--port 8642] [--repo owner/name] [--open]

Only the page this server served can call its actions (a per-run token),
and only through 127.0.0.1 or localhost (the Host header is checked).
"""
import argparse
import concurrent.futures as cf
import datetime as dt
import http.server
import json
import os
import re
import secrets
import subprocess
import sys
import threading
import time
import urllib.parse
import webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dispatch as d  # noqa: E402  (constants and the gh helper; importing it acts on nothing)

CACHE_TTL_S = 60
WORKFLOW = "dispatcher.yml"
MODE_VAR = "SM_DISPATCH_MODE"
STOP_TITLE = "STOP AGENTS"
BUSY = {"in-progress", "needs-human", d.NEEDS_BUILDER, d.NEEDS_COORDINATOR, "blocked", "wontfix", "cannot-reproduce"}
NEEDS_YOU = {"needs-human"}
AGENTS = {  # role -> how its posts are recognised
    "tester": "<!-- sm-agent: tester -->",
    "fixer": "<!-- sm-agent: fixer -->",
    "coordinator": "<!-- sm-agent: coordinator -->",
    "builder": "<!-- sm-agent: builder -->",
}
SCHEDULES = {  # role -> (cron in UTC, routine ids); keep in step with the routines and dispatch.py
    "fixer": (f"{','.join(map(str, d.SCHEDULED_FIXER_MINUTES))} * * * *", (d.ROUTINES["FIXER"], d.ROUTINES["FIXER_B"])),  # B: on demand
    "reviewer": ("45 * * * *", (d.ROUTINES["REVIEWER"],)),
    "coordinator": (f"{d.SCHEDULED_COORDINATOR_MINUTE} * * * *", (d.ROUTINES["COORDINATOR"],)),
    "tester": (f"{d.TESTER_SCHEDULE[0]} {','.join(map(str, d.TESTER_SCHEDULE[1]))} * * *", (d.ROUTINES["TESTER"],)),
    "builder": ("30 16 * * *", (d.ROUTINES["BUILDER"],)),
}


def now():
    return dt.datetime.now(dt.timezone.utc)


def iso(t):
    return t.strftime("%Y-%m-%dT%H:%M:%SZ")


def lab(item):
    return sorted(d.labels(item))


def first_line(body):
    """The first line of a post after its `<!-- sm-agent: ... -->` marker."""
    return next((ln.strip() for ln in (body or "").splitlines() if ln.strip() and not ln.startswith("<!--")), "")


def next_cron(cron, after):
    """Next time a simple 'M H * * *' cron (M a list, H '*' or a list) fires after `after`."""
    minutes, hours = cron.split()[:2]
    minutes = [int(m) for m in minutes.split(",")]
    hours = range(24) if hours == "*" else [int(h) for h in hours.split(",")]
    t = after.replace(second=0, microsecond=0) + dt.timedelta(minutes=1)
    for _ in range(2 * 24 * 60):
        if t.minute in minutes and t.hour in hours:
            return t
        t += dt.timedelta(minutes=1)
    return None


class GitHub:
    """Cached reads (CACHE_TTL_S) and uncached writes, all through `gh api`."""

    def __init__(self, repo):
        self.r = repo
        self._cache, self._lock = {}, threading.Lock()

    def get(self, path, paginate=False, fresh=False):
        key = (path, paginate)
        with self._lock:
            hit = self._cache.get(key)
        if hit and not fresh and time.time() - hit[0] < CACHE_TTL_S:
            return hit[1]
        val = d.gh(path, paginate=paginate)
        with self._lock:
            self._cache[key] = (time.time(), val)
        return val

    def write(self, path, method, fields=None, raw=None):
        cmd = ["gh", "api", "-X", method, path]
        for k, v in (fields or {}).items():
            cmd += ["-F" if isinstance(v, bool) else "-f", f"{k}={str(v).lower() if isinstance(v, bool) else v}"]
        if raw is not None:
            cmd += ["--input", "-"]
        out = subprocess.run(cmd, capture_output=True, text=True, input=json.dumps(raw) if raw is not None else None)
        if out.returncode != 0:
            raise RuntimeError(f"gh api {method} {path}: {out.stderr.strip()}")
        with self._lock:
            self._cache.clear()  # anything may have changed
        return json.loads(out.stdout) if out.stdout.strip() else None

    # ---- shared reads -----------------------------------------------------
    def open_issues(self, fresh=False):
        return [i for i in self.get(f"repos/{self.r}/issues?state=open&per_page=100", True, fresh) or []
                if "pull_request" not in i]

    def open_pulls(self, fresh=False):
        return self.get(f"repos/{self.r}/pulls?state=open&per_page=100", True, fresh) or []

    def issue(self, n, fresh=False):
        return self.get(f"repos/{self.r}/issues/{n}", fresh=fresh)


class Views:
    def __init__(self, gh):
        self.gh, self.r = gh, gh.r

    # ---- work order (#64) -------------------------------------------------
    def work(self, fresh=False):
        road = self.gh.issue(d.ROADMAP, fresh)
        body = road["body"] or ""
        issues = {i["number"]: i for i in self.gh.open_issues(fresh)}
        refs, strict = self._pr_refs(fresh), self._pr_refs(fresh, strict=True)
        items, section, in_list, first_free, walking, first = [], "", False, None, True, True
        in_progress = 0
        for line in body.splitlines():
            if line.startswith("## "):
                in_list = line.strip() == "## Work list"
                continue
            if not in_list:
                continue
            if line.startswith("### "):
                section = line[4:].strip()
                continue
            m = re.match(r"^- \[( |x)\] #(\d+)\s*(.*)", line)
            if not m:
                continue
            done, n, text = m[1] == "x", int(m[2]), m[3]
            if done:
                items.append({"n": n, "section": section, "text": text, "done": True})
                continue
            issue = issues.get(n)
            labels = lab(issue) if issue else []
            prs = refs.get(n, [])
            in_progress += "in-progress" in labels
            busy = bool(set(labels) & BUSY) or n in strict
            status = "closed (tick it)" if issue is None else ("busy" if busy else "free")
            # The Fixer's selection walk (agents/fixer.md step 3, dispatch.py roadmap_candidate):
            # past busy items only through parallel-ok ones; the first free item without it stops the walk.
            role = ""
            if issue is not None and walking:
                if busy:
                    role = "busy"
                elif first or "parallel-ok" in labels:
                    role, walking, first_free = "next", False, n
                else:
                    role, walking = "waits", False
            if issue is not None:
                first = False
            items.append({"n": n, "section": section, "text": text, "done": False, "closed": issue is None,
                          "title": issue["title"] if issue else "", "labels": labels, "prs": prs,
                          "status": status, "role": role, "url": f"https://github.com/{self.r}/issues/{n}"})
        open_items = [i for i in items if not i["done"] and not i.get("closed")]
        return {"items": items, "open": len(open_items), "done": sum(i["done"] for i in items),
                "in_progress": in_progress, "max_in_progress": d.MAX_IN_PROGRESS, "next_free": first_free,
                "roadmap_url": road["html_url"], "roadmap_author": road["user"]["login"]}

    def _pr_refs(self, fresh=False, strict=False):
        """Open PRs per issue. strict: only `Fixes|Refs|Closes #N` in the body, which makes the item busy;
        otherwise also yardstick drafts that say `for #N` or `(#N)` in the title."""
        refs = {}
        for pr in self.gh.open_pulls(fresh):
            text = pr["body"] or ""
            pat = r"(?:Fixes|Refs|Closes) #(\d+)\b"
            found = set(re.findall(pat, text))
            if not strict:
                found |= set(re.findall(r"(?:for )?#(\d+)\)", pr["title"]))
            for n in found:
                refs.setdefault(int(n), []).append(pr["number"])
        return refs

    # ---- PRs ----------------------------------------------------------------
    def prs(self, fresh=False):
        pulls = self.gh.open_pulls(fresh)

        def detail(pr):
            n, sha = pr["number"], pr["head"]["sha"]
            ci = self.gh.get(f"repos/{self.r}/commits/{sha}/check-runs?check_name=ci-ok", fresh=fresh)["check_runs"]
            checks = self.gh.get(f"repos/{self.r}/commits/{sha}/check-runs?per_page=100", fresh=fresh)["check_runs"]
            revs = [x for x in self.gh.get(f"repos/{self.r}/pulls/{n}/reviews?per_page=100", fresh=fresh) or []
                    if d.is_bot(x["user"])]
            full = self.gh.get(f"repos/{self.r}/pulls/{n}", fresh=fresh)
            files = self.gh.get(f"repos/{self.r}/pulls/{n}/files?per_page=100", True, fresh) or []
            failed = sorted({c["name"] for c in checks if c.get("conclusion") == "failure"})
            running = sorted({c["name"] for c in checks if c.get("status") != "completed"})
            rev = revs[-1] if revs else None
            return {
                "n": n, "title": pr["title"], "url": pr["html_url"], "draft": pr["draft"], "labels": lab(pr),
                "author": pr["user"]["login"], "branch": pr["head"]["ref"], "head": sha[:7],
                "updated": pr["updated_at"], "auto_merge": pr.get("auto_merge") is not None,
                "ci_ok": (ci[0].get("conclusion") or ci[0].get("status")) if ci else "none",
                "failed": failed, "running": running,
                "review": {"state": rev["state"], "on_head": rev["commit_id"] == sha, "url": rev["html_url"]} if rev else None,
                "mergeable_state": full.get("mergeable_state"),
                "protected": any(f["filename"].startswith(d.PROTECTED) for f in files),
                "files": len(files),
            }

        with cf.ThreadPoolExecutor(8) as ex:
            return {"prs": list(ex.map(detail, pulls))}

    # ---- issues ---------------------------------------------------------------
    def issues(self, fresh=False):
        body = self.gh.issue(d.ROADMAP, fresh)["body"] or ""
        listed = {int(m) for m in re.findall(r"#(\d+)", body)}
        refs = self._pr_refs(fresh)
        out = []
        for i in self.gh.open_issues(fresh):
            labels = lab(i)
            record = bool(set(labels) & d.RECORD_LABELS) or i["number"] in (d.ROADMAP, d.LOG_ISSUE)
            out.append({"n": i["number"], "title": i["title"], "url": i["html_url"], "labels": labels,
                        "created": i["created_at"], "updated": i["updated_at"], "comments": i["comments"],
                        "placed": i["number"] in listed, "record": record, "prs": refs.get(i["number"], [])})
        return {"issues": out}

    # ---- agents ---------------------------------------------------------------
    def agents(self, fresh=False):
        since = iso(now() - dt.timedelta(hours=48))
        comments = self.gh.get(f"repos/{self.r}/issues/comments?since={since}&per_page=100", True, fresh) or []
        last, counts = {}, {}
        for c in comments:
            body = c["body"] or ""
            role = next((r for r, mark in AGENTS.items() if body.startswith(mark) or mark in body[:200]), None)
            if role is None and d.is_bot(c["user"]):
                role = "reviewer"
            if role is None and c["user"]["login"] == "aray4702" and "sm-agent:" not in body:
                role = "owner"
            if role is None:
                continue
            counts[role] = counts.get(role, 0) + 1
            if role not in last or c["created_at"] > last[role]["at"]:
                n = int(c["issue_url"].rsplit("/", 1)[1])
                last[role] = {"at": c["created_at"], "url": c["html_url"], "n": n, "first": first_line(body)[:200]}
        # The Reviewer mostly posts PR reviews, which aren't issue comments.
        recent = self.gh.get(f"repos/{self.r}/pulls?state=all&sort=updated&direction=desc&per_page=15", fresh=fresh) or []
        with cf.ThreadPoolExecutor(8) as ex:
            reviews = ex.map(lambda pr: (pr, self.gh.get(f"repos/{self.r}/pulls/{pr['number']}/reviews?per_page=100",
                                                         fresh=fresh) or []), recent)
            for pr, revs in reviews:
                for rv in revs:
                    at = rv.get("submitted_at") or ""
                    if not d.is_bot(rv["user"]) or at < since:
                        continue
                    counts["reviewer"] = counts.get("reviewer", 0) + 1
                    if "reviewer" not in last or at > last["reviewer"]["at"]:
                        last["reviewer"] = {"at": at, "url": rv["html_url"], "n": pr["number"],
                                            "first": f"{rv['state'].lower()} on PR #{pr['number']}: "
                                                     + first_line(rv.get("body"))[:160]}
        log_body = self.gh.issue(d.LOG_ISSUE, fresh)["body"] or ""
        dispatched = dict(re.findall(r"<!-- dispatcher-last-([a-z_]+): ([0-9TZ:-]+) -->", log_body))
        t = now()
        roles = []
        for role in ("coordinator", "fixer", "reviewer", "tester", "builder"):
            cron, rids = SCHEDULES[role]
            nxt = next_cron(cron, t)
            roles.append({"role": role, "cron": cron, "routines": list(rids), "next": iso(nxt) if nxt else None,
                          "last_post": last.get(role), "posts_48h": counts.get(role, 0),
                          "last_dispatch": dispatched.get(role),
                          "last_dispatch_b": dispatched.get("fixer_b") if role == "fixer" else None})
        return {"roles": roles, "owner_last": last.get("owner"), "notify_last": dispatched.get("notify")}

    # ---- dispatcher -------------------------------------------------------------
    def dispatcher(self, fresh=False):
        runs = self.gh.get(f"repos/{self.r}/actions/workflows/{WORKFLOW}/runs?per_page=25", fresh=fresh)["workflow_runs"]
        try:
            mode = self.gh.get(f"repos/{self.r}/actions/variables/{MODE_VAR}", fresh=fresh)["value"]
        except RuntimeError:
            mode = "(unset: dry run)"
        return {"mode": mode, "stop": self.stop_issue(fresh),
                "runs": [{"id": r["id"], "event": r["event"], "status": r["status"], "conclusion": r["conclusion"],
                          "at": r["created_at"], "url": r["html_url"]} for r in runs]}

    def stop_issue(self, fresh=False):
        hit = [i for i in self.gh.open_issues(fresh) if i["title"].strip() == STOP_TITLE]
        return {"n": hit[0]["number"], "url": hit[0]["html_url"]} if hit else None

    # ---- log (#111) ---------------------------------------------------------------
    def log(self, fresh=False):
        issue = self.gh.issue(d.LOG_ISSUE, fresh)
        total = issue["comments"]
        page = max(1, (total + 29) // 30)
        comments = self.gh.get(f"repos/{self.r}/issues/{d.LOG_ISSUE}/comments?per_page=30&page={page}", fresh=fresh) or []
        if len(comments) < 15 and page > 1:
            comments = (self.gh.get(f"repos/{self.r}/issues/{d.LOG_ISSUE}/comments?per_page=30&page={page - 1}",
                                    fresh=fresh) or []) + comments
        out = []
        for c in comments:
            body = c["body"] or ""
            m = re.search(r"sm-agent: ([a-z]+)", body[:200])
            who = m[1] if m else ("reviewer" if d.is_bot(c["user"]) else "owner")
            out.append({"at": c["created_at"], "who": who, "url": c["html_url"],
                        "body": re.sub(r"<!--.*?-->\n?", "", body, flags=re.S).strip()})
        return {"comments": out[::-1], "url": issue["html_url"], "n": d.LOG_ISSUE}

    # ---- overview -------------------------------------------------------------------
    def overview(self, fresh=False):
        with cf.ThreadPoolExecutor(4) as ex:
            fw, fi, fd, fa = (ex.submit(self.work, fresh), ex.submit(self.issues, fresh),
                              ex.submit(self.dispatcher, fresh), ex.submit(self.agents, fresh))
            work, issues, disp, agents = fw.result(), fi.result(), fd.result(), fa.result()
        pulls = self.gh.open_pulls(fresh)
        needs_you = [{"kind": "issue", "n": i["n"], "title": i["title"], "url": i["url"]}
                     for i in issues["issues"] if NEEDS_YOU & set(i["labels"])]
        needs_you += [{"kind": "PR", "n": p["number"], "title": p["title"], "url": p["html_url"]}
                      for p in pulls if NEEDS_YOU & set(d.labels(p))]
        since = iso(now() - dt.timedelta(hours=24))
        log_comments = self.gh.get(f"repos/{self.r}/issues/{d.LOG_ISSUE}/comments?since={since}&per_page=100",
                                   True, fresh) or []
        asks = [{"at": c["created_at"], "url": c["html_url"], "first": first_line(c["body"])[:240]}
                for c in log_comments if re.match(r"\W*NEEDS YOU", first_line(c["body"]))]
        unplaced = [i for i in issues["issues"] if not i["placed"] and not i["record"]
                    and not {"stage:later", "stage:ai", "wontfix"} & set(i["labels"])]
        held = {k: [i["n"] for i in issues["issues"] if k in i["labels"]]
                for k in ("needs-coordinator", "needs-builder", "blocked", "in-progress")}
        last_run = disp["runs"][0] if disp["runs"] else None
        return {"work": {k: work[k] for k in ("open", "done", "in_progress", "max_in_progress", "next_free")},
                "next_items": [i for i in work["items"] if i.get("role")][:6],
                "needs_you": needs_you, "asks_24h": asks[::-1], "unplaced": unplaced, "held": held,
                "prs": len(pulls), "mode": disp["mode"], "stop": disp["stop"], "last_run": last_run,
                "agents": agents["roles"], "now": iso(now())}


class Actions:
    def __init__(self, gh, views):
        self.gh, self.v, self.r = gh, views, gh.r

    def run(self, a):
        kind = a.get("kind")
        n = int(a["n"]) if a.get("n") not in (None, "") else None
        if kind == "comment":
            body = (a.get("body") or "").strip()
            if not body:
                raise ValueError("empty comment")
            return self.gh.write(f"repos/{self.r}/issues/{n}/comments", "POST", {"body": body})["html_url"]
        if kind == "label":
            name = a["label"].strip()
            if a.get("op") == "add":
                self.gh.write(f"repos/{self.r}/issues/{n}/labels", "POST", raw={"labels": [name]})
            else:
                self.gh.write(f"repos/{self.r}/issues/{n}/labels/{urllib.parse.quote(name)}", "DELETE")
            return f"#{n}: {a.get('op')} {name}"
        if kind == "close":
            reason = a.get("reason", "completed")
            if reason not in ("completed", "not_planned"):
                raise ValueError("reason must be completed or not_planned")
            if (a.get("body") or "").strip():
                self.gh.write(f"repos/{self.r}/issues/{n}/comments", "POST", {"body": a["body"].strip()})
            self.gh.write(f"repos/{self.r}/issues/{n}", "PATCH", {"state": "closed", "state_reason": reason})
            return f"#{n} closed ({reason})"
        if kind == "merge":
            res = self.gh.write(f"repos/{self.r}/pulls/{n}/merge", "PUT", {"merge_method": "squash"})
            return f"#{n} merged: {res.get('sha', '')[:7]}"
        if kind == "stop":
            hit = self.v.stop_issue(fresh=True)
            if a.get("on"):
                if hit:
                    return f"already stopped (#{hit['n']})"
                res = self.gh.write(f"repos/{self.r}/issues", "POST", {
                    "title": STOP_TITLE,
                    "body": "The Owner stopped the agents from the local console. The Dispatcher does nothing "
                            "while this issue is open; close it to resume."})
                return f"stopped: {res['html_url']}"
            if not hit:
                return "not stopped"
            self.gh.write(f"repos/{self.r}/issues/{hit['n']}", "PATCH", {"state": "closed", "state_reason": "completed"})
            return f"resumed (closed #{hit['n']})"
        if kind == "mode":
            value = a.get("value")
            if value not in ("live", "dry"):
                raise ValueError("mode must be live or dry")
            self.gh.write(f"repos/{self.r}/actions/variables/{MODE_VAR}", "PATCH", {"name": MODE_VAR, "value": value})
            return f"{MODE_VAR}={value}"
        if kind == "dispatch_now":
            self.gh.write(f"repos/{self.r}/actions/workflows/{WORKFLOW}/dispatches", "POST", {"ref": "main"})
            return "dispatcher run requested"
        if kind == "dry_run":
            out = subprocess.run([sys.executable, os.path.join(HERE, "dispatch.py"), "--verbose", "--repo", self.r],
                                 capture_output=True, text=True, timeout=300)
            return out.stdout + (("\n" + out.stderr) if out.returncode else "")
        raise ValueError(f"unknown action {kind!r}")


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "sm-console"

    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (time.strftime("%H:%M:%S"), fmt % args))

    def _ok_host(self):
        port = self.server.server_address[1]
        return self.headers.get("Host") in (f"127.0.0.1:{port}", f"localhost:{port}")

    def _send(self, code, body, ctype="application/json"):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype + "; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _json(self, code, obj):
        self._send(code, json.dumps(obj))

    def do_GET(self):
        if not self._ok_host():
            return self._send(403, "bad host", "text/plain")
        url = urllib.parse.urlparse(self.path)
        if url.path == "/favicon.ico":  # the page sets its own icon; answer browsers that ask anyway
            self.send_response(204)
            self.end_headers()
            return
        if url.path == "/":
            with open(os.path.join(HERE, "console.html"), encoding="utf-8") as f:
                page = f.read().replace("__TOKEN__", self.server.token).replace("__REPO__", self.server.gh.r)
            return self._send(200, page, "text/html")
        view = url.path.removeprefix("/api/")
        if url.path.startswith("/api/") and view in ("overview", "work", "prs", "issues", "agents", "dispatcher", "log"):
            fresh = "fresh" in urllib.parse.parse_qs(url.query)
            try:
                return self._json(200, getattr(self.server.views, view)(fresh))
            except Exception as e:  # show the gh error in the page
                return self._json(502, {"error": str(e)})
        self._send(404, "not found", "text/plain")

    def do_POST(self):
        if not self._ok_host() or self.headers.get("X-Console-Token") != self.server.token:
            return self._send(403, "forbidden", "text/plain")
        if self.path != "/api/action":
            return self._send(404, "not found", "text/plain")
        try:
            a = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
            result = self.server.actions.run(a)
            sys.stderr.write(f"{time.strftime('%H:%M:%S')} ACTION {json.dumps({k: v for k, v in a.items() if k != 'body'})}\n")
            return self._json(200, {"ok": True, "result": result})
        except Exception as e:
            return self._json(400, {"ok": False, "error": str(e)})


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", type=int, default=8642)
    ap.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", "aray4702/streaming_media"))
    ap.add_argument("--open", action="store_true", help="open the page in the browser")
    args = ap.parse_args()
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    srv.token = secrets.token_urlsafe(24)
    srv.gh = GitHub(args.repo)
    srv.views = Views(srv.gh)
    srv.actions = Actions(srv.gh, srv.views)
    url = f"http://127.0.0.1:{args.port}/"
    print(f"console for {args.repo} at {url} (Ctrl-C to stop)")
    if args.open:
        webbrowser.open(url)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    sys.exit(main())
