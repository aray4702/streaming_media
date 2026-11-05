#!/usr/bin/env python3
"""Signaling service for the web app, the AI server and the iOS app (#1102, C15).

Python standard library only (asyncio, with a small RFC 6455 WebSocket server in
this file: no third-party dependency to install next to coturn).

  WS  /ws    JSON messages (protocol: docs/deploy/signaling.md); relayed ones get "from"
  GET /turn  {"urls","username","credential","ttl"} from TURN_SECRET (coturn use-auth-secret)
  GET /lk-token?room=R&identity=I[&subscribe=0]
             {"token","url","ttl"}: a LiveKit access token to join room R as I (C19, docs/deploy/livekit.md),
             signed with LIVEKIT_API_SECRET; the secret never leaves the server
  GET /*     the web app's files (apps/web), `index.html` for `/`

Config from the environment: PORT (default 8080), HOST (default 0.0.0.0),
TURN_URLS (comma separated; default none), TURN_SECRET (the shared secret; without it
/turn returns STUN only), STUN_URLS (default stun:stun.l.google.com:19302), WEB_DIR,
LIVEKIT_API_KEY and LIVEKIT_API_SECRET (the LiveKit server's key pair; without them /lk-token returns 503),
LIVEKIT_URL (optional: the LiveKit URL clients connect to, returned with the token; empty: this service's origin).
"""

import asyncio
import base64
import hashlib
import hmac
import json
import os
import re
import struct
import sys
import time
import urllib.parse

MAX_PEERS = 4
TTL = 86400
GUID = b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
DEFAULT_WEB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'web')
TYPES = {'.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.css': 'text/css'}
LK_TTL = 3600  # LiveKit tokens: one hour to join; LiveKit itself refreshes a joined client's token (refresh_token).
LK_NAME = re.compile(r'[A-Za-z0-9_-][A-Za-z0-9_.-]{0,63}')  # Room codes and identities (fullmatch).


def turn_response(secret, urls, stun_urls, peer_id='web', now=None):
    """What GET /turn returns. The credential is CreateTurnRestCredentials(secret, peer_id, expiry)."""
    ice = [u for u in stun_urls if u]
    urls = [u for u in urls if u]
    if not secret or not urls:  # STUN only; {'urls': []} when no STUN is set either (#1135)
        return {'urls': ice, 'ttl': TTL}
    expiry = int(now if now is not None else time.time()) + TTL
    username = '%d:%s' % (expiry, peer_id)
    mac = hmac.new(secret.encode(), username.encode(), hashlib.sha1).digest()
    return {'urls': ice + list(urls), 'username': username,
            'credential': base64.b64encode(mac).decode(), 'ttl': TTL}


def _b64url(data):
    return base64.urlsafe_b64encode(data).rstrip(b'=').decode()


def livekit_token(api_key, api_secret, room, identity, now=None, ttl=LK_TTL, subscribe=True):
    """A LiveKit access token, an HS256 JWT as livekit/protocol's auth.AccessToken makes it: `iss` the API key, `sub`
    the identity, `nbf`/`exp`, and the `video` grant (VideoGrant) to join `room`, publish and (unless not
    `subscribe`) subscribe."""
    now = int(now if now is not None else time.time())
    header = {'alg': 'HS256', 'typ': 'JWT'}
    claims = {'iss': api_key, 'sub': identity, 'name': identity, 'nbf': now, 'exp': now + ttl,
              'video': {'roomJoin': True, 'room': room, 'canPublish': True, 'canSubscribe': bool(subscribe),
                        'canPublishData': True}}
    signing = (_b64url(json.dumps(header, separators=(',', ':')).encode()) + '.' +
               _b64url(json.dumps(claims, separators=(',', ':')).encode()))
    sig = hmac.new(api_secret.encode(), signing.encode(), hashlib.sha256).digest()
    return signing + '.' + _b64url(sig)


def livekit_token_response(api_key, api_secret, url, query, now=None):
    """GET /lk-token: (status, body). 400 for a missing or malformed room or identity, 503 without a key pair.
    `subscribe=0` asks for a publish-only grant (canSubscribe false): LiveKit then makes the publisher connection the
    primary one and offers no subscriber connection, which a client that only publishes wants (sm_call_smoke
    --publish-only); every app subscribes by default (C20)."""
    room, identity = query.get('room', [''])[0], query.get('identity', [''])[0]
    subscribe = query.get('subscribe', ['1'])[0] != '0'
    if not LK_NAME.fullmatch(room) or not LK_NAME.fullmatch(identity):
        return 400, {'error': 'room and identity: 1-64 characters of A-Z a-z 0-9 _ - . (not starting with .)'}
    if not api_key or not api_secret:
        return 503, {'error': 'LiveKit is not configured on this server'}
    return 200, {'token': livekit_token(api_key, api_secret, room, identity, now, subscribe=subscribe),
                 'url': url or '', 'ttl': LK_TTL}


class WebSocket:
    def __init__(self, reader, writer):
        self.reader, self.writer = reader, writer

    async def recv(self):
        """The next text message, or None when the connection closed."""
        data, opcode0 = b'', None
        while True:
            try:
                b0, b1 = await self.reader.readexactly(2)
                n = b1 & 0x7F
                if n == 126:
                    n = struct.unpack('>H', await self.reader.readexactly(2))[0]
                elif n == 127:
                    n = struct.unpack('>Q', await self.reader.readexactly(8))[0]
                if n > (1 << 20):
                    return None
                mask = await self.reader.readexactly(4) if b1 & 0x80 else None
                payload = await self.reader.readexactly(n)
            except (asyncio.IncompleteReadError, ConnectionError):
                return None
            if mask:
                payload = bytes(c ^ mask[i & 3] for i, c in enumerate(payload))
            op = b0 & 0x0F
            if op == 8:
                return None
            if op == 9:
                await self._frame(10, payload)
                continue
            if op == 10:
                continue
            if op != 0:
                opcode0, data = op, b''
            data += payload
            if b0 & 0x80 and opcode0 is not None:
                return data.decode('utf-8', 'replace') if opcode0 == 1 else ''

    async def _frame(self, op, payload):
        n = len(payload)
        head = bytes([0x80 | op])
        head += bytes([n]) if n < 126 else (b'\x7e' + struct.pack('>H', n) if n < 65536 else b'\x7f' + struct.pack('>Q', n))
        self.writer.write(head + payload)
        await self.writer.drain()

    async def send(self, text):
        try:
            await self._frame(1, text.encode())
        except (ConnectionError, RuntimeError):
            pass

    def close(self):
        self.writer.close()


class Service:
    def __init__(self, turn_urls=(), turn_secret='', stun_urls=('stun:stun.l.google.com:19302',), web_dir=None,
                 livekit_key='', livekit_secret='', livekit_url=''):
        self.turn_urls, self.turn_secret, self.stun_urls = list(turn_urls), turn_secret, list(stun_urls)
        self.livekit_key, self.livekit_secret, self.livekit_url = livekit_key, livekit_secret, livekit_url
        self.web_dir = os.path.abspath(web_dir or DEFAULT_WEB_DIR)
        self.rooms = {}  # code -> {peer id -> WebSocket}
        self.ai_servers = set()  # sockets that sent {"type":"register-ai"}
        self.reports = []  # test hook: POST /report, GET /report
        self.server = None
        self.port = None

    async def start(self, host='127.0.0.1', port=0):
        self.server = await asyncio.start_server(self._connection, host, port)
        self.port = self.server.sockets[0].getsockname()[1]

    async def stop(self):
        self.server.close()
        for ws in [w for room in self.rooms.values() for w in room.values()]:
            ws.close()

    # ------------------------------------------------------------ HTTP
    async def _connection(self, reader, writer):
        try:
            head = await reader.readuntil(b'\r\n\r\n')
            lines = head.decode('latin1').split('\r\n')
            method, target, _ = lines[0].split(' ', 2)
            headers = {k.strip().lower(): v.strip() for k, _, v in (l.partition(':') for l in lines[1:] if l)}
            url = urllib.parse.urlsplit(target)
            if url.path == '/ws' and headers.get('upgrade', '').lower() == 'websocket':
                accept = base64.b64encode(hashlib.sha1(headers.get('sec-websocket-key', '').encode() + GUID).digest())
                writer.write(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                             b'Sec-WebSocket-Accept: ' + accept + b'\r\n\r\n')
                await writer.drain()
                await self._session(WebSocket(reader, writer))
                return
            body = b''
            if int(headers.get('content-length', '0')):
                body = await reader.readexactly(int(headers['content-length']))
            code, ctype, data = self._http(method, url, body)
            writer.write(('HTTP/1.1 %d X\r\nContent-Type: %s\r\nContent-Length: %d\r\nCache-Control: no-store\r\n'
                          'Connection: close\r\n\r\n' % (code, ctype, len(data))).encode() + data)
            await writer.drain()
        except (asyncio.IncompleteReadError, ConnectionError, ValueError, asyncio.LimitOverrunError):
            pass
        finally:
            writer.close()

    def _http(self, method, url, body):
        js = 'application/json'
        if url.path == '/turn':
            query = urllib.parse.parse_qs(url.query)
            peer = ''.join(c for c in query.get('peer', ['web'])[0] if c.isalnum() or c in '-_')[:32] or 'web'
            return 200, js, json.dumps(turn_response(self.turn_secret, self.turn_urls, self.stun_urls, peer)).encode()
        if url.path == '/lk-token':
            if method != 'GET':
                return 405, js, b'{}'
            code, out = livekit_token_response(self.livekit_key, self.livekit_secret, self.livekit_url,
                                               urllib.parse.parse_qs(url.query))
            return code, js, json.dumps(out).encode()
        if url.path == '/report':
            if method == 'POST':
                try:
                    self.reports.append(json.loads(body))
                except ValueError:
                    return 400, js, b'{}'
                return 200, js, b'{}'
            return 200, js, json.dumps(self.reports).encode()
        name = 'index.html' if url.path == '/' else url.path.lstrip('/')
        path = os.path.abspath(os.path.join(self.web_dir, name))
        if method != 'GET' or not path.startswith(self.web_dir + os.sep) or not os.path.isfile(path):
            return 404, js, b'{}'
        with open(path, 'rb') as f:
            return 200, TYPES.get(os.path.splitext(path)[1], 'application/octet-stream'), f.read()

    # ------------------------------------------------------- signaling
    async def _session(self, ws):
        room = peer = None
        try:
            while True:
                text = await ws.recv()
                if text is None:
                    break
                try:
                    msg = json.loads(text)
                    kind = msg['type']
                except (ValueError, KeyError, TypeError):
                    continue
                if kind == 'register-ai':
                    self.ai_servers.add(ws)
                elif kind == 'join' and peer is None:
                    code, pid = str(msg.get('room', '')), str(msg.get('peer', ''))
                    members = self.rooms.setdefault(code, {})
                    if not code or not pid or pid in members:
                        await ws.send(json.dumps({'type': 'error', 'reason': 'bad room or peer id'}))
                        continue
                    if len(members) >= MAX_PEERS:
                        await ws.send(json.dumps({'type': 'error', 'reason': 'room full'}))
                        continue
                    others = list(members)
                    members[pid] = ws
                    room, peer = code, pid
                    await ws.send(json.dumps({'type': 'joined', 'peers': others}))
                    for other in others:
                        await members[other].send(json.dumps({'type': 'peer-joined', 'peer': pid}))
                    if code.startswith('ai-') and not others:  # the room's first peer: the AI server answers it
                        for ai in list(self.ai_servers):
                            await ai.send(json.dumps({'type': 'ai-call', 'room': code}))
                elif peer is not None and kind in ('offer', 'answer', 'candidate', 'bye'):
                    target = self.rooms.get(room, {}).get(str(msg.get('to', '')))
                    if target is not None:
                        out = dict(msg)
                        out['from'] = peer
                        await target.send(json.dumps(out))
        finally:
            self.ai_servers.discard(ws)
            if peer is not None:
                members = self.rooms.get(room, {})
                if members.get(peer) is ws:
                    del members[peer]
                    for other in members.values():
                        await other.send(json.dumps({'type': 'peer-left', 'peer': peer}))
                    if not members:
                        self.rooms.pop(room, None)
            ws.close()


async def serve():
    svc = Service([u for u in os.environ.get('TURN_URLS', '').split(',') if u.strip()],
                  os.environ.get('TURN_SECRET', ''),
                  [u for u in os.environ.get('STUN_URLS', 'stun:stun.l.google.com:19302').split(',') if u.strip()],
                  os.environ.get('WEB_DIR'), os.environ.get('LIVEKIT_API_KEY', ''),
                  os.environ.get('LIVEKIT_API_SECRET', ''), os.environ.get('LIVEKIT_URL', ''))
    await svc.start(os.environ.get('HOST', '0.0.0.0'), int(os.environ.get('PORT', '8080')))
    print('signaling on port %d' % svc.port, flush=True)
    await asyncio.Event().wait()


if __name__ == '__main__':
    try:
        asyncio.run(serve())
    except KeyboardInterrupt:
        sys.exit(0)
