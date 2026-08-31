# Current Capabilities And Known Limits

Status: Current
Last reviewed: 2026-08-07

This document summarizes what Taiyin can currently do, what limits remain, and which work areas are suitable for future versions. Specific API details are documented in the corresponding topic documents and headers:

- Runtime / data / cache: `ephemeris_runtime_architecture.md`, `catalog_cache_model.md`, `opc_catalog_format.md`
- Event search: `event_search.md`
- Eclipse search: `eclipse_search.md`
- Star catalog limits: `tsc1_v1_known_limitations.md`

## Current Capabilities

### Runtime And Data Routes

The current runtime is no longer the early draft design. The main structure is:

```text
Runtime
EphemerisEngine
EphemerisBlockCatalog
EphemerisBodyRegistry
EphemerisRouteRuleTable
EphemerisSegmentCache
RouteInflightMap
```

Supported data sources include OPM2, SPK, TKC1/Kepler, Taiyin Kepler files,
the built-in semi-analytical model, and custom ephemeris methods/file methods.
The catalog discovers and describes local data. Route rules decide method
priority. The segment cache stores loaded data segments. There is no shared
exact-JD numeric-result cache.

### Main-Body Position And Apparent/Observed Chain

Main-body position entry points cover TDB, TT, UT, and UTC:

```text
calc_position_tdb / calc_positions_tdb
calc_position_tt / calc_positions_tt
calc_position_ut / calc_positions_ut
calc_position_utc / calc_positions_utc
calc_observed_ut
calc_observed_utc
```

The current apparent/observed chain supports combinations of light-time, annual aberration, solar/multi-body gravitational deflection, topocentric observer, horizontal az/alt, refraction, frame selection, UTC/EOP/UT1/polar motion/CPO, and related options. The global runtime owns EOP, leap-second, and lunar-limb data. User location, atmosphere, the explicit UTC out-of-range fallback flag, model IDs, and route rule live on `NativeCalcContext`; flags control calculation switches. Observed APIs use a `uint64_t` layering convention: their low 32 bits accept only calculation semantics (`SPEED`, `TRUEPOS`, `NO_ABERR`, `NO_GDEFL`, `ASTROMETRIC`, `TOPOCENTRIC`, and `ALLOW_BARYCENTER_APPROX`), while horizontal/refraction/meteorology options occupy the high 32 bits. Output-shape and frame-selector flags (`XYZ`, `EQUATORIAL`, `RADIANS`, and `NONUT`) return `TAIYIN_ERROR_UNSUPPORTED`, rather than being silently ignored.

New native contexts default to light-time, annual aberration, and Sun-only gravitational deflection; Shapiro delay remains opt-in. `SPEED` is supported with that correction set. `NO_ABERR` and `NO_GDEFL` override the corresponding correction for one call, while `ASTROMETRIC` and `TRUEPOS` select broader reduced-position conventions. Custom deflector arrays replace the built-in Sun-only list and must identify the solar entry explicitly when annual aberration or another solar-specific term remains enabled.

### Composite Bodies And Data Fallback

Earth/Moon and major-body barycenter/body-offset logic is centralized in the runtime body registry and built-in body rules. Catalog initialization marks direct-capable bodies; bodies that cannot be read directly from a data file are handled by fallback evaluators.

Composite evaluators request component routes through `EphemerisEngine`. AUTO
routes can fall back by priority. When a single-method rule such as OPM2, SPK,
or the explicit Taiyin semi-analytical route is selected, only that route rule
is tried, which avoids silent method mixing during searches or composite
calculations.

Major-planet body IDs remain strict by default when only barycenter data is available. `TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX` is an explicit native-position opt-in for using the matching barycenter as an approximation for Mars through Pluto; diagnostics keep the requested body as `target_id` and report the barycenter in `component_target_id`.

### Data-Free Satellite Fallback Boundary

The built-in semi-analytical route includes Phobos/Deimos, the Galilean moons,
the Pluto system, and Triton over their separately documented intervals. These
are compact fallback states rather than precision satellite ephemerides:
individual relative-state errors range from metres or kilometres to hundreds of
kilometres, while the mass-weighted physical-planet center correction may be
far smaller. Precise satellite astrometry and satellite phenomena require a
matching SPK or OPM2 source. No Saturnian or Uranian satellite theory is built
in for 1.0; a physical Saturn/Uranus request therefore needs direct data unless
the caller explicitly permits the ordinary barycenter approximation.

### Stars

High-level star APIs are available:

```text
add_global_tsc1_star_catalog / add_global_tsf1_star_catalog
calc_star_position_tdb / tt / ut
calc_star_positions_tdb / tt / ut
calc_observed_star_ut / calc_observed_stars_ut
```

TSC1/TSF1 providers are wrapped by the global star store, so application code usually does not need to pass `Tsc1StarProvider*` directly. Stars support alias lookup, linear space motion, proper-motion propagation, spherical/XYZ output, velocity output, and observed flags aligned with ordinary main bodies.

`calc_star_position_*` returns observer-relative fixed-star positions and applies the same fixed-star apparent corrections used by the observed-star path: annual aberration and solar gravitational deflection are enabled by default, while `TRUEPOS`, `ASTROMETRIC`, `NO_ABERR`, and `NO_GDEFL` select the corresponding reduced models.

For the 1.0 API, fixed-star position and observed-star calls require
`NativeCalcContext::observer_id == TAIYIN_BODY_EARTH`. A non-Earth observer
returns `TAIYIN_ERROR_UNSUPPORTED`. Historical semi-analytical routes are still
supported for an Earth observer: the built-in model synthesizes Sun-to-SSB from
its nine heliocentric planetary-barycenter states, so the Earth, Sun, and
stellar catalog remain in one barycentric frame.

Topocentric observation is likewise Earth-only in 1.0. Topocentric context
setters and native/observed topocentric requests return
`TAIYIN_ERROR_UNSUPPORTED` for a non-Earth observer. Supporting another body's
surface would require that body's reference ellipsoid, rotation/orientation,
and (where relevant) atmospheric model; those models are intentionally outside
the 1.0 scope.

### Event Search

Low-level event search currently covers:

```text
solar/lunar longitude crossing
bounded body longitude crossing
longitude station
relative longitude / aspect crossing
exact aspect
lunar phase
UT / TT entry points
auto-step convenience wrappers
```

These APIs are numeric primitives. They do not provide solar-term names, zodiac ingress names, aspect names, orb, applying/separating labels, or retrograde labels. Domain semantics should be layered above the core runtime by calendar, BaZi, or astrology extensions.

### Eclipses

Eclipse search currently covers:

```text
lunar eclipse solve/search
local lunar eclipse visibility
global solar eclipse solve/search
local solar eclipse circumstances/search
solar Besselian elements
instantaneous global geometry / route rows / route curves
local boundary helpers
```

Public local solar/lunar eclipse APIs read the observer from
`NativeCalcContext::observer_location`. Local APIs reject missing or invalid
observer latitude, and internally normalize topocentric contexts back to a
geocentric apparent state before applying their own local geometry.

Model conventions, contact-time meanings, PMO/NASA oracles, and algorithm sources are documented in `eclipse_search.md`.

### Visibility Search

Public visibility entry points exist for the Sun, Moon, and planets:

```text
search_solar_rise_set_ut
search_solar_twilight_ut
search_solar_transit_ut
search_moon_rise_set_ut
search_moon_transit_ut
search_planet_rise_set_ut
search_planet_transit_ut
```

Solar and lunar searches support limb selection, refraction, fixed disc size, and custom horizon. Planet searches support rise/set/transit, refraction, limb selection, and custom horizon. `search_planet_transit_ut()` accepts `uint64_t flags`; its low 32-bit native-position word is forwarded unchanged, and its high word is reserved for future transit options. This lets a physical Jupiter request use `TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX` on historical semi-analytic data without teaching the transit solver about barycenters; strict-first fallback remains in the position/apparent route and diagnostics retain the requested target. Ordinary public rise/set entry points default to refracted apparent altitude; use the `*_VISIBILITY_FLAG_NO_REFRACTION` flags for true-altitude searches. A refracted request needs real atmosphere data unless the context explicitly opts into the documented ISA-style standard-atmosphere fallback with `native_context_set_atmosphere_policy_flags(..., TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK)`. The corresponding high-word `*_VISIBILITY_STRICT_METEOROLOGY` flags disable that fallback for one call and require supplied atmosphere fields. Local solar-eclipse APIs deliberately differ: their visibility window is geometric by default, and only `TAIYIN_ECLIPSE_LOCAL_REFRACTION` requests the refracted window; `TAIYIN_ECLIPSE_LOCAL_STRICT_METEOROLOGY` requires that option and prohibits fallback. Public regression/oracle tests already cover examples such as Denver and Longyearbyen.

### Body Phenomena

`calc_body_phenomena_ut` / `calc_body_phenomena_tt` are available for phase angle, illuminated fraction, solar elongation, apparent diameter, empirical apparent magnitude, and the Moon's geocentric horizontal parallax. Mercury through Neptune implement the Mallama & Hilton (2018) empirical formulas; the Sun, Moon, and Pluto use separate empirical models documented in `phenomena_magnitude_models.md`. These models are still not a high-fidelity physical photometry solver. In particular, for the Moon, `illuminated_fraction` is ideal-sphere geometric sunlit area, not a brightness ratio; `apparent_magnitude` is an almanac-grade empirical model, not a ROLO-grade lunar irradiance model.

## Current Limits

### C ABI Is Frozen; C++ ABI Is Not

The versioned C99 ABI is the binding and application compatibility boundary
from library version `1.0.0` onward. Existing C symbols and structure contracts
must remain source- and binary-compatible within ABI major 10. New fields use
the documented `struct_size` convention.

The C++ headers remain implementation-facing. They may evolve between minor
versions and are not promised as a stable binary ABI. Bindings and distributed
applications should use `include/taiyin/c/`.

### External Oracle Coverage Is Still Incomplete

Current comparisons include OPM2/SPK/JPL, SOFA/ERFA, PMO/NASA, sxwnl, and partial SwissEph checks, but coverage is not exhaustive.

Areas that still need stronger coverage:

```text
apparent oracle sweeps for more dates and bodies
precise UTC/EOP/CPO topocentric observed external checks
horizontal azimuth / refraction convention checks
SPK type 21 small-body oracles or baked fixtures
broader external tables for local solar/lunar visibility
```

### The Star Model Is TSC1 v1 Linear Motion

TSC1 v1 uses a linear 3D space-motion model. It does not include stellar acceleration, binary or multiple-star dynamics, or Gaia nonlinear solutions. It is usable for ordinary historical sky reconstruction, but should not be presented as microarcsecond-grade historical astrometry for high-proper-motion nearby stars, long time spans, or binary systems.

See `tsc1_v1_known_limitations.md` for details.

### Relativistic Corrections Are Practical Approximations

Light-time, Shapiro delay, and gravitational deflection currently use practical apparent-position models, not a complete moving-deflector post-Newtonian integration.

Known boundaries:

```text
multi-body Shapiro is first-order static summation
per-deflector retarded time is not solved
deflector velocity/c^2 terms are not modeled
spacecraft ranging or high-precision occultation work needs a stronger model
```

### TT/TDB And Timescale Models Need More End-To-End Verification

The project has fast periodic TDB, SOFA-style full TDB, a configurable Delta-T model, leap-second tables, and EOP-table routes. Ordinary main-body and observed calculations are usable, but different external systems' TDB/TT/UT1 conventions can cause small differences. End-to-end oracles should not be tightened to microarcsecond levels too early.

### Eclipse/Visibility Models Have Convention Differences

Eclipse contact times, magnitude, lunar shadow radius, and local visibility depend heavily on shadow model, Moon radius model, refraction, limb convention, and the publication precision of external tables. Taiyin records model presets explicitly, but second-level differences between different model sources should not automatically be treated as bugs.

### Low-Level Flat Kernels Still Have Long Signatures

Low-level kernels such as `calc_apparent_batch` still use explicit-parameter signatures. The benefit is transparent behavior that is suitable for tests and internal composition. The cost is long call sites. The short-term strategy is to keep typed wrappers at the runtime layer rather than wrapping the low-level kernel in a complex pipeline.

## High-Level Capabilities Not Yet Complete

### ASC / MC And Houses Live In The Astrology Extension

The optional astrology extension now exposes ASC, MC, ARMC, Vertex, East
Point, a pure ARMC geometry entry point, a custom house-system registry, and
ten typed house systems. This is intentionally not part of the core astronomy
runtime. Cusp/angle speeds, fractional house-position queries, Gauquelin
sectors, and the remaining school-specific house variants are not implemented.

### Phenomena API

There is now a public scalar API similar to `swe_pheno`:

```text
phase angle
elongation
illuminated fraction
angular diameter
apparent magnitude
```

Magnitude currently uses documented empirical models. Higher-precision lunar visual magnitude, fixed-star magnitude handling, and Swiss-compatible empirical formula alignment remain later compatibility/model work.

### Occultation / Transit / Appulse

Solar and lunar eclipses have dedicated APIs. Event search now provides a three-dimensional minimum-separation primitive, geocentric Mercury/Venus transits of the Sun, and local topocentric contact/visibility handling for Mercury/Venus solar transits. The occultation module has first lunar fixed-star and lunar solar-system-body next-search APIs: from a supplied UT start time it can search forward or backward for geocentric/local lunar occultations of a named target. Point-star targets return the maximum-occultation / minimum-separation time and C1/C4; body targets return the maximum-occultation / minimum-separation time and C1-C4, with type bits for total, partial, annular, grazing, central, and noncentral cases; if the active model combination cannot evaluate center-line classification, it sets `CENTRALITY_UNAVAILABLE` instead of failing the main event. Search contacts can optionally use the globally loaded TLL1 lunar-limb model. The `where` entry can return a center-line / best-observer point, but its surface limits do not yet apply TLL1. It also has a basic local visibility summary that samples Moon/target/Sun altitude, azimuth, and visibility bits at contacts and maximum occultation. It is not yet a complete occultation/appulse system; full surface visibility regions/path summaries, batch catalog scanning, mutual planetary occultations, satellite transits, continuous local visibility intervals, and external published-oracle coverage remain future work. Known major-body targets use the shared mean physical-radius registry by default; callers with their own metadata can pass an explicit circular radius. Planetary oblateness and Saturn's rings are not modeled, and the core runtime does not infer small-body diameters from magnitude.

## SwissEph Alignment Gaps

Taiyin's core runtime now overlaps many SwissEph workflows, but it is not a
Swiss-compatible API layer. The following areas are still intentionally
incomplete or belong in a separate compatibility/extension package:

```text
Swiss-shaped C API / Python API, global state, error strings, file-path semantics
exact Swiss flag interactions for TRUEPOS/NOABERR/NOGDEFL/TOPOCTR/HELCTR/BARYCTR/SIDEREAL
Swiss body IDs, fixed-star names/aliases, asteroid file conventions, and hypothetical bodies
the optional astrology extension provides ten typed house systems, mean and true lunar
nodes, Delaunay-mean and osculating lunar apogees, and a small typed sidereal
baseline (Fagan/Bradley, Lahiri, Raman, Krishnamurti, true Chitra/Spica, and
true Galactic-Center 0 Sagittarius). A native custom ayanamsha registry is
available, but the extension does not provide the complete catalog of named
compatibility modes, natural/interpolated Lilith, lots, or other
school-specific synthetic points
artificial-sky components, crescent-specific lunar first-visibility, and broader limiting-magnitude profiles; point-source heliacal event searches are available
complete lunar occultation region/path products, local rise/set intervals during occultation, and Swiss-style attr[] fields
complete eclipse/occultation attr-style output and all Swiss local circumstance fields
Swiss-compatible refraction, solar-disk light-deflection, radius constants, and empirical magnitude conventions
```

Near-term work should not start by cloning SwissEph's full surface. The safer
sequence is:

1. Keep core runtime APIs explicit through `NativeCalcContext`, typed result
   structs, and model/profile fields.
2. Strengthen edge coverage for already-public APIs, especially Moon/planet
   visibility high-latitude, custom-horizon, and tangent/grazing cases.
3. Add Swiss-compatible shims only in a compatibility layer once the exact flag,
   ID, constant, and convention mapping is documented.

### Engine / Store Facade

The global runtime is usable today, but a modern C++ `Engine / Store / Config / Scratch` facade has not landed yet. If server-side multi-instance usage, model-profile isolation, long-running cache sharing, and plugin-style configuration become important, this layer should be designed before expanding the free-function surface further.

### Result Assembly And Formatting

The runtime currently returns numeric arrays or C++ result structs. It does not yet provide chart-level result assembly, JSON formatting, localized names, unit conversion, or debug reports. Astrology, calendar, CLI, and server output layers should organize those outside the core runtime.

## Planning Directions

Future versions can expand around these areas. Which version gets which item depends on API stability, external-oracle coverage, and data readiness.

```text
more complete observed UTC/EOP/CPO external verification
clearer horizontal/refraction convention documentation and comparisons
broader external tables for solar, lunar, and planetary visibility
higher-fidelity photometry and sky-brightness models
house cusp/angle speeds, fractional house-position queries, and remaining house variants
Engine / Store facade for multi-instance and long-running services
```

The following are longer-term capabilities, not current core-runtime commitments:

```text
complete occultation/appulse system, including surface visibility regions/path summaries, batch star-catalog scanning, mutual planetary occultations, and satellite transits
complex chart workflow pipeline
rewriting the low-level flat apparent kernel
putting solar-term/zodiac/aspect names into the core runtime
```
