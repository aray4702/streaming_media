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


def vtrace(frames, padding=(), seq0=0, header=49):
    """frames: [(rtp_ts, key, [sizes])] in send order; `header` is each packet's trace header column."""
    rows = ["time_us,size,kind,seq,queue_bytes,outcome,arrival_us,rtp_ts,marker,osn,key,header"]
    seq, t = seq0, 0
    for ts, key, sizes in frames:
        for sz in sizes:
            rows.append("%d,%d,video,%d,0,delivered,%d,%d,0,-1,%d,%d" % (t, sz, seq, t + 1, ts, 1 if key else 0, header))
            seq, t = seq + 1, t + 1000
    for sz in padding:
        rows.append("%d,%d,padding,%d,0,delivered,%d,0,0,-1,-1,%d" % (t, sz, seq, t + 1, header))
        seq, t = seq + 1, t + 1000
    return "\n".join(rows) + "\n"


def packetize(nalu_bytes, limit, header=49):
    """A delta frame of one NAL unit of `nalu_bytes`, split as libwebrtc splits it under the wire packet
    limit `limit` with the same header on every packet: wire sizes."""
    if nalu_bytes + header <= limit:
        return [nalu_bytes + header]
    frags = hd.split_about_equally(nalu_bytes - 1, limit - header - hd.FUA_HEADER, 0, 0)
    return [f + hd.FUA_HEADER + header for f in frags]


def vdata(sm, rtc, name="clean"):
    return {"clean" if name == "clean" else name: {"scenario": name, "seed": 1, "sm": sm, "webrtc": rtc}}


class VideoSplit(unittest.TestCase):
    # A 1200 B limit: a 2296 B NAL unit (2295 fragment bytes) splits into 1147 + 1148 payload bytes.
    FRAMES = [(0, True, [77, 1155, 1155, 1155]), (3000, False, [955]), (6000, False, [1198, 1199])]

    def test_same_splits_pass_with_other_sequence_numbers_and_timestamps(self):
        other = [(ts + 90000, k, s) for ts, k, s in self.FRAMES]
        rows, failed = hd.check_video(vdata(vtrace(self.FRAMES, [303]), vtrace(other, [303], seq0=6666)), ["clean"])
        self.assertEqual(failed, [])
        self.assertEqual((rows[0]["shared"], rows[0]["differ"]), (3, 0))

    def test_a_frame_split_differently_fails(self):
        theirs = self.FRAMES[:2] + [(6000, False, [1197, 1200])]
        rows, failed = hd.check_video(vdata(vtrace(self.FRAMES), vtrace(theirs)), ["clean"])
        self.assertEqual(len(failed), 1)
        self.assertEqual(rows[0]["differ"], 1)

    def test_a_keyframe_and_a_delta_frame_of_one_size_are_not_compared(self):
        ours = self.FRAMES + [(9000, True, [1100, 900])]
        theirs = self.FRAMES + [(9000, False, [1000, 1000])]
        _, failed = hd.check_video(vdata(vtrace(ours), vtrace(theirs)), ["clean"])
        self.assertEqual(failed, [])

    def test_frames_in_one_packet_only_compare_the_largest_packet(self):
        # issue_29_ulpfec_over_red before #513 had 1460 on ours, 1462 on libwebrtc. With every delta frame
        # in one packet, neither side bounds its limit from above, so the largest packets must match.
        one = [(0, True, [77, 1155]), (3000, False, [955])]
        _, failed = hd.check_video(vdata(vtrace(one + [(9000, False, [1460])]),
                                         vtrace(one + [(9000, False, [1462])])), ["clean"])
        self.assertEqual(len(failed), 1)
        _, failed = hd.check_video(vdata(vtrace(one + [(9000, False, [1462])]),
                                         vtrace(one + [(9000, False, [1462])])), ["clean"])
        self.assertEqual(failed, [])

    def test_frames_over_one_packet_on_one_side_only_fail(self):
        one = [(0, True, [77, 1155]), (3000, False, [955])]
        _, failed = hd.check_video(vdata(vtrace(one), vtrace(self.FRAMES)), ["clean"])
        self.assertEqual(len(failed), 1)

    def test_a_trace_without_the_header_column_fails(self):
        old = "\n".join(",".join(r.split(",")[:-1]) for r in vtrace(self.FRAMES).splitlines()) + "\n"
        rows, failed = hd.check_video(vdata(old, vtrace(self.FRAMES)), ["clean"])
        self.assertEqual(len(failed), 1)
        self.assertIsNone(rows[0]["limit_ours"])


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


class PacketLimit(unittest.TestCase):
    """#518: the limit both sides' splits allow, from each packet's payload (size minus header)."""
    # At a 1462 B wire limit with 49 B headers, a packet has room for 1411 fragment bytes after the 2 B
    # FU-A header: 3 * 1411 = 4233 fragment bytes fit in 3 packets at 1462 and need 4 at 1461.
    PIN = 4234  # NAL unit bytes (4233 fragment bytes)
    COMMON = [(0, True, [77, 1155]), (3000, False, [955]), (6000, False, packetize(3000, 1462))]

    def side(self, limit, nalus):
        frames = self.COMMON + [(9000 + 3000 * i, False, packetize(n, limit)) for i, n in enumerate(nalus)]
        return vtrace(frames, [303])

    def test_the_split_of_a_limit_is_explained_by_that_limit(self):
        for limit in (1200, 1461, 1462, 1500):
            lim = hd.packet_limits(self.side(limit, [self.PIN, 2500, 7000, 10000]))
            self.assertEqual(lim[3], 0, limit)
            self.assertLessEqual(lim[0], limit)
            self.assertGreaterEqual(lim[1], limit)

    def test_the_pin_frame_bounds_each_limit(self):
        self.assertEqual(packetize(self.PIN, 1462), [1462, 1462, 1462])
        self.assertEqual(len(packetize(self.PIN, 1461)), 4)
        self.assertEqual(hd.packet_limits(self.side(1462, [self.PIN]))[0], 1462)
        self.assertLessEqual(hd.packet_limits(self.side(1461, [self.PIN]))[1], 1461)

    def test_limits_one_byte_apart_fail_when_both_sides_reach_them(self):
        # Builder's condition (b): both send a limit-hitting frame, the limits differ by 1 B.
        ours = self.side(1462, [self.PIN])
        theirs = self.side(1461, [self.PIN - 3, self.PIN])
        self.assertEqual(packetize(self.PIN - 3, 1461), [1461, 1461, 1461])
        rows, failed = hd.check_video(vdata(ours, theirs), ["clean"])
        self.assertEqual(len(failed), 1)
        self.assertEqual(rows[0]["differ"], 0)

    def test_equal_limits_pass_when_only_one_side_reaches_them(self):
        # #549's case: the same 1462 B limit, but only our side sends a frame that fills a packet.
        ours = self.side(1462, [self.PIN])
        theirs = self.side(1462, [self.PIN - 3])
        self.assertEqual(packetize(self.PIN - 3, 1462), [1461, 1461, 1461])
        _, failed = hd.check_video(vdata(ours, theirs), ["clean"])
        self.assertEqual(failed, [])

    def test_a_split_no_limit_explains_fails(self):
        theirs = self.COMMON + [(9000, False, [1462, 1000, 1462])]
        lim = hd.packet_limits(vtrace(theirs))
        self.assertEqual(lim[3], 1)
        _, failed = hd.check_video(vdata(self.side(1462, []), vtrace(theirs, [303])), ["clean"])
        self.assertEqual(len(failed), 1)

    def test_first_and_last_packet_headers_are_reductions(self):
        # A first packet 4 B longer header (an extension only it carries) gets 4 B less payload room.
        frags = hd.split_about_equally(5000, 1462 - 49 - hd.FUA_HEADER, 4, 0)
        sizes = [f + hd.FUA_HEADER + 49 for f in frags]
        rows = ["time_us,size,kind,seq,queue_bytes,outcome,arrival_us,rtp_ts,marker,osn,key,header"]
        for i, sz in enumerate(sizes):
            h = 53 if i == 0 else 49
            rows.append("%d,%d,video,%d,0,delivered,0,0,0,-1,0,%d" % (i, sz + (4 if i == 0 else 0), i, h))
        lo, hi, multi, unexplained = hd.packet_limits("\n".join(rows) + "\n")
        self.assertEqual((multi, unexplained), (1, 0))
        self.assertTrue(lo <= 1462 <= hi)

    def known_case(self, name, theirs_max=1462):
        # Limits 1 B apart that both sides reach, as in test_limits_one_byte_apart_fail_when_both_sides_reach_them,
        # with an extra frame on libwebrtc's side so its largest delta packet is `theirs_max`.
        ours = self.side(1462, [self.PIN])
        theirs = self.side(1461, [self.PIN - 3, self.PIN]) + "99000,%d,video,99,0,delivered,99001,99000,0,-1,0,49\n" % theirs_max
        return hd.check_video(vdata(ours, theirs, name), [name])

    def test_a_known_limit_difference_is_reported_not_required(self):
        # Builder, 2026-10-28, on #553: issue_257_zero_max_payload's limit test is KNOWN (#555).
        name = next(iter(hd.KNOWN_LIMIT))
        rows, failed = self.known_case(name)
        self.assertEqual(failed, [])
        self.assertEqual(rows[0]["known"], hd.KNOWN_LIMIT[name])
        self.assertIn("KNOWN (#%d)" % hd.KNOWN_LIMIT[name], hd.markdown_video(rows, failed))

    def test_a_known_row_still_requires_equal_largest_packets(self):
        name = next(iter(hd.KNOWN_LIMIT))
        _, failed = self.known_case(name, theirs_max=1461)
        self.assertEqual(len(failed), 1)

    def test_the_same_limit_difference_on_any_other_row_fails(self):
        self.assertEqual(list(hd.KNOWN_LIMIT), ["issue_257_zero_max_payload"])
        for name in hd.VIDEO_REQUIRED:
            if name not in hd.KNOWN_LIMIT:
                rows, failed = self.known_case(name)
                self.assertEqual(len(failed), 1, name)
                self.assertIsNone(rows[0]["known"], name)


if __name__ == "__main__":
    unittest.main()
