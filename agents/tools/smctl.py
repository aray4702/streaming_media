#!/usr/bin/env python3
"""Command line for the agent system: status in tabs, the Owner's controls, and the local dashboard.

Same data and actions as the web console (agents/tools/console.py), in the terminal.

Status (one tab, printed):
  smctl overview | requirements | work | prs | issues | agents | dispatcher | log   [--fresh] [--all]
  smctl                      interactive tabs (←/→ or 1-8 switch, ↑/↓ scroll, r refresh,
                             : runs a command below, q quits)

Manage (each asks y/N; --yes skips it):
  smctl comment N "text"     comment as the Owner (no agent marker)
  smctl label add|remove N LABEL
  smctl close N [--reason completed|not_planned] [-m "comment"]
  smctl merge N              squash-merge a PR
  smctl stop | resume        open or close the STOP AGENTS issue
  smctl mode live|dry        set SM_DISPATCH_MODE
  smctl dispatch             start the dispatcher workflow now
  smctl dry-run              local dry run of dispatch.py (writes nothing)

Dashboard (the web console, in the background):
  smctl dashboard start [--port 8642] [--open] | stop | status | open
"""
import argparse
import datetime as dt
import json
import os
import shlex
import signal
import subprocess
import sys
import textwrap
import time
import urllib.request
import webbrowser
from zoneinfo import ZoneInfo

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import console as c  # noqa: E402  (views and actions shared with the web console)

PT = ZoneInfo("America/Los_Angeles")
TABS = ["overview", "requirements", "work", "prs", "issues", "agents", "dispatcher", "log"]
STATE_DIR = os.path.join(os.path.expanduser("~"), ".cache", "sm-console")
STATE = os.path.join(STATE_DIR, "dashboard.json")
DEFAULT_PORT = 8642


# ---- formatting ---------------------------------------------------------------
def when(iso):
    if not iso:
        return "-"
    t = dt.datetime.strptime(iso, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=dt.timezone.utc)
    mins = round((c.now() - t).total_seconds() / 60)
    a = abs(mins)
    rel = f"{a} min" if a < 60 else f"{round(a / 60)} h" if a < 2880 else f"{round(a / 1440)} d"
    return f"{t.astimezone(PT):%a %-I:%M %p} ({rel} ago)" if mins >= 0 else f"{t.astimezone(PT):%a %-I:%M %p} (in {rel})"


def labels(ls):
    return " ".join(f"[{x}]" for x in ls)


def cut(s, n):
    s = " ".join((s or "").replace("**", "").split())
    return s if len(s) <= n else s[: n - 1] + "…"


def render(tab, d, width=100, show_all=False):
    """Lines of text for one tab's data (the dict a console view returns)."""
    w = max(60, width)
    out = []
    add = out.append
    if tab == "requirements":
        if d.get("missing"):
            add("The register (tools/requirements.json) is not on main yet.")
            return out
        add(f"STAGE 1 REQUIREMENTS: {d['done']} of {d['total']} done")
        add("")
        add(f"  {'area':<44} {'done':>5} {'in prog':>8} {'open':>5} {'unassessed':>11}")
        for a, cnt in d["areas"].items():
            add(f"  {cut(a, 44):<44} {cnt['done']:>5} {cnt['in-progress']:>8} {cnt['open']:>5} {cnt['unassessed']:>11}")
        for p in d["problems"]:
            add(f"  ! {p}")
        add("")
        for q in d["requirements"]:
            if not show_all and q["status"] in ("done", "unassessed"):
                continue
            items = ", ".join(f"#{i['n']}" for i in q["items"]) or "-"
            add(f"  {q['id']:<6} {q['status']:<11} {cut(q['title'], w - 40)}  [{items}]")
        if not show_all:
            add("  (in progress and open only; --all lists every requirement)")
        return out
    if tab == "overview":
        rq = d.get("requirements") or {}
        add("REQUIREMENTS  " + ("register not on main yet" if rq.get("missing") else f"{rq.get('done')} of {rq.get('total')} done"))
        add("")
        wk = d["work"]
        add("NEEDS YOU")
        for x in d["needs_you"] or []:
            add(f"  {x['kind']} {'#' + str(x['n']) if x['n'] else ''}  {cut(x['title'], w - 14)}")
        if not d["needs_you"]:
            add("  nothing labelled needs-owner")
        if d["asks_24h"]:
            add("  NEEDS YOU posts on #111, last 24 h (a later post may withdraw one):")
            for a in d["asks_24h"]:
                add(f"    {when(a['at'])}  {cut(a['first'], w - 30)}")
        add("")
        add(f"WORK ORDER (#64)  {wk['open']} open, {wk['done']} done · in-progress {wk['in_progress']}/{wk['max_in_progress']}"
            f" · next free {('#' + str(wk['next_free'])) if wk['next_free'] else 'none'}")
        for i in d["next_items"]:
            add(f"  #{i['n']:<5} {i['role']:<6} {cut(i['title'], w - 16)}")
        add("")
        add("HELD")
        for k, ns in d["held"].items():
            add(f"  {k:<18} {', '.join('#' + str(n) for n in ns) or '-'}")
        add(f"  {'unplaced':<18} {', '.join('#' + str(i['n']) for i in d['unplaced']) or '-'}")
        add("")
        add(f"DISPATCHER  mode {d['mode']} · {'STOPPED (#' + str(d['stop']['n']) + ')' if d['stop'] else 'agents running'}"
            f" · open PRs {d['prs']}")
        r = d["last_run"]
        if r:
            add(f"  last run {when(r['at'])} {r['conclusion'] or r['status']} ({r['event']})")
        add("")
        add("AGENTS        last post                    next run")
        for a in d["agents"]:
            nxt = when(a["next"]) if a["next"] else ("on demand" if a["enabled"] else "off")
            add(f"  {a['label']:<12}{when(a['last_post']['at']) if a['last_post'] else 'none in 48 h':<29}{nxt}")
    elif tab == "work":
        add(f"{d['open']} open, {d['done']} done · in-progress {d['in_progress']}/{d['max_in_progress']}"
            f" · next free {('#' + str(d['next_free'])) if d['next_free'] else 'none'}")
        add("next = a Fixer takes it now; busy = held; waits = free but not parallel-ok, so the walk stops")
        section = None
        for i in d["items"]:
            if i["done"] and not show_all:
                continue
            if i["section"] != section:
                section = i["section"]
                add("")
                add(section)
            if i["done"]:
                add(f"  [x] #{i['n']:<5} {cut(i['text'], w - 14)}")
                continue
            st = i["status"] + (f"/{i['role']}" if i["role"] and i["role"] != i["status"] else "")
            prs = " ".join(f"PR#{p}" for p in i["prs"])
            add(f"  [ ] #{i['n']:<5} {st:<12} {cut(i['title'] or i['text'], w - 30)}")
            if i["labels"] or prs:
                add(f"      {labels(i['labels'])} {prs}".rstrip())
    elif tab == "prs":
        for p in d["prs"]:
            rv = f"{p['review']['state'].lower()}{'' if p['review']['on_head'] else ' (older head)'}" if p["review"] else "no review"
            flags = " ".join(x for x in ("draft" if p["draft"] else "", "protected" if p["protected"] else "") if x)
            add(f"#{p['n']}  {cut(p['title'], w - 8)}")
            add(f"   ci-ok {p['ci_ok']} · {rv} · auto-merge {'on' if p['auto_merge'] else 'off'} · {p['mergeable_state']} {flags}")
            if p["failed"]:
                add(f"   failed: {', '.join(p['failed'])}")
            if p["running"]:
                add(f"   running: {', '.join(p['running'])}")
            add(f"   {p['branch']} @ {p['head']} · updated {when(p['updated'])} {labels(p['labels'])}")
            add("")
        if not d["prs"]:
            add("No open PRs.")
    elif tab == "issues":
        for i in d["issues"]:
            place = "record" if i["record"] else "on #64" if i["placed"] else "NOT ON #64"
            add(f"#{i['n']:<5} {place:<10} {cut(i['title'], w - 18)}")
            add(f"       {labels(i['labels'])}{'  PRs ' + ' '.join('#' + str(p) for p in i['prs']) if i['prs'] else ''}")
    elif tab == "agents":
        for a in d["roles"]:
            lp = a["last_post"]
            add(f"{a['label'].upper()}  {a['schedule']}{' (' + a['cron'] + ' UTC)' if a['cron'] else ''}"
                f" · next {when(a['next']) if a['next'] else 'only when triggered'} · {a['model']}")
            for x in a["triggers"]:
                add(f"  + {cut(x, w - 6)}")
            add(f"  last post  {when(lp['at']) + ' on #' + str(lp['n']) if lp else 'none in 48 h'} · {a['posts_48h']} posts in 48 h")
            if lp:
                add(f"             {cut(lp['first'], w - 14)}")
            add(f"  last started by the Dispatcher  {when(a['last_dispatch']) if a['last_dispatch'] else 'not yet'}")
            if a.get("start_failing"):
                add(f"  START FAILING since {when(a['start_failing']['since'])}: {a['start_failing']['error']}")
            add(f"  routine {a['name']} ({a['routine']})")
            add("")
        add(f"Your last own post: {when(d['owner_last']['at']) if d['owner_last'] else 'none in 48 h'}"
            f" · last Discord forward: {when(d['notify_last'])}")
    elif tab == "dispatcher":
        add(f"mode {d['mode']} · {'STOPPED by #' + str(d['stop']['n']) if d['stop'] else 'agents running (no STOP AGENTS issue)'}")
        add("")
        add("recent runs")
        for r in d["runs"]:
            add(f"  {when(r['at']):<28} {r['event']:<20} {r['conclusion'] or r['status']}")
    elif tab == "log":
        for cm in d["comments"]:
            add(f"── {cm['who']} · {when(cm['at'])} · {cm['url']}")
            for para in cm["body"].splitlines():
                out.extend(textwrap.wrap(para, w - 2, initial_indent="  ", subsequent_indent="  ") or [""])
            add("")
    return out


# ---- the system -----------------------------------------------------------------
class System:
    def __init__(self, repo):
        self.gh = c.GitHub(repo)
        self.views = c.Views(self.gh)
        self.actions = c.Actions(self.gh, self.views)

    def view(self, tab, fresh=False):
        return getattr(self.views, tab)(fresh)


def describe(a):
    k = a["kind"]
    return {
        "comment": lambda: f"Post a comment on #{a.get('n')} as you:\n  {cut(a.get('body', ''), 200)}",
        "label": lambda: f"{a.get('op', '').capitalize()} label '{a.get('label')}' on #{a.get('n')}",
        "close": lambda: f"Close #{a.get('n')} as {a.get('reason')}" + (" with your comment" if a.get("body") else ""),
        "merge": lambda: f"Squash-merge PR #{a.get('n')} into main",
        "stop": lambda: "Stop the agents (open a STOP AGENTS issue)" if a.get("on") else "Resume the agents (close STOP AGENTS)",
        "mode": lambda: f"Set SM_DISPATCH_MODE={a.get('value')}",
        "dispatch_now": lambda: "Start a dispatcher workflow run now",
        "dry_run": lambda: "",
    }[k]()


def action_from_args(ns):
    k = ns.cmd
    if k == "comment":
        return {"kind": "comment", "n": ns.n, "body": " ".join(ns.text)}
    if k == "label":
        return {"kind": "label", "op": ns.op, "n": ns.n, "label": ns.label}
    if k == "close":
        return {"kind": "close", "n": ns.n, "reason": ns.reason, "body": ns.m or ""}
    if k == "merge":
        return {"kind": "merge", "n": ns.n}
    if k in ("stop", "resume"):
        return {"kind": "stop", "on": k == "stop"}
    if k == "mode":
        return {"kind": "mode", "value": ns.value}
    if k == "dispatch":
        return {"kind": "dispatch_now"}
    if k == "dry-run":
        return {"kind": "dry_run"}
    return None


# ---- dashboard (the web console in the background) --------------------------------
def dash_state():
    try:
        with open(STATE) as f:
            s = json.load(f)
        os.kill(s["pid"], 0)
        return s
    except (OSError, ValueError, KeyError):
        return None


def dash_up(port):
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=2) as r:
            return r.status == 200 and b"<title>streaming media</title>" in r.read(4096)
    except OSError:
        return False


def dashboard(ns):
    s = dash_state()
    if ns.op == "status":
        if s:
            print(f"running: pid {s['pid']}, http://127.0.0.1:{s['port']}/ (since {when(s['started'])}, log {s['log']})")
        else:
            print("not running")
        return 0
    if ns.op == "open":
        if not s:
            print("not running; start it with: smctl dashboard start --open")
            return 1
        webbrowser.open(f"http://127.0.0.1:{s['port']}/")
        return 0
    if ns.op == "stop":
        if not s:
            print("not running")
            return 0
        os.kill(s["pid"], signal.SIGTERM)
        for _ in range(50):
            try:
                os.kill(s["pid"], 0)
            except OSError:
                break
            time.sleep(0.1)
        os.remove(STATE)
        print(f"stopped (pid {s['pid']})")
        return 0
    # start
    if s:
        print(f"already running: http://127.0.0.1:{s['port']}/ (pid {s['pid']})")
        if ns.open:
            webbrowser.open(f"http://127.0.0.1:{s['port']}/")
        return 0
    os.makedirs(STATE_DIR, exist_ok=True)
    log = os.path.join(STATE_DIR, "dashboard.log")
    with open(log, "a") as lf:
        p = subprocess.Popen([sys.executable, os.path.join(HERE, "console.py"), "--port", str(ns.port), "--repo", ns.repo],
                             stdout=lf, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, start_new_session=True)
    for _ in range(50):
        if p.poll() is not None:
            print(f"failed to start (exit {p.returncode}); see {log}")
            return 1
        if dash_up(ns.port):
            break
        time.sleep(0.1)
    else:
        print(f"started pid {p.pid} but it doesn't answer on port {ns.port}; see {log}")
    with open(STATE, "w") as f:
        json.dump({"pid": p.pid, "port": ns.port, "started": c.iso(c.now()), "log": log}, f)
    print(f"dashboard running: http://127.0.0.1:{ns.port}/ (pid {p.pid}); stop it with: smctl dashboard stop")
    if ns.open:
        webbrowser.open(f"http://127.0.0.1:{ns.port}/")
    return 0


# ---- interactive tabs --------------------------------------------------------------
def tui(system, parser, start_tab="overview"):
    import curses

    def main(scr):
        curses.curs_set(0)
        curses.use_default_colors()
        for i, col in enumerate((curses.COLOR_CYAN, curses.COLOR_RED, curses.COLOR_GREEN, curses.COLOR_YELLOW), 1):
            curses.init_pair(i, col, -1)
        tab, data, scroll, msg = TABS.index(start_tab), {}, {t: 0 for t in TABS}, ""

        def load(fresh=False):
            nonlocal msg
            name = TABS[tab]
            draw(loading=True)
            try:
                data[name] = (time.time(), system.view(name, fresh))
                msg = ""
            except Exception as e:  # show gh errors in the status line
                msg = f"error: {e}"

        def draw(loading=False):
            scr.erase()
            h, w = scr.getmaxyx()
            x = 0
            for i, t in enumerate(TABS):
                label = f" {i + 1} {t} "
                if x + len(label) >= w:
                    break
                scr.addstr(0, x, label, curses.A_REVERSE | curses.A_BOLD if i == tab else curses.A_DIM)
                x += len(label) + 1
            scr.hline(1, 0, curses.ACS_HLINE, w)
            name = TABS[tab]
            if loading or name not in data:
                scr.addstr(2, 1, "Loading from GitHub…", curses.A_DIM)
                lines = []
            else:
                lines = render(name, data[name][1], w - 2)
                top = scroll[name] = max(0, min(scroll[name], max(0, len(lines) - (h - 4))))
                for row, line in enumerate(lines[top: top + h - 4]):
                    attr = 0
                    if line.isupper() or line.startswith(("NEEDS YOU", "WORK ORDER", "HELD", "DISPATCHER", "AGENTS")):
                        attr = curses.color_pair(1) | curses.A_BOLD
                    if "needs-owner" in line or "STOPPED" in line or "failed:" in line or "NOT ON #64" in line:
                        attr = curses.color_pair(2)
                    scr.addnstr(2 + row, 1, line, w - 2, attr)
            stamp = f"updated {dt.datetime.fromtimestamp(data[name][0], PT):%-I:%M %p} PT" if name in data else ""
            help_ = "←/→ tabs  ↑/↓ scroll  r refresh  : command  q quit"
            scr.hline(h - 2, 0, curses.ACS_HLINE, w)
            scr.addnstr(h - 1, 0, (msg or help_).ljust(w - len(stamp) - 2) + stamp, w - 1,
                        curses.color_pair(2) if msg.startswith("error") else curses.A_DIM)
            scr.refresh()

        def prompt(text):
            h, w = scr.getmaxyx()
            curses.curs_set(1)
            curses.echo()
            scr.move(h - 1, 0)
            scr.clrtoeol()
            scr.addnstr(h - 1, 0, text, w - 1, curses.A_BOLD)
            s = scr.getstr(h - 1, len(text), 300).decode(errors="replace")
            curses.noecho()
            curses.curs_set(0)
            return s.strip()

        def command(line):
            nonlocal msg, tab
            try:
                ns = parser.parse_args(shlex.split(line))
            except SystemExit:
                msg = f"error: not a command: {line} (try: label add 409 blocked)"
                return
            if ns.cmd in TABS:
                tab = TABS.index(ns.cmd)
                return load(True)
            if ns.cmd == "dashboard":
                curses.def_prog_mode()
                curses.endwin()
                dashboard(ns)
                input("press Enter to return")
                curses.reset_prog_mode()
                return
            a = action_from_args(ns)
            if a is None:
                msg = f"error: {ns.cmd} isn't available here"
                return
            if a["kind"] != "dry_run" and prompt(describe(a).splitlines()[0] + "? [y/N] ").lower() != "y":
                msg = "cancelled"
                return
            draw(loading=True)
            try:
                res = system.actions.run(a)
            except Exception as e:
                msg = f"error: {e}"
                return
            if a["kind"] == "dry_run":
                curses.def_prog_mode()
                curses.endwin()
                print(res)
                input("press Enter to return")
                curses.reset_prog_mode()
                msg = "dry run done"
            else:
                msg = f"done: {cut(str(res), 120)}"
                data.clear()
            load(True)

        load()
        while True:
            draw()
            k = scr.getch()
            name = TABS[tab]
            h = scr.getmaxyx()[0]
            if k in (ord("q"), 27):
                return
            if k in (curses.KEY_RIGHT, ord("\t"), ord("l")):
                tab = (tab + 1) % len(TABS)
            elif k in (curses.KEY_LEFT, curses.KEY_BTAB, ord("h")):
                tab = (tab - 1) % len(TABS)
            elif ord("1") <= k <= ord(str(len(TABS))):
                tab = k - ord("1")
            elif k in (curses.KEY_DOWN, ord("j")):
                scroll[name] += 1
            elif k in (curses.KEY_UP, ord("k")):
                scroll[name] = max(0, scroll[name] - 1)
            elif k in (curses.KEY_NPAGE, ord(" ")):
                scroll[name] += h - 5
            elif k == curses.KEY_PPAGE:
                scroll[name] = max(0, scroll[name] - (h - 5))
            elif k == ord("g"):
                scroll[name] = 0
            elif k == ord("r"):
                load(True)
                continue
            elif k == ord(":"):
                line = prompt(":")
                if line:
                    command(line)
                continue
            if TABS[tab] not in data or time.time() - data[TABS[tab]][0] > c.CACHE_TTL_S * 2:
                load()

    curses.wrapper(main)


# ---- entry ----------------------------------------------------------------------------
def build_parser():
    p = argparse.ArgumentParser(prog="smctl", description=__doc__.splitlines()[0],
                                formatter_class=argparse.RawDescriptionHelpFormatter,
                                epilog="\n".join(__doc__.splitlines()[2:]))
    p.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", "aray4702/streaming_media"))
    p.add_argument("-y", "--yes", action="store_true", help="don't ask before acting")
    sub = p.add_subparsers(dest="cmd")
    for t in TABS:
        s = sub.add_parser(t, help=f"show the {t} tab")
        s.add_argument("--fresh", action="store_true", help="bypass the 60 s cache")
        s.add_argument("--all", action="store_true", help="work: include done items")
        s.add_argument("--json", action="store_true", help="print the raw data")
    s = sub.add_parser("comment", help="comment as the Owner")
    s.add_argument("n", type=int)
    s.add_argument("text", nargs="+")
    s = sub.add_parser("label", help="add or remove a label")
    s.add_argument("op", choices=["add", "remove"])
    s.add_argument("n", type=int)
    s.add_argument("label")
    s = sub.add_parser("close", help="close an issue")
    s.add_argument("n", type=int)
    s.add_argument("--reason", choices=["completed", "not_planned"], default="completed")
    s.add_argument("-m", help="comment to post first")
    s = sub.add_parser("merge", help="squash-merge a PR")
    s.add_argument("n", type=int)
    sub.add_parser("stop", help="open the STOP AGENTS issue")
    sub.add_parser("resume", help="close the STOP AGENTS issue")
    s = sub.add_parser("mode", help="set SM_DISPATCH_MODE")
    s.add_argument("value", choices=["live", "dry"])
    sub.add_parser("dispatch", help="start the dispatcher workflow now")
    sub.add_parser("dry-run", help="local dry run of dispatch.py")
    s = sub.add_parser("dashboard", help="the web console in the background")
    s.add_argument("op", choices=["start", "stop", "status", "open"])
    s.add_argument("--port", type=int, default=DEFAULT_PORT)
    s.add_argument("--open", action="store_true", help="start: open it in the browser")
    s.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", "aray4702/streaming_media"))
    s = sub.add_parser("tui", help="interactive tabs (the default on a terminal)")
    s.add_argument("tab", nargs="?", choices=TABS, default="overview")
    return p


def main(argv=None):
    parser = build_parser()
    ns = parser.parse_args(argv)
    if ns.cmd == "dashboard":
        return dashboard(ns)
    system = System(ns.repo)
    if ns.cmd in (None, "tui"):
        if not sys.stdout.isatty():
            ns.cmd, ns.fresh, ns.all, ns.json = "overview", False, False, False
        else:
            return tui(system, build_parser(), getattr(ns, "tab", "overview"))
    if ns.cmd in TABS:
        d = system.view(ns.cmd, ns.fresh)
        if ns.json:
            print(json.dumps(d, indent=1))
        else:
            width = os.get_terminal_size().columns if sys.stdout.isatty() else 110
            print("\n".join(render(ns.cmd, d, width, ns.all)))
        return 0
    a = action_from_args(ns)
    if a["kind"] != "dry_run" and not ns.yes:
        if not sys.stdin.isatty():
            print("refusing to act without a terminal to confirm; pass --yes", file=sys.stderr)
            return 2
        if input(describe(a) + "? [y/N] ").strip().lower() != "y":
            print("cancelled")
            return 1
    try:
        print(system.actions.run(a))
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
