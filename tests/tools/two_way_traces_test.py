#!/usr/bin/env python3
"""A two-way call writes each direction's traces, and the trace tools read them (issue #749).

Usage: two_way_traces_test.py SM_TESTBENCH SCENARIO.scn
"""
import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import bwe_trace  # noqa: E402
import link_trace  # noqa: E402


def main():
    exe, scn = sys.argv[1], sys.argv[2]
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "r.json")
        subprocess.run([exe, "--quiet", "--timeseries", "--library", "--json=" + out, "--link-trace=" + d, scn], check=True)
        report = json.load(open(out))[0]
        assert "timeseries" not in report, "a two-way call prefixes its time series"
        for dir_ in ("up", "down"):
            assert report[dir_ + ".timeseries"]["rows"], dir_
            assert any(k.startswith(dir_ + ".sender.gcc.") for k in report["library"]), dir_
            assert any(k.startswith(dir_ + ".receiver.neteq.") for k in report["library"]), dir_
        base = os.path.join(d, report["scenario"] + ".1")
        names = ("trace", "video", "frames", "bwe", "audio", "audiosend")
        for dir_ in ("up", "down"):
            for n in names:
                p = "%s.%s.sm.%s.csv" % (base, dir_, n)
                assert os.path.exists(p), p
        assert not os.path.exists(base + ".sm.trace.csv"), "no one-way file names in a two-way call"
        with contextlib.redirect_stdout(io.StringIO()) as text:
            link_trace.main([base + ".up.sm.trace.csv", base + ".down.sm.trace.csv"])
            bwe_trace.main([base + ".up.sm.bwe.csv", base + ".down.sm.bwe.csv", "--to", "3"])
        assert "audio" in text.getvalue()
    print("ok")


if __name__ == "__main__":
    main()
