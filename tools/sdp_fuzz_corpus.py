#!/usr/bin/env python3
"""Builds the seed corpus of the `sdp_parser` fuzz target (fuzz/sdp_parser_fuzzer.cc, issue #1062) into
fuzz/corpus/sdp_parser/, one file per input named by its SHA-1 as libFuzzer names corpus files. Regenerate it, don't
edit it:

  python3 tools/sdp_fuzz_corpus.py [--tests tests/unit/pc_sdp/webrtc_sdp_test.cc] [--out fuzz/corpus/sdp_parser]

The inputs are the SDP strings of tests/unit/pc_sdp/webrtc_sdp_test.cc (libwebrtc's webrtc_sdp_unittest.cc strings
and the Chrome-style offer and answer): every complete description (a constant that starts with "v=0") as it is,
and every m= section constant after the test's session prefix.
"""
import argparse
import ast
import hashlib
import pathlib
import re
import shutil

CONSTANT = re.compile(r'constexpr char (k\w+)\[\]\s*=\s*((?:\s*"(?:[^"\\]|\\.)*")+)\s*;', re.S)
PIECE = re.compile(r'"((?:[^"\\]|\\.)*)"')


def constants(path):
    text = pathlib.Path(path).read_text()
    out = {}
    for name, body in CONSTANT.findall(text):
        out[name] = "".join(ast.literal_eval('"' + piece + '"') for piece in PIECE.findall(body))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tests", default="tests/unit/pc_sdp/webrtc_sdp_test.cc")
    parser.add_argument("--out", default="fuzz/corpus/sdp_parser")
    args = parser.parse_args()
    found = constants(args.tests)
    prefix = found["kSdpSessionPrefix"]
    inputs = []
    for name, value in sorted(found.items()):
        if value.startswith("v=0"):
            inputs.append(value)
        elif value.startswith("m="):
            inputs.append(prefix + value)
    out = pathlib.Path(args.out)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    for data in inputs:
        raw = data.encode()
        (out / hashlib.sha1(raw).hexdigest()).write_bytes(raw)
    print(f"{len(inputs)} inputs in {out}")


if __name__ == "__main__":
    main()
