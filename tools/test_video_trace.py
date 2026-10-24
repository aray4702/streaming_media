#!/usr/bin/env python3
"""Tests for tools/video_trace.py: python3 tools/test_video_trace.py"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import video_trace as vt  # noqa: E402

HEAD = ("t_s,encoder_target_bps,alloc_target_bps,captured,encoded,dropped_capturer,dropped_encoder_queue,"
        "dropped_rate_limiter,dropped_congestion_window,dropped_encoder,paused,encoded_bytes,pacer_queue_ms\n")
SM = HEAD + "1,50000,60000,30,24,0,0,6,0,0,0,9000,12.5\n2,40000,60000,30,22,0,0,6,2,0,0,8000,30.0\n"
RTC = HEAD + "1,50000,55000,30,14,0,4,12,0,0,0,5000,5.0\n2,30000,55000,30,14,0,0,0,0,16,1,5200,7.0\n"


class Summary(unittest.TestCase):
    def test_totals_and_drops_by_reason(self):
        s = vt.summarize(vt.parse(SM))
        self.assertEqual((s["captured"], s["encoded"], s["dropped"]), (60, 46, 14))
        self.assertEqual(s["drops"]["dropped_rate_limiter"], 12)
        self.assertEqual(s["drops"]["dropped_congestion_window"], 2)
        self.assertAlmostEqual(s["encoded_fps"], 23.0)
        self.assertAlmostEqual(s["encoded_kbps"], 68.0)
        self.assertAlmostEqual(s["encoder_target_kbps_mean"], 45.0)
        self.assertEqual(s["pacer_queue_ms_max"], 30.0)

    def test_qp_and_encode_time_weighted_by_frames(self):
        head = HEAD.rstrip("\n") + ",qp_mean,encode_ms_mean\n"
        rows = vt.parse(head + "1,50000,60000,30,30,0,0,0,0,0,0,9000,1.0,40.0,10.00\n"
                               "2,50000,60000,30,10,0,0,20,0,0,0,3000,1.0,20.0,2.00\n")
        s = vt.summarize(rows)
        self.assertAlmostEqual(s["qp_mean"], 35.0)
        self.assertAlmostEqual(s["encode_ms_mean"], 8.0)
        # A trace from before issue #84 has neither column.
        self.assertEqual(vt.summarize(vt.parse(SM))["qp_mean"], 0.0)

    def test_resolution_keyframes_and_rendering(self):
        head = HEAD.rstrip("\n") + (",qp_mean,encode_ms_mean,encode_width,encode_height,keyframes,keyframe_bytes,"
                                    "rendered,max_render_gap_ms\n")
        rows = vt.parse(head + "1,50000,60000,30,30,0,0,0,0,0,0,9000,1.0,40.0,10.00,640,360,1,1200,30,40.0\n"
                               "2,50000,60000,30,0,0,0,30,0,0,0,0,1.0,0.0,0.00,0,0,0,0,0,0.0\n"
                               "3,50000,60000,30,30,0,0,0,0,0,0,9000,1.0,40.0,10.00,480,270,2,2000,10,1450.5\n")
        s = vt.summarize(rows)
        self.assertEqual((s["keyframes"], s["keyframe_bytes"]), (3, 3200))
        self.assertEqual(s["resolution_switches"], 1)  # 360 -> 270; the second with no frame is skipped
        self.assertEqual(s["rendered"], 40)
        self.assertEqual(s["max_render_gap_ms"], 1450.5)
        self.assertIn(" 270    2     10    1450", vt.series({"sm": rows}))
        # A trace from before issue #333 reads the new columns as 0.
        self.assertEqual(vt.summarize(vt.parse(SM))["keyframes"], 0)

    def test_paused_and_other_reasons(self):
        s = vt.summarize(vt.parse(RTC))
        self.assertEqual(s["paused_seconds"], 1)
        self.assertEqual(s["drops"]["dropped_encoder_queue"], 4)
        self.assertEqual(s["drops"]["dropped_encoder"], 16)

    def test_render_and_series_have_both_stacks(self):
        named = {"sm": vt.parse(SM), "webrtc": vt.parse(RTC)}
        text = vt.render({n: vt.summarize(r) for n, r in named.items()})
        self.assertIn("rate_limiter", text)
        self.assertIn("frames encoded", text)
        lines = vt.series(named).splitlines()
        self.assertEqual(len(lines), 3)  # header and two seconds

    def test_json_input(self):
        data = {"traces": [{"scenario": "x", "seed": 1, "sm": "", "webrtc": "", "sm_video": SM, "webrtc_video": RTC}]}
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "c.json")
            out = os.path.join(d, "o.json")
            with open(path, "w") as f:
                json.dump(data, f)
            self.assertEqual(vt.main([path, "--json", out]), 0)
            with open(out) as f:
                res = json.load(f)
        self.assertEqual(res["x, seed 1"]["webrtc"]["encoded"], 28)


if __name__ == "__main__":
    unittest.main()
