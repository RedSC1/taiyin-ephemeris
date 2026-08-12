# Lunar Nodes And Apsides

Status: Current
Primary header: `include/taiyin/astrology/lunar_points.h`
Position-target header: `include/taiyin/astrology/targets.h`

This optional extension provides conventional mean and geocentric osculating
lunar points. Its technical definitions are intentional: familiar astrology
labels such as *true Lilith* and *mean Lilith* are aliases, not claims that
there is one universally preferred definition.

## API

```cpp
calc_lunar_mean_node_ut(&context, jd_ut, TAIYIN_LUNAR_NODE_ASCENDING,
                        reference_flags, &node, &diagnostic)
calc_lunar_true_node_tt(&context, jd_tt, TAIYIN_LUNAR_NODE_DESCENDING,
                        native_position_flags, &node, &diagnostic)

calc_lunar_mean_apogee_ut(&context, jd_ut, reference_flags, &apsis, &diagnostic)
calc_lunar_osculating_apogee_tt(
    &context, jd_tt, native_position_flags, &apsis, &diagnostic)
calc_lunar_fitted_apogee_ut(
    &context, jd_ut, reference_flags, &apsis, &diagnostic)
```

## Position Target Registration

Applications that link `taiyin_astrology_extension` can register its built-in
lunar points once during setup:

```cpp
using namespace taiyin::astrology;

register_builtin_astrology_targets();
runtime::calc_position_tt(
    &context,
    TAIYIN_ASTROLOGY_TARGET_TRUE_NODE,
    jd_tt,
    runtime::TAIYIN_NATIVE_POSITION_SPEED,
    out,
    &diagnostic);
```

The registered IDs are `TRUE_NODE`, `TRUE_DESCENDING_NODE`, `MEAN_NODE`,
`MEAN_DESCENDING_NODE`, `MEAN_LILITH`, `OSCULATING_LILITH`, and
`FITTED_LILITH`. They use the same `calc_position_*` and `calc_positions_*`
entry points as physical bodies. An osculating point asks the normal Moon route
for its dependency, so OPM/SPK/semi-analytical routing and the segment cache remain
underneath it. The fitted point evaluates its generated coefficient table and
does not require DE441 at runtime.

Nodes and mean Lilith are directions, not physical Cartesian bodies. Their
spherical position result has `distance_au == NAN`; the osculating and fitted
Lilith definitions provide distances. Cartesian requests are accepted: a
direction-only target then returns `NAN` Cartesian components, whereas
osculating and fitted Lilith supply derived Cartesian position and velocity.
Topocentric requests are accepted but currently preserve these conventional
geocentric definitions rather than applying artificial virtual-point parallax.
The typed APIs below retain their narrower definition-only contracts.

An evaluator may also register an exact `NativeStateEvaluatorFn`, whose
position, velocity, and acceleration are forwarded unchanged by
`calc_state_*()`. A position-only evaluator falls back to a centered finite
difference with a default 0.001-day step: it preserves finite callback
velocities, derives missing velocity from positions, and derives acceleration
from neighboring velocities or, when necessary, neighboring positions. If the
neighboring evaluations are unavailable, only the missing derivative is `NAN`.

`LunarNodePosition` contains longitude, instantaneous longitude rate, and the
effective reference frame. `LunarTrueNodePosition` remains a source-compatible
alias for the initial true-node API.

`LunarApsisPosition` contains longitude, latitude, their rates, the reference
frame, and a `definition` field:

- `TAIYIN_LUNAR_APSIS_DELAUNAY_MEAN`: a Delaunay mean-apogee direction. This
  is the convention commonly called **mean Black Moon / mean Lilith**. It has
  no invented physical distance, so `distance_au` and `distance_rate_au_per_day`
  are `NAN`.
- `TAIYIN_LUNAR_APSIS_OSCULATING_TWO_BODY`: the apoapsis of the two-body
  ellipse osculating the corrected geocentric Moon state. This is commonly
  called **true Lilith**. Its instantaneous apoapsis distance and rate are
  populated.
- `TAIYIN_LUNAR_APSIS_DE441_FITTED_NATURAL`: a Taiyin-defined continuous
  natural-apogee convention. Piecewise Delaunay-Poisson series fit physical
  DE441 lunar apogee events. Each event direction is reconstructed in fixed
  ICRF from a Delaunay mean direction plus a fitted spherical tangent
  correction; neighboring fitted event vectors are then joined by a
  nonuniform cubic Hermite curve. It is neither a linear interpolation of two
  longitudes nor a coefficient-compatible implementation of another
  ephemeris package's interpolated apogee.

## Definitions

The mean node is the IERS 2003 Delaunay argument `Omega(T)`. The Delaunay mean
apogee uses `varpi = F + Omega - l`, then rotates the apogee direction around
the mean node by the conventional mean lunar inclination of `5.145396` degrees.
These are model definitions, not fitted corrections to the Moon's instantaneous
position. Taiyin intentionally does not import a long-range empirical residual
table from another ephemeris implementation.

The osculating apogee is computed from the corrected Earth-centered Moon state:

```text
h = r x v
e = (v x h) / mu - r / |r|
apogee direction = -normalize(e)
apogee distance = a * (1 + |e|)
```

Its angular and distance rates use the same state acceleration and reference
frame matrix derivative. No date finite difference or apsis search is used for
this instantaneous construction.

The fitted natural apogee was generated from 402,838 Earth-centered lunar
apogee events extracted from NASA/JPL DE441 by solving `dot(r, v) = 0`. The
DE441 interval is divided into 30 fitted pieces: normally 1000 Julian years
each, with the short final remainder merged into the adjacent piece. Each
piece uses a cubic long-term term and 16 selected Delaunay harmonics. Training
uses a 100-year overlap and runtime evaluation blends adjacent pieces over 50
years. Direction residuals are stored as the spherical logarithm of the DE441
unit vector in the local east/north tangent basis of the Delaunay mean
direction. The basis and mean direction are transformed to ICRF with a fixed
IAU 2006 mean-ecliptic reference, then the spherical exponential reconstructs
the fitted unit vector. This avoids longitude wrapping, does not fit rotating
global Cartesian components, and makes the generated model independent of the
caller's precession choice. The continuous position and analytic velocity
come from a nonuniform cubic Hermite curve through successive reconstructed
event vectors in ICRF. The caller's selected precession model is applied only
when forming the requested output frame.

The generated fit covers JD `-3100015.5` through `8000016.5`. Outside that
interval the closest boundary piece is deliberately extrapolated and
`LunarApsisPosition::extrapolated` is set. This keeps historical astrology
workflows evaluable without hiding that the result is outside the source
ephemeris interval.

The DE441 fitting workflow that produced the checked-in coefficient header is
maintained privately; it is not required by normal builds or included in the
public source snapshot.

## Reference And Flag Contract

The output frame in `NativeCalcContext` selects the result reference plane. All
runtime output frames are supported:

- `TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC`
- `TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE`
- `TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE`
- `TAIYIN_APPARENT_FRAME_ICRF`
- `TAIYIN_APPARENT_FRAME_J2000_MEAN_EQUATOR`
- `TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE`
- `TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE`
- `TAIYIN_APPARENT_FRAME_CIRS`

Osculating points always evaluate a **geocentric** Moon state. A caller may use
a context that also has a local observer installed; its topocentric offset is
intentionally removed for this calculation.

Normal native correction controls continue to apply to the **osculating** Moon
state: `TRUEPOS`, `ASTROMETRIC`, `NO_ABERR`, `NO_GDEFL`, and `NONUT` are
accepted. `EQUATORIAL` selects the true equator of date (or mean equator of
date with `NONUT`). The typed APIs reject output-shape flags such as `XYZ`,
`RADIANS`, and `SPEED`; registered position targets accept `RADIANS`, `SPEED`,
`XYZ`, and `TOPOCENTRIC` with their usual `calc_position_*` meanings.

Mean points are constructed from their conventional Delaunay arguments, so
they accept only `EQUATORIAL` and `NONUT` as reference-frame selectors.
`TRUEPOS`, `ASTROMETRIC`, `NO_ABERR`, and `NO_GDEFL` are rejected rather than
silently pretending to alter a mean model.

The fitted natural point has the same reference-only flag contract as the mean
point. Its model is already fixed by the generated DE441 fit, so apparent
correction flags cannot change its definition.

When `NONUT` is requested with `TRUE_ECLIPTIC_OF_DATE` or
`TRUE_EQUATOR_OF_DATE`, the result reports the corresponding mean frame. CIRS
does not have a nutation-free equivalent and is rejected with `NONUT`.

## Validation

IERS formula checks lock the mean node and mean apogee definitions. Swiss
Ephemeris Moshier sanity cases are also recorded for the current epoch. OPM2
and Moshier differ by a few arcseconds for the osculating node and about 43
arcseconds for the osculating-apogee longitude in the tested case, because an
osculating apsis amplifies small differences in the lunar velocity model.

The fitted model has an independent DE441 event oracle at JD
`2460420.5913274437`: its longitude and latitude differ by about 21.9 and 15.5
arcseconds, and its distance by about 0.66 km. Stratified withheld events over
the complete DE441 interval are excluded from both harmonic selection and the
temporary validation fit. They show event-time maxima of about 431 to 813
seconds per segment, fitted event-direction residuals of roughly 0.5 to 3
arcminutes, and distance residuals of roughly 5 to 12 km. The generated
production coefficients are then refitted with all events. These are model
errors, not floating-point tolerances.
