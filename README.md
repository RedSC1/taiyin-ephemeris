# Taiyin Ephemeris（太阴星历）

[中文版 README](README_CN.md) · [Documentation](docs/index.md) · [Roadmap](ROADMAP.md)

> **Pre-release notice:** The current `1.0.0-preview.6` version identifies an
> in-development pre-release, not a final production release. Public APIs,
> packaged data boundaries, documentation, and the planned language bindings
> may still change before the first stable release.

Taiyin Ephemeris is an embeddable astronomy library for computing Solar System
positions, observer coordinates, visibility, astronomical events, eclipses,
occultations, fixed-star positions, calendars, and local solar time. It is
written in C++ and also provides a versioned C99 ABI for applications and FFI
bindings. Python, Dart, and JavaScript wrappers are under active development;
their public APIs and distribution packages are not yet released.

> **Civil-time limitation:** Taiyin's native calendar and Ziwei layers use a
> fixed UTC offset (or an explicit mean-solar meridian), not a named time-zone
> database. In countries with daylight-saving/standard-time transitions, a new
> moon or solar term near a transition can receive the wrong local civil-day
> label under actual legal time. The astronomical instant remains valid; do not
> treat its native lunar-date assignment as a legal-time reproduction there.

The library uses the OPM2 ephemeris format together with a built-in
semi-analytical ephemeris. For a typical major-body OPM2 product, the
compression/reconstruction difference from its source DE441 or DE442 ephemeris
is on the order of **0.001 arcsec**. This describes the OPM2 state-compression
error, not the final apparent or topocentric result, which also depends on time
scales, observer geometry, and the selected correction models.

The repository packages two major-body OPM2 products for the Sun, Moon,
Mercury, Venus, EMB, Mars, Jupiter, Saturn, Uranus, Neptune, and Pluto:

- a DE441-derived 600-year product for **1800-01-01 through 2400-01-01**;
- a DE442-derived full-coverage product over DE442's common source interval,
  approximately **1550 through 2650**.

The runtime recognizes their product identities and, where coverage overlaps,
AUTO selects the DE442-derived OPM2 product by default. Applications can still
select a specific source or change its provider-local priority when
reproducibility requires the older DE441 product.

Taiyin can also read current NASA/JPL SPK files, including DE441 and SPK files
for asteroids and other targets. Here, “current” refers to using the current
Delta-T and time-scale treatment paired with the DE441-era data. Taiyin does
not implement the historical tidal-acceleration correction parameters used by
DE431-era historical ephemeris tables, so it should not be presented as a DE431
historical-reproduction implementation.

When no higher-priority SPK or OPM2 source is available, the built-in
semi-analytical fallback covers approximately calendar years **-3000 through
+3000**. See [`docs/semi_analytic_ephemeris.md`](docs/semi_analytic_ephemeris.md)
for its model and validation details.

### Built-in satellite fallback scope

The data-free route also includes Phobos and Deimos, the four Galilean moons,
the Pluto system, and Triton over their separately declared validation
intervals. These are compact fallback models, **not precision satellite
ephemerides**: their relative-position errors range from tens of kilometres
for some fitted residual tables to several hundred kilometres for the compact
Galilean model. Their mass-weighted planet-center corrections are often much
smaller. Use a matching SPK or OPM2 data package when satellite astrometry or
phenomena require precision.

No data-free Saturnian or Uranian satellite route is registered in 1.0. Their
major-moon systems need either a dedicated validated theory or an external
satellite data package; Taiyin does not silently substitute a barycenter for a
requested physical planet unless the caller explicitly enables that fallback.

## What You Can Build

- **Astronomical positions:** apparent, astrometric, topocentric, equatorial,
  ecliptic, spherical, Cartesian, and velocity outputs.
- **Observer calculations:** UTC/UT routes, horizontal coordinates, atmosphere
  and refraction, leap seconds, EOP, polar motion, and celestial pole offsets.
- **Events and visibility:** rise/set, twilight, transit, lunar phases, angular
  events, planetary stations, heliacal visibility, and orbital events.
- **Eclipses and occultations:** global and local solar eclipses, lunar eclipses,
  lunar occultations, contacts, circumstances, and visibility summaries.
- **Fixed stars:** TSC1/TSF1 catalogs, aliases, astrometry, proper motion,
  parallax, observed positions, and horizontal coordinates.
- **Calendar and astrology extensions:** Chinese calendar primitives,
  sidereal positions, ayanamsha, houses, lunar nodes and apsides, plus optional
  BaZi/Ganzhi support.
- **Application integration:** a versioned C99 API with opaque contexts,
  diagnostics, capability queries, and FFI-friendly ownership rules. C ABI
  version 8 is the application compatibility boundary; the C++ API is not a
  stable binary ABI.

### Observer Scope in 1.0

Topocentric, horizontal-coordinate, and atmospheric-refraction calculations
are supported only for observers on Earth. A topocentric request with a
non-Earth `observer_id` returns `TAIYIN_ERROR_UNSUPPORTED`. Fixed-star position,
observed-star, and body-star search APIs also require an Earth observer in 1.0.
Non-topocentric observer-relative Solar System calculations remain available
when the selected ephemeris route provides the required states.

## Data Sources

Taiyin separates the runtime from large ephemeris files. You can use packaged
data, register an explicit data root, or add external source files such as SPK
kernels and fixed-star catalogs. The runtime can select an available source for
a target and time range while keeping the source data outside your application
binary.

The repository includes the DE441 600-year and full DE442 major-body OPM2
products, selected asteroid OPM2 data, and compact center-of-body corrections.
Other external datasets remain separately selectable; see
[`docs/current_limitations.md`](docs/current_limitations.md) for coverage and
data-package boundaries.

## Quick Start

Build the library and its tests:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

### CMake presets

`CMakePresets.json` keeps the local and CI toolchain choices aligned. It
requires CMake 3.21 or newer; the project itself continues to support its
declared CMake 3.16 minimum when configured with ordinary command-line
arguments. The source baseline is C++11 (and C99 for C), with compiler choice
made before `CMakeLists.txt` is evaluated.

The `modular-bazi` preset is the normal native validation build:

```sh
cmake --preset modular-bazi
cmake --build --preset modular-bazi
ctest --preset modular-bazi
```

Use `linux-gcc`, `linux-clang`, `macos-appleclang`, `windows-mingw-gcc`,
`windows-llvm-mingw-arm64`, or `windows-msvc` to select the corresponding host
compiler. On Windows x64, MinGW-w64 GCC is the recommended release toolchain;
on Windows ARM64, use llvm-mingw Clang/LLD. Both match the published Python
wheels. Visual Studio 2022/MSVC remains a compatibility target covered by CI;
it is supported on a best-effort basis rather than used to gate Windows wheel
releases. `android-arm64` uses the NDK toolchain and therefore requires the
`ANDROID_NDK` environment variable:

```sh
cmake --preset android-arm64
cmake --build --preset android-arm64
```

Presets choose the toolchain; `CMakeLists.txt` only adapts target properties to
the already selected compiler and platform.

Maintainer-only generators, benchmarks, and fit experiments are excluded from
normal builds. In the private development tree, enable them explicitly with
`-DTAIYIN_BUILD_MAINTAINER_TOOLS=ON`; they are intentionally absent from the
public source snapshot.

A minimal C++ calculation looks like this:

```cpp
#include "taiyin/body_id.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

using namespace taiyin;
using namespace taiyin::runtime;

EphemerisRuntimeConfig config;
config.data_root = "/path/to/taiyin/data";
if (!initialize_global_ephemeris_runtime(config)) {
    return 1;
}

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

For spherical output, `positions[i][0..2]` contains longitude, latitude, and
distance. Add `TAIYIN_NATIVE_POSITION_SPEED` to request rates in
`positions[i][3..5]`. See [`docs/c_api.md`](docs/c_api.md) for the stable C
interface and the native API headers for typed C++ entry points.

## Build Structure And Optional Extensions

The source tree keeps `taiyin_ephemeris`, `taiyin_astrology_extension`, and
the Chinese-calendar static targets separate so their internal dependencies are
explicit. Astrology, Chinese-calendar, and Ganzhi functionality are **built
in**: they are always built and are always linked into the base C ABI library
(`taiyin` in a modular build, or `taiyin_c` in the legacy aggregate build).
`taiyin_astrology_extension` is therefore a source-tree C++ link target, not a
separately selectable package or shared library.

Direct C++ source-tree consumers link `taiyin_ephemeris` for core astronomy and
also link `taiyin_astrology_extension` when calling sidereal, house, or
lunar-point C++ entry points. The C++ ABI itself is not a stable distribution
interface; application and FFI consumers should use the base C ABI instead.

Chinese metaphysics extensions are disabled by default. Without
  `TAIYIN_BUILD_CHINESE_METAPHYSICS_EXTENSIONS=ON`, CMake does not build BaZi
  code, targets, tests, C ABI symbols, or headers.
- BaZi is the only currently optional native extension. To build it, explicitly enable both
  `-DTAIYIN_BUILD_CHINESE_METAPHYSICS_EXTENSIONS=ON` and
  `-DTAIYIN_BUILD_BAZI_EXTENSION=ON`; no extra compiler toolchain is required.

Ganzhi is built into the Chinese-calendar implementation because it supplies
the calendrical year/month/day/hour cycle rather than a metaphysics
interpretation. Future Qimen or Liuren extensions will use their own opt-in
options under the same metaphysics gate.

The extension documentation is listed in [`docs/index.md`](docs/index.md),
including [sidereal astrology](docs/astrology_sidereal.md),
[houses](docs/astrology_houses.md), [lunar points](docs/astrology_lunar_points.md),
and the [Chinese calendar](docs/chinese_calendar.md).
The optional metaphysics layer is documented separately in
[BaZi](docs/bazi.md).

## Documentation

- [`docs/index.md`](docs/index.md) — documentation hub and API map.
- [`docs/c_api.md`](docs/c_api.md) — versioned C99 ABI and FFI contract.
- [`docs/current_limitations.md`](docs/current_limitations.md) — coverage,
  data-package, and known-behavior boundaries.
- [`docs/bazi.md`](docs/bazi.md) — optional BaZi build, ownership, calculation,
  and validation contract.
- [`docs/event_search.md`](docs/event_search.md) — event-search primitives.
- [`docs/solar_visibility.md`](docs/solar_visibility.md) — solar rise/set,
  twilight, transit, fast paths, and refraction conventions.
- [`docs/eclipse_search.md`](docs/eclipse_search.md) — eclipse algorithms and
  API examples.
- [`docs/occultation_search.md`](docs/occultation_search.md) — lunar occultation
  searches and local visibility summaries.
- [`docs/semi_analytic_ephemeris.md`](docs/semi_analytic_ephemeris.md) — built-in
  fallback model and validation.
- [`docs/ephemeris_runtime_architecture.md`](docs/ephemeris_runtime_architecture.md)
  — lower-level runtime design.

## License

Copyright 2026 RedSC1.

Taiyin's original code and documentation are licensed under the [Mozilla Public
License 2.0](LICENSE). Third-party code and packaged data remain subject to the
licenses and terms recorded in [`NOTICE`](NOTICE) and the adjacent provenance
files. See the [third-party software and data overview](docs/third_party.md) for
a user-facing source index.
