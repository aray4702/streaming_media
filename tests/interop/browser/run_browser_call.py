#!/usr/bin/env python3
"""Runs a call between headless Chrome and a peer (C6 part 4, #1088).

The driver:
  * starts a tiny signaling server on 127.0.0.1 (HTTP, standard library
    only) that serves page.html and relays JSON messages between the
    parties "browser" (Chrome) and "native" (the peer);
  * starts the native peer, if one is given, and bridges it to the server
    over its stdin/stdout: each line it reads on stdin is a message from
    Chrome, each JSON line it writes to stdout is a message to Chrome,
    except {"type": "result", ...}, its verdict. Other lines are logged;
  * opens the page in headless Chrome with fake media devices (no CDP
    client is needed: the page runs the call and posts its verdict back);
  * prints both verdicts and exits 0 when every check passed, 1 when one
    failed, and 77 (ctest's SKIP_RETURN_CODE) when no Chrome is found.

Usage:
  run_browser_call.py --self [--role=offer|answer]       harness self-test
  run_browser_call.py --peer=PATH [--role=offer|answer]  call our endpoint

`--role=offer` means Chrome offers. The peer is run as
`PATH --role=<its role> --ip=<host address>`, where its role is the
opposite of Chrome's and the host address is the one Chrome gathers host
candidates on (Chrome skips loopback interfaces). Chrome is found from
$SM_CHROME, then the usual install paths and PATH names. With
SM_BROWSER_REQUIRED=1 (CI's browser-interop job) a missing Chrome fails
instead of skipping.
"""

import argparse
import http.server
import json
import os
import platform
import queue
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

SKIP = 77
HERE = os.path.dirname(os.path.abspath(__file__))


def find_chrome():
    env = os.environ.get('SM_CHROME')
    if env:
        return env if os.path.exists(env) else None
    candidates = [
        '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
        '/Applications/Chromium.app/Contents/MacOS/Chromium',
        '/Applications/Google Chrome for Testing.app/Contents/MacOS/Google Chrome for Testing',
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    for name in ('google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser', 'chrome'):
        p = shutil.which(name)
        if p:
            return p
    return None


def host_address():
    """The address of the interface with the default route (no packet is sent)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('192.0.2.1', 9))  # TEST-NET-1: never routed to, only used to pick an interface.
        return s.getsockname()[0]
    except OSError:
        return '127.0.0.1'
    finally:
        s.close()


class Relay:
    """Message queues per party, plus the page's verdict."""

    def __init__(self):
        self.lock = threading.Lock()
        self.queues = {'browser': [], 'native': []}
        self.listeners = {}  # party -> callable(msg): delivers instead of queueing.
        self.result = queue.Queue()

    def send(self, to, msg):
        listener = self.listeners.get(to)
        if listener:
            listener(msg)
            return
        with self.lock:
            self.queues.setdefault(to, []).append(msg)

    def poll(self, me):
        with self.lock:
            out, self.queues[me] = self.queues.get(me, []), []
        return out


def make_handler(relay, verbose):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            if verbose:
                sys.stderr.write('[http] ' + fmt % args + '\n')

        def _reply(self, code, body, ctype='application/json'):
            data = body.encode() if isinstance(body, str) else body
            self.send_response(code)
            self.send_header('Content-Type', ctype)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(data)

        def _query(self):
            path, _, q = self.path.partition('?')
            args = dict(p.split('=', 1) for p in q.split('&') if '=' in p)
            return path, args

        def do_GET(self):
            path, args = self._query()
            if path in ('/', '/page.html'):
                with open(os.path.join(HERE, 'page.html'), 'rb') as f:
                    self._reply(200, f.read(), 'text/html; charset=utf-8')
            elif path == '/poll':
                self._reply(200, json.dumps(relay.poll(args.get('me', ''))))
            else:
                self._reply(404, '{}')

        def do_POST(self):
            path, args = self._query()
            n = int(self.headers.get('Content-Length', '0'))
            try:
                msg = json.loads(self.rfile.read(n) or b'null')
            except ValueError:
                self._reply(400, '{}')
                return
            if path == '/send':
                relay.send(args.get('to', ''), msg)
                self._reply(200, '{}')
            elif path == '/result':
                relay.result.put(msg)
                self._reply(200, '{}')
            else:
                self._reply(404, '{}')

    return Handler


class NativePeer:
    """The peer process, bridged to the relay over stdin/stdout."""

    def __init__(self, cmd, relay, verbose):
        self.relay = relay
        self.verbose = verbose
        self.result = queue.Queue()
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     text=True, bufsize=1)
        relay.listeners['native'] = self.deliver
        threading.Thread(target=self._read, daemon=True).start()

    def deliver(self, msg):
        if self.verbose:
            sys.stderr.write('[to peer] %s\n' % json.dumps(msg)[:200])
        try:
            self.proc.stdin.write(json.dumps(msg) + '\n')
            self.proc.stdin.flush()
        except (BrokenPipeError, ValueError):
            pass

    def _read(self):
        for line in self.proc.stdout:
            line = line.strip()
            try:
                msg = json.loads(line)
            except ValueError:
                msg = None
            if not isinstance(msg, dict) or 'type' not in msg:
                sys.stderr.write('[peer] %s\n' % line)
                continue
            if msg['type'] == 'result':
                self.result.put(msg)
            else:
                if self.verbose:
                    sys.stderr.write('[from peer] %s\n' % line[:200])
                self.relay.send('browser', msg)
        self.result.put(None)

    def stop(self):
        try:
            self.proc.stdin.close()
        except OSError:
            pass
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def chrome_command(chrome, profile, url):
    cmd = [chrome, '--headless=new', '--user-data-dir=' + profile, '--no-first-run',
           '--no-default-browser-check', '--disable-gpu', '--disable-extensions',
           '--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream',
           '--autoplay-policy=no-user-gesture-required', '--remote-debugging-port=0', url]
    if platform.system() == 'Linux' and hasattr(os, 'geteuid') and os.geteuid() == 0:
        cmd.insert(1, '--no-sandbox')
    return cmd


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--self', dest='self_test', action='store_true', help='harness self-test, no native peer')
    ap.add_argument('--peer', help='the native peer program')
    ap.add_argument('--role', choices=('offer', 'answer'), default='offer', help="Chrome's role")
    ap.add_argument('--timeout', type=float, default=40.0, help='seconds for the whole call')
    ap.add_argument('--no-data-channel', action='store_true',
                    help='no data channel and no data checks (our endpoint has none until C7 is wired in)')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()
    if not args.self_test and not args.peer:
        ap.error('give --self or --peer')

    chrome = find_chrome()
    if not chrome:
        if os.environ.get('SM_BROWSER_REQUIRED') == '1':
            print('FAIL: no Chrome found and SM_BROWSER_REQUIRED=1')
            return 1
        print('SKIPPED: no Chrome found (set SM_CHROME to its path)')
        return SKIP

    relay = Relay()
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), make_handler(relay, args.verbose))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]

    peer = None
    if args.peer:
        peer_role = 'answer' if args.role == 'offer' else 'offer'
        peer = NativePeer([args.peer, '--role=' + peer_role, '--ip=' + host_address()], relay, args.verbose)

    page_timeout_ms = int((args.timeout - 10) * 1000)
    url = 'http://127.0.0.1:%d/page.html?role=%s&timeout_ms=%d%s' % (
        port, args.role, page_timeout_ms, ('&self=1' if args.self_test else '') + ('&dc=0' if args.no_data_channel else ''))
    profile = tempfile.mkdtemp(prefix='sm-chrome-')
    print('chrome: %s' % chrome)
    print('page:   %s' % url)
    proc = subprocess.Popen(chrome_command(chrome, profile, url), stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE if not args.verbose else None)
    page = None
    try:
        page = relay.result.get(timeout=args.timeout)
    except queue.Empty:
        print('FAIL: the page posted no result within %.0f s' % args.timeout)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        shutil.rmtree(profile, ignore_errors=True)

    native = None
    if peer:
        try:
            native = peer.result.get(timeout=10)
        except queue.Empty:
            pass
        peer.stop()
    server.shutdown()

    ok = bool(page and page.get('ok'))
    if page:
        for line in page.get('log', []):
            print('[page] ' + line)
        print('chrome checks: %s' % json.dumps(page.get('checks', {}), sort_keys=True))
        if page.get('browser'):
            print('chrome stats:  %s' % json.dumps(page['browser'].get('stats', {}), sort_keys=True))
        if page.get('error'):
            print('page error: %s' % page['error'])
    if peer and page and page.get('browser'):
        # What Chrome received from our endpoint: Opus and H.264 (Constrained Baseline is all it offers).
        stats = page['browser'].get('stats', {})
        codecs = (stats.get('audio', {}).get('inCodec'), stats.get('video', {}).get('inCodec'))
        if codecs != ('audio/opus', 'video/H264'):
            print('FAIL: Chrome received %s, not Opus and H.264' % (codecs,))
            ok = False
    if peer:
        if native is None:
            print('FAIL: the native peer gave no result')
            ok = False
        else:
            print('peer checks:   %s' % json.dumps(native.get('checks', {}), sort_keys=True))
            if native.get('stats'):
                print('peer stats:    %s' % json.dumps(native['stats'], sort_keys=True))
            ok = ok and bool(native.get('ok'))
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
