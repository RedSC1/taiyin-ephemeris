# Astrology House Foundations

Status: Current
Primary header: `include/taiyin/astrology/houses.h`

This extension computes the local angular points and the first supported
tropical house systems. It is independent of sidereal/ayanamsha selection;
apply a sidereal offset to the returned tropical longitudes only when a
caller explicitly needs a sidereal chart.

## Input And Reference Contract

```cpp
calc_houses_ut(&context, jd_ut, system_id, &result)
calc_houses_tt(&context, jd_tt, system_id, &result)
calc_houses_from_armc(armc_rad, latitude_rad, true_obliquity_rad, system_id, &result)
```

`NativeCalcContext` must have a valid observer location installed through
`native_context_set_observer_location()` or a topocentric observer setter.
Longitude is east-positive. Height, atmospheric settings, and topocentric
position corrections do not affect house geometry.

`calc_houses_from_armc()` is the time-model-independent geometry entry point.
It accepts radians and does not read a `NativeCalcContext`. It is useful when
the caller already has local ARMC and the true obliquity for the intended
epoch.

For `calc_houses_ut()` and `calc_houses_tt()`, the result uses the context's
selected precession and nutation models. `calc_houses_from_armc()` instead uses
the caller-supplied ARMC and true obliquity directly:

- `armc_rad` is local apparent sidereal time.
- `ascendant_rad`, `midheaven_rad`, `vertex_rad`, `east_point_rad`, and every
  cusp are tropical longitudes on the true ecliptic of date.
- `calc_houses_ut()` interprets its input as UT1. `calc_houses_tt()` derives
  UT1 with the context's selected Delta-T model before evaluating sidereal time.

The time-based entry points also return centered angular rates in radians per
input-scale day for ARMC, ASC, MC, Vertex, East Point, and all twelve cusps.
`calc_houses_from_armc()` has no time axis, so its rate fields remain `NAN`.
If either neighboring sample resolves to a different fallback model, the
position result remains valid, rates remain `NAN`, and
`TAIYIN_HOUSE_RESULT_SPEED_UNAVAILABLE` is set.

## Supported Systems

| Identifier | Meaning |
| --- | --- |
| `TAIYIN_HOUSE_SYSTEM_WHOLE_SIGN` | The first cusp is the beginning of the Ascendant's 30-degree sign. |
| `TAIYIN_HOUSE_SYSTEM_EQUAL` | The first cusp is the Ascendant; further cusps are 30 degrees apart. |
| `TAIYIN_HOUSE_SYSTEM_PORPHYRY` | The MC-to-ASC and ASC-to-IC ecliptic arcs are each trisected. |
| `TAIYIN_HOUSE_SYSTEM_PLACIDUS` | Semidiurnal and seminocturnal arcs are trisected through the standard iterative Placidus construction. |
| `TAIYIN_HOUSE_SYSTEM_KOCH` | The ascensional difference of the MC is trisected. |
| `TAIYIN_HOUSE_SYSTEM_REGIOMONTANUS` | The celestial equator is divided and projected through great-circle house circles. |
| `TAIYIN_HOUSE_SYSTEM_CAMPANUS` | The prime vertical is divided into equal sectors and projected onto the ecliptic. |
| `TAIYIN_HOUSE_SYSTEM_ALCABITIUS` | The Ascendant's semidiurnal and seminocturnal arcs are trisected. |
| `TAIYIN_HOUSE_SYSTEM_POLICH_PAGE` | Polich/Page topocentric house-circle construction. |
| `TAIYIN_HOUSE_SYSTEM_MORINUS` | Equatorial points separated by 30 degrees are transformed to the ecliptic. |

`HouseResult::cusp_longitude_rad[0]` is the first-house cusp. The requested
and resolved system identifiers are both returned. If Placidus is unavailable
inside the polar circle, or its iterative construction does not converge, it
falls back to Porphyry. Koch uses the same fallback inside the polar circle.
The result keeps the original `requested_system_id`, records the final model in
`resolved_system_id`, and sets `TAIYIN_HOUSE_RESULT_USED_FALLBACK`. It also sets
`TAIYIN_HOUSE_RESULT_FALLBACK_PORPHYRY` when the final resolved model is
Porphyry.

## Continuous Cusp-Arc Position

```cpp
calc_house_position_from_longitude(&houses, longitude_rad, &position)
```

This helper locates a tropical ecliptic longitude in an existing
`HouseResult`. It returns a one-based house number, a fraction from that cusp
to the next cusp, and `house_number + fraction`. An exact cusp belongs to the
following house.

This is deliberately a longitude/cusp-partition operation. It does not claim
the latitude-sensitive semi-arc semantics used by specialized Placidus or
Gauquelin point-placement formulas. Callers needing that convention should not
substitute this result.

## Custom House Systems

House-system selection uses the extension registry rather than a hard-coded
switch in the public calculation path. A custom system can be installed with:

```cpp
bool my_houses(
    const HouseSystemDispatchData* data,
    double cusps_rad[12]
);

add_house_system_model(HouseSystemModelEntry(
    TAIYIN_HOUSE_SYSTEM_CUSTOM_START,
    &my_houses,
    TAIYIN_HOUSE_SYSTEM_PORPHYRY));
```

Custom IDs must be at least `TAIYIN_HOUSE_SYSTEM_CUSTOM_START`. Built-in and
existing IDs cannot be replaced. A fallback must already be registered, which
keeps fallback chains acyclic by construction. The callback receives normalized
ARMC, geodetic latitude, true obliquity, ASC, and MC. It returns `true` only
after filling all 12 finite tropical cusp longitudes in radians.
Callbacks may be invoked concurrently, and registered models cannot be removed;
their code must remain loaded and concurrency-safe for the remainder of the
process.

## Polar-Latitude Behavior

At high latitude, the raw horizon/ecliptic intersection can select the western
branch of the Ascendant. The supported systems apply the standard opposite-
branch correction before deriving their cusps, matching the conventional
Swiss-compatible Equal, Whole Sign, and Porphyry behavior. This is not a
Porphyry fallback.

Placidus and Koch are unavailable when
`abs(latitude) >= 90 degrees - true obliquity`. They use the documented
Porphyry fallback described above.
