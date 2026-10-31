#!/usr/bin/env python3
"""Tests for tools/merge_compare.py: python3 tools/test_merge_compare.py"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import merge_compare as mc  # noqa: E402

SHOWN = {"video.freeze_total_ms": {"better": "lower", "floor": 100}}


def full_run(names, skipped=()):
    """What one sm_compare run over `names` writes: (json, text); `skipped` names say `compare off`."""
    scenarios = [{"scenario": n, "sm": {"video.freeze_total_ms": i}, "webrtc": {"video.freeze_total_ms": 2 * i},
                  "per_seed": []} for i, n in enumerate(names) if n not in skipped]
    text = "streaming_media vs libwebrtc M157, mean of 10 seed(s)\n"
    for i, n in enumerate(names):
        if n in skipped:
            text += "\n%s: skipped (compare off: not comparable with libwebrtc)\n" % n
            continue
        text += "\n%s - scenario %d\n  %-30s %12s %12s  %s\n  video.freeze_total_ms %d %d  sm\n" % (
            n, i, "metric", "sm", "webrtc", "better", i, 2 * i)
        text += "  forward link per kind (information only)\n"
    text += "\nbetter: sm %d, webrtc 0, tie 0 (of the metrics shown)\n\nMetrics measured differently for libwebrtc:\n  x: y\n" % len(scenarios)
    j = {"webrtc_version": "M157", "seeds": 10, "shown": SHOWN, "scenarios": scenarios}
    return j, text


class Merge(unittest.TestCase):
    def test_round_robin_shards_merge_back_to_the_full_run(self):
        self.check_shards(["s%02d" % i for i in range(13)], ())

    def test_a_skipped_scenario_keeps_its_place(self):
        # #921: `compare off` leaves shard 0 one JSON entry short; the old merge failed on round-robin counts.
        self.check_shards(["s%02d" % i for i in range(13)], ("s04", "s09"))

    def check_shards(self, names, skipped):
        whole_json, whole_text = full_run(names, skipped)
        n = 4
        jsons, txts = [], []
        for k in range(n):
            j, t = full_run(names[k::n], skipped)
            # A shard numbers its own scenarios; give them the full run's values.
            j["scenarios"] = [s for s in whole_json["scenarios"] if s["scenario"] in names[k::n]]
            jsons.append(j)
            txts.append(t)
        # Each shard's text carries its own scenario lines; rebuild them from the full run's blocks.
        whole_blocks = mc.split_txt(whole_text)[1]
        for k in range(n):
            header, _, wins, tail = mc.split_txt(txts[k])
            txts[k] = header + "\n" + "".join("\n" + b + "\n" for b in whole_blocks[k::n]) + "\n" + tail
            wins = sum(1 for nm in names[k::n] if nm not in skipped)
            txts[k] = mc.BETTER.sub("better: sm %d, webrtc 0, tie 0 (of the metrics shown)" % wins, txts[k], count=1)
        merged, text = mc.merge(jsons, txts)
        self.assertEqual(merged, whole_json)
        self.assertEqual(text, whole_text)

    def test_shards_must_agree(self):
        a, ta = full_run(["a"])
        b, tb = full_run(["b"])
        b["seeds"] = 3
        with self.assertRaises(SystemExit):
            mc.merge([a, b], [ta, tb])

    def test_block_count_must_match(self):
        a, ta = full_run(["a", "b"])
        a["scenarios"] = a["scenarios"][:1]
        with self.assertRaises(SystemExit):
            mc.merge([a], [ta])


if __name__ == "__main__":
    unittest.main()
