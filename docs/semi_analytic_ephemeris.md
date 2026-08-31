# Built-in Semi-Analytical Ephemerides

Status: Current
Last reviewed: 2026-08-31
Primary code: `src/long_range_analytic.cpp`, `src/semi_analytic.cpp`

Taiyin exposes one frozen, data-file-free semi-analytical ephemeris provider.
It is registered below SPK and OPM2 and above TKC1/Kepler sources. The public
route is `TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC`; its internal components are
selected per target and epoch, so callers do not have to choose between a
"compact" and a "long-range" theory.

The primary runtime consists of:

- VSOP2013/TOP2013-derived L/B/R tables for Mercury through Neptune and the
  physical Earth;
- one globally calibrated ELP-derived Moon series;
- a high-accuracy 1600–2200 Pluto model plus a broad, lower-accuracy fallback;
- internally reconstructed EMB and Sun/SSB states; and
- the existing satellite and physical-center/COB correction models.

The former compact major-body and lunar implementation is legacy code. Its
major-body table is retained only where it is more accurate or substantially
cheaper inside the Pluto and Sun/SSB composite routes. It is not registered as
a second provider and cannot be selected as a separate public route.

## Coverage And Routes

The principal Sun, planet, EMB, Earth, Moon, and Pluto routes cover JD TDB
`-470455.0` through `5373545.0`, approximately astronomical years -6000
through +10000. Satellite routes retain their own separately documented
coverage intervals and do not extrapolate.

Supported direct routes are:

```text
Mercury/Venus/EMB/Mars/Jupiter/Saturn/Uranus/Neptune/Pluto barycenter / Sun
Sun / SSB
Earth / Sun
Moon / Earth
Phobos and Deimos / Mars, with Mars / Mars barycenter reconstructed from both
Io, Europa, Ganymede, and Callisto / Jupiter, with a Galilean-dominant
  Jupiter / Jupiter barycenter correction
Charon / Pluto, with Pluto / Pluto barycenter reconstructed from Charon and
  the four mass-bearing small satellites
Triton / Neptune, with Neptune / Neptune barycenter reconstructed from Triton
```

The explicit data-free route is:

```text
TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC  Taiyin semi-analytical ephemeris
```

No Moshier or PLAN404 code or coefficient table is loaded or distributed by
either route.

## Primary Planetary Model

The runtime form is the classic Poisson/Fourier-monomial layout

```text
sum(T^n * A * cos(phase + frequency * T))
T = (JD_TDB - 2451545.0) / 365250
```

evaluated independently for heliocentric longitude, latitude, and radius.
Mercury, Venus, physical Earth, and Mars are generated from the complete
VSOP2013 elliptic-element solutions; Jupiter through Neptune use TOP2013 L/B/R
tables. Offline conversion, coefficient selection, and residual fitting target
DE441. Polynomial and periodic fitted residuals are folded into the final
tables rather than evaluated as a second correction layer.

The checked-in planetary artifact contains 8,697 terms in 209
coordinate/time-power groups. The native VSOP2013/TOP2013 axes are converted
with the same fixed matrix used by the source JavaScript implementation:
official theory axes to ICRF, then ICRF to Taiyin's mean-J2000 ecliptic frame.
The runtime subsequently returns ICRF/J2000 equatorial Cartesian states. This
is intentionally not replaced by a small-angle approximate inverse matrix;
such an approximation creates distance-scaled errors of hundreds of
kilometres for outer planets.

A second-order jet evaluator returns position, velocity, and acceleration
analytically; it does not finite-difference the series. EMB is reconstructed
from the physical-Earth and Moon states. Sun/SSB is reconstructed from the
nine planetary systems; inside the legacy table's well-tested interior the
equivalent compact Sun/SSB evaluator remains in use as a faster component,
with C2-continuous ten-year transitions at its coverage boundaries.

### DE441 comparison and evaluation cost

The following deterministic comparison uses 401 uniformly spaced TDB epochs
from 1600 through 2200 and DE441 `target -> Sun` Cartesian states as the
oracle. The local Release microbenchmark measures one direct compiled state
evaluation, including analytical position, velocity, and acceleration but
excluding catalog routing. Timing is machine-dependent.

| Target | position RMS km | angular RMS arcsec | us/state |
| --- | ---: | ---: | ---: |
| Mercury | 6.11 | 0.0218 | 5.4 |
| Venus | 10.48 | 0.0193 | 2.7 |
| Earth | 15.34 | 0.0211 | 6.8 |
| Mars | 40.50 | 0.0364 | 8.3 |
| Jupiter | 79.80 | 0.0209 | 10.9 |
| Saturn | 91.68 | 0.0132 | 17.0 |
| Uranus | 373.12 | 0.0254 | 8.4 |
| Neptune | 793.78 | 0.0347 | 2.8 |

Across the complete advertised -6000 through +10000 interval (1,201 uniform
samples per target), L/B/R position RMS / angular RMS are:

| Target | RMS km | RMS arcsec | Maximum km |
| --- | ---: | ---: | ---: |
| Mercury | 25.67 | 0.0911 | 103.09 |
| Venus | 96.65 | 0.1829 | 505.58 |
| Earth | 98.57 | 0.1350 | 494.93 |
| Mars | 409.64 | 0.3691 | 1458.10 |
| Jupiter | 760.50 | 0.1971 | 3104.76 |
| Saturn | 2352.06 | 0.3121 | 17664.74 |
| Uranus | 14405.56 | 1.0124 | 50081.60 |
| Neptune | 6119.70 | 0.2596 | 22708.21 |

These are sparse deterministic grids, not formal worst-case guarantees. They
describe the frozen geometric state model, not final apparent/topocentric
coordinates.

## Moon And Pluto

The Moon uses 1,241 folded terms with 763 shared phase arguments. The three
coordinates reuse those arguments and are differentiated analytically. DE441
comparison results are `0.282 km / 0.129 arcsec` RMS over 1600–2200 (601
uniform samples), `0.833 km / 0.421 arcsec` over -3000–+3000 (1,201 samples),
and `2.251 km / 1.173 arcsec` over the full -6000–+10000 interval (2,001
samples).

Pluto uses a direct DE441-fit Chebyshev model from 1600 through 2200, with
ten-year C2 transitions to the more accurate legacy compact model inside its
old coverage. At the old coverage endpoints it transitions over ten years to
a broad 25-group Legendre/Fourier fallback. The modern model measures about
`891 km / 0.025 arcsec` RMS against DE441 on 601 uniform samples. The complete
-6000–+10000 composite is deliberately only a computable fallback, about
`786,000 km / 25.5 arcsec` RMS on a 2,001-point grid; it must not be presented
as precision Pluto astrometry.

### Satellite Precision Boundary

The satellite routes are data-free fallback states, not precision satellite
ephemerides. Their documented held-out errors range from tens of kilometres
for selected residual-table routes to several hundred kilometres for the
compact Galilean model. A mass-weighted planet-center correction can be much
more accurate than the individual satellite state, but it does not upgrade the
satellite state itself. Use a source-matched SPK or OPM2 data package whenever
precise satellite astrometry or satellite phenomena are required.

No Saturnian or Uranian satellite route is built in. The physical Saturn and
Uranus requests therefore require direct data, or the caller must explicitly
allow the ordinary barycenter approximation where that is acceptable.

## Legacy Compact Components

The former compact major-body coefficient artifact now lives under
`src/legacy/`. Its bounded Pluto and Sun/SSB components remain useful in the
unified provider where their measured accuracy or cost is better. The old
Moon and ordinary-planet paths are no longer selected by production routing.
Satellite and center-of-body models remain current components and are
documented below; moving the former primary model to legacy does not alter
those routes.

The generated primary table records the source JavaScript revision and a
SHA-256 over the complete coefficient payload actually consumed by the C++
generator. Regeneration is a maintainer process and is not part of a normal
build.

### Mars-System Satellite Residual Layer

MAR099 provides explicit `Phobos (401) -> Mars (499)` and
`Deimos (402) -> Mars (499)` routes over JD TDB `2305447.5` through
`2670691.5` (approximately 1600–2600). Both use the residual-table evaluator
and do not extrapolate beyond the kernel's coverage. The compact choices favor
the physical Mars-center correction over a precision Mars-surface satellite
ephemeris: deterministic in-coverage hold-outs are `65.0 km` RMS for Phobos
and `5.95 km` RMS for Deimos.

MAR099 identifies no other mass-bearing satellite entries. Its embedded Phobos,
Deimos, and complete-system GMs are therefore used to sum
`Mars (499) -> Mars barycenter (4)` explicitly. The two model errors are
attenuated to approximately `0.046 mm` and `0.013 mm` RMS in that center
correction. The generated C++ table is
`src/internal/mars_satellite_coefficients.inc`; its sampled-fit inputs remain
private maintainer artifacts.

### Jupiter-System Galilean L1.2 Layer

The data-file-free fallback provides `Io (501)`, `Europa (502)`,
`Ganymede (503)`, and `Callisto (504)` relative to the physical Jupiter
center (`599`) over JD TDB `2305456.5` through `2524602.5` (approximately
1600-01-10 through 2200-01-10). It retains the complete compact L1.2
Galilean-moon series distributed by Astronomy Engine and evaluates it with
Taiyin's own second-order state evaluator. The L1.2 independent variable is
TT; the runtime TDB epoch differs at only the millisecond scale and is
immaterial at this model's declared accuracy.

Against JUP365 on a deterministic two-day grid over that shared interval,
the relative-state RMS / P95 / maximum position errors are `847.6 / 1553.1 /
1743.3 km` (Io), `382.4 / 723.6 / 975.4 km` (Europa), `295.8 / 472.1 /
640.2 km` (Ganymede), and `403.4 / 593.8 / 900.2 km` (Callisto). These are
compact fallback states suitable for ordinary geometry and apparent-position
work, not precision satellite ephemerides.

The four published JUP365 Galilean GMs are mass-weighted to provide
`Jupiter (599) -> Jupiter barycenter (5)`. This is explicitly a
**Galilean-dominant** correction: the compact model does not include Amalthea,
Thebe, Adrastea, or Metis. On the same grid the resulting physical-Jupiter
offset differs from JUP365's direct `599 -> 5` state by `52.8 m` RMS,
`94.8 m` P95, and `151.7 m` maximum. The omitted small-moon contribution is
therefore not represented as a complete Jupiter-system reconstruction.

The retained coefficient source and MIT notice are in
`src/third_party/astronomy_engine/`; its implementation identifies the
underlying Galilean theory as L1.2 by Duriez, Lainey, and Vienne. JUP365 is a
validation input and is not bundled.

### Pluto-System Charon Residual Layer

The built-in semi-analytic route additionally provides the PLU060-calibrated
`Charon (901) -> Pluto (999)` relative state over its exact common SPK
coverage, JD TDB `2378497.5` through `2524591.5` (approximately
1800-01-02 through 2199-12-30).  It combines a compact analytic carrier with
400 year-length residual segments.  Each segment stores three orbital-channel
corrections (radius, in-plane phase, and normal height), represented by a
degree-4 Chebyshev secular part plus one linearly modulated carrier harmonic.
Adjacent segments use a 14-day C1 blend.

The residual artifact is explicitly unavailable outside that interval: the
runtime does not extrapolate its first or last Chebyshev segment, and no
data-file-free Charon route is advertised outside that coverage.

The same route exposes `Pluto (999) -> Pluto barycenter (9)` and therefore a
physical-center Pluto state. It uses the fitted PLU060 system GMs and includes
all five mass-bearing companions: the compact Charon residual layer plus
two-basic-angle Poisson state tables for Nix, Hydra, Kerberos, and Styx. The
latter retain Charon's fitted orbital angle as their second basic angle; Pluto's
heliocentric longitude is deliberately not used because it does not represent
the compact system's resonant dynamics.

On an independent 32-day PLU060 grid, the four small-satellite tables together
leave `1.28 m` RMS, `1.72 m` P95, and `1.92 m` maximum error in their
mass-weighted contribution to the physical-Pluto correction. The Charon
residual table's `11.1 m` relative-state RMS is attenuated by its system mass
fraction to roughly `1.2 m`; the combined data-free Pluto-center route is thus
a few-metre-class fallback over the stated coverage. These figures are for the
source SPK and are not a claim of accuracy beyond PLU060's own model.

Regeneration is an offline private maintainer workflow using a local
`plu060.bsp`; neither input SPK nor sampled states are bundled in the runtime
artifact. The resulting C++ tables are
`src/internal/charon_plu060_coefficients.inc` and
`src/internal/pluto_small_satellite_coefficients.inc`. The small-satellite
tables are deliberately coarser satellite-position models than Charon, but
their errors are assessed in the physically relevant mass-weighted COB sum.

### Neptune-System Triton Residual Layer

The same generic residual-table evaluator provides `Triton (801) -> Neptune
(899)` from the NAIF NEP098 satellite solution over JD TDB `2378496.5` through
`2524592.5` (approximately 1800-01-01 through 2200-01-01).  Its generated
table contains 400 year-length segments, each with a degree-4 Chebyshev part
and four carrier harmonics whose amplitudes vary quadratically in segment time.
The 14-day C1 overlap and strict no-extrapolation rule are the same as for
Charon.

On the deterministic per-segment held-out daily samples, the Triton state is
`18.960 km` RMS, `35.272 km` P95, and `56.112 km` maximum relative to NEP098.
NEP098's embedded source summary supplies Triton's GM
(`1428.495462910464 km^3/s^2`) and the complete Neptunian-system GM
(`6836531.640925204 km^3/s^2`), which give the explicit Triton mass fraction
used for `Neptune (899) -> Neptune barycenter (8)`.

This is again a **Triton-dominant** physical-center reconstruction, not a
complete Neptunian satellite system.  Against NEP098, the exact-Triton-only
mass formula differs from Neptune's direct center state by `0.045 km` RMS and
`0.051 km` maximum because Naiad, Thalassa, Despina, Galatea, Larissa, and
Proteus have not yet been added.  The held-out Triton interpolation error
contributes at most about `0.012 km` more to that center offset.  The model is
therefore useful as a data-free physical-center correction, but must not be
advertised as a complete system-COB solution.

It is generated by the same private residual-fitting workflow from a local
NEP098 SPK. The checked-in runtime artifact is
`src/internal/triton_nep098_coefficients.inc`.

## Held-Out Accuracy

The source model's held-out DE441 validation over calendar years -3000 through
+3000 reported these heliocentric angular RMS values:

| Target | RMS |
| --- | ---: |
| Mercury | 1.66 arcsec |
| Venus | 0.66 arcsec |
| EMB | 0.56 arcsec |
| Mars | 2.29 arcsec |
| Jupiter | 3.31 arcsec |
| Saturn | 0.29 arcsec |
| Uranus | 3.65 arcsec |
| Neptune | 0.21 arcsec |
| Pluto | 1.53 arcsec |

On an independent 68,484-epoch grid sampled every 32 days across the supported
interval, the corrected geocentric lunar model measured `0.666 km` position
RMS, `0.337 arcsec` angular RMS, and `0.236 km` radial RMS against DE441.
Maximum errors on that grid were `4.845 km`, `2.691 arcsec`, and `1.739 km`.
For comparison, the previous XL1-based frozen model measured `1.336 km`
position RMS and `0.704 arcsec` angular RMS on the same grid.

Over calendar years 1700 through 2300, the new corrected model measured
`0.323 km` position RMS and `0.157 arcsec` angular RMS. The correction is
essential for the full six-millennium interval even though the selected raw
ELP series is already slightly more accurate in the modern subrange.

These figures describe the frozen base state model, not the final apparent or
topocentric output. Light time, aberration, deflection, precession/nutation,
observer geometry, and explicit UTC/time-scale conversion are applied by the normal runtime
layers selected by the caller.

## Provenance

The planetary frozen model comes from `taiyin-exp` revision
`a5bdf675f921804874dc4e0a0838beebfbcf2b32`. The generated lunar C++ table
records the runtime artifact checksum. The Sun/SSB reconstruction uses the
Solar System GMs from NAIF's JPL DE440 constants kernel
[`gm_de440.tpc`](https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/gm_de440.tpc).

The lunar artifact is generated from the complete ELP/MPP02 DE405 table and a
DE441 BSP. The private maintainer workflow deterministically selects the base
series, reserves contiguous sample blocks from the sparse fit, and packages
only the selected phases and fitted coefficients. It uses NumPy and jplephem,
but neither dependency nor either multi-megabyte source data file is needed by
normal builds or at runtime. The generation and DE441-validation tools are not
part of the public source snapshot. The recorded 4,096-epoch
central-difference velocity comparison measures `0.273 km/day` vector RMS for
the current frozen artifact, compared with `0.417 km/day` for the previous
model.
