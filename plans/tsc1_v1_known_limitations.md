# TSC1 v1 Known Limitations

This document records known limitations and intentional trade-offs in the first TSC1 precision-star catalog implementation.

## Scope

TSC1 v1 is intended to get the first precision star tiers running locally:

1. `stars-fixed-traditional`
2. `stars-bright-gaia-bsc`
3. `stars-hipparcos-gaia`

It is a local binary precision star catalog, not a deep render-star catalog and not a full stellar dynamics model.

## Astrometry Model

### Linear space motion only

TSC1 v1 uses a linear 3D space-motion model:

```text
position(t) = position_ref + velocity_ref * dt
velocity(t) = velocity_ref
acceleration(t) = 0
```

The source catalog fields are converted from:

```text
RA / Dec / proper motion / parallax / radial velocity / reference_epoch
```

into:

```text
3D reference position + 3D reference velocity
```

This is better than directly applying linear RA/Dec deltas, because projecting the 3D vector back to the sky naturally captures some nonlinear apparent angular behavior such as perspective effects from radial velocity.

### No explicit stellar acceleration

Most normal star catalogs do not provide reliable per-star acceleration. Gaia/Hipparcos/BSC-style input data generally provides position, proper motion, parallax, radial velocity, and reference epoch, but not acceleration.

Therefore TSC1 v1 sets stellar acceleration to zero.

Known affected cases:

- very nearby high-proper-motion stars,
- long time spans of several thousand years or more,
- stars with significant perspective acceleration,
- unresolved or close binary/multiple systems,
- stars with nonlinear astrometric solutions.

Future formats may add:

```text
model_type = LinearSpaceMotion
model_type = AcceleratedMotion
model_type = BinaryOrbit
```

or equivalent flags/extension records.

### No binary-star orbital model

TSC1 v1 does not model binary-star or multiple-star orbital motion.

Examples that may require special handling in future:

```text
Sirius
Alpha Centauri
Castor
Algol
61 Cygni
Proxima Centauri / Alpha Centauri system
```

For v1, these are treated as normal catalog astrometry rows.

## Historical Accuracy

TSC1 v1 is expected to be useful for ordinary historical sky reconstruction on century-to-millennium scales when combined with correct precession, nutation, obliquity, and frame conversion.

The largest apparent change over one to two thousand years is usually Earth's precession, not stellar acceleration.

However, TSC1 v1 should not be presented as a microarcsecond-grade historical astrometry solution for arbitrary stars over very long time spans.

Known limitations for ancient/far-future use:

- high-proper-motion stars accumulate larger error,
- missing or inaccurate radial velocity affects perspective motion,
- missing or inaccurate parallax affects distance and transverse velocity,
- binary orbital motion is ignored,
- catalog measurements are tied to their source epoch and source quality,
- ancient observations also depend on Delta-T, calendar conversion, and atmospheric/observational uncertainty.

## Missing or Partial Source Fields

### Missing radial velocity

Many stars do not have radial velocity. TSC1 v1 preserves a `HAS_RADIAL_VELOCITY` flag.

When radial velocity is missing, runtime should treat radial velocity as unknown/zero for the linear propagation model.

### Missing parallax

Some fallback rows may not have reliable parallax. TSC1 v1 preserves a `HAS_PARALLAX` flag.

When parallax is missing or non-positive, runtime may use a large placeholder distance for direction-only propagation, matching the existing fixed-star approach.

### Mixed reference epochs

TSC1 rows are not all J2000:

```text
Gaia DR3 rows:    reference_epoch = 2016.0
Hipparcos rows:   reference_epoch = 1991.25
BSC5 fallback:    reference_epoch = 2000.0
Manual rows:      record-specific, currently 2000.0 for special directions
```

Runtime must use each record's `reference_epoch`. It must not assume all stars are J2000.

## Source/Fallback Quality

Current enrichment hierarchy:

```text
Gaia DR3 source_id / HIP best-neighbour
  -> Hipparcos fallback
  -> BSC5 fallback
  -> missing/manual/special handling
```

Known current counts after strict fallback validation:

```text
total identity rows: 118,332
Gaia DR3:             99,525
Hipparcos:            18,430
BSC5:                    101
missing:                 276
```

The missing rows are skipped by the compiler unless they are known manual/special records.

This means the generated Hipparcos-level TSC1 catalog currently has fewer stars than the identity manifest:

```text
identity rows: 118,332
compiled stars: 118,058
```

## Manual/Special Records

TSC1 v1 includes special direction records such as:

```text
galactic_center_j2000
sgr_a_apparent
```

These are marked as:

```text
astrometry_source = Manual
SPECIAL_DIRECTION flag set
```

Known limitation: these are not normal Gaia/Hipparcos point-source stars and should be handled carefully by runtime code. They are direction placeholders/special targets, not ordinary stellar motion records.

## Alias Handling

Aliases are stored in a side table and looked up by normalized alias + FNV-1a 64-bit hash.

Known limitations:

- alias hash is an accelerator only; runtime must verify string equality,
- ambiguous aliases are currently resolved deterministically by compiler policy unless strict mode is used,
- some catalog designations may remain incomplete or normalized differently from external tools,
- alias coverage can improve later with SIMBAD/name curation.

## Runtime Reader Limitations

Current C++ TSC1 runtime support includes:

```text
memory-backed loading
file-backed loading
POSIX mmap on macOS/Linux
Windows file mapping implementation
fallback owned-buffer file loading
alias lookup
header/offset/string validation
Tsc1StarProvider runtime evaluation
lazy global body registry registration
per-star StorageEphemerisBlock compilation
EphemerisBlockCache integration
```

Known limitations:

- Windows memory mapping is implemented through `CreateFileA`, `CreateFileMappingA`, and `MapViewOfFile`, but it has not yet been validated on an actual Windows machine. macOS/Linux mmap has been built and tested locally.
- If platform mapping fails, `MappedFile` falls back to reading the whole file into an owned byte buffer.
- Reader currently assumes native little-endian runtime. This is checked during catalog validation so unsupported endian layouts fail safely instead of silently misreading fields.
- `Tsc1StarProvider` registers stars lazily when they are resolved, not by bulk-registering every star at catalog load time.
- Higher-level automatic catalog discovery/selection and public pipeline-style integration are still incomplete.

## Cache Design Limitations

Existing project LRU cache, `EphemerisBlockCache`, stores compiled calculation blocks, not final per-time results.

TSC1 v1 uses a two-layer model:

```text
.tsc1 file
  -> mmap / OS page cache for raw catalog bytes
Tsc1StarRecord
  -> position_ref_au + velocity_ref_au_per_day + reference_jd
  -> StorageEphemerisBlock
  -> EphemerisBlockCache
```

The `.tsc1` file itself is not inserted into `EphemerisBlockCache`; the cache stores per-star compiled evaluator blocks created by `Tsc1StarProvider` on demand.

A separate future cache would be needed for final computed results:

```text
(target, jd, observer, frame, correction flags) -> computed state/apparent result
```

That is not part of TSC1 v1.

## Not a Deep Render Catalog

TSC1 v1 is for precision/named/BSC/Hipparcos-scale stars.

It is not designed for dense Gaia render catalogs with millions or billions of stars.

Future render catalogs should use a separate format, likely spatially tiled by sky region and magnitude, for example:

```text
TSR1 or equivalent render catalog
HEALPix/spatial tile index
magnitude bins
view-dependent tile loading
```

## Packaging Limitations

Generated catalog files are currently local artifacts under:

```text
data/stars/catalogs/
```

They are intentionally ignored by the core repository.

Future work should decide whether to publish them through:

```text
separate data repository
release artifacts
downloader/discovery mechanism
```

## Summary of v1 Intent

TSC1 v1 intentionally prioritizes:

```text
local binary catalog
safe reader
fast alias lookup
per-row reference epoch
Gaia/Hipparcos/BSC source preservation
simple linear 3D space motion
```

over:

```text
stellar acceleration
binary orbit modeling
microarcsecond ancient astrometry
deep render catalog tiling
final pipeline/result cache integration
```

These limitations are acceptable for the first runtime-capable precision-star catalog, as long as they are documented and not presented as solved.
