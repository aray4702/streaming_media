// Node smoke test for the browser build: loads the ES module, runs a
// scenario end to end and drives each bound component once.
//   node web/test/smoke.mjs <path/to/sm_web.js> <scenario.scn> [<two-way scenario.scn>]
import { readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

const [modPath, scnPath, twoWayPath] = process.argv.slice(2);
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

// A two-way call, user <-> AI (#746-#750): each direction's metrics and time series.
if (twoWayPath) {
  const tw = JSON.parse(sm.runScenario(readFileSync(twoWayPath, 'utf8'), true));
  check(tw.error === undefined, `two-way scenario runs${tw.error ? ': ' + tw.error : ''}`);
  check(tw.passed === true, `two-way scenario ${tw.scenario} passes`);
  for (const d of ['up', 'down']) {
    check(tw.metrics[`${d}.audio.concealed_pct`] !== undefined, `${d}.audio.* reported`);
    check(tw[`${d}.timeseries`] && tw[`${d}.timeseries`].rows.length > 10, `${d}.timeseries present`);
    check(Object.keys(tw.library).some((k) => k.startsWith(`${d}.sender.gcc.`)), `${d}.sender.* library metrics`);
  }
  check(tw.timeseries === undefined && tw.metrics['audio.concealed_pct'] === undefined, 'no one-way names in a two-way call');
  check(sm.metricCatalog().some((m) => m.name === 'up.audio.concealed_pct'), 'catalog lists per-direction names');
}

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

// At 44.1 kHz, the usual AudioContext rate, NetEq runs at 48 kHz and the
// binding resamples (#579): the page hears the tone, not silence.
const jb44 = new sm.AudioJitterBuffer(44100);
let expands44 = 0, energy44 = 0;
for (let t = 0; t < 1000; t += 10) {
  if (t % 20 === 0) {
    const x = new Float32Array(882);
    for (let i = 0; i < 882; i++) x[i] = 0.3 * Math.sin(2 * Math.PI * 440 * (Math.round(t * 44.1) + i) / 44100);
    jb44.insertPacket(Math.round(t * 44.1), x, t + 30);
  }
  const out = jb44.getAudio(441, t + 30);
  if (out.length !== 441) failures++;
  if (jb44.lastOperation() === 'expand') expands44++;
  if (t >= 500) for (const s of out) energy44 += s * s;
}
const rms44 = Math.sqrt(energy44 / (50 * 441));
check(expands44 < 5, `jitter buffer at 44.1 kHz plays without concealment (${expands44} expands)`);
check(rms44 > 0.15, `jitter buffer at 44.1 kHz plays the tone (rms ${rms44.toFixed(3)})`);
jb44.delete();

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
