# Solar Visibility

Status: Current  
Last reviewed: 2026-08-07  
Primary headers: `include/taiyin/runtime/solar_visibility.h`, `include/taiyin/c/visibility.h`

This module searches Sun rise, set, twilight, and meridian-transit events for a
geographic observer. The context supplies the ephemeris routes, time-scale
models, apparent-position convention, observer/atmosphere configuration, and
refraction model. The C++ entry points use `SplitJulianDate`; matching C ABI
functions use `taiyin_split_julian_date` and `taiyin_*` names.

## Precise Searches

```cpp
search_solar_rise_set_ut(...)
search_solar_rise_set_at_horizon_ut(...)
search_solar_twilight_ut(...)
search_solar_transit_ut(...)
```

`search_solar_rise_set_ut()` uses the ordinary apparent horizon. The
`_at_horizon_` form accepts an explicit geometric horizon altitude in radians.
Both accept a rise/set event kind, an upper/center/lower solar-limb choice, and
visibility flags. `search_solar_twilight_ut()` selects civil, nautical, or
astronomical twilight. `search_solar_transit_ut()` searches upper or lower
meridian transit.

Each rise/set or transit search returns a `SolarVisibilityEventResult` with an
altitude state, crossing direction, refined UT instant, residual extrema, and
sample/refinement counts. An interval with no ordinary crossing can report
`ALWAYS_ABOVE`, `ALWAYS_BELOW`, or `TANGENT`; callers must inspect
`altitude_state`, not assume a finite event time.

## Fast Daily Rise/Set And Transit

```cpp
compute_solar_rise_set_fast_tt(
    context, center_jd_tt, longitude_deg, latitude_deg, height_m,
    limb_kind, horizon_altitude_rad, solar_visibility_flags, out, diagnostic)

compute_solar_transit_fast_tt(
    context, center_jd_tt, longitude_deg, latitude_deg, height_m,
    out, diagnostic)
```

The fast rise/set entry is a one-day, TT-facing convenience calculation. It
uses a local analytical seed with a small apparent-geometry refinement; at
high latitudes or difficult crossings it falls back to the ordinary visibility
search so that limb and refraction semantics remain the same. It returns TT
rise/set epochs and an altitude state. Use the interval-search APIs when the
caller needs an arbitrary time range, explicit event direction, or full
residual diagnostics.

`longitude_deg` is east-positive. `latitude_deg` is in `[-90, 90]` and
`height_m` is observer height above the WGS84 ellipsoid. The supplied location
is used for this call even if the source context has no stored observer
location.

`limb_kind` is one of:

- `TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER`;
- `TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER`;
- `TAIYIN_SOLAR_VISIBILITY_LIMB_LOWER`.

`horizon_altitude_rad` is a geometric horizon offset. Pass `0.0` for the
ordinary horizon, then use the limb and refraction options to select the
observed convention.

## Visibility And Atmosphere Flags

The C++ constants below have matching C ABI names with the
`TAIYIN_VISIBILITY_` prefix.

- `TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION` requests apparent, refracted
  altitude.
- `TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION` requests geometric true
  altitude.
- `TAIYIN_SOLAR_VISIBILITY_FLAG_FIXED_DISC_SIZE` uses the documented fixed
  solar-disc convention instead of the distance-dependent physical angular
  radius.
- `TAIYIN_SOLAR_VISIBILITY_STRICT_METEOROLOGY` is a high-word flag that
  forbids use of a standard-atmosphere fallback for a refracted request.

With neither refraction flag set, public solar rise/set APIs select refraction.
`REFRACTION` and `NO_REFRACTION` are mutually exclusive. A geometric request
does not require atmosphere data. A refracted request uses atmosphere fields
from `NativeCalcContext`; without them it succeeds only when the context has
explicitly enabled `TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK`.
`STRICT_METEOROLOGY` disables that fallback and therefore requires valid
caller-supplied atmosphere fields.

Invalid limb kinds, contradictory/unknown flags, invalid coordinates, or an
unavailable requested refraction model return `TAIYIN_ERROR_INVALID_ARGUMENT`.
