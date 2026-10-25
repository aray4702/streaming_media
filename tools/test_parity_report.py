#!/usr/bin/env python3
"""Tests for tools/parity_report.py: python3 tools/test_parity_report.py"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parity_report as pr  # noqa: E402

SHOWN = {
    "video.freeze_total_ms": {"better": "lower", "floor": 100},
    "video.render_fps": {"better": "higher", "floor": 0.5},
    "video.keyframe_requests": {"better": "lower", "floor": 0.5},
}


def scn(name, rows):
    """rows: metric -> (list of sm values, list of webrtc values), one per seed."""
    n = len(next(iter(rows.values()))[0])
    mean = lambda xs: sum(xs) / len(xs)
    return {
        "scenario": name,
        "sm": {m: mean(v[0]) for m, v in rows.items()},
        "webrtc": {m: mean(v[1]) for m, v in rows.items()},
        "per_seed": [{"seed": i + 1, "sm": {m: v[0][i] for m, v in rows.items()},
                      "webrtc": {m: v[1][i] for m, v in rows.items()}} for i in range(n)],
    }


def data(*scenarios):
    return {"webrtc_version": "test", "seeds": 5, "shown": SHOWN, "scenarios": list(scenarios)}


class Gaps(unittest.TestCase):
    def test_both_directions_are_gaps(self):
        d = data(scn("a", {
            "video.freeze_total_ms": ([5000] * 5, [1000] * 5),   # ours worse
            "video.render_fps": ([30] * 5, [20] * 5),           # ours better
            "video.keyframe_requests": ([1] * 5, [1.2] * 5),    # within the floor
        }))
        g = {x["metric"]: x for x in pr.gaps(d)}
        self.assertEqual(set(g), {"video.freeze_total_ms", "video.render_fps"})
        self.assertEqual(g["video.freeze_total_ms"]["direction"], "behind")
        self.assertEqual(g["video.render_fps"]["direction"], "ahead")
        self.assertEqual(g["video.freeze_total_ms"]["seeds_agree"], 5)

    def test_band_includes_libwebrtc_seed_spread(self):
        wide = [0, 4000, 0, 4000, 0]  # mean 1600, stdev ~2190
        d = data(scn("a", {"video.freeze_total_ms": ([2500] * 5, wide)}))
        self.assertEqual(pr.gaps(d, sigma=1.0), [])        # 900 apart, inside one stdev
        self.assertEqual(len(pr.gaps(d, sigma=0.0)), 1)    # without the spread it is a gap

    def test_relative_tolerance(self):
        d = data(scn("a", {"video.freeze_total_ms": ([10300] * 5, [10000] * 5)}))
        self.assertEqual(pr.gaps(d), [])  # 3%: a tie, as in sm_compare
        d = data(scn("a", {"video.freeze_total_ms": ([11000] * 5, [10000] * 5)}))
        self.assertEqual(len(pr.gaps(d)), 1)

    def test_seeds_agree_counts_only_seeds_in_the_gaps_direction(self):
        d = data(scn("a", {"video.freeze_total_ms": ([5000, 5000, 5000, 0, 0], [0, 0, 0, 100, 100])}))
        (g,) = pr.gaps(d)
        self.assertEqual((g["seeds_agree"], g["seeds"]), (3, 5))

    def test_missing_shown_table_is_an_error(self):
        with self.assertRaises(SystemExit):
            pr.gaps({"scenarios": []})

    def test_works_without_per_seed(self):
        d = data({"scenario": "a", "sm": {"video.render_fps": 30}, "webrtc": {"video.render_fps": 20}})
        (g,) = pr.gaps(d)
        self.assertEqual(g["seeds"], 0)


class Owners(unittest.TestCase):
    def test_rules_match_by_pattern_and_unowned_is_reported(self):
        d = data(scn("outage_3s", {"video.render_fps": ([30] * 5, [20] * 5),
                                   "video.keyframe_requests": ([9] * 5, [1] * 5)}),
                 scn("clean_link", {"video.freeze_total_ms": ([5000] * 5, [0] * 5)}))
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
            json.dump({"rules": [{"metric": "video.keyframe_requests", "issues": [23]},
                                 {"scenario": "outage_*", "metric": "*", "issues": [20]}]}, f)
        try:
            g = {(x["scenario"], x["metric"]): x["owners"] for x in pr.report(d, f.name)}
        finally:
            os.unlink(f.name)
        self.assertEqual(g[("outage_3s", "video.keyframe_requests")], [23, 20])
        self.assertEqual(g[("outage_3s", "video.render_fps")], [20])
        self.assertEqual(g[("clean_link", "video.freeze_total_ms")], [])

    def test_audio_delay_cells_belong_to_the_send_side_items(self):
        # #62 closed (Owner 2026-10-22): NetEq matches libwebrtc's per 10 ms (yardstick #266), so the
        # remaining audio jitter-buffer and mouth-to-ear gaps are owned by the send-side items.
        cells = {"cross_traffic": 204, "probe_v2_cross_traffic": 204, "issue_5_adjuster_default": 204,
                 "low_bandwidth": 224, "issue_53_cwnd_pushback": 224, "issue_12_lossbwe_policer": 217,
                 "issue_23_outage_500ms": 218, "issue_85_video_pause": 218}
        metrics = {"audio.jb_delay_p95_ms": ([300] * 5, [400] * 5), "audio.mouth_to_ear_p95_ms": ([300] * 5, [400] * 5),
                   "audio.concealed_pct": ([9] * 5, [1] * 5)}
        d = data(*[scn(n, metrics) for n in cells])
        d["shown"] = dict(SHOWN, **{m: {"better": "lower", "floor": 1} for m in metrics})
        owners = os.path.join(os.path.dirname(os.path.abspath(__file__)), "parity_owners.json")
        g = {(x["scenario"], x["metric"]): x["owners"] for x in pr.report(d, owners)}
        self.assertEqual(len(g), 3 * len(cells))
        for (name, metric), o in g.items():
            self.assertNotIn(62, o, (name, metric))
            if metric != "audio.concealed_pct":
                self.assertIn(cells[name], o, (name, metric))

    def test_sync_cells_follow_the_items_that_own_their_inputs(self):
        # #379 (Owner 2026-10-25): lip sync matches libwebrtc (yardstick #396); each offset cell follows
        # an audio jitter-buffer, video-delay or RTT gap, so that item owns it.
        cells = {"lossy_feedback": [365], "issue_12_lossbwe_shallow_step": [24, 37],
                 "issue_12_lossbwe_shallow": [24, 37], "issue_331_low_bandwidth_av_offset_seed4": [360],
                 "issue_76_session_layer_v2": [63],
                 "issue_152_alr_request_probe_v2": [360]}
        metrics = {"sync.av_offset_p95_ms": ([300] * 5, [100] * 5)}
        d = data(*[scn(n, metrics) for n in cells] + [scn("clean_link", metrics)])
        d["shown"] = dict(SHOWN, **{m: {"better": "lower", "floor": 1} for m in metrics})
        owners = os.path.join(os.path.dirname(os.path.abspath(__file__)), "parity_owners.json")
        g = {x["scenario"]: x["owners"] for x in pr.report(d, owners)}
        for name, o in cells.items():
            self.assertEqual(g[name], o, name)
        self.assertNotIn(379, sum(g.values(), []))
        self.assertEqual(g["clean_link"], [])  # A new sync gap needs an owner placed on the roadmap.


class Delta(unittest.TestCase):
    def test_closed_opened_and_moved(self):
        base = data(scn("a", {"video.freeze_total_ms": ([5000] * 5, [0] * 5),
                              "video.render_fps": ([30] * 5, [20] * 5)}))
        head = data(scn("a", {"video.freeze_total_ms": ([0] * 5, [0] * 5),         # closed
                              "video.render_fps": ([30] * 5, [20] * 5),
                              "video.keyframe_requests": ([9] * 5, [1] * 5)}))   # opened
        md = pr.markdown(head, pr.report(head), pr.report(base))
        self.assertIn("**1 closed, 1 opened**", md)
        self.assertIn("Closed:", md)
        self.assertIn("Opened:", md)

    def test_cli_writes_files_and_check_owned_fails_on_unowned(self):
        d = data(scn("a", {"video.render_fps": ([30] * 5, [20] * 5)}))
        with tempfile.TemporaryDirectory() as t:
            src = os.path.join(t, "c.json")
            with open(src, "w") as f:
                json.dump(d, f)
            md, js = os.path.join(t, "p.md"), os.path.join(t, "p.json")
            self.assertEqual(pr.main([src, "--markdown", md, "--json", js, "--check-owned"]), 1)
            with open(md) as f:
                self.assertIn("1 without an owner", f.read())
            with open(js) as f:
                self.assertEqual(json.load(f)["gaps"][0]["metric"], "video.render_fps")
            self.assertEqual(pr.main([src, "--markdown", md, "--base", os.path.join(t, "missing.json")]), 0)


if __name__ == "__main__":
    unittest.main()
