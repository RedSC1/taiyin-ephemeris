import assert from 'node:assert/strict';
import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {resolve, dirname} from 'node:path';
import {pathToFileURL} from 'node:url';
import {spawnSync} from 'node:child_process';
import {cpus, release} from 'node:os';
import {createHash} from 'node:crypto';
import {gzipSync, brotliCompressSync} from 'node:zlib';
import {check, createExperimentRuntime, decodeCallResult} from './runtime.mjs';

const [wasmArg, nativeArg, dataArg, outputArg] = process.argv.slice(2);
if (!dataArg) throw new Error('Usage: node compare.mjs WASM.mjs NATIVE_PROBE OPM2_DIRECTORY [REPORT.json]');
const wasmPath = resolve(wasmArg), nativePath = resolve(nativeArg), dataPath = resolve(dataArg);
const createModule = (await import(pathToFileURL(wasmPath).href)).default;
const binary = await readFile(wasmPath.replace(/\.mjs$/, '.wasm'));
const nativeCwd = resolve(dirname(nativePath), 'run', 'empty');
await mkdir(nativeCwd, {recursive: true});
const files = ['sun', 'moon', 'emb', 'mercury', 'venus', 'mars', 'jupiter',
               'saturn', 'uranus', 'neptune', 'pluto'];
const datasets = await Promise.all(files.map(async name => ({
  name: `${name}.opm2`, bytes: await readFile(resolve(dataPath, `${name}.opm2`)),
})));

function compareSuite(actual, expected) {
  assert.equal(actual.positions.length, expected.positions.length);
  const maxima = {angularArcsec: 0, distanceAu: 0, speedDegreesPerDay: 0,
    speedAuPerDay: 0, eventSeconds: 0, eclipseGeometryDegrees: 0};
  let valid = 0, expectedErrors = 0;
  actual.positions.forEach((a, index) => {
    const b = expected.positions[index];
    for (const key of ['target', 'jd', 'inputFlags', 'status', 'flags', 'method'])
      assert.equal(a[key], b[key], `position ${index}: ${key}`);
    if (a.status < 0) {
      // OPM2-only must reject outside coverage, never silently substitute the
      // analytic model. Interior failures are not an acceptable comparison.
      assert.ok(a.jd === 2000000.5 || a.jd === 3000000.5, `interior failure ${index}`);
      assert.ok(a.status === -1001 || a.status === -1002);
      expectedErrors++; return;
    }
    valid++;
    a.values.forEach((value, k) => {
      assert.equal(typeof value, 'number');
      assert.ok(Number.isFinite(value));
      const delta = Math.abs(value - b.values[k]);
      if (a.inputFlags & 2) {
        maxima.distanceAu = Math.max(maxima.distanceAu, delta);
        assert.ok(delta < 1e-10, `XYZ ${index}/${k}: ${delta}`);
      } else if (k < 2) {
        const angle = k === 0 ? Math.abs(((value - b.values[k] + 540) % 360) - 180) : delta;
        maxima.angularArcsec = Math.max(maxima.angularArcsec, angle * 3600);
        assert.ok(angle * 3600 < 1e-4, `angle ${index}/${k}: ${angle * 3600}`);
      } else if (k === 2) {
        maxima.distanceAu = Math.max(maxima.distanceAu, delta);
        assert.ok(delta < 1e-10, `range ${index}: ${delta}`);
      } else {
        const key = k === 5 ? 'speedAuPerDay' : 'speedDegreesPerDay';
        maxima[key] = Math.max(maxima[key], delta);
        assert.ok(delta < (k === 5 ? 1e-9 : 1e-6), `speed ${index}/${k}: ${delta}`);
      }
    });
  });
  assert.ok(valid >= 150, `too few successful positions: ${valid}`);
  assert.deepEqual(actual.calendar, expected.calendar);
  const lunarDates = [[2025, 1, 1, 0, 30], [2033, 11, 1, 1, 29], [2033, 12, 1, 0, 30]];
  actual.calendar.forEach((date, i) => {
    assert.equal(date.status, 0);
    assert.deepEqual(date.values, lunarDates[i]);
  });
  for (const key of ['summerSolstice', 'solarEclipse']) {
    const a = actual[key], b = expected[key];
    assert.equal(a.status, 0, key);
    assert.equal(a.flags, b.flags, key);
    const difference = Math.abs((a.day - b.day) + (a.fraction - b.fraction)) * 86400;
    maxima.eventSeconds = Math.max(maxima.eventSeconds, difference);
    assert.ok(difference < 0.001, `${key}: ${difference} seconds`);
  }
  assert.equal(actual.solarEclipse.kind, expected.solarEclipse.kind);
  assert.equal(actual.solarEclipse.day, 2460409);
  actual.solarEclipse.values.forEach((value, i) => {
    const difference = Math.abs(value - expected.solarEclipse.values[i]);
    if (i < 2) {
      maxima.eclipseGeometryDegrees = Math.max(maxima.eclipseGeometryDegrees, difference);
      assert.ok(difference < 1e-6);
    } else if (i % 2 === 0) assert.equal(value, expected.solarEclipse.values[i]);
    else {
      maxima.eventSeconds = Math.max(maxima.eventSeconds, difference * 86400);
      assert.ok(difference * 86400 < 0.001);
    }
  });
  return {successfulPositions: valid, matchedErrorResults: expectedErrors, maxima};
}

async function run(mode) {
  console.error(`[${mode}] native baseline`);
  const baseline = spawnSync(nativePath, [mode === 'precise' ? dataPath : '-'],
    {encoding: 'utf8', maxBuffer: 16 * 1024 * 1024, cwd: nativeCwd,
     env: {...process.env, TAIYIN_DATA_ROOT: ''}});
  assert.equal(baseline.status, 0, baseline.stderr);
  const native = JSON.parse(baseline.stdout);
  assert.equal(native.opm2Count, mode === 'precise' ? 11 : 0);
  const instantiateStart = performance.now();
  const module = await createModule({wasmBinary: binary});
  const instantiateMs = performance.now() - instantiateStart;
  const initStart = performance.now();
  const runtime = createExperimentRuntime(module);
  for (const status of [0, -1, -1004, -2147483648]) {
    for (const flags of [0, 1, 0x80000000, 0xffffffff]) {
      const raw = module._taiyin_make_call_result(status, flags);
      assert.deepEqual(decodeCallResult(raw), {status, flags});
      assert.equal(module._taiyin_call_result_status(raw), status);
      assert.equal(module._taiyin_call_result_flags(raw) >>> 0, flags);
    }
  }
  assert.throws(() => decodeCallResult(123), TypeError);
  assert.throws(() => runtime.loadOpm2Bytes('../escape.opm2', new Uint8Array()), TypeError);
  assert.throws(() => runtime.loadOpm2Base64('bad.opm2', '@@@@'), TypeError);
  assert.throws(() => runtime.loadOpm2Bytes('bad.opm2', new Uint8Array(28)), TypeError);
  // Correct magic but invalid content must be rejected by the real OPM2 parser.
  const corrupt = new Uint8Array(28); corrupt.set([79, 80, 77, 50]);
  assert.throws(() => runtime.loadOpm2Bytes('corrupt.opm2', corrupt));
  assert.equal(module._probe_opm2_count(), 0);
  let byteLoadMs = 0, base64DecodeAndLoadMs = 0;
  if (mode === 'precise') {
    for (const [index, file] of datasets.entries()) {
      const start = performance.now();
      if (index % 2) runtime.loadOpm2Base64(file.name, file.bytes.toString('base64'));
      else runtime.loadOpm2Bytes(file.name, file.bytes);
      const duration = performance.now() - start;
      if (index % 2) base64DecodeAndLoadMs += duration; else byteLoadMs += duration;
    }
    assert.throws(() => runtime.loadOpm2Bytes(datasets[0].name, datasets[0].bytes));
  }
  assert.equal(module._probe_opm2_count(), mode === 'precise' ? 11 : 0);
  const context = runtime.createContext(mode === 'precise' ? 1 : 3);
  const initializationMs = performance.now() - initStart;
  try {
    console.error(`[${mode}] Wasm suite`);
    const suite = runtime.suite(context);
    const precision = compareSuite(suite, native.suite);
    let seamSamples = 0, seamMaxAu = 0, seamErrors = 0;
    if (mode === 'precise') for (const [index, file] of datasets.entries()) {
      const samples = runtime.seams(context, file.name);
      assert.equal(samples.length, 15);
      assert.equal(samples.length, native.seams[index].length);
      samples.forEach((a, i) => {
        const b = native.seams[index][i];
        assert.equal(a.status, b.status, `seam ${file.name}/${i}`);
        assert.equal(a.flags, b.flags);
        seamSamples++;
        if (a.status < 0) { seamErrors++; return; }
        a.values.forEach((v, k) => {
          const delta = Math.abs(v - b.values[k]);
          seamMaxAu = Math.max(seamMaxAu, delta);
          assert.ok(Number.isFinite(v) && delta < 1e-10, `seam ${file.name}/${i}/${k}`);
        });
      });
    }
    Object.assign(precision, {seamSamples, seamMatchedErrors: seamErrors, seamMaxAu});
    console.error(`[${mode}] benchmark`);
    const benchmarks = native.benchmarks.map(b => {
      const repeats = Array.from({length: 5}, () => module._probe_benchmark(
        context, 2000, b.bodies, b.inputFlags)).sort((a, b) => a - b);
      assert.ok(repeats[0] > 0);
      return {...b, nativeUsPerBody: b.usPerBody, wasmUsPerBody: repeats[2],
        wasmOverNative: repeats[2] / b.usPerBody, usPerBody: undefined};
    });
    const out = module._malloc(48);
    let bridgeUsPerBody;
    try {
      const start = performance.now();
      for (let i = 0; i < 5000; i++) {
        check(module._probe_position(context, 10, 2460409, (i % 64) / 64, 0, out));
        // Copy result as a user-facing wrapper would; do not retain a heap view.
        Array.from(module.HEAPF64.subarray(out / 8, out / 8 + 3));
      }
      bridgeUsPerBody = (performance.now() - start) * 1000 / 5000;
      const invalid = module._probe_position(context, -1234567, 2460409, 0, 0, out);
      assert.ok(decodeCallResult(invalid).status < 0);
    } finally { module._free(out); }
    const eclipseUs = module._probe_eclipse_benchmark(context);
    assert.ok(native.eclipseBenchmarkUs > 0 && eclipseUs > 0);
    return {mode, opm2Count: module._probe_opm2_count(), instantiateMs,
      initializationMs, nativeInitializationMs: native.initializationMs,
      byteLoadMs, base64DecodeAndLoadMs, wasmMemoryBytes: module.HEAPU8.buffer.byteLength,
      precision, benchmarks, bridgeUsPerBody,
      eclipse: {nativeUs: native.eclipseBenchmarkUs,
                wasmUs: eclipseUs}, suite};
  } finally { runtime.destroyContext(context); }
}

const results = [];
for (const mode of ['analytic', 'precise']) results.push(await run(mode));
const loader = await readFile(wasmPath);
const totalData = Buffer.concat(datasets.map(x => x.bytes));
const report = {node: process.version, platform: `${process.platform}/${process.arch}`,
  cpu: cpus()[0]?.model, osRelease: release(),
  wasm: {bytes: binary.length, gzip: gzipSync(binary).length,
    brotli: brotliCompressSync(binary).length},
  generatedLoader: {bytes: loader.length, gzip: gzipSync(loader).length},
  data: {bytes: totalData.length, gzip: gzipSync(totalData).length,
    files: datasets.map(x => ({name: x.name, bytes: x.bytes.length,
      sha256: createHash('sha256').update(x.bytes).digest('hex')}))}, results};
if (outputArg) await writeFile(outputArg, JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify({...report, results: results.map(({suite, ...r}) => r)}, null, 2));
