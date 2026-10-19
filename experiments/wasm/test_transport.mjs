// Direct real-Wasm malformed Split-JD regression, without a wrapper validator.
// node experiments/wasm/test_transport.mjs build-wasm/taiyin_web.js
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {dirname, resolve} from 'node:path';
const loader = resolve(process.argv[2] ?? 'build-wasm/taiyin_web.js');
const module = await createRequire(import.meta.url)(loader)({
  locateFile: name => resolve(dirname(loader), name),
});
function inputBuffer(input, action) {
  const pointer = module._malloc(Math.max(64, input.length) * 8);
  assert.ok(pointer);
  try {
    module.HEAPF64.fill(0, pointer / 8, pointer / 8 + Math.max(64, input.length));
    module.HEAPF64.set(input, pointer / 8);
    return action(pointer);
  } finally { module._free(pointer); }
}
function fixed(name, prefix, input) {
  return inputBuffer(input, p => {
    const out = module._malloc(512 * 8);
    assert.ok(out);
    try {
      module.HEAPF64.fill(0, out / 8, out / 8 + 512);
      const status = module[name](...prefix, p, out);
      return {status, values: Array.from(module.HEAPF64.subarray(out / 8, out / 8 + 512))};
    } finally { module._free(out); }
  });
}
function structured(name, prefix, input) {
  return inputBuffer(input, p => {
    const reply = module[name](...prefix, p, input.length);
    assert.ok(reply);
    try {
      const data = module._taiyin_web_eclipse_data(reply) / 8;
      const length = module._taiyin_web_eclipse_length(reply);
      return Array.from(module.HEAPF64.subarray(data, data + length));
    } finally { module._taiyin_web_eclipse_destroy(reply); }
  });
}
assert.equal(fixed('_taiyin_web_initialize', [], [1, 0, 0, 0]).status, 0);
// create_context only takes an output buffer, unlike the other fixed adapters.
const out = module._malloc(32 * 8);
let ctx = 0, calendar = 0, bazi = 0, catalog = 0, ziwei = 0, shenCatalog = 0;
const text = value => {
  const bytes = Array.from(new TextEncoder().encode(value));
  return [bytes.length, ...bytes];
};
try {
  assert.equal(module._taiyin_web_context_create(out), 0);
  ctx = module.HEAPF64[out / 8 + 1];
  const created = fixed('_taiyin_web_calendar_create', [ctx], [2, 0, 480, 120]);
  assert.equal(created.status, 0);
  calendar = created.values[1];
  const local = [2024, 1, 1, 8, 0, 0];
  const pillars = fixed('_taiyin_web_calendar', [calendar, 12], [2460310, 0.5, 0, ...local]);
  assert.equal(pillars.status, 0);
  const baziCreated = structured('_taiyin_web_bazi', [0, calendar, 0], [0, 0, 0, 0]);
  assert.equal(baziCreated[1], 0);
  bazi = baziCreated[32];
  const chart = structured('_taiyin_web_bazi', [bazi, calendar, 8], pillars.values.slice(1, 5));
  assert.equal(chart[1], 0);
  const qiyun = day => [day, 0.5, ...local, ...chart.slice(32), 0];
  const rulesPath = '/taiyin/ziwei/rules/default.toml';
  const cat = structured('_taiyin_web_ziwei', [0, 0, calendar, 0], text(rulesPath));
  assert.equal(cat[1], 0); catalog = cat[32];
  const zw = structured('_taiyin_web_ziwei', [catalog, 0, calendar, 4], [0, 0]);
  assert.equal(zw[1], 0); ziwei = zw[32];
  const shen = structured('_taiyin_web_bazi', [0, calendar, 22], []);
  assert.equal(shen[1], 0); shenCatalog = shen[32];
  for (const bad of [NaN, Infinity, -Infinity, 2 ** 63, -(2 ** 63),
    2 ** 53, -(2 ** 53), 2460310.5]) {
    assert.equal(structured('_taiyin_web_eclipse', [ctx, 22], [bad, 0, 0.04])[1], -1);
    // Also reject an invalid second date, not just the first one.
    assert.equal(structured('_taiyin_web_eclipse', [ctx, 14], [2460310, 0, bad, 0, 0, 0, 0, 2])[1], -1);
    assert.equal(structured('_taiyin_web_bazi', [bazi, calendar, 15], qiyun(bad))[1], -1);
    assert.equal(structured('_taiyin_web_ziwei', [0, 0, calendar, 25], [0, 0, bad, 0])[1], -1);
    assert.equal(fixed('_taiyin_web_position', [ctx, 0], [0, 301, bad, 0]).status, -1);
    assert.equal(fixed('_taiyin_web_position', [ctx, 1], [2, 301, 2460310, 0, bad, 0]).status, -1);
    const batch = new Array(33).fill(0); batch[2] = bad; batch[32] = 301;
    assert.equal(fixed('_taiyin_web_positions', [ctx, 1], batch).status, -1);
    assert.equal(fixed('_taiyin_web_solar_time', [ctx, 0], [bad, 0]).status, -1);
    assert.equal(fixed('_taiyin_web_time', [ctx, 1], [0, bad, 0]).status, -1);
    assert.equal(fixed('_taiyin_web_calendar', [calendar, 9], [bad, 0]).status, -1);
    assert.equal(fixed('_taiyin_web_calendar', [calendar, 12], [bad, 0.5, 0, ...local]).status, -1);
  }
  assert.equal(structured('_taiyin_web_eclipse', [ctx, 22], [2460409, 0.25, 0.04])[1], 0);
  assert.equal(structured('_taiyin_web_bazi', [bazi, calendar, 15], qiyun(2460310))[1], 0);
  assert.equal(structured('_taiyin_web_ziwei', [0, 0, calendar, 25], [0, 0, 2460310, 0.5])[1], 0);
  assert.equal(fixed('_taiyin_web_position', [ctx, 0], [0, 301, 2460310, 0.5]).status, 0);
  assert.equal(fixed('_taiyin_web_calendar', [calendar, 9], [2460310, 0.5]).status, 0);

  // Complete otherwise-valid packets: rejection must not be an accidental
  // short/trailing-packet error, or a failure from a null native handle.
  const eclipseRange = [2460310, 0.5, 2460311, 0.5, 0, 0, 0, 1];
  assert.equal(structured('_taiyin_web_eclipse', [ctx, 14], eclipseRange)[1], 0);
  assert.equal(structured('_taiyin_web_bazi', [bazi, calendar, 1], [0])[1], 0);
  assert.equal(structured('_taiyin_web_ziwei', [ziwei, 0, calendar, 8], [0])[1], 0);
  for (const bad of [1.5, NaN, Infinity, -Infinity, -1, 2 ** 32]) {
    for (const slot of [5, 6, 7]) {
      const input = [...eclipseRange]; input[slot] = bad;
      assert.equal(structured('_taiyin_web_eclipse', [ctx, 14], input)[1], -1);
    }
    assert.equal(structured('_taiyin_web_bazi', [bazi, calendar, 1], [bad])[1], -1);
    assert.equal(structured('_taiyin_web_ziwei', [ziwei, 0, calendar, 8], [bad])[1], -1);
    const position = [0, 301, 2460310, 0.5, 0, 0, 0, bad];
    assert.equal(fixed('_taiyin_web_position', [ctx, 0], position).status, -1);
  }
  for (const bad of [256, 65536]) {
    assert.equal(structured('_taiyin_web_bazi', [bazi, calendar, 1], [bad])[1], -1);
  }
  assert.equal(structured('_taiyin_web_ziwei', [ziwei, 0, calendar, 8], [65536])[1], -1);
  for (const bad of [2 ** 31, -(2 ** 31) - 1, 2024.5, NaN])
    assert.equal(structured('_taiyin_web_bazi', [bazi, calendar, 9], [bad])[1], -1);
  assert.equal(structured('_taiyin_web_bazi', [bazi, calendar, 9], [-1045])[1], 0);
  assert.equal(fixed('_taiyin_web_configure', [ctx, 12], [NaN]).status, -1);
  assert.equal(fixed('_taiyin_web_calendar', [calendar, 8], [2024, 258, 1]).status, -1);
  assert.equal(fixed('_taiyin_web_initialize', [], [1, 0, 1.5, 0]).status, -1,
    'malformed initialization rejected before resetting live resources');

  // Serialized strings must survive as a whole C string, never as a prefix.
  assert.equal(structured('_taiyin_web_ziwei', [0, 0, calendar, 0], text(rulesPath + '\0ignored'))[1], -1);
  for (const value of ['ziwei\0ignored', '\0ziwei'])
    assert.equal(structured('_taiyin_web_ziwei', [ziwei, 0, calendar, 7], text(value))[1], -1);
  assert.equal(structured('_taiyin_web_ziwei', [catalog, 0, calendar, 4],
    [1, 0, ...text('ziwei'), ...text('option1\0ignored'), 0])[1], -1);
  for (const bad of [1.5, NaN, Infinity, -1, 2 ** 32])
    assert.equal(structured('_taiyin_web_ziwei', [ziwei, 0, calendar, 7], [bad, 65])[1], -1);
  for (const bad of [0, -1, 1.5, NaN, Infinity, 256])
    assert.equal(structured('_taiyin_web_ziwei', [ziwei, 0, calendar, 7], [1, bad])[1], -1);
  const moduleInput = fields => [
    ...text(fields[0]), 1, ...text(fields[1]), ...text(fields[2]), 1,
  ];
  const fields = ['my-school', 'custom-rule', '自定义⭐'];
  for (let i = 0; i < fields.length; ++i) {
    const invalid = [...fields]; invalid[i] += '\0ignored';
    assert.equal(structured('_taiyin_web_bazi', [shenCatalog, calendar, 23], moduleInput(invalid))[1], -1);
  }
  const moduleResult = structured('_taiyin_web_bazi', [shenCatalog, calendar, 23], moduleInput(fields));
  assert.equal(moduleResult[1], 0, 'UTF-8 bytes >= 128 remain valid');
  assert.equal(structured('_taiyin_web_bazi', [moduleResult[32], calendar, 27], [])[1], 0);
  assert.equal(structured('_taiyin_web_ziwei', [ziwei, 0, calendar, 7], text('ziwei'))[1], 0);
  assert.equal(structured('_taiyin_web_eclipse', [ctx, 14], eclipseRange)[1], 0);
} finally {
  if (shenCatalog) structured('_taiyin_web_bazi', [shenCatalog, calendar, 27], []);
  if (ziwei) structured('_taiyin_web_ziwei', [ziwei, 0, calendar, 5], []);
  if (catalog) structured('_taiyin_web_ziwei', [catalog, 0, calendar, 1], []);
  if (bazi) module._taiyin_bazi_context_destroy(bazi);
  if (calendar) module._taiyin_chinese_calendar_context_destroy(calendar);
  if (ctx) module._taiyin_context_destroy(ctx);
  module._free(out);
}
console.log('Wasm Split-JD/integer/text validation and recovery regressions passed');
