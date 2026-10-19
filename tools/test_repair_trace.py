#!/usr/bin/env python3
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import repair_trace as rt

HEAD = "t_ms,rtx_packets,key_frames_encoded,nack_requests,unique_nack_requests,plis,key_frames_decoded,frames_rendered\n"


class RepairTraceTest(unittest.TestCase):
    def test_group_sums_into_steps(self):
        rows = rt.parse(HEAD + "100,1,0,2,1,0,0,3\n200,2,1,0,0,1,0,3\n600,4,0,0,0,0,1,3\n")
        g = rt.group(rows, 0, 1000, 500)
        self.assertEqual(g[500]["rtx_packets"], 3)
        self.assertEqual(g[500]["key_frames_encoded"], 1)
        self.assertEqual(g[1000]["key_frames_decoded"], 1)

    def test_render_totals_and_skips_idle_steps(self):
        a = rt.parse(HEAD + "100,5,0,0,0,0,0,0\n")
        b = rt.parse(HEAD + "1100,2,0,0,0,0,0,0\n")
        out = rt.render(a, b, 0, 2000, 500)
        self.assertIn("total", out)
        self.assertEqual(out.count("\n"), 4)  # header, two active steps, total, note
        self.assertIn("     5 /     0", out)


if __name__ == "__main__":
    unittest.main()
