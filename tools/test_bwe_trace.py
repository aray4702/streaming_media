#!/usr/bin/env python3
"""Tests for tools/bwe_trace.py: python3 tools/test_bwe_trace.py"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bwe_trace as bt  # noqa: E402

HEAD = "time_us,event,bitrate_bps,detector,fraction_loss,packets,loss_state,loss_limit_bps,instant_ub_bps\n"
SM = (HEAD +
      "100000,delay_based,1500000,normal,,,,,\n"
      "100000,loss_based,1500000,,0,30,increasing,,5000000\n"
      "1200000,delay_based,1800000,normal,,,,,\n"
      "1200000,loss_based,1600000,,8,25,decreasing,1600000,5000000\n"
      "1250000,target,1600000,,,,,,\n")
RTC = (HEAD +
       "100000,delay_based,1400000,normal,,,,,\n"
       "100000,loss_based,1400000,,0,30,,,\n"
       "1300000,delay_based,1900000,overusing,,,,,\n"
       "1300000,loss_based,1500000,,9,28,,,\n")


class Parse(unittest.TestCase):
    def test_empty_columns_are_none(self):
        rows = bt.parse(SM)
        self.assertEqual(rows[0]["fraction_loss"], None)
        self.assertEqual(rows[1]["loss_state"], "increasing")
        self.assertEqual(rows[3]["loss_limit_bps"], 1600000)


class Bound(unittest.TestCase):
    def test_delay_when_the_target_equals_the_delay_estimate(self):
        rows = bt.per_second(bt.parse(SM), 0, 0)
        self.assertEqual(rows[0]["bound"], "delay")

    def test_loss_when_the_target_is_below_it(self):
        rows = bt.per_second(bt.parse(SM), 1, 1)
        self.assertEqual((rows[0]["delay_bps"], rows[0]["loss_bps"], rows[0]["bound"]), (1800000, 1600000, "loss"))
        self.assertEqual(rows[0]["target_bps"], 1600000)

    def test_values_are_held_and_a_missing_stream_has_no_bound(self):
        rows = bt.per_second(bt.parse(RTC), 0, 2)
        self.assertEqual([r["delay_bps"] for r in rows], [1400000, 1900000, 1900000])
        self.assertEqual(rows[1]["detector"], "overusing")
        self.assertIsNone(bt.per_second([], 0, 0)[0]["bound"])


class Output(unittest.TestCase):
    def test_side_by_side_and_summary(self):
        text = bt.side_by_side(bt.parse(SM), bt.parse(RTC))
        self.assertIn("sm      target held by the delay-based estimate in 1 of 2 seconds", text)
        self.assertIn("webrtc  target held by the delay-based estimate in 1 of 2 seconds", text)

    def test_json_input(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "c.json")
            with open(p, "w") as f:
                json.dump({"traces": [{"scenario": "x", "seed": 1, "sm_bwe": SM, "webrtc_bwe": RTC}]}, f)
            bt.main([p, "--scenario", "x", "--from", "1"])


if __name__ == "__main__":
    unittest.main()
