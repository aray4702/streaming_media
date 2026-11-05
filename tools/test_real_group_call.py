#!/usr/bin/env python3
"""Tests for tools/real_group_call.py (C21, #1180): python3 tools/test_real_group_call.py

The summaries and the results doc's tables, from synthetic samples; the live call is the tool itself (it needs
SM_SIGNALING_URL, Chrome and out/native).
"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import real_group_call as rgc  # noqa: E402


def page_report(t, frames, freezes, w, active_f, bytes_q, bytes_f, speakers):
    """A page's group report at time t: it receives 'native-1' and sends three layers."""
    return (t, {
        'participants': {'native-1': {'videoFrames': frames, 'videoWidth': w, 'videoHeight': w * 9 // 16,
                                      'videoBytesIn': 1000 * t, 'audioBytesIn': 4000 * t, 'videoFreezes': freezes,
                                      'videoFreezeMs': 300 * freezes, 'videoPacketsLost': 2 * freezes,
                                      'audioPacketsLost': t, 'audioPacketsReceived': 50 * t, 'videoJitterBufferMs': 80,
                                      'audioJitterBufferMs': 60, 'rttMs': 40, 'videoCodec': 'video/H264'}},
        'sending': {'layers': [{'rid': 'q', 'active': True, 'bytesSent': bytes_q, 'width': 320, 'height': 180,
                                'fps': 15, 'qualityLimitation': 'none', 'encoder': 'OpenH264'},
                               {'rid': 'f', 'active': active_f, 'bytesSent': bytes_f, 'width': 0, 'height': 0,
                                'fps': 0, 'qualityLimitation': 'bandwidth', 'encoder': 'OpenH264'}],
                    'audioBytesOut': 4000 * t},
        'activeSpeakers': speakers, 'speakersSeen': ['native-1', 'ai-1'], 'count': 3})


class Summaries(unittest.TestCase):
    def setUp(self):
        self.samples = [page_report(0, 0, 0, 320, True, 0, 0, ['native-1']),
                        page_report(5, 100, 1, 320, False, 25000, 0, ['ai-1']),
                        page_report(10, 250, 3, 640, False, 50000, 0, [])]

    def test_page_receive_takes_differences_over_the_window(self):
        r = rgc.page_receive(self.samples)['native-1']
        self.assertEqual(r['fps'], 25.0)
        self.assertEqual((r['freezes'], r['freeze_ms'], r['video_lost']), (3, 900, 6))
        self.assertEqual((r['audio_lost'], r['audio_received']), (10, 500))
        self.assertAlmostEqual(r['video_kbps'], 8.0)
        self.assertEqual(r['video'], '320x180 67%, 640x360 33%')
        self.assertEqual(r['codec'], 'H264')

    def test_page_send_per_layer(self):
        s = rgc.page_send(self.samples)
        self.assertEqual(list(s['layers']), ['q', 'f'])
        self.assertAlmostEqual(s['layers']['q']['kbps'], 40.0)
        self.assertEqual(s['layers']['f']['active_pct'], 33)
        self.assertEqual(s['layers']['f']['size'], '-')
        self.assertEqual(s['layers']['f']['limitation'], 'bandwidth 100%')
        self.assertAlmostEqual(s['audio_kbps'], 32.0)

    def test_speaker_shares(self):
        self.assertEqual(rgc.speaker_shares(self.samples, 'activeSpeakers'), {'native-1': 33, 'ai-1': 33})
        native = [(0, {'livekit_active_speaker': 'ai-1'}), (1, {'livekit_active_speaker': ''}),
                  (2, {'livekit_active_speaker': 'ai-1'}), (3, {'livekit_active_speaker': 'native-1'})]
        self.assertEqual(rgc.speaker_shares(native, 'livekit_active_speaker'), {'ai-1': 50, 'native-1': 25})

    def test_native_receive_and_send(self):
        def line(t, frames, lost):
            return (t, {'livekit_group': [{'identity': 'web-a', 'has_video': True, 'video_frames_decoded': frames,
                                           'video_width': 320, 'video_height': 180, 'audio_packets_lost': lost,
                                           'audio_packets_received': 50 * t, 'audio_concealed_pct': 1.5,
                                           'audio_jitter_ms': 20, 'audio_played_s': t}],
                        'send_kbps': 100 + t, 'target_kbps': 300, 'opus_kbps': 32, 'video_frames_sent': 30 * t,
                        'rtt_ms': 50 + 10 * t, 'livekit_signal_rtt_ms': 60, 'local_type': 'prflx',
                        'remote_type': 'host', 'protocol': 'udp'})
        samples = [line(0, 0, 0), line(2, 40, 3), line(4, 100, 5)]
        r = rgc.native_receive(samples)['web-a']
        self.assertEqual((r['fps'], r['audio_lost'], r['audio_received']), (25.0, 5, 200))
        s = rgc.native_send(samples)
        self.assertEqual(s['video'], 'video')  # Frames sent, no size in the stats.
        self.assertEqual((s['fps'], s['rtt_ms_p50'], s['rtt_ms_max'], s['send_kbps']), (30.0, 70, 90, 102))

    def test_report_and_doc(self):
        run = {'room': 'grp-1', 'date_pt': '2026-11-04 22:00 PST', 'participants_planned': 3, 'participants_max': 3,
               'mix': 'test', 'window': [0, 10], 'all_in_s': 4.0, 'native_id': 'native-1', 'ai_id': 'ai-1',
               'reports': {'web-a': self.samples}, 'native': None, 'ai': None, 'notes': [],
               'probe': [(1, 40.0, 2.0, 35.0, 10.0), (2, 60.0, 2.5, 45.0, 12.0)], 'reference': '1.1.1.1:443',
               'chrome_events': [{'signal_reconnects': 1, 'ice_restarts': 1, 'full_reconnects': 0}]}
        text = rgc.report(run, rgc.summarize(run))
        self.assertIn('| page 1 (web-a) | native (native-1) | 320x180 67%, 640x360 33% H264 | 25 | 8 | 3 (900) | 6 |'
                      ' 10 / 500 | 80 / 60 | 40 |', text)
        self.assertIn('| page 1 (web-a) | f | - | 0 | 0 | 33% | bandwidth 100% | OpenH264 |', text)
        self.assertIn('took 40 ms (median), 60 ms at most', text)
        self.assertIn('signal reconnects 1; ICE restarts 1', text)
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'doc.md')
            rgc.write_doc(path, '## Run A\n', '# Title\n')
            rgc.write_doc(path, '## Run B\n', '# Title\n')
            with open(path) as f:
                doc = f.read()
            self.assertTrue(doc.startswith('# Title'))
            self.assertLess(doc.index('## Run B'), doc.index('## Run A'))  # Newest first.

    def test_endpoints(self):
        self.assertEqual(rgc.endpoints('https://h.example:8443/'),
                         ('wss://h.example:8443', 'https://h.example:8443/lk-token'))
        with self.assertRaises(ValueError):
            rgc.endpoints('wss://h/rtc')


if __name__ == '__main__':
    unittest.main(verbosity=2)
