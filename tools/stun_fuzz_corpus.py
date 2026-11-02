#!/usr/bin/env python3
"""Builds the seed corpus of the `stun` fuzz target (fuzz/stun_fuzzer.cc,
issue #1017) into fuzz/corpus/stun/, one file per input named by its SHA-1 as
libFuzzer names corpus files. Regenerate it, don't edit it:

  python3 tools/stun_fuzz_corpus.py [--tests tests/unit/p2p/stun_test.cc] [--out fuzz/corpus/stun]

The inputs:
- every sample packet of tests/unit/p2p/stun_test.cc (libwebrtc's
  stun_unittest.cc packets, #986/#1014), among them RFC 5769's test vectors;
- the messages the STUN transactions exchange with the test STUN server
  (include/sm/sim/stun_server.h, #997), built here as it builds them: a
  binding request and its XOR-MAPPED-ADDRESS response (IPv4 and IPv6), an RFC
  3489 request and its MAPPED-ADDRESS response, and the 600 error it answers
  other requests with;
- ICE connectivity checks as libwebrtc's Connection sends them (USERNAME,
  PRIORITY, ICE-CONTROLLING / ICE-CONTROLLED, USE-CANDIDATE, GOOG-NETWORK-INFO,
  MESSAGE-INTEGRITY or GOOG-MESSAGE-INTEGRITY-32, FINGERPRINT), signed with
  RFC 5769's short-term password, and their responses;
- TURN Allocate and its 401 error, as a TURN client and server exchange them.
"""
import argparse
import hashlib
import hmac
import ipaddress
import pathlib
import re
import shutil
import struct
import zlib

COOKIE = 0x2112A442
PASSWORD = b"VOkJxbRl1RmTxUk/WvJxBt"  # RFC 5769 2.1's short-term password.

ARRAY = re.compile(r"constexpr uint8_t (k\w+)\[\]\s*=\s*\{([^}]*)\};", re.S)


def test_packets(path):
    text = pathlib.Path(path).read_text()
    out = {}
    for name, body in ARRAY.findall(text):
        body = re.sub(r"//[^\n]*", "", body)
        out[name] = bytes(int(v, 0) for v in re.findall(r"0x[0-9a-fA-F]+|\d+", body))
    return out


def attr(t, v):
    pad = (-len(v)) % 4
    return struct.pack(">HH", t, len(v)) + v + b"\0" * pad


def header(t, length, tid):
    if len(tid) == 16:  # RFC 3489: no magic cookie.
        return struct.pack(">HH", t, length) + tid
    return struct.pack(">HHI", t, length, COOKIE) + tid


def message(t, tid, attrs, integrity=None, integrity32=False, fingerprint=False):
    body = b"".join(attrs)
    if integrity is not None:
        size = 4 if integrity32 else 20
        atype = 0xC060 if integrity32 else 0x0008
        length = len(body) + 4 + size
        mac = hmac.new(integrity, header(t, length, tid) + body, hashlib.sha1).digest()[:size]
        body += attr(atype, mac)
    if fingerprint:
        length = len(body) + 8
        crc = zlib.crc32(header(t, length, tid) + body) ^ 0x5354554E
        body += attr(0x8028, struct.pack(">I", crc))
    return header(t, len(body), tid) + body


def address(ip, port, xor_tid=None):
    a = ipaddress.ip_address(ip)
    raw = a.packed
    family = 1 if a.version == 4 else 2
    if xor_tid is not None:
        port ^= COOKIE >> 16
        key = struct.pack(">I", COOKIE) + (xor_tid if a.version == 6 else b"")
        raw = bytes(x ^ y for x, y in zip(raw, key))
    return struct.pack(">BBH", 0, family, port) + raw


def error_code(code, reason):
    return struct.pack(">HBB", 0, code // 100, code % 100) + reason


def built_messages():
    tid = bytes(range(1, 13))
    tid2 = bytes(range(0x40, 0x4C))
    legacy_tid = bytes(range(0x80, 0x90))
    msgs = {}
    # The test STUN server's exchanges (sim/stun_server.h).
    msgs["binding_request"] = message(0x0001, tid, [])
    msgs["binding_request_fp"] = message(0x0001, tid, [attr(0x8022, b"sm test")], fingerprint=True)
    msgs["binding_response_v4"] = message(0x0101, tid, [attr(0x0020, address("203.0.113.7", 50000, tid))])
    msgs["binding_response_v6"] = message(
        0x0101, tid, [attr(0x0020, address("2001:db8::1:2:3:4", 3478, tid))])
    msgs["legacy_binding_request"] = message(0x0001, legacy_tid, [])
    msgs["legacy_binding_response"] = message(0x0101, legacy_tid, [attr(0x0001, address("198.51.100.2", 4000))])
    msgs["server_600_error"] = message(0x0113, tid, [attr(0x0009, error_code(600, b"Operation Not Supported"))])
    # ICE connectivity checks and their responses.
    check = [
        attr(0x0006, b"evtj:h6vY"),                            # USERNAME
        attr(0xC057, struct.pack(">HH", 1, 50)),               # GOOG-NETWORK-INFO
        attr(0x0024, struct.pack(">I", 0x6E7F1EFF)),           # PRIORITY
    ]
    msgs["ice_check_controlling"] = message(
        0x0001, tid2, check + [attr(0x802A, struct.pack(">Q", 0x0123456789ABCDEF)), attr(0x0025, b"")],
        integrity=PASSWORD, fingerprint=True)
    msgs["ice_check_controlled"] = message(
        0x0001, tid2, check + [attr(0x8029, struct.pack(">Q", 0xFEDCBA9876543210))],
        integrity=PASSWORD, fingerprint=True)
    msgs["ice_check_mi32"] = message(
        0x0001, tid2, check + [attr(0x8029, struct.pack(">Q", 1))],
        integrity=PASSWORD, integrity32=True, fingerprint=True)
    msgs["ice_check_response"] = message(
        0x0101, tid2, [attr(0x0020, address("192.0.2.1", 32853, tid2))], integrity=PASSWORD, fingerprint=True)
    msgs["ice_role_conflict"] = message(
        0x0111, tid2, [attr(0x0009, error_code(487, b"Role Conflict"))], integrity=PASSWORD, fingerprint=True)
    msgs["ice_unsigned_response"] = message(0x0101, tid2, [attr(0x0020, address("192.0.2.1", 32853, tid2))])
    # TURN Allocate (RFC 5766) and the server's 401.
    msgs["turn_allocate"] = message(0x0003, tid, [attr(0x0019, bytes([17, 0, 0, 0])), attr(0x000D, struct.pack(">I", 600))])
    msgs["turn_allocate_401"] = message(
        0x0113, tid, [attr(0x0009, error_code(401, b"Unauthorized")), attr(0x0014, b"example.org"),
                      attr(0x0015, b"f//499k954d6OL34oL9FSTvy64sA")])
    msgs["turn_allocate_response"] = message(
        0x0103, tid, [attr(0x0016, address("203.0.113.9", 49152, tid)), attr(0x0020, address("198.51.100.4", 5000, tid)),
                      attr(0x000D, struct.pack(">I", 600))])
    return msgs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tests", default="tests/unit/p2p/stun_test.cc")
    ap.add_argument("--out", default="fuzz/corpus/stun")
    a = ap.parse_args()

    inputs = {**test_packets(a.tests), **built_messages()}
    out = pathlib.Path(a.out)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    names = set()
    for data in inputs.values():
        name = hashlib.sha1(data).hexdigest()
        names.add(name)
        (out / name).write_bytes(data)
    print(f"{len(names)} inputs in {out} ({len(inputs)} sources)")


if __name__ == "__main__":
    main()
