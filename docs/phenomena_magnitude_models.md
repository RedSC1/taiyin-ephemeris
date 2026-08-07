# Body Phenomena Magnitude Models

Status: Current
Last reviewed: 2026-07-02
Code entry point: `src/runtime/phenomena.cpp`

This document describes how `BodyPhenomena::apparent_magnitude` is computed by `calc_body_phenomena_ut()` / `calc_body_phenomena_tt()`. These are empirical V-band apparent-magnitude models intended to match the semantics of ordinary almanac and SwissEph-style `swe_pheno*()` output. They are not radiative-transfer, surface-scattering, or full photometric-calibration models.

## Common Distance Term

Except for the Sun, reflecting bodies use:

```text
m = phase_term + 5 log10(r * Delta)
```

where:

- `r` is the Sun-body distance in AU;
- `Delta` is the observer-body distance in AU;
- `phase_term` is the empirical phase term for the body.

The Sun is not a reflecting body and does not use the `r * Delta` term.

## Sun

The Sun currently uses a mean V-band apparent solar magnitude scaled by observer-Sun distance:

```text
m = -26.74 + 5 log10(Delta)
```

`Delta` is the observer-Sun distance in AU. `-26.74` is a common mean visual magnitude constant for the Sun; almanacs and libraries may use different photometric zero points, and SwissEph-style output can differ by about `0.1 mag`. That difference is a magnitude-model convention, not a position error.

## Moon

The Moon currently uses an Astronomical-Almanac-style empirical phase model:

```text
m = 0.21 + 5 log10(r * Delta) + phase_term(alpha)
```

`alpha` is the phase angle in degrees. `H=0.21` is the Moon's common value under the Solar System body absolute-magnitude definition. For `alpha <= 150 deg`, `phase_term` uses separate before-full-Moon and after-full-Moon polynomials. The implementation determines the branch by comparing the Moon phase angle at `jd +/- 1 hour`; this wording matches the original formula more closely than the looser waxing/waning phrasing.

For the very thin crescent range, `alpha > 150 deg`, the main polynomial is not used. Taiyin keeps a thin-crescent fallback so the result remains finite near new Moon. This fallback is intended for continuity and approximate scale only, not high-precision lunar photometry.

This Moon magnitude model is the intentional core-runtime default: it is sufficient for almanac-grade phenomena quantities and easy to compare against SwissEph `swe_pheno*()` and ordinary almanac output. A higher-precision lunar irradiance / photometry model should be added as a separate selectable model, for example a future ROLO-style context option; it should not be mixed with `illuminated_fraction`, which is a geometric quantity.

## Mercury Through Neptune

Mercury, Venus, Mars, Jupiter, Saturn, Uranus, and Neptune use the V-band empirical formulas and branch rules from Mallama & Hilton (2018).

The implemented models include body-specific terms:

- Mars: rotation and orbit corrections from sub-observer and sub-solar longitudes;
- Jupiter: the high-phase branch from the paper;
- Saturn: the ring-included brightness term, including the paper's effective ring-tilt rule for Earth/Sun views;
- Uranus: the planetographic sub-latitude term;
- Neptune: the time-dependent term and high-phase branch.

These are almanac-style empirical magnitudes. They use the active `NativeCalcContext` route and apparent/true position flags. If a route can only provide a barycenter and not the required physical body, the API returns the ordinary no-route status by default. `TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX` is the explicit opt-in for callers that accept a major-planet barycenter approximation.

## Pluto

Pluto currently uses the common small-body H-G phase function:

```text
H = -0.55
G = 0.15
m = H_G(alpha) + 5 log10(r * Delta)
```

`H=-0.55` is Pluto's commonly listed absolute magnitude in physical-parameter tables. `G=0.15` is the IAU H-G / MPC/JPL small-body default slope parameter when no dedicated slope is available. In other words, these are not tuned numbers: they are Pluto's common `H` plus the standard H-G default `G`.

Pluto's real brightness still depends on its phase curve, surface albedo distribution, observing band, and epoch. This implementation is an almanac-grade approximation, not a high-precision Pluto-specific photometric model.

A better long-term route is to store `H/G` or modern `H/G1/G2` parameters for Pluto, asteroids, and TNOs in catalog metadata instead of hard-coding them in the runtime.

## References

- Mallama, A. and Hilton, J. L. (2018), [Computing Apparent Planetary Magnitudes for The Astronomical Almanac](https://arxiv.org/abs/1808.01973).
- Willmer, C. N. A. (2018), [The Absolute Magnitude of the Sun in Several Filters](https://arxiv.org/abs/1804.07788), useful background for solar V-band zero-point differences.
- The Moon `H=0.21`, before/after full Moon phase polynomials, and the `alpha <= 150 deg` validity range are listed in the Astronomical-Almanac-style approximations summarized under [Absolute magnitude: Solar System bodies](https://en.wikipedia.org/wiki/Absolute_magnitude#Solar_System_bodies).
- Pluto `H=-0.55` is listed in common Pluto physical-parameter summaries; for example, [Pluto](https://en.wikipedia.org/wiki/Pluto) lists `Abs Magnitude = -0.55` and cites Planetary Physical Parameters among its sources.
- The minor-planet H-G phase function follows the Bowell-style `H,G` model; the formula shape and the `G=0.15` default convention are summarized in [Absolute magnitude: Solar System bodies](https://en.wikipedia.org/wiki/Absolute_magnitude#Solar_System_bodies).

## SwissEph Comparison

The test suite keeps SwissEph-generated sanity oracles. They check that Taiyin stays in the same practical range as a common reference implementation; they do not assert identical photometric zero points.

In particular:

- the Sun constant can create a systematic difference of about `0.1 mag`;
- the Moon waxing/waning branch and thin-crescent fallback are more sensitive near new Moon;
- Pluto currently uses an empirical H-G model, so model differences against SwissEph or almanacs are expected.
