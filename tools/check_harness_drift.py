#!/usr/bin/env python3
"""Harness drift check: both stacks must send the same audio where they do today.

Both sides of `sm_compare` encode the same signal with the same libopus and
settings (agents/program.md 0.2, "One test environment for both stacks";
docs/design/harness-fidelity.md). On a link without loss, nothing feeds back
into the encoder differently, so every audio packet must have the same size
on both stacks, in the same order. This reads the seed-1 forward-link traces
that compare-webrtc.json carries (sm_compare's kDefaultTraced) and fails if
any REQUIRED scenario's audio differs, so a harness change that breaks the
match is caught on the PR that makes it.

It also checks the video packetization (issue #513) on VIDEO_REQUIRED: every
frame size both stacks send is split into the same packets, a full padding
packet has the same size on both, and both sides' splits fit the same packet
limit. The limit is not read from the largest packet, which depends on the
traffic (#549: equal limits, but only one side sent a frame that reached it).
Each side's delta frames are run back through libwebrtc's H.264 split
(RtpPacketizerH264 FU-A and RtpPacketizer::SplitAboutEqually,
compare/webrtc/reference/modules/rtp_rtcp/source/rtp_format{,_h264}.cc, with
RTPSenderVideo's per-position limits), on each packet's payload (`size` minus
the trace's `header` column), to find the interval of limits that explains
every split, and the two sides' intervals must meet (#518). A row in KNOWN_LIMIT reports its limit test
as KNOWN (#item) instead of failing, while its largest delta-frame packets must still be equal.
Libwebrtc's RTPSender::RtxPacketOverhead reserves MID/RID room while the media SSRC is acked and the RTX
SSRC is not (rtp_sender.cc:564-595), so the limit changes during the call (#555): the trace's `rtx_room`
column says which phase each frame was packetized in (testbench::RtxAckPhase, on both sides), the splits and
the limit test compare frames within one phase, and both sides must have the same phases.

Scenarios with loss are reported but not required: their audio still parts
at the first loss report, because our receiver sends report blocks more often
than libwebrtc's (#484). Move them to REQUIRED once that is fixed.

It also checks the captured media (harness-fidelity row 1) on EVERY traced
scenario, lossy or not, because capture does not depend on the stack: each
audio frame must have the same RTP timestamp on both sides and the same payload
until the encoders' loss inputs part (tools/audio_send.py, the per-frame send
trace), and each second of video must have the same number of captured frames
(the video trace's `captured` column).

  tools/check_harness_drift.py compare-webrtc.json [--markdown out.md]
"""
import argparse
import csv
import io
import json
import sys

import audio_send

REQUIRED = ["issue_124_clean_link_v2", "issue_124_bufferbloat_v2", "issue_61_googcc_glue_congestive",
            "probe_v2_cross_traffic"]
# Video packetization (issue #513): both stacks get the same whole-packet limit and reserve the same
# RTX and FEC room from it, so every frame size both send is split into the same packets, the limits
# their delta frames' splits allow meet (packet_limits, #518), and a full padding packet is the same
# size. Holds with loss too, so the lossy
# traces are required here; `issue_29_ulpfec_over_red` (NACK off, FEC on) and
# `issue_257_zero_max_payload` (100-byte packets) are the cases the default limit hides.
VIDEO_REQUIRED = REQUIRED + ["issue_124_burst_loss_v2", "low_bandwidth", "very_low_capacity",
                             "issue_29_ulpfec_over_red", "issue_257_zero_max_payload"]
# Rows whose packet-limit test is reported but not required, each until its open sev:2 item fixes the
# difference (Builder, 2026-10-28, on #553). Only the limit test is exempt: the row's largest
# delta-frame packets must still be equal, as before #518, with the shared-frame splits and the padding.
# The item's fix PR removes its entry.
# #555's fix removed issue_257_zero_max_payload's entry: both sides now reserve RTPSender::RtxPacketOverhead's
# MID/RID room until the RTX SSRC is acked, and each RTX phase is compared on its own.
KNOWN_LIMIT = {}


def audio_sizes(trace_csv):
    """Sizes of the audio packets in the order first sent (retransmissions left out)."""
    seen, out = set(), []
    for r in csv.DictReader(io.StringIO(trace_csv)):
        if r["kind"] == "audio" and r["seq"] not in seen:
            seen.add(r["seq"])
            out.append(int(r["size"]))
    return out


def video_splits(trace_csv):
    """{(frame size, keyframe, RTX phase): set of splits}: the wire sizes of each frame's video packets
    (first sends, by RTP timestamp, in sequence order), keyed by their sum, the frame type and the
    trace's `rtx_room` column (#555: whether the sender reserved RTPSender::RtxPacketOverhead's MID/RID
    room when it packetized the frame, which changes the packet limit during the call). Frames are
    compared only within one phase; a trace without the column puts every frame in one phase."""
    seen, frames = set(), {}
    for r in csv.DictReader(io.StringIO(trace_csv)):
        if r["kind"] == "video" and r["seq"] not in seen:
            seen.add(r["seq"])
            frames.setdefault((r["rtp_ts"], r["key"] == "1", r.get("rtx_room") or "-1"), []).append(
                (int(r["seq"]), int(r["size"])))
    out = {}
    for (_, key, phase), pkts in frames.items():
        sizes = tuple(sz for _, sz in sorted(pkts))
        out.setdefault((sum(sizes), key, phase), set()).add(sizes)
    return out


FUA_HEADER = 2  # rtp_format_h264.cc kFuAHeaderSize


def split_about_equally(payload_len, max_len, first_red, last_red):
    """RtpPacketizer::SplitAboutEqually (rtp_format.cc) for a payload that needs more than one packet:
    the payload bytes of each packet."""
    total = payload_len + first_red + last_red
    num = max(2, -(-total // max_len))
    if payload_len < num:
        return []
    per, larger, left, out = total // num, total % num, payload_len, []
    while left > 0:
        if num == larger:
            per += 1
        cur = per
        if not out:
            cur = cur - first_red if cur > first_red + 1 else 1
        cur = min(cur, left)
        if num == 2 and cur == left:
            cur -= 1
        out.append(cur)
        left, num = left - cur, num - 1
    return out


def rtx_phase(r):
    """A trace row's `rtx_room` column (#555), "-1" when the trace has none."""
    return r.get("rtx_room") or "-1"


def rtx_phases(trace_csv):
    """The RTX phases (rtx_phase) of a trace's delta-frame video packets."""
    return {rtx_phase(r) for r in csv.DictReader(io.StringIO(trace_csv)) if r["kind"] == "video" and r.get("key") != "1"}


def delta_frames(trace_csv, phase=None):
    """Each delta frame's video packets (first sends, in sequence order) as (payload, header) pairs,
    payload being `size` minus the trace's `header` column (#518). None if the trace has no column.
    Keyframes are left out, as our SPS rewrite can make a keyframe a byte longer than libwebrtc's.
    With `phase`, only the frames of that RTX phase (#555)."""
    seen, frames = set(), {}
    for r in csv.DictReader(io.StringIO(trace_csv)):
        if r["kind"] != "video" or r["seq"] in seen:
            continue
        seen.add(r["seq"])
        if r.get("header") in (None, "", "-1"):
            return None
        if phase is not None and rtx_phase(r) != phase:
            continue
        if r.get("key") != "1":
            frames.setdefault(r["rtp_ts"], []).append((int(r["seq"]), int(r["size"]) - int(r["header"]),
                                                       int(r["header"])))
    return [[(p, h) for _, p, h in sorted(pkts)] for pkts in frames.values()]


def packet_limits(trace_csv, phase=None):
    """The interval [lo, hi] of whole-packet limits (in the trace's wire bytes, after the RTX and FEC
    room) that explains every delta frame's split, as libwebrtc's RTPSenderVideo and RtpPacketizerH264
    make it (rtp_sender_video.cc, rtp_format_h264.cc, rtp_format.cc). For a packet limit K, a packet
    whose header (the trace column) is h has K - h bytes of payload room, so the first, middle and last
    packets' headers give the reductions. A delta frame is one NAL unit (compare/webrtc/model_h264.h):
    - in one packet, it fits: K >= payload + h;
    - in m >= 2 FU-A packets carrying F fragment bytes, every packet fits, it did not fit in one packet
      (K <= F + h_single, from the side's own single-packet frames), and SplitAboutEqually with K gives m
      packets: L = K - h_mid - 2 >= ceil(total / m) and, for m >= 3, L <= (total - 1) // (m - 1), with
      total = F + (h_first - h_mid) + (h_last - h_mid). Within that interval the split depends only on m,
      so it must be the split SplitAboutEqually makes, or no limit explains the frame.
    h_mid is the frame's own middle packets' header; a two-packet frame has none, and its bounds do not
    depend on it. Returns (lo, hi, frames that need more than one packet, frames no limit explains), with
    hi None when unbounded, or None when the trace has no header column. With `phase`, only the frames of
    that RTX phase (#555): RtxPacketOverhead's MID/RID room changes the limit during the call."""
    frames = delta_frames(trace_csv, phase)
    if frames is None:
        return None
    singles = [f[0][1] for f in frames if len(f) == 1]
    h_single = max(set(singles), key=singles.count) if singles else None
    lo, hi, multi, unexplained = 0, None, 0, 0
    for f in frames:
        lo = max(lo, max(p + h for p, h in f))
        if len(f) == 1:
            continue
        multi += 1
        m, hf, hl = len(f), f[0][1], f[-1][1]
        mids = [h for _, h in f[1:-1]]
        hm = max(set(mids), key=mids.count) if mids else min(hf, hl)
        frags = [p - FUA_HEADER for p, _ in f]
        first_red, last_red = max(0, hf - hm), max(0, hl - hm)
        total = sum(frags) + first_red + last_red
        f_lo, f_hi = -(-total // m) + hm + FUA_HEADER, None
        if m >= 3:
            f_hi = (total - 1) // (m - 1) + hm + FUA_HEADER
        if h_single is not None:
            f_hi = min(f_hi, sum(frags) + h_single) if f_hi is not None else sum(frags) + h_single
        if split_about_equally(sum(frags), f_lo - hm - FUA_HEADER, first_red, last_red) != frags:
            unexplained += 1
            continue
        lo = max(lo, f_lo)
        if f_hi is not None:
            hi = f_hi if hi is None else min(hi, f_hi)
    return lo, hi, multi, unexplained


def limits_text(lim):
    if lim is None:
        return "no header column"
    lo, hi, multi, unexplained = lim
    if not multi:
        return "every frame in one packet, largest %d B" % lo
    return "%d-%s B (%d frames%s)" % (lo, "inf" if hi is None else hi, multi,
                                      ", %d unexplained" % unexplained if unexplained else "")


def max_video_packet(trace_csv, phase=None):
    """The largest video packet of a delta frame (keyframes left out, as in delta_frames), of `phase` if
    given."""
    return max((int(r["size"]) for r in csv.DictReader(io.StringIO(trace_csv))
                if r["kind"] == "video" and r.get("key") != "1" and (phase is None or rtx_phase(r) == phase)),
               default=0)


def limits_meet(a, b, max_a, max_b):
    """Both sides' splits fit one packet limit. Each side needs a header column, every split explained
    and a non-empty interval, and the two intervals must intersect. A side with no delta frame over more
    than one packet bounds the limit only from below, so if neither side has one (every frame fits in a
    packet, as on low_bandwidth), the largest delta-frame packets must be the same size, as before #518;
    if only one side has one, the row fails."""
    if a is None or b is None or a[3] or b[3]:
        return False
    if not a[2] and not b[2]:
        return max_a > 0 and max_a == max_b
    if not a[2] or not b[2]:
        return False
    hi = min(x for x in (a[1], b[1], float("inf")) if x is not None)
    return max(a[0], b[0]) <= hi


def padding_sizes(trace_csv):
    return sorted({int(r["size"]) for r in csv.DictReader(io.StringIO(trace_csv)) if r["kind"] == "padding"})


def compare_video(trace):
    """Every frame size both stacks send is split into the same packets, padding packets have the same
    sizes and the packet limits both sides' splits allow meet (issue #513: the same whole-packet limit
    and the same RTX/FEC reservations; #518)."""
    a, b = video_splits(trace["sm"]), video_splits(trace["webrtc"])
    shared = sorted(set(a) & set(b))
    differ = [n for n in shared if a[n] != b[n]]
    pa, pb = padding_sizes(trace["sm"]), padding_sizes(trace["webrtc"])
    # Padding sizes are compared only where both stacks pad (#514 owns the padding count).
    # Only the full-size padding packet is compared; smaller ones are libwebrtc's media-SSRC and audio
    # padding, which #514 owns.
    pad_ok = not pa or not pb or pa[-1] == pb[-1]
    # The limit test runs within each RTX phase both sides have (#555). The row shows the first phase that
    # fails, or else the one with the most frames over one packet.
    # A phase only one side has is a difference in when the RTX room is reserved, and fails.
    phases_a, phases_b = rtx_phases(trace["sm"]), rtx_phases(trace["webrtc"])
    phases_ok = phases_a == phases_b or "-1" in phases_a | phases_b
    results = []
    for ph in sorted(phases_a & phases_b) or [None]:
        pa_, pb_ = packet_limits(trace["sm"], ph), packet_limits(trace["webrtc"], ph)
        xa, xb = max_video_packet(trace["sm"], ph), max_video_packet(trace["webrtc"], ph)
        results.append((limits_meet(pa_, pb_, xa, xb), pa_, pb_, xa, xb))
    limit_ok = all(r[0] for r in results)
    shown = next((r for r in results if not r[0]), None) or max(results, key=lambda r: (r[1] or (0, 0, 0))[2])
    _, la, lb, ma, mb = shown
    known = KNOWN_LIMIT.get(trace["scenario"]) if not limit_ok else None
    return {"scenario": trace["scenario"], "shared": len(shared), "differ": len(differ),
            "example": differ[0] if differ else None, "padding_ours": pa, "padding_webrtc": pb,
            "limit_ours": la, "limit_webrtc": lb, "max_ours": ma, "max_webrtc": mb, "known": known,
            "phases_ours": sorted(phases_a), "phases_webrtc": sorted(phases_b),
            "ok": bool(shared) and not differ and pad_ok and phases_ok and
            (limit_ok or (known is not None and ma == mb))}


def compare(trace):
    a, b = audio_sizes(trace["sm"]), audio_sizes(trace["webrtc"])
    n = min(len(a), len(b))
    first = next((i for i in range(n) if a[i] != b[i]), None)
    same = sum(a[i] == b[i] for i in range(n))
    ok = len(a) == len(b) and first is None and n > 0
    return {"scenario": trace["scenario"], "seed": trace.get("seed"), "ours": len(a), "webrtc": len(b),
            "same": same, "first_diff": first, "ok": ok}


def captured_per_second(video_csv):
    return [int(r["captured"]) for r in csv.DictReader(io.StringIO(video_csv))]


def check_capture(trace):
    """Row 1: the same captured audio and video on both sides. Returns a list of failures."""
    name, failed = trace["scenario"], []
    if trace.get("sm_audiosend") or trace.get("webrtc_audiosend"):
        if not audio_send.compare(trace.get("sm_audiosend", ""), trace.get("webrtc_audiosend", ""), out=io.StringIO()):
            failed.append("%s: captured audio differs (an audio frame's RTP timestamp, or its payload before the "
                          "loss inputs part; tools/audio_send.py shows where)" % name)
    a, b = captured_per_second(trace.get("sm_video", "")), captured_per_second(trace.get("webrtc_video", ""))
    if any(a) or any(b):
        first = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), None)
        if len(a) != len(b) or first is not None:
            failed.append("%s: captured video differs (%d vs %d frames, first different second %s)" % (
                name, sum(a), sum(b), first if first is not None else min(len(a), len(b)) + 1))
    return failed


def check_video(traces, required):
    rows, failed = [], []
    for name in required:
        if name not in traces:
            failed.append("%s: no trace in the JSON (is it still in sm_compare's kDefaultTraced?)" % name)
            continue
        r = compare_video(traces[name])
        rows.append(r)
        if not r["ok"]:
            failed.append("%s: video split differs (%d of %d shared frame sizes, e.g. %s; packet limit "
                          "%s vs %s; full padding packet %s vs %s B; RTX phases %s vs %s)" % (
                              name, r["differ"], r["shared"], r["example"], limits_text(r["limit_ours"]),
                              limits_text(r["limit_webrtc"]), r["padding_ours"][-1:], r["padding_webrtc"][-1:],
                              r["phases_ours"], r["phases_webrtc"]))
    return rows, failed


def check(data, required=REQUIRED):
    traces = {t["scenario"]: t for t in data.get("traces", [])}
    rows, failed = [], []
    for name in required:
        if name not in traces:
            failed.append("%s: no trace in the JSON (is it still in sm_compare's kDefaultTraced?)" % name)
            continue
        r = dict(compare(traces[name]), required=True)
        rows.append(r)
        if not r["ok"]:
            failed.append("%s: audio differs (%d vs %d packets, %d same, first difference at packet %s)" % (
                name, r["ours"], r["webrtc"], r["same"], r["first_diff"]))
    for name, t in sorted(traces.items()):
        if name not in required:
            rows.append(dict(compare(t), required=False))
    for name, t in sorted(traces.items()):
        failed += check_capture(t)
    return rows, failed


def markdown_video(rows, failed):
    lines = ["## Harness drift check (video packetization, seed 1)", "",
             "| scenario | shared frame sizes | split differently | packet limit the splits allow ours / libwebrtc "
             "| full padding packet ours / libwebrtc |",
             "|---|---|---|---|---|"]
    for r in rows:
        lines.append("| %s | %d | %d | %s / %s%s | %s / %s |" % (
            r["scenario"], r["shared"], r["differ"], limits_text(r["limit_ours"]), limits_text(r["limit_webrtc"]),
            " (KNOWN (#%d); largest delta packet %d / %d B)" % (r["known"], r["max_ours"], r["max_webrtc"])
            if r.get("known") else "",
            (r["padding_ours"] or ["-"])[-1], (r["padding_webrtc"] or ["-"])[-1]))
    known = ["%s (#%d)" % (r["scenario"], r["known"]) for r in rows if r.get("known")]
    lines += ["", "**FAILED:** " + "; ".join(failed) if failed else
              "Every required scenario splits each shared frame size the same way, and both sides' splits fit the same "
              "packet limit" + (", except the known differences reported, not required: %s (their largest "
                                "delta-frame packets match)" % ", ".join(known) if known else "") + ".", ""]
    return "\n".join(lines)


def markdown(rows, failed):
    lines = ["## Harness drift check (audio packet sizes, seed 1)", "",
             "| scenario | required | audio packets ours / libwebrtc | same size | first difference |",
             "|---|---|---|---|---|"]
    for r in rows:
        lines.append("| %s | %s | %d / %d | %d | %s |" % (r["scenario"], "yes" if r["required"] else "no (#484)",
                                                     r["ours"], r["webrtc"], r["same"],
                                                     "none" if r["first_diff"] is None else "packet %d" % r["first_diff"]))
    lines += ["", "**FAILED:** " + "; ".join(failed) if failed else
              "All required scenarios match packet for packet, and every traced scenario has the same captured "
              "audio and video on both sides.", ""]
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("compare_json")
    ap.add_argument("--markdown")
    a = ap.parse_args(argv)
    with open(a.compare_json) as f:
        data = json.load(f)
    rows, failed = check(data)
    vrows, vfailed = check_video({t["scenario"]: t for t in data.get("traces", [])}, VIDEO_REQUIRED)
    failed += vfailed
    md = markdown(rows, [f for f in failed if f not in vfailed]) + "\n" + markdown_video(vrows, vfailed)
    if a.markdown:
        with open(a.markdown, "w") as f:
            f.write(md + "\n")
    print(md)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
