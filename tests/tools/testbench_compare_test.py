#!/usr/bin/env python3
"""Tests of tools/testbench_compare.py's two baseline forms (issue #498):
the single file and the per-scenario directory load to the same reports,
compare the same, and `--update` keeps both in step."""
import io
import json
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import testbench_compare as tc  # noqa: E402


def report(name, kbps, passed=True):
    return {"scenario": name, "description": f"{name} desc", "passed": passed,
            "metrics": {"cc.target_avg_kbps": kbps, "video.freeze_count": 0}}


def write(path, obj):
    with open(path, "w") as f:
        json.dump(obj, f)


def run(*argv):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = tc.main(["testbench_compare.py", *argv])
    return code, out.getvalue(), err.getvalue()


class BaselineForms(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.file = os.path.join(self.dir, "baseline.json")
        self.split = os.path.join(self.dir, "baseline")
        write(self.file, [report("a", 500), report("b", 800)])

    def tearDown(self):
        self.tmp.cleanup()

    def results(self, *reports):
        path = os.path.join(self.dir, "r.json")
        write(path, list(reports))
        return path

    def test_split_loads_the_same_reports(self):
        self.assertEqual(run("--split", self.file, self.split)[0], 0)
        self.assertEqual(sorted(os.listdir(self.split)), ["a.json", "b.json"])
        self.assertEqual(tc.load(self.file), tc.load(self.split))

    def test_compare_is_identical_on_both_forms(self):
        run("--split", self.file, self.split)
        r = self.results(report("a", 400), report("b", 800), report("c", 100))
        by_file, by_dir = run(self.file, r, "--markdown"), run(self.split, r, "--markdown")
        self.assertEqual(by_file, by_dir)
        self.assertEqual(by_file[0], 1)  # a: 500 -> 400 kbps regressed.
        self.assertIn("| a | cc.target_avg_kbps | 500 | 400 | REGRESSED |", by_file[1])
        self.assertIn("| c | (new scenario) | - | - | new |", by_file[1])

    def test_update_directory_rewrites_only_changed_files_and_the_file(self):
        run("--split", self.file, self.split)
        b_path = os.path.join(self.split, "b.json")
        os.utime(b_path, (1, 1))
        r = self.results(report("a", 600), report("b", 800), report("c", 100))
        run(self.split, r, "--update")
        self.assertEqual(sorted(os.listdir(self.split)), ["a.json", "b.json", "c.json"])
        self.assertEqual(os.stat(b_path).st_mtime, 1)  # Unchanged numbers: not rewritten.
        self.assertEqual(tc.load(self.split)["a"]["metrics"]["cc.target_avg_kbps"], 600)
        self.assertEqual(tc.load(self.file), tc.load(self.split))

    def test_update_file_writes_both_and_removes_dropped_scenarios(self):
        run("--split", self.file, self.split)
        run(self.file, self.results(report("a", 500)), "--update")
        self.assertEqual(os.listdir(self.split), ["a.json"])
        self.assertEqual(list(tc.load(self.file)), ["a"])

    def test_update_file_alone_writes_the_same_bytes_as_before(self):
        r = self.results(report("b", 800), report("a", 500))
        run(self.file, r, "--update")
        with open(self.file) as f:
            text = f.read()
        expected = [tc.baseline_entry(k, v) for k, v in sorted(tc.load(r).items())]
        self.assertEqual(text, json.dumps(expected, indent=1, sort_keys=True) + "\n")
        self.assertFalse(os.path.exists(self.split))

    def test_warns_when_the_two_forms_differ(self):
        run("--split", self.file, self.split)
        write(os.path.join(self.split, "b.json"), report("b", 900))
        code, _, err = run(self.file, self.results(report("a", 500), report("b", 800)))
        self.assertEqual(code, 0)
        self.assertIn("differ in 1 scenario(s) (b)", err)


if __name__ == "__main__":
    unittest.main()
