// Experimental loader, not a second ephemeris format or a stable public API.
export function decodeCallResult(raw) {
  if (typeof raw !== 'bigint') throw new TypeError('CallResult must remain a BigInt');
  return {status: Number(BigInt.asIntN(32, raw >> 32n)),
          flags: Number(BigInt.asUintN(32, raw))};
}

export function check(raw) {
  const result = decodeCallResult(raw);
  if (result.status < 0) {
    const error = new Error(`Taiyin status ${result.status}`);
    Object.assign(error, result);
    throw error;
  }
  return result;
}

function withString(module, value, call) {
  const pointer = module._malloc(module.lengthBytesUTF8(value) + 1);
  if (!pointer) throw new Error('Wasm allocation failed');
  try {
    module.stringToUTF8(value, pointer, module.lengthBytesUTF8(value) + 1);
    return call(pointer);
  } finally { module._free(pointer); }
}

export function createExperimentRuntime(module) {
  check(module._probe_initialize());
  const loaded = new Set();
  module.FS.mkdirTree('/opm2');
  return {
    // Setup-time only. No runtime reset, decoder changes or route special cases.
    loadOpm2Bytes(name, bytes) {
      if (!/^[A-Za-z0-9][A-Za-z0-9_-]*\.opm2$/.test(name))
        throw new TypeError('Use an OPM2 basename, not a path');
      if (loaded.has(name)) throw new Error(`Already loaded: ${name}`);
      if (!(bytes instanceof Uint8Array) || bytes.length < 28 ||
          bytes[0] !== 79 || bytes[1] !== 80 || bytes[2] !== 77 || bytes[3] !== 50)
        throw new TypeError('Expected OPM2 bytes');
      const path = `/opm2/${name}`;
      module.FS.writeFile(path, bytes);
      try {
        withString(module, path, p => check(module._taiyin_runtime_add_source_path(p)));
        loaded.add(name);
      } catch (error) { module.FS.unlink(path); throw error; }
    },
    loadOpm2Base64(name, text) {
      // Strict padded RFC 4648; reject junk rather than silently repairing it.
      if (typeof text !== 'string' || !text.length || text.length % 4 ||
          !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(text))
        throw new TypeError('Expected padded Base64');
      const binary = atob(text);
      const bytes = new Uint8Array(binary.length);
      for (let i = 0; i < binary.length; ++i) bytes[i] = binary.charCodeAt(i);
      this.loadOpm2Bytes(name, bytes);
    },
    createContext(route = 3) {
      const out = module._malloc(4); // This experiment is wasm32, not memory64.
      if (!out) throw new Error('Wasm allocation failed');
      try {
        check(module._probe_create_context(route, out));
        return module.HEAPU32[out >>> 2];
      } finally { module._free(out); }
    },
    suite(context) {
      const pointer = module._probe_suite(context);
      if (!pointer) throw new Error('Wasm allocation failed');
      try { return JSON.parse(module.UTF8ToString(pointer)); }
      finally { module._free(pointer); }
    },
    seams(context, name) {
      return withString(module, `/opm2/${name}`, path => {
        const pointer = module._probe_seams(context, path);
        if (!pointer) throw new Error('Could not read OPM2 seam metadata');
        try { return JSON.parse(module.UTF8ToString(pointer)); }
        finally { module._free(pointer); }
      });
    },
    destroyContext(context) { module._taiyin_context_destroy(context); },
  };
}
