#!/usr/bin/env python3
"""Tests for tools/requirements_report.py: python3 tools/test_requirements_report.py"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import catalog  # noqa: E402
import requirements_report as rr  # noqa: E402


def reg(*rs):
    return {"requirements": [dict(id=i, area=a, title=i, status=s) for i, a, s in rs]}


class Report(unittest.TestCase):
    def test_counts_per_area(self):
        rep = rr.report(reg(("F3.1", "Packet loss", "done"), ("F3.2", "Packet loss", "in-progress"),
                            ("H1", "Test harness", "open")), {})
        self.assertEqual((rep["done"], rep["total"]), (1, 3))
        self.assertEqual(rep["areas"]["Packet loss"]["in-progress"], 1)
        self.assertIn("1 of 3 done", rr.markdown(rep))

    def test_done_with_open_items_is_a_problem(self):
        rep = rr.report(reg(("F5.4", "cc", "done")), {"F5.4": [(492, "x", False)]})
        self.assertEqual(len(rep["problems"]), 1)

    def test_unknown_label_and_missing_feature_are_problems(self):
        rep = rr.report(reg(("F3.1", "Packet loss", "open")), {"F9.9": [(1, "x", False)]}, ["3.1", "3.2"])
        self.assertEqual(len(rep["problems"]), 2)

    def test_the_register_covers_the_catalog(self):
        rep = rr.report(rr.load_register(), {}, [a["id"] for a in catalog.ALGORITHMS])
        self.assertEqual(rep["problems"], [])
        for r in rr.load_register()["requirements"]:
            self.assertIn(r["status"], rr.STATUSES, r["id"])


if __name__ == "__main__":
    unittest.main()
