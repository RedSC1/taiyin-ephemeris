# taiyin-ephemeris

[中文版](README_CN.md) · [Docs](docs/index.md) · [Roadmap](ROADMAP.md)

`taiyin-ephemeris` is an ephemeris runtime library. It loads ephemeris data,
selects runtime routes, manages source-segment caches, and exposes astronomical
position, visibility, event, eclipse, occultation, heliacal, and astrology
extension calculations through C++ and a versioned C99 ABI.

Library version `1.0.0` is the first private core baseline. C ABI version 5 is
the compatibility boundary for FFI and applications. The C++ API remains an
implementation-facing interface and is not a stable binary ABI.

## Status

Current baseline:

- Core runtime, catalog, routing, source segment cache, and body evaluator
  architecture is active.
- Native and C APIs cover positions, states, observed coordinates, fixed stars,
  visibility, phenomena, event search, eclipses, occultations, heliacal
  visibility, orbital events, and local solar time.
- The astrology extension covers sidereal positions, ayanamsha models, houses,
  lunar nodes/apsides, fitted lunar apogee, and custom model registration.
- OPM2/OPC, SPK, the built-in semi-analytical model, TKC1/Kepler, TSC1/TSF1,
  and custom target routes are active.
- Legacy pipeline code and the old TSCA built-in star catalog are out of the
  active build and archived under `plans/legacy/`.
- The default test suite passes without private oracle data.
- C ABI v5 is frozen under the contract documented in
  [`docs/c_api.md`](docs/c_api.md).

## What Works

### Runtime And Data Loading

- Explicit source registration and packaged data-root loading.
- Source descriptors as the source of truth.
- Entry-count segment cache with eviction and reload.
- Singleflight route loading to avoid duplicated cache misses.
- Route-rule selection through built-in or user-registered route-rule tables.
- Runtime body evaluators for composite/built-in bodies such as Earth and Moon.
- OPM2 source reading and persistent OPC catalog/index support.
- SPK, Kepler/TKC1, built-in semi-analytical, TSC1/TSF1, and custom source
  plumbing.

### C ABI And FFI

- Versioned C99 umbrella API under `include/taiyin/c/`.
- Shared library target `taiyin_c` and source-tree static target
  `taiyin_c_static`.
- Opaque calculation contexts, explicit ownership, `struct_size`-guarded
  records, diagnostics, capability queries, and callback registration.
- Custom native targets, ayanamsha models, and house systems can be registered
  without exposing C++ types across the FFI boundary.
- See [`docs/c_api.md`](docs/c_api.md).

### Major-Body Positions

- `calc_position_tt`, `calc_position_ut`, `calc_position_utc`, and batch
  variants. `*_tdb` entry points remain available for expert and test code that
  already works in ephemeris time.
- `NativeCalcContext` for user-owned state such as observer, atmosphere, models,
  deflectors, and time-scale policy.
- Flag-driven output selection for spherical/XYZ, equatorial/ecliptic, radians,
  speed, true position, astrometric position, aberration, deflection, and
  topocentric output.
- Apparent calculation support for light-time, Shapiro delay, annual
  aberration, solar/multi-body deflection, precession, nutation, obliquity, and
  selectable output frames.

### Observed Positions

- `calc_observed_ut` for major bodies.
- `calc_observed_utc` for major bodies when a leap-second table and EOP table
  are attached to `NativeCalcContext`.
- User observer location stored in `NativeCalcContext`.
- Topocentric observer offset computed as per-call scratch.
- Precise UTC/EOP routing covers leap seconds, DUT1, polar motion, and CPO
  offsets for the observed UTC entry.
- Horizontal azimuth/altitude output.
- Optional refraction through the selected refraction model.
- Refraction field validation by model: Bennett-like models require pressure
  and temperature; SOFA also requires humidity and wavelength.

### Fixed Stars

- Global TSC1/TSF1 star-catalog registration.
- `calc_star_position_*` and `calc_star_positions_*` for catalog astrometry,
  proper motion, radial velocity/parallax handling, frame routing, spherical/XYZ
  output, and optional speed.
- `calc_observed_star_ut` and `calc_observed_stars_ut` using the same
  `TAIYIN_OBSERVED_*` flags as major bodies.
- Observed fixed-star support for geocentric/topocentric parallax, solar
  deflection, annual aberration, horizontal coordinates, and refraction.
- String-first star lookup: user keys and aliases resolve to catalog rows. HIP,
  HR, HD, and Gaia DR3 identifiers are metadata, not runtime body IDs.
- Provider `runtime_id` values are private cache/diagnostic keys, not portable
  star identifiers.

### Event Search

- Low-level longitude, relative-longitude, lunar-phase, exact-aspect, and
  station searches live in `include/taiyin/runtime/event_search.h`.
- Event-search `uint64_t flags` keeps native position flags in the low 32 bits
  and event-search flags in the high 32 bits.
- The core runtime returns raw numerical events: JD, longitude, and target
  angle where relevant. Domain wrappers such as solar terms, sign ingress,
  planetary returns, named aspects, and retrograde labels are intentionally
  left for downstream modules.
- See [`docs/event_search.md`](docs/event_search.md) for the API boundary,
  step-selection rules, and current behavior.

### Eclipse Search

- Lunar eclipse solve/search, global solar eclipse solve/search, local solar
  eclipse circumstances, and solar route/path helpers live in
  `include/taiyin/runtime/eclipse_search.h`.
- Eclipse search uses Meeus chapter 52 as a lunation pre-filter, then refines
  final geometry with the configured runtime ephemerides.
- Lunar shadow and Moon-radius conventions are selected on `NativeCalcContext`;
  see [`docs/eclipse_search.md`](docs/eclipse_search.md) for algorithm sources,
  model conventions, validation notes, and usage examples.

### Occultation Search

- First lunar fixed-star and lunar solar-system-body occultation next-search
  APIs live in `include/taiyin/runtime/occultation_search.h`.
- Geocentric and local/topocentric searches are available for named fixed stars
  and supported solar-system body targets.
- Results include maximum-occultation / minimum-separation time and contact
  times: C1/C4 for point-star targets, C1-C4 for body targets when inner
  contacts exist.
- Local visibility summary helpers can sample Moon, target, and Sun
  altitude/azimuth at contacts and maximum occultation, with optional
  refraction.
- See [`docs/occultation_search.md`](docs/occultation_search.md) for API
  shape, current seed/refine behavior, and known limits.

## Future Work

The 1.0 baseline is usable, but these areas remain future work:

- Broader observed-position oracle sweeps for precise EOP/CPO, CIRS/equinox
  route comparisons, horizontal azimuth convention, and refraction convention.
- External fixed-star oracle sweeps against independent catalog/runtime outputs.
- More physical target-disc metadata, irregular limb/terrain models, and
  higher-fidelity photometry/sky-brightness models.
- Complete chart-object assembly and school-specific interpretation layers.
- Dart, Python, and JavaScript bindings over the stable C ABI.
- Cross-platform prebuilt binary packages, signing, and public data packages.
- Third-party-compatible API/ABI layer. If added, it should live in a separate
  compatibility project so licensing and semantic compatibility concerns do not
  leak into the core runtime.
- Large bundled ephemeris datasets in this source repository.

## Non-Goals

The core runtime deliberately does not provide:

- broad system or home-directory source scanning;
- a fixed chart/application struct;
- a low-level workflow/pipeline engine;
- full data distribution for large SPK/OPM/oracle datasets.

School-specific interpretation, Arabic parts, complete chart assembly, and
application presentation belong in downstream applications. Astrology
calculation primitives remain isolated in `taiyin_astrology_extension`.

## Architecture

Application-facing code should start at the native APIs:

```text
NativeCalcContext + typed calculation functions
        |
        v
major-body / fixed-star apparent and observed helpers
        |
        v
EphemerisEngine
        |
        +--> EphemerisBlockCatalog
        +--> EphemerisSegmentCache
        +--> EphemerisRouteRule tables
        +--> EphemerisBodyRegistry
        |
        v
source files and descriptors
```

Use `EphemerisEngine` directly only for raw state evaluation, route-selection
tests, cache tests, provider tests, or internal tooling.

The key runtime rule is:

```text
source descriptors remain the source of truth;
compiled-block cache entries are reloadable derived state.
```

Evicting a cache entry must not lose the ability to reload a source-backed
block.

## Data Model

Supported data families currently include:

- OPM2 files for packaged numerical ephemeris segments.
- OPC catalogs for persistent packaged-data discovery.
- SPK files for external kernel-backed routes.
- TKC1/Kepler-style catalog data.
- Frozen built-in semi-analytical fallback for Mercury through Pluto, EMB,
  Earth, and Moon over calendar years -3000 through +3000.
- TSC1 precision fixed-star catalogs.
- TSF1 user fixed-star files, converted through the TSC1 provider path.

Large raw datasets are intentionally kept out of the source repository. Use
explicit data roots or external data packages.

## Quickstart

```cpp
#include "taiyin/body_id.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

using namespace taiyin;
using namespace taiyin::runtime;

EphemerisRuntimeConfig config;
config.segment_cache_max_entries = 512;
config.data_root = "/path/to/taiyin/data";
initialize_global_ephemeris_runtime(config);

NativeCalcContext context;
const int bodies[] = {
    TAIYIN_BODY_SUN,
    TAIYIN_BODY_MOON,
    TAIYIN_BODY_MERCURY_BARYCENTER,
};

const double jd_ut = julian_day({2024, 1, 1, 12, 0, 0.0});
double positions[3][6];
EphemerisEvalDiagnostic diagnostics[3];

const Status status = calc_positions_ut(
    &context,
    bodies,
    3,
    jd_ut,
    TAIYIN_NATIVE_POSITION_RADIANS,
    &positions[0][0],
    diagnostics);
```

`positions[i][0..2]` contains longitude, latitude, and distance for spherical
output. Add `TAIYIN_NATIVE_POSITION_SPEED` to request rates in `positions[i][3..5]`.

## Fixed-Star Quickstart

```cpp
#include "taiyin/runtime/star_position.h"

add_global_tsc1_star_catalog("/path/to/catalog.tsc1");

const char* stars[] = {"spica", "HIP 65474"};
double star_positions[2][6];

calc_star_positions_ut(
    &context,
    stars,
    2,
    jd_ut,
    TAIYIN_NATIVE_POSITION_RADIANS,
    &star_positions[0][0],
    nullptr);
```

For topocentric, horizontal, or refraction output, use the observed star API:

```cpp
ObservedPosition observed[2];

calc_observed_stars_ut(
    &context,
    stars,
    2,
    jd_ut,
    TAIYIN_OBSERVED_TOPOCENTRIC
        | TAIYIN_OBSERVED_HORIZONTAL
        | TAIYIN_OBSERVED_REFRACTION,
    observed,
    nullptr);
```

## Examples

```sh
cmake --build build --target example_apparent_ut_chart_table
./build/example_apparent_ut_chart_table data
```

Prints a geocentric apparent ecliptic bare-chart table.

```sh
cmake --build build --target example_observed_ut_bare_chart
./build/example_observed_ut_bare_chart data
```

Runs the observed UT path with an observer context and horizontal output.

```sh
cmake --build build --target example_observed_utc_eop_bare_chart
./build/example_observed_utc_eop_bare_chart data
```

Runs the precise observed UTC path with built-in leap seconds, built-in
finals2000A EOP data, a Beijing observer context, horizontal output, and
refraction.

## Build And Test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

BaZi is an optional Pascal-backed extension. It also enables the Ganzhi
calendar extension and requires a compatible Free Pascal compiler:

```sh
cmake -S . -B build-bazi \
  -DTAIYIN_BUILD_BAZI_EXTENSION=ON
cmake --build build-bazi
ctest --test-dir build-bazi --output-on-failure
```

Only enabled builds install `taiyin/c/bazi.h` and export `taiyin_bazi_*`.
Applications using the extension include that header explicitly rather than
relying on `taiyin/c/taiyin.h`.

Important local tests include:

- `test_ephemeris_catalog`
- `test_ephemeris_segment_cache`
- `test_ephemeris_route_rule`
- `test_custom_ephemeris_method`
- `test_event_search`
- `test_occultation_search`
- `test_body_registry`
- `test_apparent_position`
- `test_apparent_position_oracles`
- Private compatibility/oracle sweeps live under `private/` and are kept out of the default public build.
- `test_star_file`
- `test_tsc1_catalog_discovery`
- `test_opc_catalog_persistent`

Optional oracle and external-data tests skip themselves unless relevant
environment variables are set:

```text
TAIYIN_DE441_PATH
TAIYIN_OPM2_DATA_DIR
TAIYIN_NASA_BSP_ROOT
TAIYIN_MER404_TS_PATH
TAIYIN_MAIN_BELT_ASTEROIDS_SPK_PATH
TAIYIN_NEAR_EARTH_ASTEROIDS_SPK_PATH
TAIYIN_JUPITER_SATELLITES_SPK_PATH
TAIYIN_SATURN_SATELLITES_SPK_PATH
```

The optional `spk_opm2_jplephem_oracles` test uses jplephem-backed SPK
references for both SPK reader checks and packaged OPM2 major-body/asteroid
comparisons. It prints actual position, angular, and velocity deltas when run
directly.

## Documentation

- [`ROADMAP.md`](ROADMAP.md) — direction and priority order.
- [`docs/current_limitations.md`](docs/current_limitations.md) — current gaps and known limitations.
- [`docs/event_search.md`](docs/event_search.md) — low-level event-search API and extension boundary.
- [`docs/eclipse_search.md`](docs/eclipse_search.md) — eclipse algorithms, model conventions, and API examples.
- [`docs/solar_eclipse_export.md`](docs/solar_eclipse_export.md) — raw versioned JSON export for eclipse maps and almanac generators.
- [`docs/occultation_search.md`](docs/occultation_search.md) — lunar occultation search and local visibility summary APIs.
- [`docs/catalog_cache_model.md`](docs/catalog_cache_model.md) — descriptor catalog versus compiled block cache.
- [`docs/opc_catalog_format.md`](docs/opc_catalog_format.md) — persistent OPC catalog format.
- [`docs/ephemeris_runtime_architecture.md`](docs/ephemeris_runtime_architecture.md) — lower-level runtime/cache architecture.
- [`docs/tsc1_v1_known_limitations.md`](docs/tsc1_v1_known_limitations.md) — fixed-star catalog limitations.

## Legacy Code

Archived implementation paths live under `plans/legacy/`:

- `plans/legacy/pipeline_runner/` — rejected low-level workflow pipeline.
- `plans/legacy/star_tsca/` — old TSCA built-in fixed-star prototype.

These are kept for history and should not be reintroduced into the active build.

## License

Copyright 2026 RedSC1.

Taiyin's original code and documentation are licensed under the [Mozilla Public
License 2.0](LICENSE). Third-party code and packaged data remain subject to the
licenses and terms recorded in [`NOTICE`](NOTICE) and the adjacent data
provenance files.
