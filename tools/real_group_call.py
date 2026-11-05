#!/usr/bin/env python3
"""A real group call through the deployed LiveKit SFU (C21, #1180): 4 web pages, the native client and the AI.

One fresh room (a random name) on the LiveKit server behind SM_SIGNALING_URL (https://<host>:8443: LiveKit's
signaling at wss://<host>:8443/rtc, tokens from GET /lk-token; docs/deploy/livekit.md), with up to 6 participants:

  --pages (4)   headless Chrome pages with the web app's group mode (apps/web/index.html ?group=1): Chrome's fake
                camera, published with simulcast (the SDK's q/h/f layers, H.264, adaptive stream and dynacast on), and
                a quiet fake microphone (-60 dBFS noise, so the pages are heard but are not the active speaker)
  native        sm_call_smoke --livekit --video: the iOS app's controller with a synthetic 640x360 camera, talking
                in 1.5 s turns of speech-like audio and timing the AI's replies
  ai            sm_ai_server --livekit: the AI participant, answering the active speaker's turns

The web app is served from 127.0.0.1 (a secure context for getUserMedia) with this checkout's apps/web/index.html and
its vendored livekit-client; its /lk-token is fetched from the host through this script, and the page connects to
LiveKit on the host directly (lk=wss://<host>:8443). Each page posts its group report (report=1) here once a second:
per remote participant the received audio and video (inbound-rtp: size, fps, frames, freezes, loss, jitter buffer
delay, the subscriber's RTT) and its own simulcast layers (outbound-rtp per RID: bytes, size, fps, active,
qualityLimitationReason). The native clients print one JSON line of stats a second (livekit_group: per participant).
The window starts when every participant is in the room and ends when the native client's call ends; rates, frames
and freezes are the differences over it.

Usage:
  SM_SIGNALING_URL=https://<host>:8443 tools/real_group_call.py [--pages=4] [--seconds=60] [--no-native] [--no-ai]
      [--out=DIR] [--doc=docs/results/real-group-call.md] [--smoke=PATH --ai-server=PATH]
  tools/real_group_call.py --summary=DIR/run.json [--doc=...]   # the tables of a run already made
Exit status: 0 when every participant joined and every page received every other's audio (and video from those that
send it), 1 otherwise, 77 when SM_SIGNALING_URL is unset or its /lk-token does not answer.
"""

import argparse
import collections
import datetime
import http.server
import json
import math
import os
import random
import re
import secrets
import shutil
import socket
import struct
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
MAX_PARTICIPANTS = 6  # The server's room.max_participants.
NATIVE_SLACK_S = 15    # The native call's length beyond the window: the room fills within this after it joins.
RUNS_MARKER = '<!-- runs: tools/real_group_call.py inserts each run below, newest first -->'


def endpoints(base):
    """(LiveKit wss:// URL, /lk-token URL) for an https:// (or http://) service base URL."""
    u = urllib.parse.urlsplit(base.rstrip('/'))
    if u.scheme not in ('https', 'http') or not u.netloc:
        raise ValueError('SM_SIGNALING_URL must be https://HOST:PORT, not %r' % base)
    return ('wss' if u.scheme == 'https' else 'ws') + '://' + u.netloc, u.scheme + '://' + u.netloc + '/lk-token'


def probe(token_url):
    """True when /lk-token issues a token (the server is up and has its key pair)."""
    try:
        q = urllib.parse.urlencode({'room': 'probe-' + secrets.token_hex(3), 'identity': 'probe'})
        with urllib.request.urlopen(token_url + '?' + q, timeout=10) as r:
            return bool(json.loads(r.read()).get('token'))
    except (OSError, ValueError):
        return False


def write_quiet_wav(path, seconds=10, dbfs=-60):
    """48 kHz mono noise at `dbfs` (apps/web/test_group_call.py's): heard, but never an active speaker."""
    amp = 32767 * 10 ** (dbfs / 20)
    rng = random.Random(1)
    n = 48000 * seconds
    data = struct.pack('<%dh' % n, *[int(rng.uniform(-amp, amp) * math.sqrt(3)) for _ in range(n)])
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ' +
                struct.pack('<IHHIIHH', 16, 1, 1, 48000, 96000, 2, 16) + b'data' + struct.pack('<I', len(data)) + data)


def json_lines(lines, key):
    """The (t, object) of each timestamped line that is a JSON object containing `key`."""
    out = []
    for t, line in lines:
        if line.startswith('{') and key in line:
            try:
                out.append((t, json.loads(line)))
            except ValueError:
                pass
    return out


# --- The page server --------------------------------------------------------------------------------------------------

class PageServer:
    """Serves the web app and its vendored SDK on 127.0.0.1, proxies /lk-token to the host and keeps every POST /report
    (with its arrival time) by page."""

    def __init__(self, token_url):
        self.reports = collections.defaultdict(list)  # peer -> [(t, report)]
        self.lock = threading.Lock()
        web = os.path.join(ROOT, 'apps', 'web')
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
                    with open(os.path.join(web, 'index.html'), 'rb') as f:
                        self.reply(200, 'text/html; charset=utf-8', f.read())
                elif path.path == '/vendor/livekit-client/livekit-client.umd.js':
                    with open(os.path.join(web, 'vendor', 'livekit-client', 'livekit-client.umd.js'), 'rb') as f:
                        self.reply(200, 'text/javascript', f.read())
                elif path.path == '/lk-token':
                    try:
                        with urllib.request.urlopen(token_url + '?' + path.query, timeout=10) as r:
                            self.reply(200, 'application/json', r.read())
                    except OSError as e:
                        self.reply(502, 'application/json', json.dumps({'error': str(e)}).encode())
                else:
                    self.reply(404, 'text/plain', b'not found')

            def do_POST(self):
                body = self.rfile.read(int(self.headers.get('Content-Length', 0)))
                if self.path == '/report':
                    try:
                        r = json.loads(body)
                    except ValueError:
                        r = None
                    if isinstance(r, dict) and r.get('peer') and 'group' in r:
                        with owner.lock:
                            owner.reports[r['peer']].append((time.time(), r['group']))
                self.reply(200, 'application/json', b'{}')

        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.url = 'http://127.0.0.1:%d' % self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def snapshot(self):
        with self.lock:
            return {peer: list(rs) for peer, rs in self.reports.items()}

    def close(self):
        self.server.shutdown()


class Probe:
    """Once a second: the time of GET /lk-token over one kept-alive HTTPS connection (the path to the host and its
    nginx, without LiveKit), the time of a new TCP connection's handshake to the same port (the host's kernel
    answers it: the path's queueing, not the server's processes), a TCP handshake to a reference host elsewhere
    (`reference`, HOST:PORT: this machine's own link), and this machine's 1-minute load average; to tell a congested
    link or server from a loaded client machine."""

    def __init__(self, token_url, reference=None):
        self.samples = []  # (t, token_ms or None, load1, connect_ms or None, reference_connect_ms or None)
        self.reference = reference
        self.stop_flag = threading.Event()
        u = urllib.parse.urlsplit(token_url)
        self.u = u
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _conn(self):
        import http.client
        cls = http.client.HTTPSConnection if self.u.scheme == 'https' else http.client.HTTPConnection
        return cls(self.u.netloc, timeout=10)

    @staticmethod
    def _connect_ms(host, port):
        try:
            c0 = time.time()
            socket.create_connection((host, port), timeout=5).close()
            return 1000 * (time.time() - c0)
        except OSError:
            return None

    def _run(self):
        conn = None
        while not self.stop_flag.is_set():
            t = time.time()
            ms = None
            try:
                if conn is None:
                    conn = self._conn()
                    conn.connect()
                start = time.time()
                conn.request('GET', self.u.path + '?room=probe&identity=probe')
                conn.getresponse().read()
                ms = 1000 * (time.time() - start)
            except OSError:
                conn = None
            connect_ms = self._connect_ms(self.u.hostname, self.u.port or 443)
            ref_ms = None
            if self.reference:
                host, _, port = self.reference.rpartition(':')
                ref_ms = self._connect_ms(host, int(port))
            self.samples.append((t, ms, os.getloadavg()[0], connect_ms, ref_ms))
            self.stop_flag.wait(max(0.0, 1.0 - (time.time() - t)))

    def stop(self):
        self.stop_flag.set()
        self.thread.join(15)


class Proc:
    """A native client: its stdout lines with arrival times, its stderr in a log file."""

    def __init__(self, cmd, log_path):
        self.cmd = cmd
        self.lines = []
        self.log_path = log_path
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=open(log_path, 'wb'), text=True,
                                     errors='replace')
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self):
        for line in self.proc.stdout:
            self.lines.append((time.time(), line.rstrip()))

    def stop(self, timeout=10):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.thread.join(5)


# --- Summaries (pure: tested by tools/test_real_group_call.py) --------------------------------------------------------

def in_window(samples, t0, t1):
    return [(t, s) for t, s in samples if t0 <= t <= t1]


def rate(first, last, key, scale=1.0):
    """The rate of a cumulative counter between two (t, dict) samples, per second times `scale`."""
    dt = last[0] - first[0]
    if dt <= 0 or key not in last[1] or key not in first[1]:
        return None
    return max(0.0, (last[1][key] - first[1][key]) * scale / dt)


def delta(first, last, key):
    if key not in last[1] or key not in first[1]:
        return None
    return last[1][key] - first[1][key]


def sizes(samples, wkey, hkey):
    """The received sizes over the samples, as 'WxH NN%' by share of samples, largest share first ('-': none)."""
    count = collections.Counter('%dx%d' % (s[wkey], s[hkey]) for _, s in samples if s.get(wkey))
    total = sum(count.values())
    if not total:
        return '-'
    return ', '.join('%s %d%%' % (k, round(100 * v / total)) for k, v in count.most_common())


def page_receive(samples):
    """Per remote participant: what one page received over the window. samples: [(t, group report)]."""
    out = {}
    if len(samples) < 2:
        return out
    ids = set()
    for _, g in samples:
        ids.update(g.get('participants', {}))
    for pid in sorted(ids):
        series = [(t, g['participants'][pid]) for t, g in samples if pid in g.get('participants', {})]
        if len(series) < 2:
            continue
        a, b = series[0], series[-1]
        out[pid] = {
            'video': sizes(series, 'videoWidth', 'videoHeight'),
            'fps': rate(a, b, 'videoFrames'),
            'video_kbps': rate(a, b, 'videoBytesIn', 8 / 1000),
            'audio_kbps': rate(a, b, 'audioBytesIn', 8 / 1000),
            'freezes': delta(a, b, 'videoFreezes'),
            'freeze_ms': delta(a, b, 'videoFreezeMs'),
            'video_lost': delta(a, b, 'videoPacketsLost'),
            'audio_lost': delta(a, b, 'audioPacketsLost'),
            'audio_received': delta(a, b, 'audioPacketsReceived'),
            'video_jb_ms': b[1].get('videoJitterBufferMs'),
            'audio_jb_ms': b[1].get('audioJitterBufferMs'),
            'rtt_ms': b[1].get('rttMs'),
            'codec': (b[1].get('videoCodec') or '').replace('video/', ''),
        }
    return out


def page_send(samples):
    """One page's uplink over the window: per simulcast layer (RID) kbps, size, fps, the share of samples it was
    active, and its quality limitation; audio kbps."""
    if len(samples) < 2:
        return {}
    layers = collections.OrderedDict()
    for t, g in samples:
        for layer in g.get('sending', {}).get('layers', []):
            layers.setdefault(layer['rid'] or '-', []).append((t, layer))
    out = {'layers': collections.OrderedDict()}
    for rid, series in layers.items():
        a, b = series[0], series[-1]
        sent = [l for _, l in series if l.get('width')]
        reasons = collections.Counter(l.get('qualityLimitation') or 'none' for _, l in series)
        out['layers'][rid] = {
            'kbps': rate(a, b, 'bytesSent', 8 / 1000),
            'size': sizes(series, 'width', 'height'),
            'fps': (sum(l.get('fps', 0) for l in sent) / len(sent)) if sent else 0,
            'active_pct': round(100 * sum(1 for _, l in series if l.get('active')) / len(series)),
            'limitation': ', '.join('%s %d%%' % (k, round(100 * v / len(series))) for k, v in reasons.most_common()),
            'encoder': b[1].get('encoder', ''),
        }
    a, b = samples[0], samples[-1]
    out['audio_kbps'] = rate((a[0], a[1].get('sending', {})), (b[0], b[1].get('sending', {})), 'audioBytesOut',
                             8 / 1000)
    return out


def speaker_shares(samples, key):
    """Share of the samples in which each identity was among the active speakers; key selects the list (a page's
    'activeSpeakers') or the single identity (the native client's 'livekit_active_speaker')."""
    count = collections.Counter()
    for _, s in samples:
        v = s.get(key)
        for pid in (v if isinstance(v, list) else [v] if v else []):
            count[pid] += 1
    n = len(samples)
    return {pid: round(100 * c / n) for pid, c in count.most_common()} if n else {}


def native_receive(samples):
    """Per remote participant: what a native client received over the window (its livekit_group lines)."""
    out = {}
    if len(samples) < 2:
        return out
    ids = set()
    for _, s in samples:
        ids.update(p['identity'] for p in s.get('livekit_group', []))
    for pid in sorted(ids):
        series = [(t, p) for t, s in samples for p in s.get('livekit_group', []) if p['identity'] == pid]
        if len(series) < 2:
            continue
        a, b = series[0], series[-1]
        out[pid] = {
            'video': sizes(series, 'video_width', 'video_height'),
            'fps': rate(a, b, 'video_frames_decoded') if b[1].get('has_video') else None,
            'audio_lost': delta(a, b, 'audio_packets_lost'),
            'audio_received': delta(a, b, 'audio_packets_received'),
            'audio_concealed_pct': b[1].get('audio_concealed_pct'),
            'audio_jitter_ms': b[1].get('audio_jitter_ms'),
            'audio_played_s': delta(a, b, 'audio_played_s'),
        }
    return out


def native_send(samples):
    if not samples:
        return {}
    vals = [s for _, s in samples]
    mean = lambda k: sum(v.get(k, 0) for v in vals) / len(vals)  # noqa: E731
    last = vals[-1]
    fps = rate(samples[0], samples[-1], 'video_frames_sent')
    video = '-'
    if last.get('video_send_width'):
        video = '%dx%d' % (last['video_send_width'], last.get('video_send_height', 0))
    elif fps:
        video = 'video'  # Sent, size not in its stats.
    rtts = [v['rtt_ms'] for v in vals if v.get('rtt_ms', -1) >= 0]
    sig = [v['livekit_signal_rtt_ms'] for v in vals if v.get('livekit_signal_rtt_ms', -1) >= 0]
    return {'send_kbps': mean('send_kbps'), 'target_kbps': mean('target_kbps'), 'opus_kbps': mean('opus_kbps'),
            'target_kbps_min': min((v.get('target_kbps', 0) for v in vals), default=None),
            'video': video, 'fps': fps, 'rtt_ms': last.get('rtt_ms'),
            'rtt_ms_p50': percentile(rtts, 50), 'rtt_ms_max': max(rtts) if rtts else None,
            'signal_rtt_ms_p50': percentile(sig, 50), 'signal_rtt_ms_max': max(sig) if sig else None,
            'jitter_buffer_ms': last.get('jitter_buffer_ms'), 'pair': '%s/%s %s' % (
                last.get('local_type') or '?', last.get('remote_type') or '?', last.get('protocol') or '')}


def percentile(values, q):
    v = sorted(values)
    if not v:
        return None
    return v[min(len(v) - 1, max(0, int(round(q / 100 * (len(v) - 1)))))]


def summarize(run):
    """The run's per-receiver and per-sender figures, from its raw samples (run.json)."""
    t0, t1 = run['window']
    s = {'pages': {}, 'native': None, 'ai': None, 'window_s': t1 - t0}
    probe = in_window([(p[0], p) for p in run.get('probe', [])], t0, t1)
    token_ms = [p[1] for _, p in probe if p[1] is not None]
    connect_ms = [p[3] for _, p in probe if len(p) > 3 and p[3] is not None]
    ref_ms = [p[4] for _, p in probe if len(p) > 4 and p[4] is not None]
    s['probe'] = {'token_ms_p50': percentile(token_ms, 50), 'token_ms_max': max(token_ms) if token_ms else None,
                  'connect_ms_p50': percentile(connect_ms, 50),
                  'connect_ms_max': max(connect_ms) if connect_ms else None,
                  'reference_ms_p50': percentile(ref_ms, 50), 'reference_ms_max': max(ref_ms) if ref_ms else None,
                  'token_failed': sum(1 for _, p in probe if p[1] is None),
                  'load1_max': max((p[2] for _, p in probe), default=None)}
    for peer, samples in run['reports'].items():
        w = in_window(samples, t0, t1)
        s['pages'][peer] = {'receive': page_receive(w), 'send': page_send(w),
                            'speakers': speaker_shares(w, 'activeSpeakers'),
                            'speakers_seen': w[-1][1].get('speakersSeen', []) if w else [],
                            'count': max((g.get('count', 0) for _, g in w), default=0)}
    if run.get('native') is not None:
        w = in_window(json_lines(run['native'], '"livekit_group"'), t0, t1)
        result = [o for _, o in json_lines(run['native'], '"call-result"')]
        s['native'] = {'receive': native_receive(w), 'send': native_send(w),
                       'speakers': speaker_shares(w, 'livekit_active_speaker'),
                       'speakers_seen': w[-1][1].get('livekit_speakers_seen', []) if w else [],
                       'result': result[-1] if result else None}
    if run.get('ai') is not None:
        lines = json_lines(run['ai'], '"ai-call-metrics"')
        w = in_window([(t, o['stats']) for t, o in lines if 'stats' in o], t0, t1)
        s['ai'] = {'receive': native_receive(w), 'send': native_send(w),
                   'final': {k: v for k, v in lines[-1][1].items() if k != 'stats'} if lines else None}
    return s


def fmt(v, unit='', nd=None):
    if v is None or v == '':
        return '-'
    if isinstance(v, float):
        if nd is not None:
            v = '%.*f' % (nd, v)
        else:
            v = ('%.1f' % v).rstrip('0').rstrip('.') if abs(v) < 100 else '%d' % round(v)
    return '%s%s' % (v, unit)


RECEIVE_COLUMNS = ['receiver', 'sender', 'video received (share of samples)', 'fps', 'video kbps', 'freezes (ms)',
                   'video lost', 'audio lost / received', 'jitter buffer ms (video / audio)', 'RTT ms']


def receive_rows(summary, names):
    rows = []
    for peer in sorted(summary['pages'], key=lambda p: names.get(p, p)):
        for pid, r in summary['pages'][peer]['receive'].items():
            freezes = '-' if r['freezes'] is None else '%d (%d)' % (r['freezes'], r['freeze_ms'] or 0)
            rows.append([names.get(peer, peer), names.get(pid, pid),
                         r['video'] + (' ' + r['codec'] if r['codec'] and r['video'] != '-' else ''),
                         fmt(r['fps']) if r['video'] != '-' else '-',
                         fmt(r['video_kbps']) if r['video'] != '-' else '-',
                         freezes if r['video'] != '-' else '-', fmt(r['video_lost']) if r['video'] != '-' else '-',
                         '%s / %s' % (fmt(r['audio_lost']), fmt(r['audio_received'])),
                         '%s / %s' % (fmt(r['video_jb_ms']) if r['video'] != '-' else '-', fmt(r['audio_jb_ms'])),
                         fmt(r['rtt_ms'])])
    for who, key in (('native', 'native'), ('AI', 'ai')):
        node = summary.get(key)
        if not node:
            continue
        for pid, r in node['receive'].items():
            rows.append([names.get(who, who), names.get(pid, pid), r['video'],
                         fmt(r['fps']) if r['fps'] is not None else '-', '-', '-', '-',
                         '%s / %s' % (fmt(r['audio_lost']), fmt(r['audio_received'])),
                         '- / -', '-'])
    return rows


SEND_COLUMNS = ['sender', 'layer (RID)', 'size', 'fps', 'kbps', 'active (share of samples)', 'quality limitation',
                'encoder']


def send_rows(summary, names):
    rows = []
    for peer in sorted(summary['pages'], key=lambda p: names.get(p, p)):
        send = summary['pages'][peer]['send']
        for rid, l in send.get('layers', {}).items():
            rows.append([names.get(peer, peer), rid, l['size'], fmt(l['fps']), fmt(l['kbps']),
                         '%d%%' % l['active_pct'] + (' (paused)' if l['active_pct'] == 0 else ''), l['limitation'],
                         l['encoder'] or '-'])
        rows.append([names.get(peer, peer), 'audio', '-', '-', fmt(send.get('audio_kbps')), '-', '-', '-'])
    for who, key in (('native', 'native'), ('AI', 'ai')):
        node = summary.get(key)
        if node and node['send']:
            s = node['send']
            video = s['video'] != '-'
            rows.append([names.get(who, who), 'audio + video (one layer)' if video else 'audio only',
                         s['video'] if s['video'] != 'video' else '-', fmt(s['fps']) if video else '-',
                         '%s (target %s, Opus %s)' % (fmt(s['send_kbps']), fmt(s['target_kbps']),
                                                      fmt(s['opus_kbps'])),
                         '-', '-', 'library (VideoToolbox H.264)' if video else '-'])
    return rows


def md_table(columns, rows):
    lines = ['| ' + ' | '.join(columns) + ' |', '|' + '---|' * len(columns)]
    for r in rows:
        lines.append('| ' + ' | '.join(str(c) for c in r) + ' |')
    return '\n'.join(lines)


def names_of(run):
    """Short names: page 1..n in join order (first report), native, AI."""
    names = {}
    for i, peer in enumerate(sorted(run['reports'], key=lambda p: run['reports'][p][0][0] if run['reports'][p] else 0)):
        names[peer] = 'page %d (%s)' % (i + 1, peer)
    if run.get('native_id'):
        names[run['native_id']] = 'native (%s)' % run['native_id']
        names['native'] = names[run['native_id']]
    if run.get('ai_id'):
        names[run['ai_id']] = 'AI (%s)' % run['ai_id']
        names['AI'] = names[run['ai_id']]
    return names


def report(run, summary):
    """The run's section of the results doc (Markdown)."""
    names = names_of(run)
    out = ['## Run %s' % run['date_pt'], '']
    joined = run.get('participants_max', 0)
    out.append('- Room `%s` on `<host>` (LiveKit %s), %d of %d participants in the room at once: %s.' % (
        run['room'], run.get('livekit_version', '?'), joined, run.get('participants_planned', 0),
        run.get('mix', '')))
    out.append('- Window: %.0f s, from the moment every page counted all the participants (%.1f s after the first '
               'page started).' % (summary['window_s'], run.get('all_in_s', 0)))
    p = summary.get('probe') or {}
    if p.get('token_ms_p50') is not None:
        out.append('- The path to the host without LiveKit, once a second: GET /lk-token on a kept-alive HTTPS '
                   'connection took %s ms (median), %s ms at most%s; a TCP handshake to the host\'s port %s ms '
                   '(median), %s ms at most. This Mac\'s 1-minute load average peaked at %s (%s cores).' % (
                       fmt(p['token_ms_p50']), fmt(p['token_ms_max']),
                       ', %d failed' % p['token_failed'] if p['token_failed'] else '',
                       fmt(p.get('connect_ms_p50')), fmt(p.get('connect_ms_max')), fmt(p['load1_max']),
                       run.get('cpus', '?')))
        if p.get('reference_ms_p50') is not None:
            out[-1] += (' A TCP handshake to %s (elsewhere on the internet, through the same home link) took %s ms '
                        '(median), %s ms at most.' % (run.get('reference', '?'), fmt(p['reference_ms_p50']),
                                                       fmt(p['reference_ms_max'])))
    for who, key in (('native client', 'native'), ('AI', 'ai')):
        s = (summary.get(key) or {}).get('send') or {}
        if s.get('rtt_ms_p50') is not None:
            out.append('- The %s\'s publisher connection: RTT %s ms median, %s ms at most; LiveKit signaling '
                       '(WebSocket ping) RTT %s ms median, %s ms at most; congestion controller target %s kbps mean, '
                       '%s kbps lowest.' % (who, fmt(s['rtt_ms_p50']), fmt(s['rtt_ms_max']),
                                            fmt(s.get('signal_rtt_ms_p50')), fmt(s.get('signal_rtt_ms_max')),
                                            fmt(s['target_kbps']), fmt(s.get('target_kbps_min'))))
    ev = run.get('chrome_events') or []
    if ev:
        out.append('- The pages\' SDK (console log, per page in launch order): signal reconnects %s; ICE restarts %s; '
                   'full reconnects %s.' % tuple(', '.join(str(e[k]) for e in ev) for k in (
                       'signal_reconnects', 'ice_restarts', 'full_reconnects')))
    if run.get('notes'):
        for n in run['notes']:
            out.append('- ' + n)
    out += ['', '### Received, per receiver and sender', '',
            md_table(RECEIVE_COLUMNS, receive_rows(summary, names)), '',
            '### Sent: uplink per sender and simulcast layer', '',
            md_table(SEND_COLUMNS, send_rows(summary, names)), '', '### Active speakers', '']
    for peer in sorted(summary['pages'], key=lambda p: names.get(p, p)):
        p = summary['pages'][peer]
        shares = ', '.join('%s %d%%' % (names.get(k, k), v) for k, v in p['speakers'].items()) or 'nobody'
        out.append('- %s: active in the samples: %s; seen: %s.' % (
            names.get(peer, peer), shares, ', '.join(names.get(k, k) for k in sorted(p['speakers_seen'])) or 'none'))
    if summary.get('native'):
        n = summary['native']
        shares = ', '.join('%s %d%%' % (names.get(k, k), v) for k, v in n['speakers'].items()) or 'nobody'
        out.append('- native: loudest active speaker in the samples: %s; seen: %s.' % (
            shares, ', '.join(names.get(k, k) for k in sorted(n['speakers_seen'])) or 'none'))
    out += ['', '### The conversation (native client <-> AI)', '']
    r = (summary.get('native') or {}).get('result')
    if r:
        out.append('- native client: %s turns, %s replies (%s missed); response (end of its speech to the AI\'s first '
                   'audio played) p50 %s ms, transport part %s ms; all: %s ms.' % (
                       r.get('turns'), r.get('replies'), r.get('missed_replies', 0), fmt(r.get('response_ms_p50')),
                       fmt(r.get('response_transport_ms_p50')), r.get('response_ms')))
    a = (summary.get('ai') or {}).get('final')
    if a:
        out.append('- AI: %s user turns heard, %s replies, %s barge-ins; it listened to %s; response delay at the AI '
                   '(its turn end to its first reply audio sent) %s ms.' % (
                       a.get('user_turns'), a.get('replies'), a.get('barge_ins'),
                       ', '.join(names.get(k, k) for k in a.get('listened', [])) or 'nobody',
                       a.get('conv.response_delay_ms')))
    if not r and not a:
        out.append('- none.')
    return '\n'.join(out) + '\n'


def write_doc(path, section, header):
    if os.path.exists(path):
        with open(path) as f:
            text = f.read()
        if RUNS_MARKER in text:
            text = text.replace(RUNS_MARKER, RUNS_MARKER + '\n\n' + section.rstrip() + '\n', 1)
        else:
            text = text.rstrip() + '\n\n' + section
    else:
        text = header.rstrip() + '\n\n' + RUNS_MARKER + '\n\n' + section
    with open(path, 'w') as f:
        f.write(text)


DOC_HEADER = '''# A real group call (C21, #1180)

Six participants in one room on the LiveKit SFU deployed on the Lightsail host (Oregon; docs/deploy/livekit.md),
measured at the callers by `tools/real_group_call.py`. `<host>` is the server's name, not written here (as in
[real-network-calls.md](real-network-calls.md)).

```sh
export SM_SIGNALING_URL=https://<host>:8443
tools/real_group_call.py --seconds=60 --out=group-run --doc=docs/results/real-group-call.md
```
'''


# --- The call ---------------------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--pages', type=int, default=4)
    ap.add_argument('--seconds', type=float, default=60, help='the measured window')
    ap.add_argument('--no-native', action='store_true')
    ap.add_argument('--no-ai', action='store_true')
    ap.add_argument('--smoke', default=os.path.join(ROOT, 'out', 'native', 'apps', 'call', 'sm_call_smoke'))
    ap.add_argument('--ai-server', default=os.path.join(ROOT, 'out', 'native', 'apps', 'ai_server', 'sm_ai_server'))
    ap.add_argument('--window-size', default='1280,800', help="Chrome's window (the grid's tiles: adaptive stream)")
    ap.add_argument('--reference', default='1.1.1.1:443',
                    help='HOST:PORT also probed with a TCP handshake once a second (this machine\'s own link); '
                         '"" for none')
    ap.add_argument('--out', help='directory for run.json and the logs (default: a temporary one)')
    ap.add_argument('--doc', help='insert the run into this results doc (created with its header if missing)')
    ap.add_argument('--summary', help='summarize a run.json already made and exit')
    args = ap.parse_args()

    if args.summary:
        with open(args.summary) as f:
            run = json.load(f)
        section = report(run, summarize(run))
        print(section)
        if args.doc:
            write_doc(args.doc, section, DOC_HEADER)
        return 0

    base = os.environ.get('SM_SIGNALING_URL')
    if not base:
        print('SKIPPED: SM_SIGNALING_URL is not set')
        return SKIP
    lk_url, token_url = endpoints(base)
    if not probe(token_url):
        print('SKIPPED: %s does not answer' % token_url)
        return SKIP
    chrome = find_chrome()
    if args.pages and not chrome:
        print('FAIL: no Chrome found (SM_CHROME)')
        return 1
    native_on, ai_on = not args.no_native, not args.no_ai
    planned = args.pages + native_on + ai_on
    if planned > MAX_PARTICIPANTS:
        print('note: %d participants planned, the room takes %d' % (planned, MAX_PARTICIPANTS))
    out_dir = args.out or tempfile.mkdtemp(prefix='sm-group-call-')
    os.makedirs(out_dir, exist_ok=True)
    room_name = 'grp-' + secrets.token_hex(4)
    tag = secrets.token_hex(2)
    native_id, ai_id = 'native-' + tag, 'ai-' + tag
    now_pt = subprocess.run(['date', '+%Y-%m-%d %H:%M %Z'], capture_output=True, text=True,
                            env=dict(os.environ, TZ='America/Los_Angeles')).stdout.strip()
    print('room %s, %d pages%s%s; logs in %s' % (room_name, args.pages, ', native' if native_on else '',
                                                  ', AI' if ai_on else '', out_dir), flush=True)
    quiet = os.path.join(out_dir, 'quiet.wav')
    write_quiet_wav(quiet)
    server = PageServer(token_url)
    url = server.url + '/?' + urllib.parse.urlencode({'room': room_name, 'group': '1', 'auto': '1', 'report': '1',
                                                      'dtx': '0', 'lk': lk_url})
    chromes, profiles, native, ai = [], [], None, None
    run = {'room': room_name, 'date_pt': now_pt, 'date_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
           'participants_planned': planned, 'native_id': native_id if native_on else None,
           'ai_id': ai_id if ai_on else None, 'notes': [],
           'mix': ', '.join(filter(None, ['%d headless Chrome pages (web app group mode, Chrome %s)' % (
               args.pages, chrome_version(chrome)) if args.pages else '',
               '`sm_call_smoke --livekit --video` (native, talking in turns)' if native_on else '',
               '`sm_ai_server --livekit` (the AI)' if ai_on else '']))}
    start = time.time()
    ok = True
    probe_thread = Probe(token_url, args.reference or None)
    try:
        for i in range(args.pages):
            profile = tempfile.mkdtemp(prefix='sm-web-')
            profiles.append(profile)
            cmd = chrome_command(chrome, profile, url)
            cmd[1:1] = ['--use-file-for-fake-audio-capture=' + quiet, '--disable-features=AudioServiceSandbox',
                        '--window-size=' + args.window_size,
                        '--enable-logging=stderr', '--v=0']
            chromes.append(subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                            stderr=open(os.path.join(out_dir, 'chrome-%d.log' % (i + 1)), 'wb')))
            time.sleep(1.5)
        if ai_on:
            ai = Proc([args.ai_server, '--livekit=' + lk_url, '--token=' + token_url, '--room=' + room_name,
                       '--identity=' + ai_id, '--max-seconds=%d' % (args.seconds + 90)],
                      os.path.join(out_dir, 'ai.log'))
            time.sleep(1.0)
        # Everyone in: every page counts all the planned participants (or 25 s pass).
        expect = min(planned, MAX_PARTICIPANTS)
        if native_on:
            native = Proc([args.smoke, '--livekit=' + lk_url, '--token=' + token_url, '--room=' + room_name,
                           '--peer=' + native_id, '--video',
                           '--seconds=%d' % (args.seconds + NATIVE_SLACK_S),
                           '--label=group-' + tag], os.path.join(out_dir, 'native.log'))
        deadline = time.time() + 25
        all_in = None
        while time.time() < deadline:
            snap = server.snapshot()
            counts = [rs[-1][1].get('count', 0) for rs in snap.values() if rs]
            if len(counts) >= args.pages and counts and min(counts) >= expect:
                all_in = time.time()
                break
            time.sleep(0.5)
        if all_in is None:
            all_in = time.time()
            snap = server.snapshot()
            run['notes'].append('Not every page counted %d participants within 25 s (counts: %s).' % (
                expect, sorted(rs[-1][1].get('count', 0) for rs in snap.values() if rs)))
            ok = False
        run['all_in_s'] = all_in - start
        print('all in after %.1f s; measuring %g s' % (all_in - start, args.seconds), flush=True)
        end = all_in + args.seconds
        time.sleep(max(0.0, end - time.time()))
        if native is not None:
            # Its call ends by itself a little after the window (--seconds), with its result line.
            try:
                native.proc.wait(NATIVE_SLACK_S + 30)
            except subprocess.TimeoutExpired:
                pass
            native.stop()
            stats = json_lines(native.lines, '"livekit_group"')
            if stats and stats[-1][0] < end:  # It ended early: the window ends with its last stats line.
                end = stats[-1][0]
                run['notes'].append('The native client\'s call ended before the window did.')
        run['window'] = [all_in, end]
    finally:
        probe_thread.stop()
        if ai is not None:
            ai.stop()
        for p in chromes:
            p.terminate()
            try:
                p.wait(10)
            except subprocess.TimeoutExpired:
                p.kill()
        for d in profiles:
            shutil.rmtree(d, ignore_errors=True)
        server.close()
    run['reports'] = server.snapshot()
    run['native'] = native.lines if native is not None else None
    run['ai'] = ai.lines if ai is not None else None
    run['chrome_events'] = chrome_events(out_dir, args.pages)
    run['probe'] = probe_thread.samples
    run['cpus'] = os.cpu_count()
    run['reference'] = args.reference
    run['participants_max'] = max((g.get('count', 0) for rs in run['reports'].values() for _, g in rs), default=0)
    run['livekit_version'] = server_version(out_dir, args.pages)
    with open(os.path.join(out_dir, 'run.json'), 'w') as f:
        json.dump(run, f)
    summary = summarize(run)
    with open(os.path.join(out_dir, 'summary.json'), 'w') as f:
        json.dump(summary, f, indent=1, sort_keys=True)
    ok = check(run, summary) and ok
    section = report(run, summary)
    print(section)
    if args.doc:
        write_doc(args.doc, section, DOC_HEADER)
    print('run: %s' % os.path.join(out_dir, 'run.json'))
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1


def chrome_events(out_dir, pages):
    """Per Chrome page (launch order): the SDK's signal reconnects and ICE restarts, from its console log."""
    out = []
    for i in range(pages):
        try:
            with open(os.path.join(out_dir, 'chrome-%d.log' % (i + 1)), errors='replace') as f:
                text = f.read()
        except OSError:
            continue
        out.append({'signal_reconnects': text.count('-> signalReconnecting'), 'ice_restarts': text.count(
            'triggering ICE restart'), 'full_reconnects': text.count('-> reconnecting')})
    return out


def server_version(out_dir, pages):
    """The LiveKit server's version, as the SDK logs it on joining ('?' when no page logged it)."""
    for i in range(pages):
        try:
            with open(os.path.join(out_dir, 'chrome-%d.log' % (i + 1)), errors='replace') as f:
                m = re.search(r'connected to Livekit Server [^"]*?version: ([0-9][0-9.]*)', f.read())
        except OSError:
            continue
        if m:
            return 'v' + m.group(1)
    return '?'


def chrome_version(chrome):
    try:
        v = subprocess.run([chrome, '--version'], capture_output=True, text=True, timeout=10).stdout.strip()
        return v.split()[-1] if v else '?'
    except (OSError, subprocess.TimeoutExpired):
        return '?'


def check(run, summary):
    """Every page received every other participant's audio, and video from the pages and the native client."""
    ok = True
    senders = list(run['reports'])
    video_senders = senders + ([run['native_id']] if run.get('native_id') else [])
    audio_senders = video_senders + ([run['ai_id']] if run.get('ai_id') else [])
    for peer, p in summary['pages'].items():
        for other in audio_senders:
            if other == peer:
                continue
            r = p['receive'].get(other)
            got_audio = r is not None and (r['audio_received'] or 0) > 50
            got_video = other not in video_senders or (r is not None and (r['fps'] or 0) > 1)
            if not (got_audio and got_video):
                print('FAIL: page %s did not receive %s (%s)' % (peer, other, r))
                ok = False
    return ok


if __name__ == '__main__':
    sys.exit(main())
