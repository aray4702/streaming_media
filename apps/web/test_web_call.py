#!/usr/bin/env python3
"""Calls through the signaling service with the web app in headless Chrome (#1102).

  test_web_call.py                 two tabs (two Chrome processes) of the web app call each other
  test_web_call.py --peer=PATH     our endpoint (sm_interop_browser_peer) calls the web app: the
                                   script joins the room as peer "native", bridges the service's
                                   messages to the endpoint's stdin/stdout, and Chrome offers.

The pages run with fake media devices and report their state to POST /report (?report=1). A call
passes when every page is connected and received audio and video (the native peer: its own
verdict). Exits 77 (ctest: skipped) when no Chrome is found; SM_BROWSER_REQUIRED=1 fails instead.
"""

import argparse
import asyncio
import json
import os
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'signaling'))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tests', 'interop', 'browser'))
import server  # noqa: E402
import run_browser_call as rbc  # noqa: E402
from test_signaling import Client  # noqa: E402

SKIP = 77


def launch_chrome(chrome, url):
    profile = tempfile.mkdtemp(prefix='sm-web-')
    return profile, asyncio.create_subprocess_exec(*rbc.chrome_command(chrome, profile, url),
                                                   stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)


def page_ok(report):
    calls = report.get('calls', {})
    return bool(calls) and all(c['connection'] == 'connected' and c['audioBytesIn'] > 2000 and c['videoFrames'] > 5
                               for c in calls.values())


async def wait_pages(svc, peers, timeout):
    """Waits until every one of `peers` has reported a passing call; returns the latest reports."""
    end = time.time() + timeout
    latest = {}
    while time.time() < end:
        for r in list(svc.reports):
            latest[r['peer']] = r
        ok = [p for p in peers if p in latest and page_ok(latest[p])]
        if len(ok) == len(peers):
            return True, latest
        await asyncio.sleep(0.5)
    return False, latest


async def bridge(svc_port, proc, room, log):
    """Native peer <-> signaling service: the endpoint speaks lines of JSON on stdin/stdout."""
    ws = await Client.connect(svc_port)
    await ws.send({'type': 'join', 'room': room, 'peer': 'native'})
    remote = {'peer': None}
    result = asyncio.get_running_loop().create_future()

    async def from_service():
        while True:
            try:
                m = await ws.recv(timeout=3600)
            except Exception:
                return
            if m.get('type') in ('offer', 'answer', 'candidate'):
                remote['peer'] = m.get('from')
                proc.stdin.write((json.dumps(m) + '\n').encode())
                await proc.stdin.drain()
            elif m.get('type') in ('bye', 'peer-left'):
                proc.stdin.write(b'{"type":"bye"}\n')
                await proc.stdin.drain()

    async def from_peer():
        async for raw in proc.stdout:
            line = raw.decode().strip()
            try:
                m = json.loads(line)
            except ValueError:
                log('[peer] ' + line)
                continue
            if m.get('type') == 'result':
                result.set_result(m)
            elif remote['peer']:
                m['to'] = remote['peer']
                await ws.send(m, pad=0)
        if not result.done():
            result.set_result(None)

    tasks = [asyncio.create_task(from_service()), asyncio.create_task(from_peer())]
    return ws, result, tasks


async def main_async(args, chrome):
    svc = server.Service()
    await svc.start()
    room = 'test-%d' % os.getpid()
    base = 'http://127.0.0.1:%d/?room=%s&auto=1&report=1' % (svc.port, room)
    procs, profiles, tasks, native = [], [], [], None
    ok = False
    peers = []
    try:
        if args.peer:
            native = await asyncio.create_subprocess_exec(
                args.peer, '--role=answer', '--no-data-channel', '--ip=' + rbc.host_address(),
                stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE)
            ws, native_result, tasks = await bridge(svc.port, native, room, lambda s: print(s, file=sys.stderr))
            await asyncio.sleep(0.5)
        for _ in range(1 if args.peer else 2):
            profile, coro = launch_chrome(chrome, base)
            profiles.append(profile)
            procs.append(await coro)
            await asyncio.sleep(1.5)  # the second tab joins later, so it offers
        # The pages' ids are random: find them from the reports.
        end = time.time() + args.timeout
        passed, latest = False, {}
        while time.time() < end:
            for r in list(svc.reports):
                latest[r['peer']] = r
            if len(latest) >= len(procs) and all(page_ok(r) for r in latest.values()):
                passed = True
                break
            await asyncio.sleep(0.5)
        for peer, r in sorted(latest.items()):
            print('page %s: %s' % (peer, json.dumps(r['calls'], sort_keys=True)))
        ok = passed
        if not passed:
            print('FAIL: not every page connected and received audio and video (%d pages reported)' % len(latest))
        if native is not None:
            native.stdin.write(b'{"type":"bye"}\n')
            await native.stdin.drain()
            try:
                res = await asyncio.wait_for(native_result, 15)
            except asyncio.TimeoutError:
                res = None
            if res is None:
                print('FAIL: the native peer gave no result')
                ok = False
            else:
                print('peer checks: %s' % json.dumps(res.get('checks', {}), sort_keys=True))
                ok = ok and bool(res.get('ok'))
    finally:
        for t in tasks:
            t.cancel()
        if native is not None:
            ws.close()
        for p in procs:
            p.terminate()
            try:
                await asyncio.wait_for(p.wait(), 10)
            except asyncio.TimeoutError:
                p.kill()
        if native is not None and native.returncode is None:
            native.kill()
        for d in profiles:
            shutil.rmtree(d, ignore_errors=True)
        await svc.stop()
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--peer', help='our endpoint (sm_interop_browser_peer)')
    ap.add_argument('--timeout', type=float, default=40.0)
    args = ap.parse_args()
    chrome = rbc.find_chrome()
    if not chrome:
        if os.environ.get('SM_BROWSER_REQUIRED') == '1':
            print('FAIL: no Chrome found and SM_BROWSER_REQUIRED=1')
            return 1
        print('SKIPPED: no Chrome found (set SM_CHROME to its path)')
        return SKIP
    return asyncio.run(main_async(args, chrome))


if __name__ == '__main__':
    sys.exit(main())
