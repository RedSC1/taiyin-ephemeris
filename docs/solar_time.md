# Equation Of Time And Local Solar Time

Status: Current
Primary header: `include/taiyin/runtime/solar_time.h`

## Equation Of Time

```cpp
calc_equation_of_time_ut(&context, jd_ut, &result, &diagnostic)
calc_equation_of_time_tt(&context, jd_tt, &result, &diagnostic)
```

Taiyin defines the equation of time as:

```text
apparent solar time - mean solar time
```

`EquationOfTimeResult` returns the signed result in days and seconds, together
with the resolved UT/TT epochs, apparent geocentric Sun right ascension, and
GAST used in the calculation. A negative result means a sundial is behind mean
solar time.

The physical solar-time calculation uses Taiyin's ordinary scalar
double-precision epoch route. Split-Julian-Date API variants are deliberately
not exposed until the native ephemeris calculation itself can preserve a split
epoch end to end.

That requires the ephemeris evaluator, precession/nutation and Earth-rotation
(GAST) paths, and time-bearing result fields to consume and propagate split
TT/UT1 epochs without collapsing them into one `double`.

The Sun is evaluated geocentrically in the true equator of date. The supplied
`NativeCalcContext` still selects ephemeris routes, Delta-T, precession,
nutation, light-time, aberration, and deflection behavior. Any topocentric
state in the context is cleared for this global solar-time quantity.

## LMT And LAT

```cpp
local_mean_to_apparent_solar_time(
    &context, jd_local_mean, longitude_rad, &jd_local_apparent, &diagnostic)
local_apparent_to_mean_solar_time(
    &context, jd_local_apparent, longitude_rad, &jd_local_mean, &diagnostic)
```

Longitude is east-positive, expressed in radians, and must be in
`[-π, π]`. Julian-date local time uses:

```text
LMT = UT1 + longitude / 2π
LAT = LMT + equation_of_time
```

LAT-to-LMT is solved iteratively because the equation of time must be
evaluated at the UT1 instant implied by the candidate LMT. These functions
convert a time coordinate; they do not apply time-zone or civil-calendar
rules.
