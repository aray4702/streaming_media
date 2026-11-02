#!/usr/bin/env python3
"""Builds the seed corpus of the fuzz targets (fuzz/, issue #989) from test
bench calls.

Runs sm_testbench --packet-dump on a few scenarios that between them send
every packet kind (audio with RED, H.264 video, RTX, padding, ULPFEC over RED,
compound RTCP with reports, NACK, PLI, transport-cc and REMB), then writes
each target's inputs into fuzz/corpus/<target>/, one file per input named by
its SHA-1 as libFuzzer names corpus files. The corpus is regenerated, not
edited: rerun this after a change to what the test bench sends.

  python3 tools/fuzz_corpus.py [--testbench out/native/testbench/sm_testbench] [--out fuzz/corpus]
"""
import argparse
import hashlib
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile

SCENARIOS = [
    "clean_link.scn",                 # audio, H.264 video, transport-cc, reports
    "burst_loss.scn",                 # NACK, RTX, PLI
    "issue_29_ulpfec_over_red.scn",   # ULPFEC over RED, video RED
    "issue_514_padding_without_rtx.scn",  # padding on the media SSRC
    "conversation_burst_loss.scn",    # audio RED
]
VIDEO_RED_PT = 118   # sm::loss::ulpfec defaults
ULPFEC_PT = 119
AUDIO_RED_PT = 63    # testbench/call.cc kAudioRedPt
PER_TARGET = 48      # Inputs kept per target and source kind.

NAME = re.compile(r"^(?P<scn>.+)\.(?P<seed>\d+)\.(?P<dir>fwd|rev)\.(?P<kind>[a-z]+)\.(?P<n>\d+)\.bin$")


def rtp_fields(b):
    """(payload_type, seq, timestamp, marker, payload) of an RTP packet, or None."""
    if len(b) < 12 or b[0] >> 6 != 2:
        return None
    cc = b[0] & 0x0F
    off = 12 + 4 * cc
    if b[0] & 0x10:
        if len(b) < off + 4:
            return None
        off += 4 + 4 * struct.unpack(">H", b[off + 2:off + 4])[0]
    end = len(b)
    if b[0] & 0x20:
        end -= b[-1]
    if off > end:
        return None
    return b[1] & 0x7F, struct.unpack(">H", b[2:4])[0], struct.unpack(">I", b[4:8])[0], bool(b[1] & 0x80), b[off:end]


def split_rtcp(b):
    out, i = [], 0
    while i + 4 <= len(b):
        n = 4 * (struct.unpack(">H", b[i + 2:i + 4])[0] + 1)
        out.append(b[i:i + n])
        i += n
    return out


def lp(b):
    return struct.pack(">H", len(b)) + b


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--testbench", default="out/native/testbench/sm_testbench")
    ap.add_argument("--scenarios", default="testbench/scenarios")
    ap.add_argument("--out", default="fuzz/corpus")
    a = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        files = [os.path.join(a.scenarios, s) for s in SCENARIOS]
        subprocess.run([a.testbench, "--quiet", "--packet-dump=" + tmp, *files], check=False, stdout=subprocess.DEVNULL)
        packets = {}  # (scenario, dir, kind) -> [bytes] in send order
        for f in sorted(os.listdir(tmp), key=lambda x: (NAME.match(x).group("scn", "dir", "kind"), int(NAME.match(x).group("n")))):
            m = NAME.match(f)
            packets.setdefault((m["scn"], m["dir"], m["kind"]), []).append(pathlib.Path(tmp, f).read_bytes())

    corpus = {t: [] for t in ("rtp_packet", "rtcp", "transport_feedback", "h264_depacketizer",
                              "packet_buffer", "red", "ulpfec")}
    for (scn, _, kind), pkts in sorted(packets.items()):
        if kind == "cross":
            continue
        if kind == "rtcp":
            corpus["rtcp"] += pkts[:PER_TARGET]
            for d in pkts:
                for blk in split_rtcp(d):
                    if len(blk) >= 2 and blk[1] == 205 and blk[0] & 0x1F == 15:
                        corpus["transport_feedback"].append(blk)
            continue
        corpus["rtp_packet"] += pkts[:PER_TARGET // 4]
        parsed = [p for p in (rtp_fields(b) for b in pkts) if p]
        if kind == "audio":
            for pt, _, ts, _, pl in parsed[:PER_TARGET // 2]:
                if pt == AUDIO_RED_PT:
                    corpus["red"].append(bytes([AUDIO_RED_PT]) + struct.pack(">I", ts) + pl)
        if kind == "video":
            h264 = []
            for pt, seq, ts, marker, pl in parsed:
                if pt == VIDEO_RED_PT and pl:
                    pl = pl[1:]  # One-block RED header in front of the H.264 payload.
                h264.append((seq, ts, marker, pl))
            for _, _, _, pl in h264[:PER_TARGET]:
                corpus["h264_depacketizer"].append(pl)
            # packet_buffer: runs of 32 packets in the fuzzer's record format, in order and reordered.
            for start in range(0, min(len(h264), 4 * 32), 32):
                run = h264[start:start + 32]
                rec = [struct.pack(">HIB", s, t, 1 if m else 0) + lp(pl) for s, t, m, pl in run]
                corpus["packet_buffer"].append(b"".join(rec))
                corpus["packet_buffer"].append(b"".join(rec[1::2] + rec[0::2]))
        if kind in ("video", "fec"):
            red = [b for b in pkts if rtp_fields(b) and rtp_fields(b)[0] == VIDEO_RED_PT]
            if red and kind == "fec":
                corpus["ulpfec"].append(b"".join(b"\x00" + lp(b) for b in red[:24]))
    # ULPFEC needs media and FEC of the same frames interleaved, as sent: rebuild from the fec scenario.
    for (scn, d, kind), pkts in packets.items():
        if kind == "video" and (scn, d, "fec") in packets:
            fec = packets[(scn, d, "fec")]
            media = [b for b in pkts if rtp_fields(b) and rtp_fields(b)[0] == VIDEO_RED_PT]
            both = sorted(media + fec, key=lambda b: rtp_fields(b)[1] if rtp_fields(b) else 0)
            for start in range(0, min(len(both), 96), 24):
                run = both[start:start + 24]
                corpus["ulpfec"].append(b"".join(b"\x00" + lp(b) for b in run))
                # The same with every third media packet missing, so recovery runs.
                corpus["ulpfec"].append(b"".join(b"\x00" + lp(b) for i, b in enumerate(run)
                                                 if i % 3 or (rtp_fields(b) and rtp_fields(b)[4][:1] == bytes([ULPFEC_PT]))))

    total = 0
    for t, inputs in corpus.items():
        d = pathlib.Path(a.out, t)
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)
        seen = set()
        for b in inputs:
            h = hashlib.sha1(b).hexdigest()
            if h in seen:
                continue
            seen.add(h)
            (d / h).write_bytes(b)
        total += len(seen)
        print(f"{t}: {len(seen)} inputs")
        if not seen:
            print(f"error: no inputs for {t}", file=sys.stderr)
            return 1
    print(f"{total} inputs in {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
