# Eclipse Search

Status: Current
Last reviewed: 2026-07-01
Primary header: `include/taiyin/runtime/eclipse_search.h`

This document describes Taiyin's eclipse search algorithms, model conventions, and public C++ APIs. The eclipse layer is a numeric runtime feature: it returns Julian dates, classifications, magnitudes, geometric quantities, paths, and local circumstances. Almanac presentation, localized labels, and application-level formatting belong above this layer.

Performance measurements, third-party implementation comparisons, and experimental notes are maintainer material, not part of the public API documentation.

## Scope

The eclipse API covers:

- lunar eclipse solve and search;
- global solar eclipse solve and search;
- local solar-eclipse circumstances for a geographic observer;
- solar Besselian elements, route rows, route curves, and local boundary helpers.

All entry points use the caller's `NativeCalcContext`, so results depend on the ephemeris routes, apparent-position options, timescale policy, Delta T model, eclipse shadow model, and radius models configured in that context. Before calling these APIs, the global ephemeris runtime must be initialized with data that covers the requested date range.

## Sources And Algorithms

### Shared Search Seeds

Both lunar and solar eclipse searches use Jean Meeus, *Astronomical Algorithms*, 2nd ed., chapter 52 as the lunation pre-filter:

- lunar eclipses use the chapter 52 argument-of-latitude threshold for full moons;
- solar eclipses use the corresponding node-distance threshold for new moons;
- Meeus formulas provide cheap approximate maximum-eclipse times before the runtime evaluates full ephemeris geometry.

This pre-filter only generates candidate lunations and initial times. It does not determine final results. Final classification, magnitude, contact times, and route geometry come from the configured ephemeris runtime. Local solar-eclipse search performs observer-local probing, Besselian seeding, and topocentric exact contact refinement after global candidates have been found.

### Lunar Eclipses

The lunar-eclipse solver is a C++ port of the lunar-eclipse geometry route from `sxwnl` `eph.js` / `eph0.js`, with ephemeris inputs supplied by Taiyin runtime ephemerides.

At a candidate full moon, it computes apparent Sun/Moon longitude, latitude, distance, and speed. The Moon center is compared against Earth's umbral axis in angular coordinates. The solver refines greatest eclipse by linearizing local motion in the shadow plane and minimizing the Moon center's distance to the shadow axis.

Classification uses the refined geometry:

- `TAIYIN_ECLIPSE_TOTAL`: the Moon fully enters the umbra;
- `TAIYIN_ECLIPSE_PARTIAL`: the Moon intersects the umbra but does not fully enter it;
- `TAIYIN_ECLIPSE_PENUMBRAL`: the Moon intersects only the penumbra;
- `TAIYIN_ECLIPSE_NONE`: no lunar eclipse occurs for that lunation.

Magnitude uses the traditional diameter-ratio formula:

```text
umbral_magnitude    = (umbra_radius + moon_radius - rho) / (2 * moon_radius)
penumbral_magnitude = (penumbra_radius + moon_radius - rho) / (2 * moon_radius)
```

where `rho` is the distance from the Moon center to the shadow axis.

When `TAIYIN_ECLIPSE_INCLUDE_CONTACTS` is set, the solver fills:

- `P1`: penumbral eclipse begins;
- `U1`: partial eclipse begins;
- `U2`: total eclipse begins;
- `Greatest`: greatest eclipse;
- `U3`: total eclipse ends;
- `U4`: partial eclipse ends;
- `P4`: penumbral eclipse ends.

Non-applicable contact times are `NaN`; for example, `U2` and `U3` are `NaN` for a partial eclipse.

#### 2025-09-07 PMO Lunar Eclipse Oracle

The 2025-09-07 total lunar eclipse UT regression fixture comes from the Purple Mountain Observatory (PMO), Chinese Academy of Sciences, table `2025年9月7日月全食概况`:

- `https://pmo.cas.cn/xwdt2019/kpdt2019/202412/t20241223_7508765.html`

The PMO table gives both Terrestrial Dynamical Time (`TD`) and Beijing time. The JD UT values below are derived by subtracting 8 hours from Beijing time:

| PMO row | PMO TD | PMO Beijing Time | Taiyin field | PMO-derived JD UT |
| --- | ---: | ---: | --- | ---: |
| `半影食始` | `2025-09-07 15:28.0` | `2025-09-07 23:26.9` | `P1` | `2460926.143681` |
| `初亏` | `2025-09-07 16:27.9` | `2025-09-08 00:26.8` | `U1` | `2460926.185278` |
| `食既` | `2025-09-07 17:31.5` | `2025-09-08 01:30.4` | `U2` | `2460926.229444` |
| `食甚` | `2025-09-07 18:13.0` | `2025-09-08 02:11.8` | `Greatest` | `2460926.258194` |
| `生光` | `2025-09-07 18:54.4` | `2025-09-08 02:53.2` | `U3` | `2460926.286944` |
| `复圆` | `2025-09-07 19:58.0` | `2025-09-08 03:56.9` | `U4` | `2460926.331181` |
| `半影食终` | `2025-09-07 20:57.8` | `2025-09-08 04:56.6` | `P4` | `2460926.372639` |

Corresponding PMO-derived UT clock times:

| Taiyin field | UT |
| --- | ---: |
| `P1` | `2025-09-07 15:26:54` |
| `U1` | `2025-09-07 16:26:48` |
| `U2` | `2025-09-07 17:30:24` |
| `Greatest` | `2025-09-07 18:11:48` |
| `U3` | `2025-09-07 18:53:12` |
| `U4` | `2025-09-07 19:56:54` |
| `P4` | `2025-09-07 20:56:36` |

PMO values are rounded to `0.1` minute. The `TD` and Beijing-time columns can therefore only imply Delta T to about one minute precision, consistent within table precision with the expected 2025 value and the independent NASA decade-table greatest-eclipse TD value (`18:12:58 TD`).

Runtime comparison, last checked 2026-06-30:

| Event | PMO JD UT | Taiyin JD UT | Taiyin - PMO |
| --- | ---: | ---: | ---: |
| `P1` | `2460926.143680556` | `2460926.143621061` | `-5.14s` |
| `U1` | `2460926.185277778` | `2460926.185200211` | `-6.70s` |
| `U2` | `2460926.229444444` | `2460926.229415872` | `-2.47s` |
| `Greatest` | `2460926.258194444` | `2460926.258204186` | `+0.84s` |
| `U3` | `2460926.286944444` | `2460926.286975643` | `+2.70s` |
| `U4` | `2460926.331180556` | `2460926.331099947` | `-6.96s` |
| `P4` | `2460926.372638889` | `2460926.372520987` | `-10.19s` |

The Taiyin comparison uses the same model preset as the TS/sxwnl regression fixtures:

| Setting | Value |
| --- | --- |
| Ephemeris data | Taiyin OPM2 major-bodies 600y files |
| Shadow model | `ECLIPSE_SHADOW_CHAUVENET` |
| Shadow Earth scale | `1.02 * 0.998340` |
| Shadow Sun scale | `1.02` |
| Shadow parallax scale | `1.02` |
| Moon radius model | `ECLIPSE_MOON_ALMANAC` |
| Moon radius | `0.2725076 * 6378.1366 km` |
| Apparent options | light-time, aberration, deflection, true ecliptic of date |
| Runtime cache setting | `segment_cache=4096` |

PMO table precision is `0.1` minute, so each PMO time has about `±3s` rounding uncertainty. With the Chauvenet/Almanac preset, Taiyin `Greatest`, `U2`, and `U3` are within that publication precision; `P1`, `U1`, `U4`, and `P4` show about `5-10s` model-level differences.

With the bundled Kaguya TLL1 lunar-limb model enabled, the PMO errors become:

| Event | Circular-limb error | TLL1-limb error |
| --- | ---: | ---: |
| `P1` | `-5.14s` | `+3.10s` |
| `U1` | `-6.70s` | `-1.44s` |
| `U2` | `-2.47s` | `-1.29s` |
| `Greatest` | `+0.84s` | `+0.84s` |
| `U3` | `+2.70s` | `+4.33s` |
| `U4` | `-6.96s` | `-3.02s` |
| `P4` | `-10.19s` | `-5.55s` |

Across all seven times, MAE falls from `5.00s` to `2.79s` and RMSE from
`5.83s` to `3.22s`. Not every individual contact improves: `U3` moves farther
from the published value, and PMO's `0.1`-minute precision itself implies about
`±3s` rounding uncertainty.

### Global Solar Eclipses

The global solar-eclipse solver generates candidates from the Meeus new-moon pre-filter, then computes Sun/Moon vectors through the Taiyin runtime. It constructs the lunar shadow axis and tests its relationship to the WGS84 Earth ellipsoid. Contact times begin from Besselian/root seeds and finish against apparent geometry.

Greatest eclipse is defined as the instant when the shadow axis is closest to Earth, i.e. when `axis_distance_km` is minimal. It is not the minimum penumbral-margin value. This matches the common NASA/PMO global greatest-eclipse definition and avoids coupling greatest time to a changing penumbral radius.

Global solar-eclipse contacts use these conventions:

- `P1`: global partial eclipse begins, first penumbral contact with Earth;
- `C1`: global central eclipse begins, first umbra/antumbra axis arrival at Earth;
- `Greatest`: shadow axis closest to Earth;
- `C4`: global central eclipse ends;
- `P4`: global partial eclipse ends.

The global API does not describe what a specific observer sees. Use local solar-eclipse entry points for observer circumstances.

#### 2024-04-08 PMO Global Solar Eclipse Oracle

The 2024-04-08 global solar-eclipse regression fixture comes from Purple Mountain Observatory public almanac material:

- `https://www.pmo.cas.cn/xwdt2019/kpdt2019/202312/P020240201511299456727.txt`

The PMO table is titled `2024年 4月 8日日全食概况`. Global event rows map to Taiyin global solar contact fields as follows. JD UT values below are derived from PMO UT; current Taiyin output is shown in the runtime comparison table.

| PMO row | PMO UT | Taiyin field | PMO JD UT |
| --- | ---: | --- | ---: |
| `偏食始` | `2024-04-08 15:42:13` | `P1` | `2460409.154317129` |
| `全食始` | `2024-04-08 16:39:59` | `C1` | `2460409.194432870` |
| `食甚` | `2024-04-08 18:17:20` | `Greatest` | `2460409.262037037` |
| `全食终` | `2024-04-08 19:54:28` | `C4` | `2460409.329490741` |
| `偏食终` | `2024-04-08 20:52:21` | `P4` | `2460409.369687500` |

The same PMO row gives the greatest-eclipse location. The fixture uses these rounded values:

| Quantity | PMO value | Fixture value |
| --- | ---: | ---: |
| Greatest latitude | `+25°17.1′` | `25°17.1′` |
| Greatest longitude | `-104°8.6′` | `-104°8.6′` |

PMO times are rounded to whole seconds. Current Taiyin regression output uses Taiyin OPM2 major-bodies 600y files as the ephemeris data source and agrees with the rounded PMO table to about two seconds for this event. Swiss Ephemeris can be used as an independent comparison, but it is not the baseline for this fixture because its global P4/C4 conventions and model choices differ from PMO/Taiyin regression values by several seconds.

Runtime comparison, last checked 2026-07-01:

| Event | PMO JD UT | Taiyin JD UT | Taiyin - PMO |
| --- | ---: | ---: | ---: |
| `P1` | `2460409.154317129` | `2460409.154338569` | `+1.852s` |
| `C1` | `2460409.194432870` | `2460409.194446871` | `+1.209s` |
| `Greatest` | `2460409.262037037` | `2460409.262039739` | `+0.233s` |
| `C4` | `2460409.329490741` | `2460409.329500663` | `+0.858s` |
| `P4` | `2460409.369687500` | `2460409.369681376` | `-0.529s` |

With Kaguya TLL1 enabled and the WGS84 tangent point solved jointly, the
affected `P1/P4` errors become `-0.047s` and `-0.396s`. Both are closer to this
PMO table than the circular-limb results. `C1`, greatest eclipse, and `C4` are
axis/Earth geometry and remain unchanged.

Greatest-location comparison:

| Quantity | PMO value | Taiyin value | Taiyin - PMO |
| --- | ---: | ---: | ---: |
| Greatest latitude | `25.285000°` | `25.289609°` | `+0.004609°` |
| Greatest longitude | `-104.143333°` | `-104.147999°` | `-0.004665°` |

Global `P1/P4` therefore use Besselian roots only as seeds. At each candidate
epoch, the solver jointly minimizes the direction-dependent penumbral margin
over the WGS84 ellipsoid and then solves for a zero minimum. Using either the
cheap Besselian projected-ellipse scalar or a fixed radial-projection tangent
point as the final model produces visible second-level shifts in global partial
contacts.

#### 2026-08-12 PMO Global Solar Eclipse Oracle

Purple Mountain Observatory has published public material for the `2026年8月12日日全食` total solar eclipse:

- page: `https://pmo.cas.cn/xwdt2019/kpdt2019/202512/t20251231_8093683.html`
- overview attachment: `https://pmo.cas.cn/xwdt2019/kpdt2019/202512/P020251231400816723831.txt`
- route attachment: `https://pmo.cas.cn/xwdt2019/kpdt2019/202512/P020260624590816163664.txt`

The public page describes a totality path beginning in far northern Russia, crossing the Arctic Ocean, Greenland, Iceland, the northeastern Atlantic, and Spain, and ending in the western Mediterranean. Partial eclipse visibility covers northern North America, the Arctic Ocean, the northern Atlantic, northwestern Africa, most of Europe, and far northern Asia.

The overview attachment gives global contacts, greatest-eclipse location, magnitude, total duration, and path width. The current regression fixture uses these fields:

| PMO row | PMO UT | Taiyin field | PMO JD UT |
| --- | ---: | --- | ---: |
| `偏食始` | `2026-08-12 15:34:14` | `P1` | `2461265.148773148` |
| `全食始` | `2026-08-12 17:00:06` | `C1` | `2461265.208402778` |
| `食甚` | `2026-08-12 17:45:56` | `Greatest` | `2461265.240231481` |
| `全食终` | `2026-08-12 18:32:12` | `C4` | `2461265.272361111` |
| `偏食终` | `2026-08-12 19:57:59` | `P4` | `2461265.331932870` |

Greatest-eclipse location and path quantities:

| Quantity | PMO value |
| --- | ---: |
| Greatest latitude | `+65°13.3′` |
| Greatest longitude | `-25°15.2′` |
| Magnitude | `1.040` |
| Total duration | `2m21.2s` |
| Path width | `300.3 km` |

Runtime comparison, last checked 2026-07-06:

| Event / quantity | Taiyin - PMO |
| --- | ---: |
| `P1` | `+1.686s` |
| `C1` | `+1.389s` |
| `Greatest` | `+0.629s` |
| `C4` | `+0.531s` |
| `P4` | `+0.098s` |
| Greatest latitude | `+0.001996°` |
| Greatest longitude | `+0.012161°` |
| Total duration | `-2.677s` |
| Path width | `-7.555 km` |

With Kaguya TLL1 enabled and the WGS84 tangent point solved jointly, `P1/P4`
change from `+1.686s / +0.098s` to `-2.269s / -0.467s`. Both remain within the
rough `±3s` publication resolution of the whole-second PMO table, but an
individual topographic contact is not guaranteed to move closer to a rounded
table value. Across all ten global times in the 2024 and 2026 PMO cases, MAE
falls from `0.90s` to `0.80s` and RMSE from `1.07s` to `1.02s`.

This fixture mainly covers high-latitude North Atlantic / European path geometry outside the 2024 North American total-eclipse case. The PMO overview is public almanac material: contact times are listed to whole seconds, and path quantities are rounded at about the `0.1` arcminute / kilometer level. It is therefore a second/kilometer-level sanity oracle, not a higher-precision source for internal geometric constants. `path_width_km` is the local transverse path-width estimate along the center-line normal; when available, it is computed from the intersections between that normal and the north/south limit curves, not from the ground arc distance between the same-instant north and south limits.

### Local Solar Eclipses

Local solar-eclipse routines are a C++ port of the solar-eclipse Besselian/local geometry from `sxwnl` `eph.js` / `eph0.js`, using Taiyin runtime positions underneath. They compute topocentric Sun/Moon circumstances for a geographic longitude, latitude, and height.

Local search scans global solar-eclipse candidates first, then uses an observer-local probe table to decide whether the observer may see the eclipse. Contact times use a Besselian local scalar as a seed and are refined with topocentric apparent geometry. This covers cases where greatest eclipse is below the horizon, but a partial eclipse is still visible at sunrise or sunset.

Local results include:

- observer-visible kind bits;
- local greatest-eclipse time;
- magnitude and obscuration;
- solar altitude and azimuth at greatest eclipse;
- contact times `C1`, `C2`, `C3`, `C4`, and local `Greatest`;
- first/last contact position angle and vertex angle;
- total/annular duration;
- sunrise/sunset magnitudes at relevant instants.

Unless the observer experiences totality or annularity, `C2` and `C3` are `NaN`.

### Solar Route And Besselian Helpers

Route APIs expose lower-level path products:

- `compute_solar_besselian_elements_tt` computes Besselian elements for one instant;
- `compute_solar_besselian_polynomial_tt` samples and fits a polynomial over a time span;
- `compute_solar_eclipse_route_row_*` returns a route row near one instant;
- `compute_solar_eclipse_route_*` samples rows over an interval;
- `compute_solar_eclipse_route_curves_*` returns route and limit curve points;
- `compute_solar_eclipse_route_product_*` returns the core north/south limits and a convenience core-path polygon;
- `compute_solar_eclipse_route_map_product_*` closes core, penumbral, and half-magnitude layers into map-product polygons, using sunrise/sunset maximum boundaries when one physical wide-limit curve does not exist;
- `compute_local_solar_eclipse_boundary_*` computes a local boundary near a given point and time.

These functions are for map/path generation and diagnostics. A simple "is there an eclipse near this date?" query does not need them.

Route curves and route products are derived from Taiyin route rows. They include the geometrically applicable center line, penumbral limits, core limits, and half-magnitude limits sampled through the configured ephemeris runtime. Polygon longitudes are kept with an unwrapped longitude field so antimeridian-crossing envelopes can be rendered without tearing; callers may normalize individual points after projection. These APIs return numeric geometry products, not a finished map layer: downstream code is still responsible for map projection, thinning, segmentation, styling, and tile/viewport clipping.

The default route-curve sampling density is
`TAIYIN_SOLAR_ROUTE_DEFAULT_SAMPLE_COUNT` samples across the source route span,
matching the sxwnl-style map workflow. The `_with_options` variants of
`compute_solar_eclipse_route_curves_*`,
`compute_solar_eclipse_route_product_*`, and
`compute_solar_eclipse_route_map_product_*` accept an explicit
`route_sample_count` in the documented min/max range. This controls exported
curve and polygon point density only; it does not change the ephemeris,
Delta T, radius, shadow, or lunar-limb models.

The default smooth route uses one parameterized `sxwnl::solar::jieX()` scan for
the center line, north/south limits, and sunrise/sunset maximum curves. At an
`mQie` valid/invalid transition, Taiyin bisects the adjacent samples instead of
using the original one-step linear endpoint estimate; this prevents reversed
endpoint order at high latitudes. Center-line and core-limit transition intervals are also
adaptively subdivided to a maximum `0.05` degree spherical segment at the
default density, so the final point count may be slightly larger than the base
`route_sample_count`. Central paths additionally export `core_begin_horizon`
and `core_end_horizon`: the core-radius horizon-intersection curves joining the
north/south limits at each end of the path. A core polygon is exported only
when both of those curves are available, so it never falls back to an
artificial straight end cap. Penumbral and half-magnitude products also expose partial-contact curves,
sunrise/sunset maximum curves, and every physically present north/south limit.
For polar or noncentral events with only one physical limit, the polygon uses
the corresponding sunrise/sunset maximum boundary instead of inventing a
second limit. The curves use refined endpoints at `mQie` solution transitions.
With TLL1 enabled, the main north/south limits and contact times still use the
direction-dependent lunar limb.

Every route-row, curve, and product entry point explicitly accepts a
`uint64_t flags` argument. Route geometry accepts only
`TAIYIN_ECLIPSE_TRUEPOS` and
`TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION`; unrelated eclipse options such as
search direction and contact-output flags are rejected. With lunar-limb
correction enabled, each sampled epoch prepares the lunar orientation and
apparent Sun/Moon vectors once, then iterates the direction-dependent radius
separately for the north and south core, penumbral, and half-magnitude limits.
Total or annular duration in each route row uses the smooth shadow-velocity
result as a seed and then refines C2/C3 at the center point with the same limb
model. The shadow-axis center line is independent of lunar radius. Without the
flag, the original smooth-limb route and duration semantics remain unchanged.

For a noncentral partial eclipse, `compute_solar_eclipse_route_curves_*`
returns the physically present one-sided penumbral limit and returns a
half-magnitude limit only when the event reaches magnitude 0.5. It does not
return a center line or core limits. The other side of the complete partial
visibility region is horizon geometry determined by sunrise, sunset, and
contact times. `compute_solar_eclipse_route_map_product_*` uses that boundary
to close the penumbral polygon without fabricating a second penumbral limit.
It closes the half-magnitude polygon the same way when magnitude 0.5 is
reached. Empty half-magnitude curves and polygons are valid for a shallow
partial eclipse.

## Timescales

Every eclipse API has TT and UT variants:

- `*_tt` functions accept and return TT Julian dates;
- `*_ut` functions accept and return UT Julian dates and report `delta_t_seconds` when the result struct has that field.

Internal ephemeris positions are computed for TT instants by deriving TDB through the context's TDB model. UT variants use the context's Delta T policy for conversion. Use TT when comparing against ephemeris-time tables; use UT for civil-clock applications.

## Flags

Eclipse functions accept `uint64_t flags`. The low 32 bits are native position semantic flags; the high 32 bits are eclipse-specific options.

The currently accepted low-word native flags are:

- `TAIYIN_NATIVE_POSITION_TRUEPOS` / `TAIYIN_ECLIPSE_TRUEPOS`.

`TAIYIN_ECLIPSE_TRUEPOS` is a compatibility alias for `TAIYIN_NATIVE_POSITION_TRUEPOS`. Output-shape flags such as `XYZ`, `SPEED`, `EQUATORIAL`, `RADIANS`, and `TOPOCENTRIC` are rejected because eclipse result shapes are fixed. Position-convention flags that are not wired through the eclipse fast paths, such as `ASTROMETRIC`, `NO_ABERR`, and `NO_GDEFL`, are also rejected rather than silently ignored.

Eclipse-specific options live in the high 32 bits:

- `TAIYIN_ECLIPSE_INCLUDE_CONTACTS`: compute contact times;
- `TAIYIN_ECLIPSE_EXCLUDE_PENUMBRAL`: ignore penumbral-only lunar eclipses;
- `TAIYIN_ECLIPSE_BACKWARD`: search previous instead of next;
- `TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION`: polish solar/lunar contact times
  with the TLL1 model loaded by the global runtime.

Lunar-limb correction is opt-in and requires a globally loaded model. See
[`lunar_limb_model.md`](lunar_limb_model.md) for loading, lifetime, coverage,
and corrected-contact semantics.

By default, eclipse calculation uses the built-in apparent-position route. Light-time, annual aberration, gravitational deflection, Shapiro delay, frame model, TDB/Delta T policy, and related settings are therefore part of Sun/Moon geometry, Besselian seeds, correction windows, and contact refinement. The default context should use the recommended apparent-position convention; this is the configuration to state first when comparing against public almanacs, PMO/NASA material, or Swiss-style oracles.

`TAIYIN_ECLIPSE_TRUEPOS` is not a "more accurate" switch; it changes the model convention. When set, eclipse geometry uses geometric true positions and disables apparent corrections such as light-time, aberration, deflection, and Shapiro delay. It is useful for model experiments, debugging, and regression tests. Public almanac comparisons should usually use the default apparent-position route.

Single-correction eclipse switches such as astrometric-only, no-aberration, or no-deflection are not part of this public API yet. Add them only after the solar, lunar, local, Besselian, and correction-window paths can all consume the same semantics.

## Shadow Models

Lunar-eclipse contact times and magnitudes depend strongly on the selected Earth-shadow model. Solar-eclipse contacts, path width, and route-limit products are also affected by shadow-radius convention, although global solar greatest eclipse remains closest approach of the shadow axis. The model is selected on `NativeCalcContext` with `native_context_set_eclipse_shadow_model`.

Built-in models:

- `dispatch::ECLIPSE_SHADOW_NASA_DANJON`: NASA-style empirical 1% enlargement;
- `dispatch::ECLIPSE_SHADOW_CHAUVENET`: 2% enlargement plus Earth-oblateness factor;
- `dispatch::ECLIPSE_SHADOW_GEOMETRIC`: pure geometry, no atmospheric enlargement;
- `dispatch::ECLIPSE_SHADOW_RAW_DANJON`: Danjon's 1/85 enlargement.

`NativeCalcContext` currently defaults to `ECLIPSE_SHADOW_NASA_DANJON` and `ECLIPSE_MOON_ALMANAC`. In the current validation set, this combination best matches NASA lunar-catalog duration and magnitude. `Chauvenet` is a valid alternative convention, but its contacts and magnitudes should not be directly compared to NASA catalog values.

Important oracle caveat: NASA HTML lunar catalogs provide greatest-eclipse time, magnitudes, and phase durations (`P4-P1`, `U4-U1`, `U3-U2`). They do not provide individual `P1/U1/U2/U3/U4/P4` contact times. Do not derive individual contacts as `greatest +/- duration/2`; contact intervals are usually not symmetric around greatest eclipse.

## Public Entry Points

### Lunar Eclipses

```cpp
solve_lunar_eclipse_at(...)
solve_lunar_eclipse_at_ut(...)
search_next_lunar_eclipse_tt(...)
search_next_lunar_eclipse_ut(...)
search_lunar_eclipses_tt(...)
search_lunar_eclipses_ut(...)
compute_local_lunar_eclipse_visibility_tt(...)
compute_local_lunar_eclipse_visibility_ut(...)
search_next_local_lunar_eclipse_tt(...)
search_next_local_lunar_eclipse_ut(...)
```

Use `solve_*` when the date is near a known eclipse lunation. Use `search_next_*` for next/previous. Use bounded `search_lunar_eclipses_*` for interval queries.

`compute_local_lunar_eclipse_visibility_ut()` and `search_next_local_lunar_eclipse_ut()` describe observer-local lunar-eclipse visibility. They read the observer longitude, latitude, and height from `NativeCalcContext::observer_location`; if the context has no observer location, they return `TAIYIN_ERROR_INVALID_ARGUMENT`. They do not solve a separate local eclipse geometry. Instead, they sample Moon-center altitude/azimuth at the global lunar-eclipse contacts and search moonrise/moonset within the P1-P4 interval. Visibility is reported in `LocalLunarEclipseResultUt::visibility_flags` using the `TAIYIN_ECLIPSE_*_VISIBLE` bits.

Refraction is off by default. If `TAIYIN_LOCAL_LUNAR_ECLIPSE_REFRACTION` is set, contact sampling and moonrise/moonset use the context refraction model and atmosphere fields.

### Global Solar Eclipses

```cpp
solve_solar_eclipse_at(...)
solve_solar_eclipse_at_ut(...)
search_next_solar_eclipse_tt(...)
search_next_solar_eclipse_ut(...)
search_solar_eclipses_tt(...)
search_solar_eclipses_ut(...)
```

Global results describe whether the lunar shadow reaches Earth and when the global event reaches maximum. They do not provide observer-local visibility.

### Local Solar Eclipses

```cpp
solve_local_solar_eclipse_at_tt(...)
solve_local_solar_eclipse_at_ut(...)
search_next_local_solar_eclipse_tt(...)
search_next_local_solar_eclipse_ut(...)
compute_local_solar_circumstances_tt(...)
compute_local_solar_circumstances_ut(...)
```

These entry points are for geographic observers. They read the observer longitude, latitude, and height from `NativeCalcContext::observer_location`; if the context has no observer location, they return `TAIYIN_ERROR_INVALID_ARGUMENT`. `search_next_local_solar_eclipse_*` scans global solar-eclipse candidates first, then computes local circumstances and applies the requested kind filter to the observer-local result. For example, a global total solar eclipse that is only partial at the observer will not be returned by a `TAIYIN_ECLIPSE_TOTAL` local search.

### Solar Path Products

```cpp
compute_solar_besselian_elements_tt(...)
compute_solar_besselian_polynomial_tt(...)
evaluate_solar_besselian_polynomial(...)
compute_solar_eclipse_route_row_tt(...)
compute_solar_eclipse_route_row_ut(...)
compute_solar_eclipse_route_tt(...)
compute_solar_eclipse_route_ut(...)
compute_solar_eclipse_route_curves_tt(...)
compute_solar_eclipse_route_curves_ut(...)
compute_solar_eclipse_route_curves_tt_with_options(...)
compute_solar_eclipse_route_curves_ut_with_options(...)
compute_solar_eclipse_route_product_tt_with_options(...)
compute_solar_eclipse_route_product_ut_with_options(...)
compute_solar_eclipse_route_map_product_tt_with_options(...)
compute_solar_eclipse_route_map_product_ut_with_options(...)
compute_local_solar_eclipse_boundary_tt(...)
compute_local_solar_eclipse_boundary_ut(...)
```

## Usage Examples

### Lunar Eclipse Near A Date

```cpp
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/runtime/eclipse_search.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/time.h"

using namespace taiyin;
using namespace taiyin::runtime;

NativeCalcContext ctx;
native_context_set_geocentric_observer(&ctx, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH);
native_context_set_eclipse_shadow_model(&ctx, dispatch::ECLIPSE_SHADOW_NASA_DANJON);
native_context_set_eclipse_moon_radius_model(&ctx, dispatch::ECLIPSE_MOON_ALMANAC);

LunarEclipseResultUt eclipse;
EphemerisEvalDiagnostic diag = {};
const double guess_ut = julian_day({2025, 9, 7, 18, 0, 0.0});

Status st = solve_lunar_eclipse_at_ut(
    &ctx,
    guess_ut,
    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
    &eclipse,
    &diag);

if (st == TAIYIN_STATUS_OK && eclipse.kind != TAIYIN_ECLIPSE_NONE) {
    // eclipse.maximum_jd_ut and eclipse.contact_jd_ut[] are populated.
}
```

### Local Lunar Eclipse Visibility For An Observer

```cpp
LocalLunarEclipseResultUt local;
EphemerisEvalDiagnostic diag = {};
native_context_set_observer_location(
    &ctx,
    native_observer_location_degrees(116.4074, 39.9042, 43.0));

Status st = search_next_local_lunar_eclipse_ut(
    &ctx,
    julian_day({2025, 9, 7, 0, 0, 0.0}),
    TAIYIN_ECLIPSE_TOTAL,
    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
    &local,
    &diag);

if (st == TAIYIN_STATUS_OK
    && (local.visibility_flags & TAIYIN_ECLIPSE_MAXIMUM_VISIBLE) != 0u) {
    // The Moon is above the horizon at greatest eclipse.
}
```

### Next Total Solar Eclipse

```cpp
SolarEclipseResultUt eclipse;
EphemerisEvalDiagnostic diag = {};

Status st = search_next_solar_eclipse_ut(
    &ctx,
    julian_day({2024, 1, 1, 0, 0, 0.0}),
    TAIYIN_ECLIPSE_TOTAL,
    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
    &eclipse,
    &diag);
```

### Local Solar Eclipse For An Observer

```cpp
LocalSolarEclipseResultUt local;
EphemerisEvalDiagnostic diag = {};
native_context_set_observer_location(
    &ctx,
    native_observer_location_degrees(-96.7970, 32.7767, 131.0));

Status st = solve_local_solar_eclipse_at_ut(
    &ctx,
    julian_day({2024, 4, 8, 18, 0, 0.0}),
    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
    &local,
    &diag);
```

## Validation Notes

Public regression tests use several source types:

- PMO public material: 2024 and 2026 global solar eclipse contacts/greatest location, 2025 total lunar eclipse contacts;
- NASA eclipse catalogs / decade tables: greatest time, classification, magnitude, and duration;
- NASA city-table values: minute-level local solar-eclipse sanity checks;
- `sxwnl` `eph.js` / `eph0.js`: oracle fixtures for ported geometry functions;
- OPM2/SPK data comparison tests: verify ephemeris reading and route composition.

PMO/NASA and similar public sources are preferred as behavior baselines. Old TypeScript fixtures are mainly migration regressions and diagnostic tables; unless a row is explicitly tied to a public source such as PMO/NASA, it is not treated as an authoritative eclipse oracle.

Local solar eclipses have multiple layers of tests: fixed Mazatlan/New York regressions, Dallas NASA city-table minute-level checks, observer-local kind filtering, truepos paths, and sunrise/sunset visible-partial samples. These tests cover classification, contacts, local search filtering, and visibility markers. More published local-circumstances tables are needed before claiming second-level external accuracy for arbitrary local contacts.

Swiss Ephemeris comparisons can be useful for compatibility or model-difference analysis, but they are not a license-neutral public oracle and not the baseline for the public test suite.

## Current Boundaries

- Public API is not frozen before the first release; struct fields and flags may still change as validation proceeds.
- Eclipse results depend on the ephemeris route, apparent options, timescale policy, Delta T model, shadow model, and Moon-radius model in `NativeCalcContext`. Comparisons should report those settings.
- Solar route curves are numeric map/path products. Downstream users usually still need projection, thinning, segmentation, and map rendering.
- Local solar-eclipse APIs compute geometric visibility and contact circumstances. They do not model weather, clouds, terrain obstruction, equipment, or human visual effects.
- Lunar-eclipse contacts, durations, and magnitudes depend strongly on shadow model and Moon-radius model. Without stating the model, second-level or percentage differences between sources should not be treated directly as errors.
- NASA HTML lunar catalogs do not provide individual `P1/U1/U2/U3/U4/P4` contact times. They provide greatest time, magnitudes, and phase durations. Individual contact oracles require sources that publish those contacts explicitly.
