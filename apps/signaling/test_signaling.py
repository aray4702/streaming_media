#!/usr/bin/env python3
"""Unit tests of the signaling service (#1102): join, relay, peer-left, the room limit, the
AI-room notice and /turn. With SM_TURN_TOOL (the sm_turn_credentials binary) the /turn
credential is checked against CreateTurnRestCredentials's own output."""

import asyncio
import base64
import json
import os
import subprocess
import sys
import unittest
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import server  # noqa: E402


class Client:
    """A minimal WebSocket client (masked frames, text only)."""

    def __init__(self, reader, writer):
        self.r, self.w = reader, writer

    @classmethod
    async def connect(cls, port):
        r, w = await asyncio.open_connection('127.0.0.1', port)
        key = base64.b64encode(os.urandom(16)).decode()
        w.write(('GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                 'Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n' % key).encode())
        head = await r.readuntil(b'\r\n\r\n')
        assert b'101' in head.split(b'\r\n')[0], head
        return cls(r, w)

    async def send(self, msg, pad=0):
        data = json.dumps(msg).encode() + b' ' * pad
        mask = os.urandom(4)
        n = len(data)
        head = bytes([0x81]) + (bytes([0x80 | n]) if n < 126 else b'\xfe' + n.to_bytes(2, 'big'))
        self.w.write(head + mask + bytes(c ^ mask[i & 3] for i, c in enumerate(data)))
        await self.w.drain()

    async def recv(self, timeout=2):
        b0, b1 = await asyncio.wait_for(self.r.readexactly(2), timeout)
        n = b1 & 0x7F
        if n == 126:
            n = int.from_bytes(await self.r.readexactly(2), 'big')
        return json.loads(await self.r.readexactly(n))

    def close(self):
        self.w.close()


def run(coro):
    return asyncio.run(coro)


class ServiceTest(unittest.TestCase):
    def serve(self, **kw):
        svc = server.Service(**kw)
        return svc

    def test_join_relay_and_peer_left(self):
        async def go():
            svc = self.serve()
            await svc.start()
            a, b = await Client.connect(svc.port), await Client.connect(svc.port)
            await a.send({'type': 'join', 'room': 'r1', 'peer': 'A'})
            self.assertEqual(await a.recv(), {'type': 'joined', 'peers': []})
            await b.send({'type': 'join', 'room': 'r1', 'peer': 'B'})
            self.assertEqual(await b.recv(), {'type': 'joined', 'peers': ['A']})
            self.assertEqual(await a.recv(), {'type': 'peer-joined', 'peer': 'B'})
            await b.send({'type': 'offer', 'to': 'A', 'sdp': 'v=0'})
            self.assertEqual(await a.recv(), {'type': 'offer', 'to': 'A', 'sdp': 'v=0', 'from': 'B'})
            await a.send({'type': 'candidate', 'to': 'B', 'candidate': 'c', 'sdpMid': '0', 'sdpMLineIndex': 0})
            got = await b.recv()
            self.assertEqual((got['type'], got['from'], got['candidate'], got['sdpMLineIndex']), ('candidate', 'A', 'c', 0))
            await a.send({'type': 'bye', 'to': 'B'})
            self.assertEqual((await b.recv())['type'], 'bye')
            a.close()
            self.assertEqual(await b.recv(), {'type': 'peer-left', 'peer': 'A'})
            b.close()
            await svc.stop()
        run(go())

    def test_rooms_are_separate(self):
        async def go():
            svc = self.serve()
            await svc.start()
            a, b = await Client.connect(svc.port), await Client.connect(svc.port)
            await a.send({'type': 'join', 'room': 'x', 'peer': 'A'})
            await a.recv()
            await b.send({'type': 'join', 'room': 'y', 'peer': 'B'})
            self.assertEqual(await b.recv(), {'type': 'joined', 'peers': []})
            await b.send({'type': 'offer', 'to': 'A', 'sdp': 's'})  # A is in another room: dropped
            with self.assertRaises(asyncio.TimeoutError):
                await a.recv(timeout=0.3)
            a.close(); b.close()
            await svc.stop()
        run(go())

    def test_room_limit_is_four(self):
        async def go():
            svc = self.serve()
            await svc.start()
            clients = []
            for i in range(4):
                c = await Client.connect(svc.port)
                await c.send({'type': 'join', 'room': 'full', 'peer': 'p%d' % i})
                self.assertEqual((await c.recv())['type'], 'joined')
                clients.append(c)
            fifth = await Client.connect(svc.port)
            await fifth.send({'type': 'join', 'room': 'full', 'peer': 'p4'})
            self.assertEqual(await fifth.recv(), {'type': 'error', 'reason': 'room full'})
            clients[0].close()  # a seat frees up
            await asyncio.sleep(0.2)
            await fifth.send({'type': 'join', 'room': 'full', 'peer': 'p4'})
            self.assertEqual((await fifth.recv())['type'], 'joined')
            for c in clients + [fifth]:
                c.close()
            await svc.stop()
        run(go())

    def test_duplicate_peer_id_is_refused(self):
        async def go():
            svc = self.serve()
            await svc.start()
            a, b = await Client.connect(svc.port), await Client.connect(svc.port)
            await a.send({'type': 'join', 'room': 'd', 'peer': 'A'})
            await a.recv()
            await b.send({'type': 'join', 'room': 'd', 'peer': 'A'})
            self.assertEqual((await b.recv())['type'], 'error')
            a.close(); b.close()
            await svc.stop()
        run(go())

    def test_long_message_uses_extended_length(self):
        async def go():
            svc = self.serve()
            await svc.start()
            a, b = await Client.connect(svc.port), await Client.connect(svc.port)
            for c, pid in ((a, 'A'), (b, 'B')):
                await c.send({'type': 'join', 'room': 'l', 'peer': pid})
                await c.recv()
            await a.recv()  # peer-joined
            await b.send({'type': 'offer', 'to': 'A', 'sdp': 'x' * 3000})
            self.assertEqual(len((await a.recv())['sdp']), 3000)
            a.close(); b.close()
            await svc.stop()
        run(go())

    def test_ai_room_notice(self):
        async def go():
            svc = self.serve()
            await svc.start()
            ai, user, other = [await Client.connect(svc.port) for _ in range(3)]
            await ai.send({'type': 'register-ai'})
            await asyncio.sleep(0.1)
            await other.send({'type': 'join', 'room': 'plain', 'peer': 'O'})
            await other.recv()
            await user.send({'type': 'join', 'room': 'ai-abc', 'peer': 'U'})
            await user.recv()
            self.assertEqual(await ai.recv(), {'type': 'ai-call', 'room': 'ai-abc'})  # not 'plain'
            await ai.send({'type': 'join', 'room': 'ai-abc', 'peer': 'ai'})
            self.assertEqual(await ai.recv(), {'type': 'joined', 'peers': ['U']})
            for c in (ai, user, other):
                c.close()
            await svc.stop()
        run(go())

    def test_serves_web_app_and_turn_stun_only(self):
        async def go():
            svc = self.serve()
            await svc.start()
            loop = asyncio.get_running_loop()
            fetch = lambda path: loop.run_in_executor(None, lambda: urllib.request.urlopen(
                'http://127.0.0.1:%d%s' % (svc.port, path)).read())
            self.assertIn(b'RTCPeerConnection', await fetch('/'))
            turn = json.loads(await fetch('/turn'))
            self.assertEqual(sorted(turn), ['ttl', 'urls'])  # no secret: STUN only
            with self.assertRaises(Exception):
                await fetch('/../server.py')
            await svc.stop()
        run(go())


class TurnTest(unittest.TestCase):
    def test_credentials_match_the_library(self):
        secret, user, now = 's3cret', 'alice', 1_700_000_000
        resp = server.turn_response(secret, ['turn:t.example:3478'], ['stun:s.example'], user, now)
        self.assertEqual(resp['urls'], ['stun:s.example', 'turn:t.example:3478'])
        self.assertEqual(resp['ttl'], 86400)
        self.assertEqual(resp['username'], '%d:alice' % (now + 86400))
        self.assertNotIn(secret, json.dumps(resp))
        tool = os.environ.get('SM_TURN_TOOL')
        if not tool:
            self.skipTest('SM_TURN_TOOL not set')
        out = json.loads(subprocess.check_output([tool, secret, user, str(now + 86400)]))
        self.assertEqual(resp['username'], out['username'])
        self.assertEqual(resp['credential'], out['credential'])
        self.assertTrue(out['credential'])

    def test_http_turn_with_secret(self):
        async def go():
            svc = server.Service(['turn:t.example:3478'], 'k')
            await svc.start()
            loop = asyncio.get_running_loop()
            body = await loop.run_in_executor(None, lambda: urllib.request.urlopen(
                'http://127.0.0.1:%d/turn?peer=bob' % svc.port).read())
            turn = json.loads(body)
            self.assertTrue(turn['username'].endswith(':bob'))
            self.assertIn('turn:t.example:3478', turn['urls'])
            await svc.stop()
        run(go())

    def test_turn_with_no_stun_has_no_empty_server(self):
        # #1135: with STUN_URLS empty and no TURN, /turn must not describe an ICE server
        # with no URLs; it returns {"urls": []} and the web app then passes no ICE server.
        self.assertEqual(server.turn_response('', [], []), {'urls': [], 'ttl': server.TTL})
        self.assertEqual(server.turn_response('', ['turn:t.example:3478'], ['']), {'urls': [], 'ttl': server.TTL})
        self.assertEqual(server.turn_response('k', [''], []), {'urls': [], 'ttl': server.TTL})
        async def go():
            svc = server.Service([], '', [])
            await svc.start()
            loop = asyncio.get_running_loop()
            body = await loop.run_in_executor(None, lambda: urllib.request.urlopen(
                'http://127.0.0.1:%d/turn?peer=bob' % svc.port).read())
            self.assertEqual(json.loads(body), {'urls': [], 'ttl': server.TTL})
            await svc.stop()
        run(go())
        html = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'web', 'index.html')).read()
        self.assertIn('if (!urls.length) { iceServers = []; return; }', html)


if __name__ == '__main__':
    unittest.main()
