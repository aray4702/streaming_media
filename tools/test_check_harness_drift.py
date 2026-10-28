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


def audiosend(ts, hashes=None):
    rows = ["kind,time_us,seq,rtp_ts,pt,payload_bytes,payload_hash,fraction_lost"]
    for i, t in enumerate(ts):
        rows.append("frame,%d,%d,%d,111,100,%s,-1" % (10000 + 20000 * i, i, t, (hashes or {}).get(i, "h%d" % i)))
    return "\n".join(rows) + "\n"


def video(captured):
    return "t_s,captured,encoded\n" + "".join("%d,%d,%d\n" % (i + 1, c, c) for i, c in enumerate(captured))


def captured_data(sm_ts=(0, 960, 1920), rtc_ts=(0, 960, 1920), sm_v=(30, 30), rtc_v=(30, 30), rtc_hashes=None):
    t = {"scenario": "lossy", "seed": 1, "sm": trace([1]), "webrtc": trace([1]),
         "sm_audiosend": audiosend(sm_ts), "webrtc_audiosend": audiosend(rtc_ts, rtc_hashes),
         "sm_video": video(sm_v), "webrtc_video": video(rtc_v)}
    return {"traces": [t]}


class Capture(unittest.TestCase):
    """Row 1: captured media must match on every traced scenario, required or not."""

    def test_same_capture_passes(self):
        _, failed = hd.check(captured_data(), [])
        self.assertEqual(failed, [])

    def test_a_different_audio_rtp_timestamp_fails(self):
        _, failed = hd.check(captured_data(rtc_ts=(0, 960, 2400)), [])
        self.assertEqual(len(failed), 1)
        self.assertIn("captured audio", failed[0])

    def test_a_different_audio_payload_fails(self):
        _, failed = hd.check(captured_data(rtc_hashes={1: "other"}), [])
        self.assertEqual(len(failed), 1)

    def test_a_different_video_capture_count_fails(self):
        _, failed = hd.check(captured_data(rtc_v=(30, 29)), [])
        self.assertEqual(len(failed), 1)
        self.assertIn("captured video", failed[0])

    def test_audio_only_without_video_rows_passes(self):
        _, failed = hd.check(captured_data(sm_v=(0, 0), rtc_v=()), [])
        self.assertEqual(failed, [])


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


def vtrace(frames, padding=(), seq0=0):
    """frames: [(rtp_ts, key, [sizes])] in send order."""
    rows = ["time_us,size,kind,seq,queue_bytes,outcome,arrival_us,rtp_ts,marker,osn,key"]
    seq, t = seq0, 0
    for ts, key, sizes in frames:
        for sz in sizes:
            rows.append("%d,%d,video,%d,0,delivered,%d,%d,0,-1,%d" % (t, sz, seq, t + 1, ts, 1 if key else 0))
            seq, t = seq + 1, t + 1000
    for sz in padding:
        rows.append("%d,%d,padding,%d,0,delivered,%d,0,0,-1,-1" % (t, sz, seq, t + 1))
        seq, t = seq + 1, t + 1000
    return "\n".join(rows) + "\n"


def vdata(sm, rtc, name="clean"):
    return {"clean" if name == "clean" else name: {"scenario": name, "seed": 1, "sm": sm, "webrtc": rtc}}


class VideoSplit(unittest.TestCase):
    FRAMES = [(0, True, [77, 1155, 1155, 1155]), (3000, False, [955]), (6000, False, [1200, 1200])]

    def test_same_splits_pass_with_other_sequence_numbers_and_timestamps(self):
        other = [(ts + 90000, k, s) for ts, k, s in self.FRAMES]
        rows, failed = hd.check_video(vdata(vtrace(self.FRAMES, [303]), vtrace(other, [303], seq0=6666)), ["clean"])
        self.assertEqual(failed, [])
        self.assertEqual((rows[0]["shared"], rows[0]["differ"]), (3, 0))

    def test_a_frame_split_differently_fails(self):
        theirs = self.FRAMES[:2] + [(6000, False, [1199, 1201])]
        rows, failed = hd.check_video(vdata(vtrace(self.FRAMES), vtrace(theirs)), ["clean"])
        self.assertEqual(len(failed), 1)
        self.assertEqual(rows[0]["differ"], 1)

    def test_a_keyframe_and_a_delta_frame_of_one_size_are_not_compared(self):
        ours = self.FRAMES + [(9000, True, [1000, 1000])]
        theirs = self.FRAMES + [(9000, False, [1100, 900])]
        _, failed = hd.check_video(vdata(vtrace(ours), vtrace(theirs)), ["clean"])
        self.assertEqual(failed, [])

    def test_a_larger_packet_limit_fails(self):
        # issue_29_ulpfec_over_red before #513: 1460 on ours, 1462 on libwebrtc.
        _, failed = hd.check_video(vdata(vtrace(self.FRAMES + [(9000, False, [1460])]),
                                         vtrace(self.FRAMES + [(9000, False, [1462])])), ["clean"])
        self.assertEqual(len(failed), 1)

    def test_full_padding_packets_of_other_sizes_fail(self):
        # issue_257_zero_max_payload before #513: 136 on ours, 128 on libwebrtc.
        _, failed = hd.check_video(vdata(vtrace(self.FRAMES, [136]), vtrace(self.FRAMES, [128])), ["clean"])
        self.assertEqual(len(failed), 1)

    def test_smaller_padding_on_one_side_only_is_left_to_514(self):
        _, failed = hd.check_video(vdata(vtrace(self.FRAMES, [303]), vtrace(self.FRAMES, [98, 303])), ["clean"])
        self.assertEqual(failed, [])

    def test_missing_required_trace_fails(self):
        _, failed = hd.check_video({}, ["clean"])
        self.assertEqual(len(failed), 1)


if __name__ == "__main__":
    unittest.main()
