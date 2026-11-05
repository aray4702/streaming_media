#!/usr/bin/env python3
"""Group calls through a local LiveKit server (C19, #1164): the web app's Group mode and the native LiveKit client.

  test_group_call.py                    two headless Chrome pages join one LiveKit room in Group mode: each publishes
                                        camera and microphone with simulcast and receives the other's audio and video
  test_group_call.py --native=PATH      sm_call_smoke (PATH) joins the room with --livekit and publishes audio and video;
                                        one Chrome page in the room receives them (--subscribe: the native client's
                                        receive side is checked too)
  test_group_call.py --all --native=PATH --ai=PATH
                                        the whole group (C20, #1172): two Chrome pages, sm_call_smoke (as the iOS app's
                                        controller, talking in turns) and sm_ai_server --livekit in one room, and a
                                        third page that joins later and leaves. Everyone must receive everyone (audio,
                                        and video from those that send it, per participant), see the late page join and
                                        leave, and see the active speaker change between the native client and the AI,
                                        which answers the native client's turns (the active speaker's audio).

The LiveKit server is `livekit-server` (SM_LIVEKIT_SERVER names it, else the PATH), started here in its dev mode's
shape (the key pair devkey/secret) on free ports, with node_ip this host's address, so nothing else may use it. The
signaling service (apps/signaling/server.py) serves the page and issues the tokens (/lk-token) with that key pair.
Exits 77 (ctest: skipped) when Chrome or livekit-server is missing; SM_BROWSER_REQUIRED=1 fails instead.
"""

import argparse
import asyncio
import json
import math
import os
import random
import shutil
import socket
import struct
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'signaling'))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tests', 'interop', 'browser'))
import server  # noqa: E402
import run_browser_call as rbc  # noqa: E402

SKIP = 77
KEY, SECRET = 'devkey', 'secret'


def free_port(kind=socket.SOCK_STREAM):
    s = socket.socket(socket.AF_INET, kind)
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port


def find_livekit():
    env = os.environ.get('SM_LIVEKIT_SERVER')
    if env:
        return env if os.path.exists(env) else None
    return shutil.which('livekit-server')


def receives(part):
    return part['audioBytesIn'] > 2000 and part['videoFrames'] > 5


def write_quiet_wav(path, seconds=10, dbfs=-60):
    """48 kHz mono noise at `dbfs`: the pages' microphone (Chrome's fake capture file), heard but never an active
    speaker, so that the native client and the AI are the room's speakers."""
    amp = 32767 * 10 ** (dbfs / 20)
    rng = random.Random(1)
    n = 48000 * seconds
    data = struct.pack('<%dh' % n, *[int(rng.uniform(-amp, amp) * math.sqrt(3)) for _ in range(n)])
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ' +
                struct.pack('<IHHIIHH', 16, 1, 1, 48000, 96000, 2, 16) + b'data' + struct.pack('<I', len(data)) + data)


def json_lines(lines, key):
    out = []
    for line in lines:
        if line.startswith('{') and key in line:
            try:
                out.append(json.loads(line))
            except ValueError:
                pass
    return out


async def start_livekit(binary, log_path):
    port, tcp, udp = free_port(), free_port(), free_port(socket.SOCK_DGRAM)
    config = '\n'.join([
        'port: %d' % port,
        'bind_addresses: ["127.0.0.1"]',
        'rtc:',
        '  tcp_port: %d' % tcp,
        '  udp_port: %d' % udp,
        '  node_ip: %s' % rbc.host_address(),
        '  use_external_ip: false',
        'keys:',
        '  %s: %s' % (KEY, SECRET),
        'room:',
        '  max_participants: 6',
        'logging:',
        '  level: info',
    ])
    log = open(log_path, 'wb')
    proc = await asyncio.create_subprocess_exec(binary, '--config-body', config, stdout=log, stderr=log)
    end = time.time() + 15
    while time.time() < end:
        try:
            r, w = await asyncio.open_connection('127.0.0.1', port)
            w.close()
            return proc, port
        except OSError:
            await asyncio.sleep(0.2)
    proc.kill()
    raise RuntimeError('livekit-server did not start (log: %s)' % log_path)


async def main_async(args, chrome, livekit):
    logs = tempfile.mkdtemp(prefix='sm-group-')
    lk, lk_port = await start_livekit(livekit, os.path.join(logs, 'livekit.log'))
    lk_url = 'ws://127.0.0.1:%d' % lk_port
    svc = server.Service(livekit_key=KEY, livekit_secret=SECRET, livekit_url=lk_url)
    await svc.start()
    room = 'group-%d' % os.getpid()
    page = 'http://127.0.0.1:%d/?room=%s&group=1&auto=1&report=1' % (svc.port, room)
    procs, profiles, native, native_out = [], [], None, []
    ok = False
    try:
        pages = 1 if args.native else args.pages
        for _ in range(pages):
            profile = tempfile.mkdtemp(prefix='sm-web-')
            profiles.append(profile)
            procs.append(await asyncio.create_subprocess_exec(
                *rbc.chrome_command(chrome, profile, page),
                stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL))
            await asyncio.sleep(3.0 if args.subscribe else 1.0)  # --subscribe: the page publishes before the native joins.
        native_id = 'native-%d' % os.getpid()
        if args.native:
            native = await asyncio.create_subprocess_exec(
                args.native, '--livekit=' + lk_url, '--token=http://127.0.0.1:%d/lk-token' % svc.port,
                '--room=' + room, '--peer=' + native_id, '--video', '--seconds=%d' % (args.timeout + 5),
                '--ip=' + rbc.host_address(), *(['--subscribe'] if args.subscribe else []),
                stdout=asyncio.subprocess.PIPE, stderr=open(os.path.join(logs, 'native.log'), 'wb'))

            async def read_native():
                async for line in native.stdout:
                    native_out.append(line.decode(errors='replace').rstrip())
            reader = asyncio.create_task(read_native())
        end = time.time() + args.timeout
        latest, passed = {}, False
        while time.time() < end:
            for r in list(svc.reports):
                if 'group' in r:
                    latest[r['peer']] = r
            if len(latest) >= pages:
                checks = []
                for peer, r in latest.items():
                    parts = r['group']['participants']
                    others = [native_id] if args.native else [p for p in latest if p != peer]
                    checks.append(all(o in parts and receives(parts[o]) for o in others))
                    # Simulcast: the camera's sender has more than one encoding (the SDK's layers).
                    checks.append(len(r['group']['sending'].get('encodings', [])) >= 2)
                if all(checks):
                    passed = True
                    break
            await asyncio.sleep(0.5)
        for peer, r in sorted(latest.items()):
            print('page %s: %s' % (peer, json.dumps(r['group'], sort_keys=True)))
        ok = passed
        if not passed:
            print('FAIL: not every page received every other participant\'s audio and video with simulcast sending '
                  '(%d pages reported; logs in %s)' % (len(latest), logs))
        if native is not None:
            await asyncio.sleep(5 if args.subscribe else 2)  # A few more of its stats lines (once a second).
            native.terminate()  # Its result line is not needed: the page checked what it published.
            try:
                await asyncio.wait_for(native.wait(), 10)
            except asyncio.TimeoutError:
                native.kill()
            try:
                await asyncio.wait_for(reader, 5)  # The rest of its output.
            except asyncio.TimeoutError:
                reader.cancel()
            stats = [l for l in native_out if l.startswith('{"state"')]
            if stats:
                print('native: %s' % stats[-1])
            published = bool(stats) and json.loads(stats[-1]).get('livekit_published', 0) >= 2
            if not published:
                print('FAIL: the native client did not publish its two tracks')
                ok = False
            if args.subscribe and stats:  # The native subscriber (C20): the page's audio and video, by participant.
                d = json.loads(stats[-1])
                group = {p['identity']: p for p in d.get('livekit_group', [])}
                pages_seen = [g for i, g in group.items() if i.startswith('web-')]
                print('native received: audio played %.1f s, video frames decoded %d, subscriber: %s'
                      % (d['audio_played_s'], d['video_frames_decoded'], d.get('livekit_subscriber', '')))
                if not (pages_seen and pages_seen[0]['audio_played_s'] > 1 and pages_seen[0]['video_frames_decoded'] > 5):
                    print('FAIL: the native client did not receive the page\'s audio and video')
                    ok = False
    finally:
        if native is not None and native.returncode is None:
            native.kill()
        for p in procs:
            p.terminate()
            try:
                await asyncio.wait_for(p.wait(), 10)
            except asyncio.TimeoutError:
                p.kill()
        for d in profiles:
            shutil.rmtree(d, ignore_errors=True)
        await svc.stop()
        lk.terminate()
        try:
            await asyncio.wait_for(lk.wait(), 10)
        except asyncio.TimeoutError:
            lk.kill()
    if ok and not args.keep_logs:
        shutil.rmtree(logs, ignore_errors=True)
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1


async def read_lines(proc, out):
    async for line in proc.stdout:
        out.append(line.decode(errors='replace').rstrip())


async def stop(proc, reader=None):
    if proc is None:
        return
    if proc.returncode is None:
        proc.terminate()
        try:
            await asyncio.wait_for(proc.wait(), 10)
        except asyncio.TimeoutError:
            proc.kill()
    if reader is not None:
        try:
            await asyncio.wait_for(reader, 5)
        except asyncio.TimeoutError:
            reader.cancel()


async def main_all(args, chrome, livekit):
    """The whole group (C20): see the module docstring."""
    logs = tempfile.mkdtemp(prefix='sm-group-all-')
    lk, lk_port = await start_livekit(livekit, os.path.join(logs, 'livekit.log'))
    lk_url = 'ws://127.0.0.1:%d' % lk_port
    svc = server.Service(livekit_key=KEY, livekit_secret=SECRET, livekit_url=lk_url)
    await svc.start()
    room = 'group-all-%d' % os.getpid()
    token = 'http://127.0.0.1:%d/lk-token' % svc.port
    quiet = os.path.join(logs, 'quiet.wav')
    write_quiet_wav(quiet)
    page = 'http://127.0.0.1:%d/?room=%s&group=1&auto=1&report=1&dtx=0' % (svc.port, room)
    native_id, ai_id = 'native-%d' % os.getpid(), 'ai-server-%d' % os.getpid()
    pages, profiles, native_out, ai_out = [], [], [], []
    native = ai = native_reader = ai_reader = None
    failures = []

    async def open_page(url):
        profile = tempfile.mkdtemp(prefix='sm-web-')
        profiles.append(profile)
        cmd = rbc.chrome_command(chrome, profile, url)
        cmd.insert(1, '--use-file-for-fake-audio-capture=' + quiet)
        return await asyncio.create_subprocess_exec(*cmd, stdout=asyncio.subprocess.DEVNULL,
                                                    stderr=asyncio.subprocess.DEVNULL)

    def latest_reports():
        latest = {}
        for r in list(svc.reports):
            if 'group' in r:
                latest[r['peer']] = r
        return latest

    try:
        for _ in range(2):
            pages.append(await open_page(page))
            await asyncio.sleep(1.5)
        ai = await asyncio.create_subprocess_exec(
            args.ai, '--livekit=' + lk_url, '--token=' + token, '--room=' + room, '--identity=' + ai_id,
            '--ip=' + rbc.host_address(), '--reply-seconds=2', '--max-seconds=%d' % (args.timeout + 30),
            stdout=asyncio.subprocess.PIPE, stderr=open(os.path.join(logs, 'ai.log'), 'wb'))
        ai_reader = asyncio.create_task(read_lines(ai, ai_out))
        await asyncio.sleep(1.0)
        native = await asyncio.create_subprocess_exec(
            args.native, '--livekit=' + lk_url, '--token=' + token, '--room=' + room, '--peer=' + native_id,
            '--video', '--seconds=%d' % (args.timeout + 30), '--ip=' + rbc.host_address(),
            stdout=asyncio.subprocess.PIPE, stderr=open(os.path.join(logs, 'native.log'), 'wb'))
        native_reader = asyncio.create_task(read_lines(native, native_out))
        await asyncio.sleep(8.0)
        # A third page joins and leaves 10 s later; it is the identity that was not reporting before.
        early = set(latest_reports())
        late = await open_page(page + '&leave=10')
        pages.append(late)
        late_id = None
        end = time.time() + args.timeout
        while time.time() < end:
            await asyncio.sleep(1.0)
            latest = latest_reports()
            if late_id is None:
                new = sorted(set(latest) - early)
                late_id = new[0] if new else None
            nat = json_lines(native_out, '"livekit_group"')
            ais = json_lines(ai_out, '"ai-call-metrics"')
            if not nat or not ais or late_id is None:
                continue
            n, a = nat[-1], ais[-1]
            if late_id in n.get('livekit_left', []) and a.get('replies', 0) >= 1 and \
                    {native_id, ai_id} <= set(n.get('livekit_speakers_seen', [])):
                break
        # Collect what everyone saw.
        latest = latest_reports()
        first_pages = [pid for pid in latest if pid != late_id]
        nat = json_lines(native_out, '"livekit_group"')
        ais = json_lines(ai_out, '"ai-call-metrics"')
        n = nat[-1] if nat else {}
        a = ais[-1] if ais else {}
        for pid in sorted(latest):
            print('page %s: %s' % (pid, json.dumps(latest[pid]['group'], sort_keys=True)))
        print('native: %s' % json.dumps(n, sort_keys=True))
        print('ai: %s' % json.dumps({k: v for k, v in a.items() if k != 'stats'}, sort_keys=True))
        if a.get('stats'):
            print('ai stats: %s' % json.dumps(a['stats'], sort_keys=True))

        def check(ok, what):
            print('%s: %s' % ('ok' if ok else 'FAIL', what))
            if not ok:
                failures.append(what)

        check(late_id is not None, 'the late page reported')
        # 1. Everyone receives everyone.
        for pid in first_pages:
            parts = latest[pid]['group']['participants']
            for other in [p for p in first_pages if p != pid] + [native_id]:
                check(other in parts and receives(parts[other]), 'page %s receives %s (audio and video)' % (pid, other))
            check(ai_id in parts and parts[ai_id]['audioBytesIn'] > 2000, 'page %s receives the AI\'s audio' % pid)
        group = {p['identity']: p for p in n.get('livekit_group', [])}
        for pid in first_pages:
            g = group.get(pid, {})
            check(g.get('audio_played_s', 0) > 1 and g.get('video_frames_decoded', 0) > 5,
                  'native receives %s (audio %.1f s, video %d frames)' % (pid, g.get('audio_played_s', 0),
                                                                          g.get('video_frames_decoded', 0)))
        g = group.get(ai_id, {})
        check(g.get('audio_played_s', 0) > 1, 'native receives the AI (audio %.1f s)' % g.get('audio_played_s', 0))
        ai_group = {p['identity']: p for p in a.get('stats', {}).get('livekit_group', [])}
        for pid in first_pages + [native_id]:
            g = ai_group.get(pid, {})
            check(g.get('audio_packets_received', 0) > 50,
                  'the AI receives %s (%d audio packets)' % (pid, g.get('audio_packets_received', 0)))
        # 2. Join and leave.
        check(late_id in n.get('livekit_joined', []) and late_id in n.get('livekit_left', []),
              'native saw %s join and leave' % late_id)
        for pid in first_pages:
            r = latest[pid]['group']
            check(late_id in r.get('joined', []) and late_id in r.get('left', []) and late_id not in r['participants'],
                  'page %s saw %s join and leave' % (pid, late_id))
        # 3. The active speaker changes between the native client and the AI, which answers the native client.
        check({native_id, ai_id} <= set(n.get('livekit_speakers_seen', [])),
              'native saw both the native client and the AI as active speaker (%s)' % n.get('livekit_speakers_seen'))
        for pid in first_pages:
            seen = set(latest[pid]['group'].get('speakersSeen', []))
            check({native_id, ai_id} <= seen, 'page %s saw both as active speaker (%s)' % (pid, sorted(seen)))
        check(native_id in a.get('listened', []) and a.get('user_turns', 0) >= 1 and a.get('replies', 0) >= 1,
              'the AI listened to the native client and answered (%d turns, %d replies)' % (a.get('user_turns', 0),
                                                                                           a.get('replies', 0)))
    finally:
        await stop(native, native_reader)
        await stop(ai, ai_reader)
        for p in pages:
            await stop(p)
        for d in profiles:
            shutil.rmtree(d, ignore_errors=True)
        await svc.stop()
        await stop(lk)
    res = [l for l in native_out if '"type":"call-result"' in l]
    if res:
        r = json.loads(res[-1])
        print('native conversation: %d turns, %d replies, response %s ms' % (r['turns'], r['replies'],
                                                                           r['response_ms']))
    ok = not failures
    if ok and not args.keep_logs:
        shutil.rmtree(logs, ignore_errors=True)
    else:
        print('logs in %s' % logs)
    print('PASS' if ok else 'FAIL (%d checks)' % len(failures))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--native', help='sm_call_smoke: publish from the native LiveKit client')
    ap.add_argument('--pages', type=int, default=2, help='Chrome pages (without --native)')
    ap.add_argument('--timeout', type=float, default=45.0)
    ap.add_argument('--subscribe', action='store_true',
                    help='with --native: also check what the native client receives (it subscribes by default)')
    ap.add_argument('--all', action='store_true', help='the whole group: pages, --native and --ai in one room')
    ap.add_argument('--ai', help='sm_ai_server, for --all')
    ap.add_argument('--keep-logs', action='store_true')
    args = ap.parse_args()
    required = os.environ.get('SM_BROWSER_REQUIRED') == '1'
    chrome, livekit = rbc.find_chrome(), find_livekit()
    missing = [n for n, v in (('Chrome (SM_CHROME)', chrome), ('livekit-server (SM_LIVEKIT_SERVER)', livekit)) if not v]
    if missing:
        print('%s: no %s found' % ('FAIL' if required else 'SKIPPED', ' or '.join(missing)))
        return 1 if required else SKIP
    if args.all:
        if not args.native or not args.ai:
            ap.error('--all needs --native and --ai')
        return asyncio.run(main_all(args, chrome, livekit))
    return asyncio.run(main_async(args, chrome, livekit))


if __name__ == '__main__':
    sys.exit(main())
