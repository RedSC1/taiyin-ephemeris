# taiyin-ephemeris

[中文版](README_CN.md) · [Roadmap](ROADMAP.md)

`taiyin-ephemeris` is the ephemeris runtime layer for OpenDestiny. Version `0.0.1` focuses on source loading, catalog selection, shared caching, apparent-position calculation, and a small runtime pipeline primitive. It is intentionally not a full end-user application framework; astronomy, astrology extensions, and other domain layers can be built on top.

## 0.0.1 scope

This release is meant to make the low-level ephemeris runtime usable and testable:

- OPM4 ephemeris source discovery and loading.
- SPK, Chebyshev/Kepler-style, star, and custom ephemeris source plumbing.
- A global ephemeris runtime shared by callers.
- A global catalog/cache path where source descriptors remain the source of truth and cache entries can be evicted/reloaded.
- Method priority selection, including target-specific overrides.
- Weighted cache eviction and singleflight loading.
- Custom ephemeris source registration and lifecycle tests.
- Apparent-position helpers with light-time, aberration, solar deflection, precession, and nutation support.
- A minimal `void*`-frame runtime pipeline for user-defined chart structures.

## Non-goals for 0.0.1

The current MVP deliberately avoids building a fixed downstream application model:

- Ephemeris source paths are explicit. There is no default system scan yet.
- Full ephemeris datasets are not embedded in the library.
- The library does not prescribe a chart/application struct, school/branch model, house system model, or domain extension registry.
- Minor bodies and asteroids depend on the source files the caller provides.
- The runtime pipeline does not own typed artifacts or connect step outputs automatically. The caller owns chart/scratch data and step wrappers.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Build only the bare-chart pipeline example:

```sh
cmake --build build --target example_bare_chart_pipeline
```

## Quickstart: global ephemeris runtime

The runtime is initialized once, then explicit source paths are added to the global catalog. The catalog remembers source descriptors; the cache only stores loaded/evaluated blocks and can evict entries without losing the source-of-truth path.

```cpp
#include "taiyin/runtime/taiyin_runtime.h"

using namespace taiyin::internal;
using namespace taiyin::runtime;

EphemerisRuntimeConfig config;
config.cache_max_bytes = 512 * 1024 * 1024;
initialize_global_ephemeris_runtime(config);

add_global_ephemeris_source_path("/path/to/data_integrated_opm4");

EphemerisRequest request;
request.target_id = 1;   // Mercury
request.center_id = 10;  // Sun
request.frame = EphemerisFrame::IcrfJ2000Equatorial;
request.jd_tdb = 2460310.500800740905;

EphemerisResult result;
if (eval_global_ephemeris_state(request, &result)) {
    // result.state.position_au
    // result.state.velocity_au_per_day
    // result.descriptor.method_id
    // result.cache_hit
}
```

## Quickstart: pipeline

The pipeline is intentionally small. It only passes three caller-owned pointers through each step:

```cpp
struct PipelineFrame {
    void* chart;
    void* scratch;
    void* user_data;
};
```

A step is a function pointer:

```cpp
typedef bool (*PipelineStepFn)(PipelineFrame* frame, void* step_data);
```

The recommended pattern is:

1. Define your own chart struct.
2. Define your own scratch/intermediate struct.
3. Write small wrapper steps that cast `frame->chart`, `frame->scratch`, and `step_data` back to your typed data.
4. Keep the actual math/runtime calls strongly typed inside those wrappers.

See the complete example:

```text
examples/bare_chart_pipeline.cpp
```

Run it with an explicit OPM4 source path:

```sh
./build/example_bare_chart_pipeline /path/to/data_integrated_opm4
```

or:

```sh
TAIYIN_OPM4_ROOT=/path/to/data_integrated_opm4 ./build/example_bare_chart_pipeline
```

The example tries to build a minimal user-defined chart with:

```text
Mercury, Venus, Mars, Jupiter, Saturn, Uranus, Neptune, Pluto, Moon
```

Bodies that are not available in the caller's source data are skipped with a warning so the example remains useful across partial local datasets.

It demonstrates this flow:

```text
OPM4 source path
        |
        v
global ephemeris runtime
        |
        v
PipelineFrame(chart/scratch)
        |
        +--> eval_bodies_step
        +--> project_longitudes_step
        +--> write_chart_step
        |
        v
user-defined BareChart
```

This example is intentionally not a complete chart calculation. It demonstrates source-backed runtime evaluation and the user-owned pipeline pattern. Production chart code can add apparent-position correction, houses, aspects, fixed stars, asteroids, or school-specific steps without changing the pipeline runner.

## Architecture

The core runtime is source/catalog/cache oriented:

```text
explicit source paths / custom descriptors
        |
        v
EphemerisBlockCatalog
        |
        v
EphemerisService
   |                 |
   v                 v
priority selection   EphemerisBlockCache
                     weighted eviction
                     singleflight loading
        |
        v
EphemerisResult
        |
        v
user code or PipelineFrame(chart/scratch/user_data)
```

Key rule: source files and descriptors are the source of truth. Cache eviction should not lose the ability to reload a source-backed block.

## Accuracy checks

The test suite includes Swiss Ephemeris (`swisseph`) oracle coverage for the apparent OPM4 path. With the current private OPM4 dataset and `swisseph` oracle setup, the tested major planets and Moon match `swisseph` apparent positions at roughly `0.0001` to `0.0013` arcseconds over selected epochs.

That number is an oracle-test result for the tested bodies, epochs, flags, and data versions. It should not be read as a universal guarantee for every source file, asteroid, epoch, or caller-defined pipeline.

## Tests

Important test groups include:

- `test_global_ephemeris_runtime` — global runtime initialization, explicit source paths, catalog/cache access.
- `test_ephemeris_mvp_end_to_end` — source/catalog/cache/runtime MVP behavior.
- `test_custom_source_lifecycle` — custom source unregister/delete/cache-hit/reload behavior.
- `test_pipeline` — minimal pipeline runner mechanics.
- `test_pipeline_bare_chart` — mock multi-method bare chart pipeline.
- `test_pipeline_opm4_vs_swiss` — OPM4-backed apparent pipeline oracle against `swisseph`.
- `test_apparent_opm4_vs_swiss` — direct apparent-position OPM4 oracle against `swisseph`.

Oracle and external-data tests are optional. They skip themselves unless the relevant environment variables are set, for example:

```text
TAIYIN_DE441_PATH
TAIYIN_OPM4_ROOT
TAIYIN_SWISS_PYTHON
TAIYIN_SWISS_EPHE
TAIYIN_VSOP87_MERCURY_PATH
TAIYIN_MER404_TS_PATH
TAIYIN_MAIN_BELT_ASTEROIDS_SPK_PATH
TAIYIN_NEAR_EARTH_ASTEROIDS_SPK_PATH
TAIYIN_JUPITER_SATELLITES_SPK_PATH
TAIYIN_SATURN_SATELLITES_SPK_PATH
```

Run everything with:

```sh
ctest --test-dir build --output-on-failure
```

## License

Copyright 2026 RedSC1.

Licensed under the [Apache License 2.0](LICENSE).
