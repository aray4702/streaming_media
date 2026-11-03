#!/usr/bin/env python3
"""Tests for tools/real_calls.py (#1121): python3 tools/test_real_calls.py

The table and parsing tests run anywhere. The live call (sm_call_smoke to the AI server, direct and relay only)
runs only when SM_SIGNALING_URL is set, and needs out/native built; otherwise it is skipped.
"""

import json
import os
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import real_calls  # noqa: E402

SMOKE_LINE = ('{"type":"call-result","client":"sm_call_smoke","label":"a-relay-1","room":"ai-rc-1","turns":3,'
              '"replies":2,"missed_replies":1,"response_ms_p50":1310,"response_transport_ms_p50":710,"barge_ins":1,'
              '"barge_ins_not_stopped":0,"barge_in_stop_ms_p50":820,"response_ms":[1300,1320],"barge_in_stop_ms":[820],'
              '"video_render_fps":0.0,"video_freezes":0,"video_freeze_ms":0,"stats":{"state":"connected",'
              '"connect_ms":1444.2,"rtt_ms":53.5,"target_kbps":86.3,"audio_packets_lost":2,"audio_concealed_pct":0.4,'
              '"jitter_buffer_ms":254.0,"audio_played_s":20.1,"local_type":"prflx","remote_type":"srflx",'
              '"protocol":"udp","relay_protocol":"udp"}}')


class Offline(unittest.TestCase):
    def test_endpoints(self):
        self.assertEqual(real_calls.endpoints('https://h.example:8443/'),
                         ('wss://h.example:8443/ws', 'https://h.example:8443/turn'))
        self.assertEqual(real_calls.endpoints('http://127.0.0.1:8080'),
                         ('ws://127.0.0.1:8080/ws', 'http://127.0.0.1:8080/turn'))
        with self.assertRaises(ValueError):
            real_calls.endpoints('wss://h/ws')

    def test_parse_result_takes_the_last_result_line(self):
        out = '{"state":"connected"}\nnoise\n' + SMOKE_LINE + '\nsmoke: ok\n'
        r = real_calls.parse_result(out)
        self.assertEqual(r['label'], 'a-relay-1')
        self.assertIsNone(real_calls.parse_result('{"state":"connected"}\nsmoke: FAILED\n'))

    def test_row_of_a_relayed_smoke_call(self):
        r = json.loads(SMOKE_LINE)
        r.update({'date': 'd', 'case': 'a', 'path': 'relay'})
        self.assertTrue(real_calls.ok(r))
        row = dict(zip(real_calls.COLUMNS, real_calls.row(r)))
        self.assertEqual(row['selected pair'], 'prflx/srflx udp, relayed (TURN over udp)')
        self.assertEqual(row['response ms (transport)'], '1310 (710), 1 missed')
        self.assertEqual(row['barge-in stop ms'], '820')
        self.assertEqual(row['connect ms'], '1444')
        self.assertEqual(row['RTT ms'], '53.5')
        self.assertEqual(row['jitter buffer ms'], '254')
        self.assertEqual(row['video fps / freezes'], '-')

    def test_chrome_and_failed_rows(self):
        web = {'type': 'call-result', 'client': 'chrome', 'connection': 'connected', 'audio_packets_received': 900,
               'local_type': 'relay', 'remote_type': 'relay', 'protocol': 'udp', 'relay_protocol': 'udp',
               'video_fps': 30, 'video_freezes': 13, 'connect_ms': 1682, 'rtt_ms': 224}
        self.assertTrue(real_calls.ok(web))
        row = dict(zip(real_calls.COLUMNS, real_calls.row(web)))
        self.assertEqual(row['video fps / freezes'], '30 / 13')
        self.assertEqual(row['response ms (transport)'], '-')
        failed = {'client': 'chrome', 'error': 'no result from Chrome', 'case': 'b', 'path': 'direct'}
        self.assertFalse(real_calls.ok(failed))
        self.assertIn('FAILED: no result from Chrome', real_calls.table([web, failed]))


@unittest.skipUnless(os.environ.get('SM_SIGNALING_URL'), 'SM_SIGNALING_URL is not set')
class Live(unittest.TestCase):
    def test_smoke_calls_the_ai_server_direct_and_relay_only(self):
        tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'real_calls.py')
        p = subprocess.run([sys.executable, tool, '--cases=a', '--seconds=15'], capture_output=True, text=True,
                           timeout=300)
        print(p.stdout[-2000:])
        self.assertEqual(p.returncode, 0)
        results = [json.loads(line) for line in p.stdout.splitlines() if line.startswith('{')]
        relay = [r for r in results if r.get('path') == 'relay']
        self.assertEqual(len(relay), 1)
        self.assertTrue(real_calls.flatten(relay[0]).get('relay_only'))
        self.assertEqual(real_calls.flatten(relay[0]).get('relay_protocol'), 'udp')


if __name__ == '__main__':
    unittest.main(verbosity=2)
