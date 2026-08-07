# Heliacal Visibility

`calc_body_heliacal_visibility_ut()` and
`calc_star_heliacal_visibility_ut()` evaluate whether a point-like target is
visible at one topocentric UT instant during morning or evening twilight.
They return the target and Sun geometry, atmospheric attenuation, limiting
magnitude, and a signed visibility margin. A positive
`visibility_margin_magnitude` means that the target is brighter than the
selected profile's limit. Profiles with an invertible Sun-altitude criterion
also populate `required_sun_altitude_rad` and
`solar_depression_margin_rad`.

The caller supplies the observer location through `NativeCalcContext`. The
calculation uses unrefracted target and Sun altitudes because the visibility
profiles model atmospheric attenuation directly; it does not reinterpret an
ordinary rise/set refraction setting as an extinction model.

All public heliacal functions take a `uint64_t flags` argument. The low 32 bits
accept `TAIYIN_NATIVE_POSITION_TRUEPOS`,
`TAIYIN_NATIVE_POSITION_ASTROMETRIC`, `TAIYIN_NATIVE_POSITION_NO_ABERR`, and
`TAIYIN_NATIVE_POSITION_NO_GDEFL`. Output-shape flags are rejected because the
API always evaluates a topocentric horizontal direction. The high 32 bits are
heliacal-specific: `TAIYIN_HELIACAL_VISIBILITY_INCLUDE_MOONLIGHT` enables the
optional lunar background, while
`TAIYIN_HELIACAL_VISIBILITY_STRICT_METEOROLOGY` forbids synthesized weather.

## Built-in Profiles

The default `NativeCalcContext` selection is
`dispatch::HELIACAL_VISIBILITY_SCHAEFER_1993`. It is the physical point-source
profile used for the external SwissEph behavior oracles in this repository.
Without an explicit `HeliacalVisibilityConditions::extinction_mag_per_airmass`,
Taiyin derives visual extinction with Schaefer's 2000 component model:
Rayleigh scattering, water vapor, ozone, and aerosols. Set
`TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK` in the context's atmosphere
policy to permit its standard-clear setup, which uses ISA pressure and
temperature at observer height plus 40% relative humidity. That is a model
convention, not live local weather.

For a local observation, pass an explicit extinction coefficient, or provide
pressure, temperature, relative humidity through `native_context_set_atmosphere()`
and a meteorological range through `native_context_set_meteorological_range_km()`.
With
`TAIYIN_HELIACAL_VISIBILITY_STRICT_METEOROLOGY`, those four inputs are required
unless an explicit extinction coefficient is supplied; the API returns
`TAIYIN_ERROR_INVALID_ARGUMENT` rather than applying standard-clear values.

`dispatch::HELIACAL_VISIBILITY_BELOKRYLOV_2011` is an alternate observational
twilight relation published by Belokrylov, Belokrylov, and Nickiforov (2011):

- its bright and faint target branches predict the Sun depression needed for
  a source of the target's attenuated visual magnitude;
- the target attenuation uses their near-horizon air-mass expression;
- a target less than 58 degrees from the Sun receives the paper's additional
  twilight-background correction.

The profile is calibrated for cloud-free naked-eye twilight observations.
Without an override, it uses the paper's reference visual extinction of
`0.25 mag/airmass`. For a local measured value, provide
`HeliacalVisibilityConditions::extinction_mag_per_airmass`.

The profile currently does **not** include moonlight, artificial sky glow,
clouds, color response, optics, or an observer-specific visual-acuity model.
It is therefore an explicit visibility criterion, not a guarantee that a
human observer will see the target.

The Moon is deliberately rejected by the body entry point. Lunar first/last
visibility needs a crescent-specific model (including crescent width and lunar
background geometry), rather than the point-source criterion used here.

The source paper is [Model of the Stellar Visibility During Twilight
(2011)](https://www.astro.bas.bg/AIJ/issues/n16/08_MNikifor2.pdf). Its
near-horizon attenuation treatment is compatible in spirit with the component
air-mass discussion in [Schaefer (1993)](https://ntrs.nasa.gov/citations/19950037102),
but the two profiles are intentionally not blended.

`dispatch::HELIACAL_VISIBILITY_SCHAEFER_1993` is the default physical
point-source profile. It implements Schaefer's day/twilight/night background
terms and applies the Hecht (1947) point-source threshold to the attenuated
target flux. It reports `sky_brightness_nanolambert`,
`threshold_illuminance_footcandles`,
`target_illuminance_footcandles`, and the profile-independent
`visibility_margin_magnitude`.

This first Schaefer profile assumes a naked-eye, cloud-free, dark-site
observation. Artificial sky glow is deliberately excluded. Moonlight is an
explicit opt-in: set `TAIYIN_HELIACAL_VISIBILITY_INCLUDE_MOONLIGHT` in the
function flags to use
the V-band Krisciunas-Schaefer (1991) scattered-moonlight model. It uses the
Moon's phase angle, target-Moon separation, and separate scattering-air-mass
terms, and reports its contribution as `moonlight_brightness_nanolambert`.
The paper reports an 8% to 23% prediction uncertainty, so this is a useful
physical estimate rather than a site photometer. Its Table 2 V-band case is
kept as a numeric regression test. The source paper is [A Model of the
Brightness of Moonlight (1991)](https://articles.adsabs.harvard.edu/pdf/1991PASP..103.1033K).
Requesting modeled moonlight without a measured sky background from a profile
that does not declare a moonlight component returns `TAIYIN_ERROR_UNSUPPORTED`;
it never silently ignores the request.

Applications with a measured target-direction background may set
`HeliacalVisibilityConditions::sky_brightness_nanolambert`; that value takes
precedence over the profile's calculated day/twilight/night/moon background. The
default dark-night contribution is `180 nL` and can be replaced through
`night_sky_brightness_nanolambert`.

## Model Registry

Heliacal profiles use the same process-wide `dispatch` registry pattern as
precession, nutation, and refraction models:

```cpp
taiyin::runtime::native_context_set_heliacal_visibility_model(
    &context,
    taiyin::dispatch::HELIACAL_VISIBILITY_SCHAEFER_1993);
```

`HeliacalVisibilityResult` records the selected profile and the profile's
extinction, twilight, and visual-threshold component IDs. Applications may
register a custom profile with an ID at or above
`HELIACAL_VISIBILITY_CUSTOM_START` using
`dispatch::add_heliacal_visibility_model()`. The evaluator receives a
`HeliacalVisibilityModelInput` and fills a `HeliacalVisibilityResult`.

Both built-in profile IDs are registered during runtime initialization. The
Schaefer profile identifies its components as Schaefer (2000) extinction and
twilight, Krisciunas-Schaefer (1991) moonlight, and a Hecht (1947) point-source
threshold; it is intentionally not a replacement for the observational
Belokrylov criterion.

## Event Search

`search_next_body_heliacal_visibility_ut()` and
`search_next_star_heliacal_visibility_ut()` find the next date-level
morning-first, morning-last, evening-first, or evening-last transition. The
caller supplies an explicit `max_search_days` bound; the API does not hide a
multi-year search horizon.

For each civil UT day the search obtains the relevant unrefracted solar window
between Sun-center altitudes `-18 deg` and `-0.85 deg`. It samples the shared
visibility margin across that window, then applies a bounded Brent
maximization only to candidate windows near the limit or at the daily
visible/non-visible transition. The parabolic proposal is rejected in favor of
a golden-section step whenever it is not safely inside the bracket.
High-latitude days without a complete
astronomical-twilight window are skipped rather than treated as invisible.

The returned `jd_ut` is the best sampled/refined instant inside the selected
event window; it is not a claim to be the exact instant when an observer first
perceives the target. `window_start_jd_ut`, `window_end_jd_ut`, and the
embedded `HeliacalVisibilityResult` retain the information needed by a caller
that wants to apply a stricter local rule.

## External Oracle

The test suite keeps fixed Swiss Ephemeris SWIEPH se1 Venus event optima for
the Schaefer profile: latitude/longitude/height `(0, 0, 0)`, pressure
`1013.25 hPa`, temperature `15 C`, relative humidity `40%`, extinction
coefficient `0.25`, naked-eye observer, and moonlight disabled. The oracle
compares Taiyin's best-window time with Swiss `swe_heliacal_ut()` `dret[1]`
for all four morning/evening first/last cases, using a ten-minute tolerance.
This is an external behavior check, not a claim that the two libraries expose
identical atmospheric or visual-acuity semantics. Belokrylov remains verified
against its own paper relations rather than forced to match Swiss dates.
