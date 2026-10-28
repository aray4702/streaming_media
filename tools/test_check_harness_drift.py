#!/usr/bin/env python3
"""Tests for tools/check_harness_drift.py: python3 tools/test_check_harness_drift.py"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_harness_drift as hd  # noqa: E402


def trace(sizes, seq0=0, extra=""):
    rows = ["time_us,size,kind,seq,queue_bytes,outcome"]
    for i, s in enumerate(sizes):
        rows.append("%d,%d,audio,%d,0,delivered" % (10000 + 20000 * i, s, seq0 + i))
        rows.append("%d,1155,video,%d,0,delivered" % (15000 + 20000 * i, i))
    return "\n".join(rows) + extra + "\n"


def data(sm, rtc, name="clean"):
    return {"traces": [{"scenario": name, "seed": 1, "sm": sm, "webrtc": rtc}]}


class Drift(unittest.TestCase):
    def test_same_sizes_pass_even_with_other_sequence_numbers(self):
        rows, failed = hd.check(data(trace([117, 201, 199]), trace([117, 201, 199], seq0=4000)), ["clean"])
        self.assertEqual(failed, [])
        self.assertEqual(rows[0]["same"], 3)

    def test_a_different_size_fails(self):
        rows, failed = hd.check(data(trace([117, 201, 199]), trace([117, 202, 199])), ["clean"])
        self.assertEqual(len(failed), 1)
        self.assertEqual(rows[0]["first_diff"], 1)

    def test_a_missing_packet_fails(self):
        _, failed = hd.check(data(trace([117, 201]), trace([117, 201, 199])), ["clean"])
        self.assertEqual(len(failed), 1)

    def test_retransmissions_are_left_out(self):
        rtx = "\n30500,201,audio,1,0,delivered"
        _, failed = hd.check(data(trace([117, 201, 199], extra=rtx), trace([117, 201, 199])), ["clean"])
        self.assertEqual(failed, [])

    def test_missing_required_trace_fails(self):
        _, failed = hd.check(data(trace([1]), trace([1]), name="other"), ["clean"])
        self.assertEqual(len(failed), 1)

    def test_not_required_scenarios_are_reported_only(self):
        rows, failed = hd.check(data(trace([1, 2]), trace([1, 3]), name="lossy"), [])
        self.assertEqual(failed, [])
        self.assertFalse(rows[0]["ok"])


if __name__ == "__main__":
    unittest.main()
