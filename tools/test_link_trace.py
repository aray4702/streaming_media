#!/usr/bin/env python3
"""Tests for tools/link_trace.py: python3 tools/test_link_trace.py"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import link_trace as lt  # noqa: E402

SM = """time_us,size,kind,seq,queue_bytes,outcome
0,213,audio,1,0,delivered
20000,213,audio,2,0,delivered
40000,213,audio,3,500,queue_drop
40000,1200,video,10,500,queue_drop
60000,1200,video,11,0,delivered
60000,60,other,-1,0,delivered
"""
RTC = """time_us,size,kind,seq,queue_bytes,outcome
0,189,audio,1,0,delivered
20000,189,audio,2,0,delivered
40000,189,audio,3,0,delivered
40000,1200,rtx,7,900,queue_drop
"""


class Summary(unittest.TestCase):
    def test_per_kind_rates_sizes_and_gaps(self):
        s = lt.summarize(lt.parse(SM), duration_s=1.0)
        a = s["kinds"]["audio"]
        self.assertEqual(a["packets"], 3)
        self.assertAlmostEqual(a["offered_kbps"], 3 * 213 * 8 / 1000.0)
        self.assertEqual((a["size_p50"], a["size_p90"], a["size_max"]), (213, 213, 213))
        self.assertEqual(a["gap_ms_p50"], 20.0)
        self.assertEqual(a["drops"]["queue_drop"], 1)
        self.assertAlmostEqual(a["drop_pct"], 100 / 3)
        self.assertEqual(s["kinds"]["other"]["packets"], 1)

    def test_drops_by_kind_share(self):
        s = lt.summarize(lt.parse(SM), duration_s=1.0)
        self.assertEqual(s["drops"], 2)
        self.assertAlmostEqual(s["kinds"]["audio"]["share_of_drops_pct"], 50.0)
        self.assertAlmostEqual(s["kinds"]["video"]["share_of_drops_pct"], 50.0)

    def test_duration_defaults_to_last_packet(self):
        self.assertAlmostEqual(lt.summarize(lt.parse(SM))["duration_s"], 0.06)

    def test_size_histogram_buckets(self):
        h = lt.size_histogram([60, 189, 213, 1200, 1500])
        self.assertEqual(h["<=100"], 1)
        self.assertEqual(h["<=200"], 1)
        self.assertEqual(h["<=250"], 1)
        self.assertEqual(h["<=1300"], 1)
        self.assertEqual(h[">1300"], 1)

    def test_nearest_rank_percentile(self):
        xs = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
        self.assertEqual(lt.percentile(xs, 50), 5)
        self.assertEqual(lt.percentile(xs, 90), 9)
        self.assertEqual(lt.percentile([], 90), 0)


class Cli(unittest.TestCase):
    def test_json_input_renders_both_stacks_and_extracts_csvs(self):
        data = {"traces": [{"scenario": "x", "seed": 1, "sm": SM, "webrtc": RTC}]}
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "c.json")
            with open(path, "w") as f:
                json.dump(data, f)
            out = os.path.join(d, "o.json")
            self.assertEqual(lt.main([path, "--json", out]), 0)
            with open(out) as f:
                res = json.load(f)["x, seed 1"]
            self.assertEqual(res["sm"]["kinds"]["audio"]["size_p50"], 213)
            self.assertEqual(res["webrtc"]["kinds"]["audio"]["size_p50"], 189)
            self.assertEqual(res["webrtc"]["kinds"]["rtx"]["drops"]["queue_drop"], 1)
            self.assertEqual(lt.main([path, "--csv-dir", os.path.join(d, "csv")]), 0)
            with open(os.path.join(d, "csv", "x.1.webrtc.trace.csv")) as f:
                self.assertEqual(f.read(), RTC)

    def test_render_has_a_column_per_stack(self):
        text = lt.render({"sm": lt.summarize(lt.parse(SM), 1.0), "webrtc": lt.summarize(lt.parse(RTC), 1.0)})
        self.assertIn("audio", text)
        self.assertIn("rtx", text)
        self.assertIn("offered kbps", text)

    def test_missing_traces_is_an_error(self):
        with self.assertRaises(SystemExit):
            lt.traces_in({"scenarios": []})


if __name__ == "__main__":
    unittest.main()
