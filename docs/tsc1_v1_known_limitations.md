# TSC1 v1 Known Limits

Status: Current limitation note
Last reviewed: 2026-08-12
Primary headers: `include/taiyin/star_catalog_tsc1.h`, `include/taiyin/star_provider_tsf1.h`

This document describes the capability boundaries, data assumptions, and design trade-offs of the first TSC1 precision-star catalog. It is for users who need to load star catalogs, call star-position APIs, or evaluate historical-sky precision boundaries.

## Scope

TSC1 v1 is a local precision-star catalog format. It currently mainly covers:

1. `stars-fixed-traditional`
2. `stars-bright-gaia-bsc`
3. `stars-hipparcos-gaia`
4. `lite/stars-bright-v5`

It is not a deep render-star catalog and not a complete stellar dynamics model. TSC1 is intended to provide a local data format for named stars, bright stars, and Hipparcos/Gaia-scale stars that can be queried, propagated, and used with Taiyin apparent/observed flags.

## Files And Loading

TSC1 is a binary precision-star catalog. TSF1 is a small text format for user-defined stars. When the runtime loads TSF1, it first compiles it into an in-memory TSC1 catalog and then reuses the TSC1 provider for computation.

Current public entry points:

```text
add_global_tsc1_star_catalog
add_global_tsc1_star_catalog_from_memory
add_global_tsf1_star_catalog
clear_global_star_catalogs
calc_star_position_ut / tt / tdb
calc_star_positions_ut / tt / tdb
calc_observed_star_ut
calc_observed_stars_ut
```

TSC1/TSF1 star catalogs do not enter the solar-system ephemeris OPC descriptor catalog. OPC covers solar-system ephemeris data such as OPM2/SPK/TKC1. Stars are held separately by the global star store. Lookup checks loaded TSC1 providers first, then TSF1 providers; providers of the same kind are checked in insertion order.

## Astrometric Model

### Linear Space Motion Only

TSC1 v1 uses a linear 3D space-motion model:

```text
position(t) = position_ref + velocity_ref * dt
velocity(t) = velocity_ref
acceleration(t) = 0
```

Source catalog fields:

```text
RA / Dec / proper motion / parallax / radial velocity / reference_epoch
```

are converted into:

```text
3D reference position + 3D reference velocity
```

This is better than directly applying linear RA/Dec deltas because projecting the 3D vector back onto the sky naturally captures part of the nonlinear apparent angular behavior, such as perspective effects caused by radial velocity.

This model is not a Taiyin-specific stellar formula. It is the ordinary propagation pattern for Gaia/Hipparcos-style astrometric catalogues: the catalogue provides `RA/Dec/proper motion/parallax/radial velocity/reference_epoch`; the runtime converts those values into a 3D reference position and velocity, then propagates with uniform rectilinear motion. This is the same class of model as IAU SOFA `starpm` / `pmsafe`, which advances stellar astrometry from proper motion, parallax, and radial velocity.

TSC1 v1 derives its authority mainly from the source catalogue:

- Gaia DR3/EDR3 provides astrometric solutions with positions, parallaxes, and proper motions at J2016.0;
- Hipparcos provides ICRS/HCRF astrometry at J1991.25;
- BSC5/manual fallback is only a lower-priority source for bright-star and special-direction coverage.

Therefore, TSC1 stellar positions are "source catalogue astrometry plus standard linear space-motion propagation", not an empirical photometry formula like planetary magnitudes.

### No Explicit Stellar Acceleration

Most ordinary star catalogs do not provide reliable per-star acceleration. Gaia/Hipparcos/BSC-style inputs usually provide position, proper motion, parallax, radial velocity, and reference epoch, but not acceleration.

TSC1 v1 therefore sets stellar acceleration to zero.

Known affected cases:

- very nearby stars with high proper motion;
- spans of thousands of years or longer;
- stars with significant perspective acceleration;
- unresolved or close binary/multiple-star systems;
- stars whose astrometric solution is nonlinear.

Later formats can extend to:

```text
model_type = LinearSpaceMotion
model_type = AcceleratedMotion
model_type = BinaryOrbit
```

or equivalent flags/extension records.

### No Binary-Star Orbit Model

TSC1 v1 does not model binary or multiple-star orbital motion.

Such systems need dedicated models for better handling, for example:

```text
Sirius
Alpha Centauri
Castor
Algol
61 Cygni
Proxima Centauri / Alpha Centauri system
```

In v1, they are handled as ordinary catalog astrometry rows.

## Historical Precision

When combined with correct precession, nutation, obliquity, and frame conversion, TSC1 v1 is expected to be useful for ordinary historical sky reconstruction on century-to-millennium scales.

Over one or two thousand years, the largest apparent change is usually Earth's precession, not stellar acceleration.

However, TSC1 v1 is not positioned as a microarcsecond-grade historical astrometry solution for arbitrary stars over arbitrary long time spans.

Known limits for ancient or far-future use:

- high-proper-motion stars accumulate larger errors;
- missing or inaccurate radial velocity affects perspective motion;
- missing or inaccurate parallax affects distance and transverse velocity;
- binary-star orbital motion is ignored;
- catalog measurements are tied to the source epoch and source quality;
- ancient observations also depend on Delta T, calendar conversion, and atmosphere/observation uncertainty.

## Missing Or Incomplete Source Fields

### Magnitude Is A Catalogue Field, Not A Photometry Model

The `magnitude` value in TSC1/TSF1 is stored and read from the catalogue record. The current runtime does not recompute stellar apparent magnitude from distance, colour, variable-star light curves, extinction, or observing passband.

This differs from solar-system `apparent_magnitude` in `calc_body_phenomena_*()`: solar-system magnitudes are empirical runtime formulas; stellar magnitudes are catalogue photometry values. Variable stars, unresolved binary combined light, bandpass differences, interstellar extinction, and historical brightness changes are outside the TSC1 v1 model.

### Missing Radial Velocity

Many stars do not have radial velocity. TSC1 v1 preserves a `HAS_RADIAL_VELOCITY` flag.

When radial velocity is missing, the runtime should treat radial velocity as unknown/zero in the linear propagation model.

### Missing Parallax

Some fallback rows may not have reliable parallax. TSC1 v1 preserves a `HAS_PARALLAX` flag.

When parallax is missing or nonpositive, the runtime can use a very large placeholder distance for direction-only propagation, matching the existing fixed-star approach.

### Mixed Reference Epochs

TSC1 rows are not all J2000:

```text
Gaia DR3 rows:    reference_epoch = 2016.0
Hipparcos rows:   reference_epoch = 1991.25
BSC5 fallback:    reference_epoch = 2000.0
Manual rows:      record-specific, currently 2000.0 for special directions
```

The runtime must use each record's own `reference_epoch`; it must not assume every star is J2000.

## Source/Fallback Quality

Current enrichment hierarchy:

```text
Gaia DR3 source_id / HIP best-neighbour
  -> Hipparcos fallback
  -> BSC5 fallback
  -> missing/manual/special handling
```

Known counts after strict fallback validation:

```text
total identity rows: 118,332
Gaia DR3:             99,525
Hipparcos:            18,431
BSC5:                    101
missing:                 275
```

Missing rows are skipped by the compiler unless they are known manual/special records.

This means the generated Hipparcos-level TSC1 catalog currently has fewer stars than the identity manifest:

```text
identity rows: 118,332
compiled stars: 118,059
```

## Manual/Special Records

TSC1 v1 includes special direction records, for example:

```text
galactic_center_j2000
sgr_a_apparent
```

They are marked as:

```text
astrometry_source = Manual
SPECIAL_DIRECTION flag set
```

Known limit: these are not ordinary Gaia/Hipparcos point-source stars, and runtime code needs to handle them carefully. They are direction placeholders/special targets, not ordinary stellar-motion records.

## Alias Handling

Aliases live in a side table and are looked up by normalized alias plus FNV-1a 64-bit hash.

Known limits:

- alias hash is only an accelerator; the runtime must verify string equality;
- ambiguous aliases are currently resolved deterministically by compiler policy unless strict mode is used;
- some catalog designations may still be incomplete or normalized differently from external tools;
- alias coverage can continue to improve through SIMBAD/name curation.

## Runtime Reader Limits

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
lazy internal runtime-id registration
per-star StorageEphemerisBlock compilation
EphemerisSegmentCache integration
global star catalog store
high-level star position / observed-star API
```

Known limits:

- Windows file mapping and catalog traversal use UTF-16 Win32 APIs (`CreateFileW`, `FindFirstFileW`, and `GetFileAttributesExW`). The raw-SPK reader also uses `GetFileSizeEx` and `SetFilePointerEx`, so large kernels such as DE441 do not rely on narrow-character C++ stream handling.
- The Windows MSVC modular CI includes a UTF-8 path mapping and directory-enumeration regression test. It does not stage the multi-gigabyte DE441 kernel, so real-world DE441 loading remains a release smoke check in addition to CI.
- The reader currently assumes native little-endian runtime. Catalog validation checks this, so unsupported endian layouts fail safely rather than silently misreading fields.
- `Tsc1StarProvider` lazily assigns an internal `runtime_id` when a star is resolved; it does not bulk-register every star at catalog load time. These IDs are used only for cache keys and diagnostics. HIP, HR, HD, Gaia DR3, and canonical aliases remain catalog metadata.
- The current star store is global, not independently held by each `NativeCalcContext`. Applications that need isolated star-catalog sets should explicitly manage `add_global_*` and `clear_global_star_catalogs` at their call boundaries.
- TSC1/TSF1 does not participate in the main ephemeris route-rule priority. Star-key override behavior comes from star-store lookup order, not from solar-system `AUTO/SPK/OPM2/semi-analytical` route rules.

## Cache Design Limits

The current runtime cache `EphemerisSegmentCache` stores compiled calculation blocks. It does not store final per-time results.

TSC1 v1 uses a two-layer model:

```text
.tsc1 file
  -> mmap / OS page cache for raw catalog bytes
Tsc1StarRecord
  -> position_ref_au + velocity_ref_au_per_day + reference_jd
  -> StorageEphemerisBlock
  -> EphemerisSegmentCache
```

The `.tsc1` file itself is not inserted into `EphemerisSegmentCache`; the cache stores per-star compiled evaluator blocks created on demand by `Tsc1StarProvider`.

The TSC1 provider itself caches only compiled evaluator blocks. It does not cache final apparent/observed output for a specific time. Output-level reuse, when useful, belongs to caller-side batching or solver-local state rather than the TSC1 file format.

## Not A Deep Render Catalog

TSC1 v1 targets precision/named/BSC/Hipparcos-scale stars.

It is not designed for dense Gaia render catalogs with millions or billions of stars.

Render-grade catalogs should use separate formats, likely with spatial tiling by sky region and magnitude, for example:

```text
TSR1 or equivalent render catalog
HEALPix/spatial tile index
magnitude bins
view-dependent tile loading
```

## Packaging Limits

The repository retains the complete generated catalogs under:

```text
data/stars/catalogs/
```

The magnitude-limited distribution option is kept separately at:

```text
data/stars/catalogs/lite/stars-bright-v5.tsc1
```

It is mechanically derived from `stars-hipparcos-gaia.tsc1`: it retains all
available records with catalogue `V <= 5.0`, the two manual special-direction
records, and every HIP star referenced by the Stellarium Chinese line figures
or the twelve western-zodiac line figures. The pinned cultural selection has
1,385 Chinese line stars and 141 zodiac line stars, or 1,399 unique HIP stars
after overlap. Unambiguous English and Simplified Chinese traditional names
are retained as aliases. Its 2,057 records remain small enough to be a default
for language bindings. The parent directory still contains the 9,098-star
bright catalog (about 1.9 MB) and the 118,059-star Hipparcos/Gaia catalog
(about 21 MB), for applications that want broader coverage.

The Stellarium-derived selection and aliases are a pinned sky-culture data
layer under CC BY-SA. TSC1 stores the astrometric records and aliases; a UI
that wants to draw the actual asterism lines should consume the separately
versioned line geometry rather than infer it from catalog membership.

The runtime does not require one fixed packaging choice. A distribution can
ship the lite table by default, offer larger catalog files as optional data, or
load a user-provided TSC1/TSF1 catalog. The checked-in lite file is
mechanically produced from the bright catalog using the requirements described
above. Maintainer tooling and raw upstream inputs are not part of the filtered
public source snapshot; the generated requirements manifest pins their source
revision, selection counts, and attribution so the distributed catalog remains
auditable.

## v1 Intent Summary

TSC1 v1 intentionally prioritizes:

```text
local binary catalog
safe reader
fast alias lookup
per-row reference epoch
Gaia/Hipparcos/BSC source preservation
simple linear 3D space motion
```

This version does not promise:

```text
stellar acceleration
binary orbit modeling
microarcsecond ancient astrometry
deep render catalog tiling
output-level result cache semantics
```

These trade-offs make TSC1 v1 a suitable first foundation format for a runtime-capable precision-star catalog. If an application needs binary orbits, high-precision astrometry over very long time spans, or large-scale render catalogs, it should use later dedicated formats or extension models.
