# Built-in Semi-Analytical Ephemeris

Status: Current
Last reviewed: 2026-07-21
Primary code: `src/semi_analytic.cpp`, `src/internal/semi_analytic_coefficients.inc`

Taiyin includes a frozen, data-file-free semi-analytical fallback for epochs
where no higher-priority SPK or OPM2 route is available. It is registered in
the default route table below SPK and OPM2 and above TKC1/Kepler sources.

## Coverage And Routes

Planetary series cover JD TDB `625295.0` through `2816795.0`, approximately
calendar years -3000 through +3000. The lunar correction has a slightly
narrower interval; Earth/Sun uses the intersection of the EMB and lunar
intervals.

Supported direct routes are:

```text
Mercury/Venus/EMB/Mars/Jupiter/Saturn/Uranus/Neptune/Pluto barycenter / Sun
Earth / Sun
Moon / Earth
```

The explicit route is `TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC`. No Moshier or
PLAN404 code or coefficient table is loaded or distributed by that route.

## Model

- Mercury through Pluto and EMB use compact harmonic series independently
  fitted to JPL DE441.
- The Moon uses the attributed truncated XL1 series from Shouxing Astronomical
  Calendar, followed by an independently fitted DE441 residual correction.
- Earth/Sun is reconstructed from EMB/Sun and Moon/Earth using the Earth-Moon
  mass ratio.
- The lunar ecliptic-of-date result is transformed to the J2000 ecliptic with
  the P03 precession model, then all routes are returned as ICRF/J2000
  equatorial Cartesian states.
- Position, velocity, and acceleration are evaluated together with second-order
  automatic differentiation. The derivatives are not finite differences.

The frozen C++ table records the source revision and SHA-256 of the generating
Python coefficient file. Regeneration is explicit through
`tools/generate_semi_analytic_builtin.py`; it is not part of a normal build.

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

The corrected geocentric lunar model measured `0.704 arcsec` angular RMS and
`0.263 km` radial RMS on held-out 32-day-grid epochs. Maximum errors on that
grid were `5.22 arcsec` and `1.52 km`.

These figures describe the frozen base state model, not the final apparent or
topocentric output. Light time, aberration, deflection, precession/nutation,
observer geometry, and time-scale policy are applied by the normal runtime
layers selected by the caller.

## Provenance

The imported frozen model comes from `taiyin-exp` revision
`27d33df2089ee1213a13a68782d5eff4ca2b2681`. Its `coefficients.py` SHA-256 is
`67beddfed388e5a8b934b8834a0f011dd69fa9888c6373a7a6becbd39eb01516`.
Shouxing attribution and the upstream permission statement for XL1 are retained
in the repository `NOTICE`.
