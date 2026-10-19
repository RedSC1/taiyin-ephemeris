// A finite, audited C-ABI transport generator, not an astronomy implementation.
// Unsupported pointer/callback shapes fail generation instead of guessing layouts.
import { readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { createHash } from "node:crypto";
const args = process.argv.slice(2);
const check = args.includes("--check");
if (args.some((arg) => arg !== "--check"))
  throw Error("Usage: node experiments/wasm/generate-astronomy.mjs [--check]");
const core = fileURLToPath(new URL("../..", import.meta.url));
function emit(path, content) {
  if (check) {
    if (readFileSync(path, "utf8") !== content)
      throw Error("Stale generated bridge: run node experiments/wasm/generate-astronomy.mjs");
  } else writeFileSync(path, content);
}
const families = [
  "base",
  "context",
  "observed",
  "star",
  "visibility",
  "phenomena",
  "events",
  "orbital",
  "heliacal",
  "occultation",
  "astrology",
  "runtime",
];
const clean = (s) =>
  s.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/[^\n]*/g, "");
const headers = Object.fromEntries(
  families.map((f) => [
    f,
    clean(readFileSync(resolve(core, `include/taiyin/c/${f}.h`), "utf8")),
  ]),
);
const all = Object.values(headers).join("\n");
const camel = (s) =>
  s
    .replace(/jd_ut1\b|jd_ut\b/g, "julian_date_ut1")
    .replace(/jd_tt\b/g, "julian_date_tt")
    .replace(/jd_tdb\b/g, "julian_date_tdb")
    .replace(/_([a-z0-9])/g, (_, c) => c.toUpperCase());
const constants = new Map(
  [...all.matchAll(/\b(TAIYIN_\w+)\s*(?:=|\s)\s*(\d+)u?\s*[,;\n]/g)].map(
    (m) => [m[1], +m[2]],
  ),
);
const structs = new Map();
for (const m of all.matchAll(/typedef struct (\w+)\s*\{([^}]+)\}\s*\w+\s*;/g)) {
  const fields = [];
  for (const statement of m[2].split(";")) {
    if (!statement.trim()) continue;
    const match = /^\s*(\w+)\s+([\s\S]+)$/.exec(statement);
    if (!match) throw Error(`Unsupported field ${m[1]} ${statement}`);
    for (const decl of match[2].split(",")) {
      const v = /^\s*(\w+)\s*((?:\[[\w]+\])*)\s*$/.exec(decl);
      if (!v) {
        fields.push({ name: "__pointer__", type: match[1], shape: [] });
        continue;
      }
      if (v[1] === "struct_size" || v[1].startsWith("reserved")) continue;
      const shape = [...v[2].matchAll(/\[(\w+)\]/g)].map((x) =>
        /^\d+$/.test(x[1]) ? +x[1] : constants.get(x[1]),
      );
      if (shape.some((x) => !Number.isInteger(x)))
        throw Error(`Unknown dimension ${decl}`);
      fields.push({ name: v[1], key: camel(v[1]), type: match[1], shape });
    }
  }
  structs.set(m[1], fields);
}
const prototypes = new Map();
for (const [family, header] of Object.entries(headers))
  for (const m of header.matchAll(
    /TAIYIN_C(?:_ASTROLOGY)?_API\s+(taiyin_call_result|double|taiyin_bool|size_t|void)\s+TAIYIN_C_CALL\s+(taiyin_\w+)\s*\(([^;]*)\)\s*;/g,
  )) {
    const args =
      m[3].trim() === "void"
        ? []
        : m[3].split(",").map((s, i) => {
            const a = /^\s*(const\s+)?(\w+)([\s\S]*?)\s*$/.exec(s);
            if (!a) throw Error(`Bad argument ${s}`);
            const name =
              /\b(\w+)\s*(?:\[\d+\])?\s*$/.exec(a[3])?.[1] ?? `arg${i}`;
            return {
              type: a[2],
              name,
              pointer: (a[3].match(/\*/g) || []).length,
              array: +(/\[(\d+)\]/.exec(a[3])?.[1] ?? 0),
              constant: !!a[1],
              raw: s.trim(),
            };
          });
    prototypes.set(m[2], { name: m[2], family, ret: m[1], args });
  }
const specs = [];
function add(family, name, method, pos = [], overrides = {}) {
  const p = prototypes.get(name);
  if (!p) throw Error(`Missing declaration ${name}`);
  const args = p.args.map((a, i) => ({ ...a, ...overrides[i] }));
  specs.push({ ...p, args, group: family, method, pos, id: specs.length });
}
for (const scale of ["tt", "ut"]) {
  const suffix = scale === "tt" ? "Tt" : "Ut1";
  add("phenomena", `taiyin_calc_body_phenomena_${scale}`, `at${suffix}`, [
    "body_id",
    `jd_${scale}`,
  ]);
  for (const [native, method] of [
    ["osculating_orbit", "osculating"],
    ["orbit_reference_points", "referencePoints"],
  ])
    add(
      "orbits",
      `taiyin_calc_body_${native}_${scale}`,
      `${method}At${suffix}`,
      ["body_id", `jd_${scale}`],
    );
  for (const [native, method] of [
    ["apsis", "searchApsis"],
    ["plane_node", "searchPlaneNode"],
  ])
    add(
      "orbits",
      `taiyin_search_next_body_${native}_${scale}`,
      `${method}From${suffix}`,
      ["body_id", "kind", `jd_start_${scale}`],
    );
  for (const obj of ["solar", "moon"])
    add(
      "events",
      `taiyin_search_${obj}_longitude_${scale}`,
      `${obj}LongitudeAt${suffix}`,
      ["target_longitude_rad", `estimate_jd_${scale}`],
    );
  for (const [n, m] of [
    ["longitude_crossings", "longitudeCrossings"],
    ["longitude_stations", "longitudeStations"],
    ["aspect_crossings", "aspectCrossings"],
    ["exact_aspects", "exactAspects"],
  ])
    add(
      "events",
      `taiyin_search_body_${n}_${scale}`,
      `${m}At${suffix}`,
      n.startsWith("longitude")
        ? ["body_id", `start_jd_${scale}`, `end_jd_${scale}`]
        : ["body_a_id", "body_b_id", `start_jd_${scale}`, `end_jd_${scale}`],
    );
  add(
    "events",
    `taiyin_search_lunar_phase_crossings_${scale}`,
    `lunarPhaseCrossingsAt${suffix}`,
    ["phase_rad", `start_jd_${scale}`, `end_jd_${scale}`],
  );
  add(
    "events",
    `taiyin_search_minimum_angular_separation_${scale}`,
    `minimumAngularSeparationAt${suffix}`,
    ["body_a_id", "body_b_id", `start_jd_${scale}`, `end_jd_${scale}`],
  );
  add(
    "events",
    `taiyin_search_minimum_body_star_angular_separation_${scale}`,
    `minimumBodyStarAngularSeparationAt${suffix}`,
    ["body_id", "star_key", `start_jd_${scale}`, `end_jd_${scale}`],
  );
}
for (const obj of ["moon", "planet", "solar", "star"]) {
  const target =
    obj === "planet" ? ["body_id"] : obj === "star" ? ["star_key"] : [];
  for (const horizon of [false, true])
    add(
      "visibility",
      `taiyin_search_${obj}_rise_set${horizon ? "_at_horizon" : ""}_ut`,
      `${obj}RiseSet${horizon ? "AtHorizon" : ""}AtUt1`,
      [...target, "start_jd_ut", "end_jd_ut"],
    );
  add("visibility", `taiyin_search_${obj}_transit_ut`, `${obj}TransitAtUt1`, [
    ...target,
    "start_jd_ut",
    "end_jd_ut",
  ]);
}
add("visibility", "taiyin_search_solar_twilight_ut", "solarTwilightAtUt1", [
  "start_jd_ut",
  "end_jd_ut",
]);
for (const n of ["rise_set", "transit"])
  add(
    "visibility",
    `taiyin_compute_solar_${n}_fast_tt`,
    n === "transit" ? "solarTransitFastAtTt" : "solarRiseSetFastAtTt",
    ["center_jd_tt"],
  );
add(
  "events",
  "taiyin_recommended_longitude_search_step_days",
  "recommendedLongitudeSearchStepDays",
  ["body_id"],
);
add(
  "events",
  "taiyin_recommended_aspect_search_step_days",
  "recommendedAspectSearchStepDays",
  ["body_a_id", "body_b_id"],
);
add(
  "events",
  "taiyin_search_greatest_elongation_ut",
  "greatestElongationAtUt1",
  ["body_id", "start_jd_ut", "end_jd_ut"],
);
add("events", "taiyin_search_next_solar_transit_ut", "nextSolarTransitAtUt1", [
  "body_id",
  "jd_start_ut",
]);
add(
  "events",
  "taiyin_compute_local_solar_transit_ut",
  "localSolarTransitAtUt1",
  ["global_transit"],
);
add(
  "events",
  "taiyin_search_next_local_solar_transit_ut",
  "nextLocalSolarTransitAtUt1",
  ["body_id", "jd_start_ut"],
);
for (const obj of ["body", "star"]) {
  const target = obj === "body" ? "body_id" : "star_key";
  add("heliacal", `taiyin_calc_${obj}_heliacal_visibility_ut`, `${obj}AtUt1`, [
    target,
    "jd_ut",
  ]);
  add(
    "heliacal",
    `taiyin_search_next_${obj}_heliacal_visibility_ut`,
    `next${obj === "body" ? "Body" : "Star"}EventAtUt1`,
    [target, "jd_start_ut"],
  );
  for (const scope of ["geocentric", "local"]) {
    const method = `next${scope === "local" ? "Local" : "Geocentric"}${obj === "body" ? "Body" : "Star"}`;
    add(
      "occultations",
      `taiyin_search_next_${scope}_lunar_${obj}_occultation_ut`,
      `${method}AtUt1`,
      [target, "jd_start_ut"],
    );
    if (obj === "body")
      add(
        "occultations",
        `taiyin_search_next_${scope}_lunar_body_occultation_with_radius_ut`,
        `${method}WithRadiusAtUt1`,
        [target, "jd_start_ut"],
      );
  }
  add(
    "occultations",
    `taiyin_compute_lunar_${obj}_occultation_local_visibility_ut`,
    `local${obj === "body" ? "Body" : "Star"}VisibilityAtUt1`,
    [target, "occultation"],
  );
  add(
    "occultations",
    `taiyin_compute_lunar_${obj}_occultation_where_ut`,
    `${obj}WhereAtUt1`,
    [target, "occultation"],
  );
}
add(
  "occultations",
  "taiyin_compute_lunar_body_occultation_where_with_radius_ut",
  "bodyWhereWithRadiusAtUt1",
  ["body_id", "occultation"],
);
add("astrology", "taiyin_calc_ayanamsha_tt", "ayanamshaAtTt", ["jd_tt"]);
for (const scale of ["tt", "ut"]) {
  const suffix = scale === "tt" ? "Tt" : "Ut1";
  for (const n of ["position", "coordinates"])
    add(
      "astrology",
      `taiyin_calc_sidereal_${n}_${scale}`,
      `sidereal${n === "position" ? "Position" : "Coordinates"}At${suffix}`,
      ["body_id", `jd_${scale}`],
    );
  add("astrology", `taiyin_calc_houses_${scale}`, `housesAt${suffix}`, [
    `jd_${scale}`,
  ]);
  for (const n of [
    "true_node",
    "mean_node",
    "mean_apogee",
    "osculating_apogee",
    "fitted_apogee",
  ]) {
    const name = `taiyin_calc_lunar_${n}_${scale}`;
    const isNode = n.endsWith("node");
    const names = [
      "context",
      `jd_${scale}`,
      ...(isNode ? ["kind"] : []),
      "flags",
      "out",
      "diagnostic",
    ];
    add(
      "astrology",
      name,
      `lunar${camel(n).replace(/^./, (s) => s.toUpperCase())}At${suffix}`,
      [`jd_${scale}`],
      Object.fromEntries(names.map((name, i) => [i, { name }])),
    );
  }
}
add("astrology", "taiyin_calc_houses_from_armc", "housesFromArmc", []);
add(
  "astrology",
  "taiyin_calc_house_position_from_longitude",
  "housePositionOf",
  ["houses", "ecliptic_longitude_rad"],
);
for (const [n, m] of [
  ["house_system", "HouseSystem"],
  ["ayanamsha", "Ayanamsha"],
])
  add("astrology", `taiyin_has_${n}_model`, `has${m}Model`, ["model_id"]);
// These setters are additions to, not replacements for, the original configuration API.
for (const n of [
  "atmosphere",
  "atmosphere_pressure_temperature",
  "standard_atmosphere",
  "atmosphere_policy",
  "meteorological_range_km",
  "topocentric_observer_offset",
  "simple_topocentric_observer",
  "precise_topocentric_observer",
  "celestial_pole_offset",
  "refraction_model",
  "heliacal_visibility_model",
  "deflectors",
  "eclipse_models",
])
  add(
    "configuration",
    `taiyin_context_set_${n}`,
    `set${camel(n).replace(/^./, (s) => s.toUpperCase())}`,
    [],
  );
add(
  "configuration",
  "taiyin_context_enable_shapiro_delay",
  "enableShapiroDelay",
  ["shapiro_delay_model_id"],
);
add(
  "configuration",
  "taiyin_context_disable_shapiro_delay",
  "disableShapiroDelay",
  [],
);
for (const [native, method, pos] of [
  [
    "set_ephemeris_source_priority",
    "setEphemerisSourcePriority",
    ["path_or_basename", "priority"],
  ],
  [
    "clear_ephemeris_source_priority",
    "clearEphemerisSourcePriority",
    ["path_or_basename"],
  ],
  [
    "clear_all_ephemeris_source_priorities",
    "clearAllEphemerisSourcePriorities",
    [],
  ],
  ["clear_ephemeris_cache", "clearEphemerisCache", []],
  ["catalog_size", "catalogSize", []],
  ["cache_entry_count", "cacheEntryCount", []],
])
  add("sources", `taiyin_runtime_${native}`, method, pos);
add(
  "sources",
  "taiyin_register_builtin_astrology_targets",
  "registerBuiltinAstrologyTargets",
  [],
);

const used = new Set([
  "taiyin_observed_position",
  "taiyin_runtime_registered_data_source",
]);
function visit(type) {
  if (
    !structs.has(type) ||
    (used.has(type) &&
      type !== "taiyin_observed_position" &&
      type !== "taiyin_runtime_registered_data_source")
  )
    return;
  used.add(type);
  for (const f of structs.get(type)) {
    if (f.name === "__pointer__") throw Error(`Opaque pointer record ${type}`);
    if (structs.has(f.type) && !used.has(f.type)) visit(f.type);
  }
}
// Start roots separately so their dependencies are traversed.
used.clear();
visit("taiyin_observed_position");
visit("taiyin_runtime_registered_data_source");
visit("taiyin_calendar_datetime");
for (const s of specs)
  for (const a of s.args)
    if (a.type !== "taiyin_context" && structs.has(a.type)) visit(a.type);
const ordered = [];
const emitted = new Set();
function order(t) {
  if (emitted.has(t)) return;
  emitted.add(t);
  for (const f of structs.get(t)) if (used.has(f.type)) order(f.type);
  ordered.push(t);
}
for (const t of used) order(t);
const schema = Object.fromEntries(
  ordered.map((t) => [
    t,
    structs.get(t).map((f) => ({ key: f.key, type: f.type, shape: f.shape })),
  ]),
);
let cpp = `// Generated by experiments/wasm/generate-astronomy.mjs. No astronomy is implemented here.
#include <cmath>
#include <limits>
#include <string>
#include <algorithm>
namespace {
struct AstrReader {const double* data;size_t count,cursor;double next(){if(cursor>=count)throw std::invalid_argument("short astronomy request");return data[cursor++];}};
template<class T>T astr_scalar(AstrReader& in){const double n=in.next();if(std::numeric_limits<T>::is_integer && (!std::isfinite(n)||n!=std::floor(n)||(sizeof(T)>=8&&std::fabs(n)>9007199254740991.0)||static_cast<long double>(n)<static_cast<long double>((std::numeric_limits<T>::min)())||static_cast<long double>(n)>static_cast<long double>((std::numeric_limits<T>::max)())))throw std::invalid_argument("astronomy integer out of range");return static_cast<T>(n);}
uint64_t astr_u64(AstrReader& in){uint32_t lo=astr_scalar<uint32_t>(in);uint32_t hi=astr_scalar<uint32_t>(in);return uint64_t(lo)|(uint64_t(hi)<<32);}
void astr_pack_u64(uint64_t v,std::vector<double>& out){out.push_back(static_cast<uint32_t>(v));out.push_back(static_cast<uint32_t>(v>>32));}
std::string astr_text(AstrReader& in){const size_t n=astr_scalar<uint32_t>(in);if(n>1048576||n>in.count-in.cursor)throw std::invalid_argument("invalid text length");std::string s;s.reserve(n);for(size_t i=0;i<n;++i){char c=static_cast<char>(astr_scalar<uint8_t>(in));if(!c)throw std::invalid_argument("embedded NUL");s.push_back(c);}return s;}
void astr_text_out(const std::string& s,std::vector<double>& out){out.push_back(s.size());for(unsigned char c:s)out.push_back(c);}
`;
for (const t of ordered) {
  const fields = structs.get(t);
  const hasInit = new RegExp(`\\b${t}_init\\s*\\(`).test(all);
  cpp += `void astr_pack(const ${t}& v,std::vector<double>& out){\n`;
  for (const f of fields) {
    const loops = f.shape
      .map((d, i) => `for(size_t i${i}=0;i${i}<${d};++i${i}) `)
      .join("");
    const v = "v." + f.name + f.shape.map((_, i) => `[i${i}]`).join("");
    cpp += ` ${loops}${structs.has(f.type) ? `astr_pack(${v},out);` : f.type === "uint64_t" ? `astr_pack_u64(${v},out);` : `out.push_back(static_cast<double>(${v}));`}\n`;
  }
  cpp += "}\n";
  cpp += `void astr_read(${t}& v,AstrReader& in){${hasInit ? `${t}_init(&v);` : "v={};"}\n`;
  for (const f of fields) {
    const loops = f.shape
      .map((d, i) => `for(size_t i${i}=0;i${i}<${d};++i${i}) `)
      .join("");
    const v = "v." + f.name + f.shape.map((_, i) => `[i${i}]`).join("");
    cpp += ` ${loops}${structs.has(f.type) ? `astr_read(${v},in);` : `${v}=${f.type === "uint64_t" ? "astr_u64(in)" : `astr_scalar<${f.type}>(in)`};`}\n`;
  }
  cpp += "}\n";
}
cpp +=
  '}\nextern "C" {\nint taiyin_web_astronomy_version(){return 1;}\nWebEclipseReply* taiyin_web_astronomy(taiyin_context* context,int operation,const double* input,size_t input_count){\ntry{std::unique_ptr<WebEclipseReply> reply(new WebEclipseReply());AstrReader in={input,input_count,0};taiyin_ephemeris_diagnostic diag;taiyin_ephemeris_diagnostic_init(&diag);taiyin_call_result result=taiyin_make_call_result(0,0);switch(operation){\n';
const descriptors = [];
for (const s of specs) {
  let body = "",
    call = [],
    inputs = [],
    outputs = [];
  const capacity = s.args.some((a) => a.name === "capacity");
  const arrayInput = new Map();
  for (let i = 0; i < s.args.length; i++) {
    const a = s.args[i];
    const next = s.args[i + 1];
    if (
      a.constant &&
      a.pointer &&
      next?.type === "size_t" &&
      !next.pointer &&
      a.type !== "char"
    )
      arrayInput.set(next.name, a.name);
  }
  for (const a of s.args) {
    const n = a.name;
    if (a.type === "taiyin_context") {
      call.push("context");
      continue;
    }
    if (a.type === "taiyin_ephemeris_diagnostic") {
      call.push("&diag");
      continue;
    }
    if (arrayInput.has(n)) {
      call.push(`${arrayInput.get(n)}.size()`);
      continue;
    }
    if (n === "capacity") {
      body +=
        'const size_t capacity=astr_scalar<uint32_t>(in);if(capacity<1||capacity>100000)throw std::invalid_argument("capacity out of range");\n';
      call.push("capacity");
      inputs.push({ name: "max_results", type: "size_t", capacity: true });
      continue;
    }
    if (n === "out_count") {
      body += "size_t out_count=0;\n";
      call.push("&out_count");
      continue;
    }
    if ((a.pointer && !a.constant) || a.array) {
      if (capacity) {
        body += `std::vector<${a.type}> ${n};\n`;
        call.push(`${n}.data()`);
        outputs.push({ name: n, type: a.type, array: true });
      } else if (a.array) {
        body += `${a.type} ${n}[${a.array}]={};\n`;
        call.push(n);
        outputs.push({ name: n, type: a.type, tuple: a.array });
      } else {
        body += `${a.type} ${n}={};${new RegExp(`\\b${a.type}_init\\s*\\(`).test(all) ? `${a.type}_init(&${n});` : ""}\n`;
        call.push("&" + n);
        outputs.push({ name: n, type: a.type });
      }
      continue;
    }
    if (a.pointer && a.type === "char") {
      body += `const std::string ${n}=astr_text(in);\n`;
      call.push(n + ".c_str()");
      inputs.push({ ...a, string: true });
      continue;
    }
    if (a.pointer && s.args.some((x) => arrayInput.get(x.name) === n)) {
      body += `const size_t ${n}_size=astr_scalar<uint32_t>(in);if(${n}_size>100000)throw std::invalid_argument("input array too large");std::vector<${a.type}> ${n}(${n}_size);for(size_t i=0;i<${n}_size;++i)${structs.has(a.type) ? `astr_read(${n}[i],in);` : `${n}[i]=astr_scalar<${a.type}>(in);`}\n`;
      call.push(n + ".data()");
      inputs.push({ ...a, arrayInput: true });
      continue;
    }
    if (a.pointer) {
      const optional = n === "reference_epoch_jd" || n === "conditions";
      body += `${a.type} ${n}={};${optional ? `const bool ${n}_present=astr_scalar<uint8_t>(in)!=0;if(${n}_present)` : ""}astr_read(${n},in);\n`;
      if (n === "conditions")
        body += `if(!${n}_present)taiyin_heliacal_visibility_conditions_init(&${n});\n`;
      call.push(
        n === "conditions"
          ? "&" + n
          : optional
            ? `${n}_present?&${n}:nullptr`
            : "&" + n,
      );
      inputs.push({ ...a, record: true, optional });
      continue;
    }
    body += `const ${a.type} ${n}=${a.type === "uint64_t" ? "astr_u64(in)" : `astr_scalar<${a.type}>(in)`};\n`;
    call.push(n);
    inputs.push(a);
  }
  body +=
    'if(in.cursor!=in.count)throw std::invalid_argument("unexpected trailing astronomy input");\n';
  if (capacity)
    for (const o of outputs) body += `${o.name}.resize(capacity);\n`;
  const expression = `${s.name}(${call.join(",")})`;
  if (s.ret === "taiyin_call_result") body += `result=${expression};\n`;
  else if (s.ret === "void") body += `${expression};\n`;
  else {
    body += `const auto scalar_result=${expression};\n`;
    outputs.push({ name: "scalar_result", type: s.ret });
  }
  body += "if(taiyin_call_result_status(result)==0){\n";
  if (capacity) {
    body +=
      'if(out_count>capacity)throw std::runtime_error("native count exceeds capacity");reply->values[2]=out_count;for(size_t row=0;row<out_count;++row){\n';
  }
  for (const o of outputs) {
    const n = o.name + (o.array ? "[row]" : "");
    const pack = (x) =>
      structs.has(o.type)
        ? `astr_pack(${x},reply->values);`
        : o.type === "uint64_t"
          ? `astr_pack_u64(${x},reply->values);`
          : `reply->values.push_back(static_cast<double>(${x}));`;
    body += o.tuple
      ? `for(size_t i=0;i<${o.tuple};++i)${pack(n + "[i]")}\n`
      : pack(n) + "\n";
  }
  if (capacity) body += "}\n";
  body += "}\nbreak;\n";
  cpp += `case ${s.id}:{ // ${s.name}\n${body}}\n`;
  descriptors.push({ ...s, inputs, outputs, capacity });
}
cpp += `default:throw std::invalid_argument("unknown astronomy operation");}\ndiagnostic(diag,reply->values.data());reply->values[0]=taiyin_call_result_flags(result);reply->values[1]=taiyin_call_result_status(result);return reply.release();}catch(const std::bad_alloc&){return web_error_reply(TAIYIN_ERROR_OUT_OF_MEMORY);}catch(const std::invalid_argument&){return web_error_reply(TAIYIN_ERROR_INVALID_ARGUMENT);}catch(...){return web_error_reply(TAIYIN_ERROR_INTERNAL);}}\n}\n`;
const protocolId =
  createHash("sha256")
    .update(JSON.stringify({ schema, descriptors }))
    .digest()
    .readUInt32LE() & 0x7fffffff;
cpp = cpp.replace(
  "int taiyin_web_astronomy_version(){return 1;}",
  `int taiyin_web_astronomy_version(){return ${protocolId};}`,
);
emit(
  resolve(core, "experiments/wasm/astronomy_bridge.generated.inc"),
  cpp,
);
console.log(
  `${check ? "Verified" : "Generated"} ${specs.length} astronomy operations, ${used.size} records, protocol ${protocolId}`,
);
