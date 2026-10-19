// Real Wasm bridge regressions, independent of the companion JS facade.
// Build taiyin_web with -DTAIYIN_WEB_ENVIRONMENT=web,node, then run:
// node experiments/wasm/test_callbacks.mjs build-wasm/taiyin_web.js
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {dirname, resolve} from 'node:path';

const loader = resolve(process.argv[2] ?? 'build-wasm/taiyin_web.js');
const module = await createRequire(import.meta.url)(loader)({
  locateFile: name => resolve(dirname(loader), name),
});
function buffers(input, action) {
  const p = module._malloc(Math.max(8, input.length * 8));
  const out = module._malloc(32 * 8);
  try {
    module.HEAPF64.set(input, p / 8);
    module.HEAPF64.fill(0, out / 8, out / 8 + 32);
    const status = action(p, out);
    return {status, values: Array.from(module.HEAPF64.subarray(out / 8, out / 8 + 32))};
  } finally {
    module._free(out);
    module._free(p);
  }
}
function callbacks(operation, input) {
  return buffers(input, p => {
    const reply = module._taiyin_web_astronomy_callbacks(operation, p, input.length);
    assert.ok(reply, 'reply allocation');
    try {
      const data = module._taiyin_web_eclipse_data(reply) / 8;
      const length = module._taiyin_web_eclipse_length(reply);
      return Array.from(module.HEAPF64.subarray(data, data + length));
    } finally {
      module._taiyin_web_eclipse_destroy(reply);
    }
  }).status;
}
function register(id, jsToken) {
  const reply = callbacks(0, [0, id, jsToken, 0, 1]);
  assert.equal(reply[1], 0);
  const token = reply.slice(32);
  assert.equal(token.length, 2);
  assert.ok(token[0] !== 0 || token[1] !== 0, 'opaque nonzero owner generation');
  return token;
}
const unregister = (id, token) => callbacks(1, [0, id, ...token])[1];
function initialize() {
  assert.equal(buffers([1, 0, 0, 0], (p, out) => module._taiyin_web_initialize(p, out)).status, 0);
}
function context() {
  const result = buffers([], (_, out) => module._taiyin_web_context_create(out));
  assert.equal(result.status, 0);
  return result.values[1];
}
const target = -910001;
let failure = 0;
module.taiyinAstronomyCallback = (kind, token, input, count, output, capacity) => {
  assert.ok(kind === 0 || kind === 1);
  assert.equal(count, 6);
  assert.equal(module.HEAPF64[input / 8], target);
  if (failure) return failure;
  assert.equal(capacity, kind === 0 ? 6 : 9);
  module.HEAPF64.fill(0, output / 8, output / 8 + capacity);
  module.HEAPF64[output / 8] = token;
  return 0;
};
function evaluate(ctx, state = 0) {
  // Explicit TDB/TT inputs make the diagnostic epoch check exact.
  return buffers([2, target, 2460310, 0.125, 2460310, 0.25, 0, 0],
    (p, out) => module._taiyin_web_position(ctx, state, p, out));
}
// Registering during setup, before the first successful initialization, is
// supported by the native C API and must retain its bridge ownership too.
const beforeInit = register(target, 100);
assert.equal(buffers([1, 0, 1.5, 0],
  (p, out) => module._taiyin_web_initialize(p, out)).status, -1);
initialize();
let ctx = context();
try {
  assert.equal(evaluate(ctx).values[1], 100, 'pre-init evaluator survives first initialization');
  assert.equal(unregister(target, beforeInit), 0, 'pre-init owner can unregister after initialization');
  const old = register(target, 101);
  assert.equal(callbacks(0, [0, target, 999, 0, 1])[1], -1, 'duplicate owner rejected');
  assert.equal(unregister(target, [0, 0]), -1, 'zero cannot remove owner');
  assert.equal(evaluate(ctx).values[1], 101);
  assert.equal(unregister(target, old), 0);
  const current = register(target, 202);
  assert.notDeepEqual(current, old);
  assert.equal(unregister(target, old), -1, 'stale double disposal rejected');
  assert.equal(evaluate(ctx).values[1], 202, 'new owner remains registered');
  assert.equal(evaluate(ctx, 1).values[1], 202, 'exact state also remains registered');
  failure = -1004;
  for (const state of [0, 1]) {
    const result = evaluate(ctx, state);
    assert.equal(result.status, failure);
    assert.equal(result.values[10], failure, 'diagnostic matches callback failure');
    assert.equal(result.values[11], target, 'diagnostic identifies target');
    assert.equal(result.values[14], 2460310, 'diagnostic has TDB day');
    assert.equal(result.values[15], 0.125, 'diagnostic has TDB fraction');
  }
  failure = 0;
  assert.equal(evaluate(ctx).status, 0, 'failure does not poison next call');
  module._taiyin_context_destroy(ctx);
  ctx = 0;
  initialize(); // Native reset clears callbacks, bridge bookkeeping must follow.
  ctx = context();
  const afterReset = register(target, 303);
  assert.notDeepEqual(afterReset, current);
  assert.equal(unregister(target, current), -1, 'pre-reset handle remains stale');
  assert.equal(evaluate(ctx).values[1], 303);
  assert.equal(unregister(target, afterReset), 0);
  assert.equal(unregister(target, afterReset), -1);
} finally {
  if (ctx) module._taiyin_context_destroy(ctx);
}
console.log('Wasm callback ownership, reset and failure-diagnostic regressions passed');
