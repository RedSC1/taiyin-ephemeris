// Standalone generator regression; no companion JS checkout or Wasm build.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {cpSync, mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const core = fileURLToPath(new URL('../..', import.meta.url));
const fixture = mkdtempSync(resolve(tmpdir(), 'taiyin-bridge-generator-'));
try {
  const wasm = resolve(fixture, 'experiments/wasm');
  mkdirSync(wasm, {recursive: true});
  mkdirSync(resolve(fixture, 'include/taiyin'), {recursive: true});
  cpSync(resolve(core, 'include/taiyin/c'), resolve(fixture, 'include/taiyin/c'), {recursive: true});
  cpSync(resolve(core, 'experiments/wasm/generate-astronomy.mjs'), resolve(wasm, 'generate-astronomy.mjs'));
  const output = resolve(wasm, 'astronomy_bridge.generated.inc');
  const run = (...args) => spawnSync(process.execPath, [resolve(wasm, 'generate-astronomy.mjs'), ...args],
    {cwd: tmpdir(), encoding: 'utf8'});
  const generated = run();
  assert.equal(generated.status, 0, generated.stderr);
  const expected = readFileSync(resolve(core, 'experiments/wasm/astronomy_bridge.generated.inc'), 'utf8');
  assert.equal(readFileSync(output, 'utf8'), expected, 'clean headers reproduce the checked-in bridge byte for byte');
  assert.equal(run('--check').status, 0, 'check succeeds without a companion repository');
  writeFileSync(output, expected + '// stale\n');
  assert.notEqual(run('--check').status, 0, 'check detects edited generated output');
  assert.equal(readFileSync(output, 'utf8'), expected + '// stale\n', 'check must not rewrite stale output');
  assert.equal(run().status, 0);
  const header = resolve(fixture, 'include/taiyin/c/base.h');
  const original = readFileSync(header, 'utf8');
  const changed = original.replace('double x;', 'double x; double generator_regression;');
  assert.notEqual(changed, original);
  writeFileSync(header, changed);
  assert.notEqual(run('--check').status, 0, 'check detects an unregenerated record layout');
  assert.equal(run().status, 0);
  const updated = readFileSync(output, 'utf8');
  const version = text => /int taiyin_web_astronomy_version\(\)\{return (\d+);\}/.exec(text)?.[1];
  assert.ok(version(expected) && version(updated));
  assert.notEqual(version(updated), version(expected), 'record changes update the protocol fingerprint');
  assert.equal(run('--check').status, 0);
  console.log('Astronomy bridge generation, stale-check and protocol regressions passed');
} finally {
  rmSync(fixture, {recursive: true, force: true});
}
