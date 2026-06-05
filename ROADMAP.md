# Roadmap

This roadmap describes the current direction of `taiyin-ephemeris`. It is not a release promise; priorities may change as the runtime, data format, and downstream OpenDestiny needs evolve.

## Guiding principles

- Keep the core library source-driven: source descriptors are the source of truth, while catalogs and caches are runtime indexes over those sources.
- Keep data outside the code repository. Large ephemeris files such as OPM4, SPK/BSP, VSOP87, and oracle datasets should be supplied explicitly by callers or by a separate data package.
- Avoid building a fixed end-user application model into the ephemeris layer. Astronomy, astrology extensions, and other domain layers should be able to build on top of the same runtime.
- Prefer small, explicit APIs over a heavy central registry or implicit global scanning.
- Preserve testability without private datasets: optional oracle and external-data tests should skip cleanly when their environment variables are not set.

## 0.0.1 baseline

The first public baseline focuses on making the low-level ephemeris runtime usable and inspectable:

- OPM4 source discovery and loading.
- SPK, Chebyshev/Kepler-style, star, and custom ephemeris source plumbing.
- Global ephemeris runtime with shared catalog/cache access.
- Source descriptors that survive cache eviction and reload.
- Method priority selection, including target-specific overrides.
- Weighted cache eviction and singleflight loading.
- Apparent-position helpers for light-time, aberration, solar deflection, precession, and nutation.
- Minimal `void*`-frame runtime pipeline for caller-owned chart and scratch data.
- Optional oracle tests against private OPM4/SPK/`swisseph` setups.

## Near-term code work

### Body identity model

Keep a small public set of stable named constants for common solar-system IDs, for example:

```cpp
TAIYIN_BODY_MERCURY_BARYCENTER
TAIYIN_BODY_SUN
TAIYIN_BODY_MOON
```

Broad asteroid, satellite, and star coverage should not be hand-written into a giant header. It should come from generated catalog metadata derived from source datasets. Fixed-star access should remain catalog/name based, with runtime route IDs treated as provider-local implementation details rather than universal star IDs.

### More useful apparent-position examples

Add a small apparent-position or apparent-chart example that demonstrates a realistic call flow:

- configure the global runtime;
- register explicit source paths;
- request selected bodies for an epoch;
- compute apparent longitude/latitude/distance-style outputs;
- skip unavailable bodies cleanly when the caller's local data package is partial.

The goal is still an ephemeris/runtime example, not a full chart application layer.

### Runtime fallback and priority tests

Continue strengthening tests around:

- method priority selection;
- target-specific overrides;
- partial source availability;
- cache eviction and reload behavior;
- custom source lifecycle behavior.

## Data packaging direction

A separate data repository or data package is expected for large external datasets. The code repository should not embed private or large ephemeris data.

A future data package may contain:

```text
README.md
DATA_SOURCES.md
MANIFEST.json
CHECKSUMS.sha256
scripts/
opm4/
spk/
vsop87/
```

The manifest/catalog idea is intentionally deferred until the data layout is more stable. Conceptually, it would be a prebuilt source catalog: a file that describes which ephemeris sources exist in a data package, where they are located, what they cover, and how they should be registered.

For now, explicit source paths and environment-variable-driven tests remain the supported path.

## Medium-term possibilities

These are useful directions, but not immediate commitments:

- A lightweight manifest loader for prebuilt source catalogs.
- Checksum verification for data packages.
- More complete public documentation for source descriptors and method selection.
- Additional apparent-position accuracy sweeps across bodies, epochs, and source types.
- CI that verifies the public no-private-data build and test path.
- Optional examples for downstream domain layers, kept separate from the core runtime.

## Non-goals

The following are not planned for the core ephemeris runtime:

- Bundling full ephemeris datasets in the source repository.
- Automatically downloading third-party datasets from inside the library.
- Redistributing datasets whose licenses do not clearly permit it.
- A fixed chart/application struct, school model, house-system model, or astrology-specific application framework.
- A heavy plugin registry that replaces explicit source descriptors and caller-owned pipeline data.
