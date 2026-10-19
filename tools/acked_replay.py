#!/usr/bin/env python3
"""Replay the acknowledged-bitrate estimate over a forward-link trace (issue #124).

The prebuilt libwebrtc exposes no accessor for GoogCC's acknowledged bitrate, so
this script rebuilds it from the per-packet forward-link trace both stacks write
(`time_us,size,kind,seq,queue_bytes,outcome,arrival_us`): every delivered packet
that carries a transport-wide sequence number (audio, video, rtx, padding, fec;
not RTCP or cross traffic) is fed in arrival order to libwebrtc's
`BitrateEstimator` (goog_cc/bitrate_estimator.cc, default settings: 500 ms
initial window, 150 ms after, uncertainty scale 10, variance step 5), the way
`AcknowledgedBitrateEstimator::IncomingPacketFeedbackVector` does, with no ALR
and `prior_unacked_data` zero. The same replay runs over both traces, so a
difference in the output is a difference in what arrived, and when.

  tools/acked_replay.py sm.trace.csv webrtc.trace.csv [--from S] [--to S]

Prints one row per second: the last estimate (kbps) of each stack, the bytes that
arrived in the second (kbps), and the packets lost or dropped.
"""
import argparse
import csv

TCC_KINDS = {"audio", "video", "rtx", "padding", "fec"}


class BitrateEstimator:
    """goog_cc/bitrate_estimator.cc with default settings."""

    def __init__(self):
        self.sum = 0
        self.current_window_ms = 0
        self.prev_time_ms = -1
        self.kbps = -1.0
        self.var = 50.0

    def update(self, at_ms, nbytes):
        window = 500 if self.kbps < 0 else 150
        sample = self._window(at_ms, nbytes, window)
        if sample < 0:
            return
        if self.kbps < 0:
            self.kbps = sample
            return
        unc = 10.0 * abs(self.kbps - sample) / self.kbps  # symmetry cap 0: min(sample, 0) == 0
        sample_var = unc * unc
        pred = self.var + 5.0
        self.kbps = (sample_var * self.kbps + pred * sample) / (sample_var + pred)
        self.var = sample_var * pred / (sample_var + pred)

    def _window(self, now_ms, nbytes, window):
        if now_ms < self.prev_time_ms:
            self.prev_time_ms, self.sum, self.current_window_ms = -1, 0, 0
        if self.prev_time_ms >= 0:
            self.current_window_ms += now_ms - self.prev_time_ms
            if now_ms - self.prev_time_ms > window:
                self.sum = 0
                self.current_window_ms %= window
        self.prev_time_ms = now_ms
        sample = -1.0
        if self.current_window_ms >= window:
            sample = 8.0 * self.sum / window
            self.current_window_ms -= window
            self.sum = 0
        self.sum += nbytes
        return sample


def replay(path):
    """Per-second rows: (estimate_kbps or None, arrived_kbps, lost_packets)."""
    arrivals, lost = [], {}
    with open(path) as f:
        for r in csv.DictReader(f):
            if r["kind"] not in TCC_KINDS:
                continue
            if r["outcome"] == "delivered":
                arrivals.append((int(r["arrival_us"]), int(r["size"])))
            else:
                sec = int(r["time_us"]) // 1000000
                lost[sec] = lost.get(sec, 0) + 1
    arrivals.sort()
    est, rows, arrived = BitrateEstimator(), {}, {}
    for t_us, size in arrivals:
        est.update(t_us // 1000, size)
        sec = t_us // 1000000
        rows[sec] = est.kbps if est.kbps >= 0 else None
        arrived[sec] = arrived.get(sec, 0) + size * 8 / 1000.0
    last = max(list(rows) + list(lost) + [0])
    out, cur = {}, None
    for s in range(last + 1):
        cur = rows.get(s, cur)
        out[s] = (cur, arrived.get(s, 0.0), lost.get(s, 0))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sm")
    ap.add_argument("webrtc")
    ap.add_argument("--from", dest="lo", type=int, default=0)
    ap.add_argument("--to", dest="hi", type=int, default=10**9)
    a = ap.parse_args()
    s, w = replay(a.sm), replay(a.webrtc)
    f = lambda v: "     -" if v is None else "%6.0f" % v
    print("   t | acked kbps: sm  webrtc | arrived kbps: sm  webrtc | lost+dropped: sm webrtc")
    for t in range(max(a.lo, 0), min(a.hi, max(max(s), max(w))) + 1):
        e1, r1, l1 = s.get(t, (None, 0.0, 0))
        e2, r2, l2 = w.get(t, (None, 0.0, 0))
        print("%4d |      %s %s |        %6.0f %6.0f |       %4d %4d" % (t, f(e1), f(e2), r1, r2, l1, l2))


if __name__ == "__main__":
    main()
