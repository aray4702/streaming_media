#!/usr/bin/env python3
"""Calls over real networks through the self-hosted signaling and TURN server (C12 part 2, #1121).

Runs against SM_SIGNALING_URL (https://<host>:8443: wss://<host>:8443/ws and GET /turn), each case direct and relay
only (TURN candidates only):

  a  sm_call_smoke (the iOS app's C++ call controller) -> the AI server
  b  headless Chrome with the web app -> the AI server
  c  headless Chrome with the web app <-> sm_call_smoke in one room (the web <-> iOS path)
  d  sm_call_smoke built for the iOS simulator, in the simulator -> the AI server (with --sim and --ios-smoke)

Each client writes one JSON line per call ({"type": "call-result", ...}); this collects them (--out appends them to
a file) and prints a Markdown table. The clients measure the conversation the same way (apps/call/call_smoke.cc,
apps/web/index.html): the user speaks 1.5 s turns of speech-like audio, response_ms is the end of that speech to the
AI's first audio played at the caller, and with barge-in every other turn talks over the reply 1 s into it.

The AI server: --ai=remote uses the one deployed on the host (it answers `ai-` rooms); --ai=local starts
out/native/apps/ai_server/sm_ai_server here, connected to the same host over wss, so the call still goes through
the real signaling and TURN server; --ai=auto (the default) tries the host's first and falls back to a local one.

The web app is served from 127.0.0.1 (a secure context for getUserMedia) with this checkout's apps/web/index.html;
its /turn is fetched from SM_SIGNALING_URL through this script, and its WebSocket goes to the host directly.

Usage:
  SM_SIGNALING_URL=https://<host>:8443 tools/real_calls.py [--cases=a,b,c] [--paths=direct,relay] [--seconds=25]
      [--ai=auto|remote|local] [--out=calls.jsonl] [--sim=UDID --ios-smoke=PATH]
  tools/real_calls.py --table=calls.jsonl       # the Markdown table of calls already run
Exit status: 0 when every call connected and played received audio, 1 otherwise, 77 when SM_SIGNALING_URL is unset.
"""

import argparse
import datetime
import http.server
import json
import os
import queue
import secrets
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tests', 'interop', 'browser'))
from run_browser_call import chrome_command, find_chrome  # noqa: E402

SKIP = 77


def endpoints(base):
    """wss:// signaling URL and /turn URL for an https:// (or http://) service base URL."""
    u = urllib.parse.urlsplit(base.rstrip('/'))
    if u.scheme not in ('https', 'http') or not u.netloc:
        raise ValueError('SM_SIGNALING_URL must be https://HOST:PORT, not %r' % base)
    ws = ('wss' if u.scheme == 'https' else 'ws') + '://' + u.netloc + '/ws'
    return ws, u.scheme + '://' + u.netloc + '/turn'


def room(prefix):
    return prefix + secrets.token_hex(3)


def parse_result(text):
    """The last {"type": "call-result"} line of a client's output, or None."""
    found = None
    for line in text.splitlines():
        line = line.strip()
        if line.startswith('{') and '"call-result"' in line:
            try:
                found = json.loads(line)
            except ValueError:
                pass
    return found


def flatten(r):
    """One flat dict per call: sm_call_smoke nests the library's stats under "stats"."""
    out = dict(r)
    out.update(r.get('stats', {}))
    out.pop('stats', None)
    return out


def ok(r):
    if not r or r.get('error'):
        return False
    f = flatten(r)
    if f.get('client') == 'chrome':
        return f.get('connection') == 'connected' and f.get('audio_packets_received', 0) > 100
    return f.get('state') == 'connected' and f.get('audio_played_s', 0) > 1


def fmt(v, unit=''):
    if v is None or v == -1 or v == '':
        return '-'
    if isinstance(v, float):
        v = ('%.1f' % v).rstrip('0').rstrip('.') if abs(v) < 100 else '%d' % round(v)
    return '%s%s' % (v, unit)


COLUMNS = ['date', 'case', 'client', 'path', 'selected pair', 'connect ms', 'RTT ms', 'response ms (transport)',
           'barge-in stop ms', 'concealed %', 'jitter buffer ms', 'lost', 'target kbps', 'video fps / freezes']


def row(r):
    f = flatten(r)
    if f.get('error'):
        return [f.get('date', ''), f.get('case', ''), f.get('client', ''), f.get('path', ''),
                'FAILED: ' + f['error']] + [''] * (len(COLUMNS) - 5)
    pair = '%s/%s %s' % (f.get('local_type') or '?', f.get('remote_type') or '?', f.get('protocol') or '')
    # A prflx candidate learned on a TURN allocation keeps its relay protocol (RTCIceCandidateStats.relayProtocol).
    if f.get('relay_protocol'):
        pair += ', relayed (TURN over %s)' % f['relay_protocol']
    resp = '-'
    if f.get('replies'):
        resp = '%s (%s)' % (fmt(f.get('response_ms_p50')), fmt(f.get('response_transport_ms_p50')))
        if f.get('missed_replies'):
            resp += ', %d missed' % f['missed_replies']
    barge = fmt(f.get('barge_in_stop_ms_p50'))
    if f.get('barge_ins_not_stopped'):
        barge += ', %d not stopped' % f['barge_ins_not_stopped']
    fps = f.get('video_fps', f.get('video_render_fps'))
    video = '-' if not fps else '%s / %s' % (fmt(float(fps)), fmt(f.get('video_freezes', 0)))
    return [f.get('date', ''), f.get('case', ''), f.get('client', ''), f.get('path', ''), pair,
            fmt(f.get('connect_ms')),
            fmt(f.get('rtt_ms')), resp, barge, fmt(f.get('audio_concealed_pct')), fmt(f.get('jitter_buffer_ms')),
            fmt(f.get('audio_packets_lost')), fmt(f.get('target_kbps')), video]


def table(results):
    lines = ['| ' + ' | '.join(COLUMNS) + ' |', '|' + '---|' * len(COLUMNS)]
    for r in results:
        lines.append('| ' + ' | '.join(str(c) for c in row(r)) + ' |')
    return '\n'.join(lines)


# --- Clients ----------------------------------------------------------------------------------------------------------

def smoke_command(binary, ws, room_name, seconds, relay, barge, video, label, prefix=(), talk=True):
    cmd = list(prefix) + [binary, '--signaling=' + ws, '--room=' + room_name, '--seconds=%g' % seconds,
                          '--label=' + label]
    if relay:
        cmd.append('--relay')
    if barge:
        cmd.append('--barge')
    if video:
        cmd.append('--video')
    if not talk:
        cmd.append('--no-talk')
    return cmd


def run_smoke(cmd, seconds):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=seconds + 40)
    except subprocess.TimeoutExpired:
        return {'error': 'timed out'}
    r = parse_result(p.stdout)
    if r is None:
        tail = (p.stderr.strip().splitlines() or ['no output'])[-1]
        return {'error': 'no result (exit %d: %s)' % (p.returncode, tail[:120])}
    return r


class LocalPage:
    """Serves the web app on 127.0.0.1, proxies /turn to the host and collects POST /result."""

    def __init__(self, turn_url):
        self.results = queue.Queue()
        page = os.path.join(ROOT, 'apps', 'web', 'index.html')
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def reply(self, code, ctype, body):
                self.send_response(code)
                self.send_header('Content-Type', ctype)
                self.send_header('Content-Length', str(len(body)))
                self.send_header('Cache-Control', 'no-store')
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self):
                path = urllib.parse.urlsplit(self.path)
                if path.path in ('/', '/index.html'):
                    with open(page, 'rb') as f:
                        self.reply(200, 'text/html; charset=utf-8', f.read())
                elif path.path == '/turn':
                    try:
                        q = ('?' + path.query) if path.query else ''
                        with urllib.request.urlopen(turn_url + q, timeout=5) as r:
                            self.reply(200, 'application/json', r.read())
                    except OSError:
                        self.reply(502, 'text/plain', b'no /turn')
                else:
                    self.reply(404, 'text/plain', b'not found')

            def do_POST(self):
                body = self.rfile.read(int(self.headers.get('Content-Length', 0)))
                if self.path == '/result':
                    try:
                        owner.results.put(json.loads(body))
                    except ValueError:
                        pass
                self.reply(200, 'application/json', b'{}')

        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.url = 'http://127.0.0.1:%d' % self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def close(self):
        self.server.shutdown()


def run_chrome(chrome, page, ws, room_name, seconds, relay, converse, barge, label):
    q = {'room': room_name, 'auto': '1', 'ws': ws, 'seconds': '%g' % seconds, 'result': '1', 'label': label}
    if relay:
        q['relay'] = '1'
    if converse:
        q['converse'] = '1'
    if barge:
        q['barge'] = '1'
    url = page.url + '/?' + urllib.parse.urlencode(q)
    profile = tempfile.mkdtemp(prefix='sm-chrome-')
    proc = subprocess.Popen(chrome_command(chrome, profile, url), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.time() + seconds + 40
        while time.time() < deadline:
            try:
                r = page.results.get(timeout=1)
            except queue.Empty:
                continue
            if r.get('label') == label:
                return r
        return {'error': 'no result from Chrome'}
    finally:
        proc.terminate()
        try:
            proc.wait(10)
        except subprocess.TimeoutExpired:
            proc.kill()
        shutil.rmtree(profile, ignore_errors=True)


# --- Main -------------------------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--cases', default='a,b,c', help='comma-separated: a, b, c, d')
    ap.add_argument('--paths', default='direct,relay')
    ap.add_argument('--seconds', type=float, default=25)
    ap.add_argument('--ai', choices=('auto', 'remote', 'local'), default='auto')
    ap.add_argument('--no-barge', action='store_true', help='no scripted barge-in')
    ap.add_argument('--build', default=os.path.join(ROOT, 'out', 'native'))
    ap.add_argument('--out', help='append the JSON lines to this file')
    ap.add_argument('--sim', help='iOS simulator UDID (case d)')
    ap.add_argument('--ios-smoke', help='sm_call_smoke built for the simulator (case d)')
    ap.add_argument('--table', help='print the table of the JSON lines in this file and exit')
    args = ap.parse_args()
    if args.table:
        with open(args.table) as f:
            print(table([json.loads(line) for line in f if line.strip()]))
        return 0

    base = os.environ.get('SM_SIGNALING_URL')
    if not base:
        print('SKIPPED: SM_SIGNALING_URL is not set')
        return SKIP
    ws, turn = endpoints(base)
    cases = [c.strip() for c in args.cases.split(',') if c.strip()]
    paths = [p.strip() for p in args.paths.split(',') if p.strip()]
    smoke = os.path.join(args.build, 'apps', 'call', 'sm_call_smoke')
    ai_bin = os.path.join(args.build, 'apps', 'ai_server', 'sm_ai_server')
    barge = not args.no_barge
    date = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d %H:%M UTC')
    results = []
    ai_proc = None
    tmp = tempfile.mkdtemp(prefix='sm-real-calls-')

    def record(case, path, r):
        r = dict(r)
        r.update({'date': date, 'case': case, 'path': path, 'ai_server': ai_where})
        r.setdefault('client', 'sm_call_smoke')
        results.append(r)
        print(json.dumps(r, sort_keys=True), flush=True)
        if args.out:
            with open(args.out, 'a') as f:
                f.write(json.dumps(r, sort_keys=True) + '\n')

    ai_where = args.ai
    if args.ai == 'auto':
        probe = run_smoke(smoke_command(smoke, ws, room('ai-probe-'), 8, False, False, False, 'probe'), 8)
        ai_where = 'remote' if ok(probe) else 'local'
        print('AI server: %s (the host\'s %s)' % (ai_where, 'answered' if ai_where == 'remote' else 'did not answer'),
              flush=True)
    if ai_where == 'local' and any(c in ('a', 'b', 'd') for c in cases):
        ai_proc = subprocess.Popen([ai_bin, '--signaling=' + ws, '--metrics=' + os.path.join(tmp, 'ai.jsonl')],
                                   stdout=subprocess.DEVNULL, stderr=open(os.path.join(tmp, 'ai.log'), 'w'))
        time.sleep(2)

    chrome = find_chrome() if any(c in ('b', 'c') for c in cases) else None
    page = LocalPage(turn) if chrome else None
    try:
        for case in cases:
            for path in paths:
                relay = path == 'relay'
                label = '%s-%s-%s' % (case, path, secrets.token_hex(2))
                if case == 'a':
                    record(case, path, run_smoke(smoke_command(smoke, ws, room('ai-rc-'), args.seconds, relay, barge,
                                                               False, label), args.seconds))
                elif case == 'b':
                    if not chrome:
                        record(case, path, {'client': 'chrome', 'error': 'no Chrome found'})
                        continue
                    record(case, path, run_chrome(chrome, page, ws, room('ai-rc-'), args.seconds, relay, True, barge,
                                                  label))
                elif case == 'c':
                    if not chrome:
                        record(case, path, {'client': 'chrome', 'error': 'no Chrome found'})
                        continue
                    name = room('rc-')
                    # sm_call_smoke joins first (with a synthetic camera), Chrome joins a moment later and offers.
                    box = {}
                    t = threading.Thread(target=lambda: box.update(r=run_smoke(
                        smoke_command(smoke, ws, name, args.seconds + 4, relay, False, True, label + '-smoke', talk=False),
                        args.seconds + 4)))
                    t.start()
                    time.sleep(2)
                    web = run_chrome(chrome, page, ws, name, args.seconds, relay, False, False, label)
                    t.join()
                    record(case, path, web)
                    record(case, path, box.get('r', {'error': 'no result'}))
                elif case == 'd':
                    if not (args.sim and args.ios_smoke):
                        print('case d needs --sim and --ios-smoke', file=sys.stderr)
                        continue
                    cmd = smoke_command(args.ios_smoke, ws, room('ai-rc-'), args.seconds, relay, barge, False, label,
                                        prefix=('xcrun', 'simctl', 'spawn', args.sim))
                    r = run_smoke(cmd, args.seconds)
                    r['client'] = 'sm_call_smoke (iOS simulator)'
                    record(case, path, r)
    finally:
        if page:
            page.close()
        if ai_proc:
            ai_proc.terminate()
            ai_proc.wait(10)
            print('local AI server log and metrics: %s' % tmp)
    print()
    print('AI server: %s' % ai_where)
    print(table(results))
    return 0 if results and all(ok(r) for r in results) else 1


if __name__ == '__main__':
    sys.exit(main())
