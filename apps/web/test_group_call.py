#!/usr/bin/env python3
"""Group calls through a local LiveKit server (C19, #1164): the web app's Group mode and the native LiveKit client.

  test_group_call.py                    two headless Chrome pages join one LiveKit room in Group mode: each publishes
                                        camera and microphone with simulcast and receives the other's audio and video
  test_group_call.py --native=PATH      sm_call_smoke (PATH) joins the room with --livekit and publishes audio and video;
                                        one Chrome page in the room receives them

The LiveKit server is `livekit-server` (SM_LIVEKIT_SERVER names it, else the PATH), started here in its dev mode's
shape (the key pair devkey/secret) on free ports, with node_ip this host's address, so nothing else may use it. The
signaling service (apps/signaling/server.py) serves the page and issues the tokens (/lk-token) with that key pair.
Exits 77 (ctest: skipped) when Chrome or livekit-server is missing; SM_BROWSER_REQUIRED=1 fails instead.
"""

import argparse
import asyncio
import json
import os
import shutil
import socket
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
            if args.subscribe and stats:  # Reported, not checked: receiving through LiveKit is C18's.
                d = json.loads(stats[-1])
                print('native received (C18 checks it): audio played %.1f s, video frames decoded %d, subscriber: %s'
                      % (d['audio_played_s'], d['video_frames_decoded'], d.get('livekit_subscriber', '')))
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--native', help='sm_call_smoke: publish from the native LiveKit client')
    ap.add_argument('--pages', type=int, default=2, help='Chrome pages (without --native)')
    ap.add_argument('--timeout', type=float, default=45.0)
    ap.add_argument('--subscribe', action='store_true',
                    help='with --native: the native client also subscribes (one remote audio and video until C18); '
                         'what it received is printed, not checked')
    ap.add_argument('--keep-logs', action='store_true')
    args = ap.parse_args()
    required = os.environ.get('SM_BROWSER_REQUIRED') == '1'
    chrome, livekit = rbc.find_chrome(), find_livekit()
    missing = [n for n, v in (('Chrome (SM_CHROME)', chrome), ('livekit-server (SM_LIVEKIT_SERVER)', livekit)) if not v]
    if missing:
        print('%s: no %s found' % ('FAIL' if required else 'SKIPPED', ' or '.join(missing)))
        return 1 if required else SKIP
    return asyncio.run(main_async(args, chrome, livekit))


if __name__ == '__main__':
    sys.exit(main())
