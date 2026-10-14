// Node smoke test for the browser build: loads the ES module, runs a
// scenario end to end and drives each bound component once.
//   node web/test/smoke.mjs <path/to/sm_web.js> <scenario.scn>
import { readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

const [modPath, scnPath] = process.argv.slice(2);
const { default: createSmModule } = await import(pathToFileURL(modPath).href);
const sm = await createSmModule();
let failures = 0;
const check = (cond, what) => {
  console.log(`${cond ? 'ok  ' : 'FAIL'} ${what}`);
  if (!cond) failures++;
};

// Test bench.
const text = readFileSync(scnPath, 'utf8');
check(sm.checkScenario(text) === '', 'scenario parses');
check(sm.checkScenario('bogus line').startsWith('line 1'), 'parse errors are reported');
const report = JSON.parse(sm.runScenario(text, true));
check(report.passed === true, `scenario ${report.scenario} passes`);
check(report.timeseries.rows.length > 10, 'timeseries present');
check(report.metrics['video.render_fps'] > 20, `video renders (${report.metrics['video.render_fps']} fps)`);
check(Object.keys(report.library).length > 50, 'library metrics exported');
check(sm.metricCatalog().length >= 30, 'metric catalog');

// Audio jitter buffer: 1 s of 20 ms frames at 16 kHz, played 10 ms at a time.
const jb = new sm.AudioJitterBuffer(16000);
let concealed = 0;
for (let t = 0; t < 1000; t += 10) {
  if (t % 20 === 0) jb.insertPacket(t * 16, new Float32Array(320).fill(0.1), t + 30);
  const out = jb.getAudio(160, t + 30);
  if (out.length !== 160) failures++;
  if (jb.lastOperation() === 'expand') concealed++;
}
check(concealed < 5, `jitter buffer plays without concealment (${concealed} expands)`);
check(jb.targetDelayMs() > 0, `jitter buffer target delay ${jb.targetDelayMs()} ms`);
jb.delete();

// GCC + transport feedback round trip.
const gcc = new sm.GccController(300, 30, 5000);
const fbb = new sm.TransportFeedbackBuilder();
check(gcc.start(0).targetKbps === 300, 'GCC starts at the start rate');
let seq = 0;
for (let t = 0; t < 2000; t += 5) {
  gcc.onPacketSent(seq, t, 1000);
  fbb.onPacket(seq, t + 20);
  seq++;
  if (t % 50 === 0) for (const msg of fbb.build()) gcc.onTransportFeedback(msg, t + 40);
  if (t % 25 === 0) gcc.onProcessInterval(t, 1600);
}
check(gcc.targetKbps() > 300, `GCC ramps up on a clean path (${gcc.targetKbps().toFixed(0)} kbps)`);
gcc.delete();
fbb.delete();

// NACK.
const nack = new sm.NackTracker();
nack.onReceivedPacket(1, true, 0);
nack.onReceivedPacket(4, false, 1);
const batch = nack.getNackBatch(2);
check(batch.includes(2) && batch.includes(3), `NACK requests the gap (${batch})`);
nack.delete();

// Freeze detector.
const fd = new sm.FreezeDetector();
for (let t = 0; t <= 1000; t += 33) fd.onFrameRendered(t);
fd.onFrameRendered(2000);
check(fd.freezeCount() === 1, 'freeze detected');
fd.delete();

process.exit(failures ? 1 : 0);
