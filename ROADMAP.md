# Roadmap

Status: Current
Last reviewed: 2026-08-09

Taiyin `1.0.0` is the first core baseline. The runtime and C ABI are
feature-complete enough for downstream bindings and applications; future work
should not delay on speculative compatibility or chart-framework features.

## 1.0 Baseline

The active architecture is:

```text
C ABI / typed C++ functions
        |
        v
NativeCalcContext + model and position flags
        |
        v
apparent / observed / event geometry
        |
        v
EphemerisEngine
        |
        v
source catalog + segment cache + route rules + body evaluators
```

The baseline includes:

- OPM2/OPC, SPK, the built-in semi-analytical model, TKC1/Kepler, TSC1/TSF1,
  and custom target sources;
- TDB/TT/UT/UTC positions and states, apparent and observed coordinates,
  topocentric/EOP routing, fixed stars, refraction, and model selection;
- visibility, phenomena, heliacal visibility, orbital events, local solar time,
  and generic event searches;
- solar/lunar eclipses, route products, lunar occultations, contact and local
  visibility calculations, and optional lunar-limb correction;
- sidereal positions, ayanamsha, ASC/MC, house systems, lunar nodes/apsides,
  fitted lunar apogee, and custom astrology model registration;
- stable C99 ABI version 7 for FFI and application integration.

The exact-JD shared result cache has been removed. Shared runtime caching is
limited to reusable source segments and route-loading coordination; user-level
memoization belongs in downstream contexts or bindings.

## Compatibility Policy

- The C ABI is the compatibility boundary. ABI major 7 keeps existing symbols,
  flag meanings, and structure contracts compatible.
- Structure growth uses trailing fields guarded by `struct_size`.
- C++ headers remain implementation-facing and are not a stable binary ABI.
- Numerical model IDs and flags must fail explicitly when unknown; no silent
  reinterpretation of unsupported values.
- Source descriptors remain the source of truth. Cache eviction must not lose
  reloadability.
- Large data remains outside the core binary and is loaded from explicit roots,
  packages, or caller-managed paths.

## Next Work

### Bindings And Packaging

1. Dart FFI wrapper and Flutter platform packaging.
2. Python direct C++ binding through pybind11, with Python-owned localization
   and input normalization rather than native DLL discovery.
3. JavaScript/Node.js binding after the native package layout is stable.
4. CI-built macOS, Linux, Windows, Android, and iOS binaries with install/package
   smoke tests, symbol checks, checksums, signing, and notices.

### Validation

- Broader independent position, EOP/CPO, fixed-star, and refraction oracles.
- Boundary regressions for grazing occultations, high-latitude eclipses, route
  polygons, and optional lunar-limb correction.
- Release benchmark suites on x86-64, Apple Silicon, Android ARM64, and a
  constrained mobile-class CPU.
- ABI symbol and structure-layout snapshots for every distributed platform.

### Model Extensions

- Physical target-disc metadata: oblateness, rings, small-body diameter, and
  caller-supplied profiles.
- More complete sky-brightness, meteorology, and lunar-crescent visibility
  models.
- Optional higher-resolution lunar-limb products. Earth terrain correction is
  deferred until a practical data/distribution model exists.
- Additional astrology schools and application semantics remain downstream
  concerns unless they are reusable calculation primitives.

## Public Distribution Gate

The source tree is published through the public repository snapshot; binary
distribution remains deferred. Before distributing binaries externally:

- run the full platform matrix and package-install smoke tests;
- compare exported symbols with the ABI v2 baseline;
- include `LICENSE`, `NOTICE`, and packaged-data provenance;
- publish data checksums and model/source identifiers;
- prepare a changelog, migration guide, support window, and security contact.

The detailed binary-release checklist is maintained in the private development
repository and is not part of this public source snapshot.

## Non-Goals

- A hidden workflow/pipeline engine for numerical kernels.
- Automatic scanning of arbitrary system or home directories.
- A fixed chart/application result object in the astronomy core.
- Bundling every large SPK, OPM, catalog, terrain, or oracle dataset in the
  source repository.
- Copying a third-party ABI or preserving its implementation-specific errors and
  flag layout inside the core library.
