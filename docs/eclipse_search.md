# Eclipse Search

Status: Current
Last reviewed: 2026-08-11
Primary header: `include/taiyin/runtime/eclipse_search.h`

This document describes Taiyin's eclipse search algorithms, model conventions, and public C++ APIs. The eclipse layer is a numeric runtime feature: it returns Julian dates, classifications, magnitudes, geometric quantities, paths, and local circumstances. Almanac presentation, localized labels, and application-level formatting belong above this layer.

Performance measurements, third-party implementation comparisons, and experimental notes are maintainer material, not part of the public API documentation.

## Scope

The eclipse API covers:

- lunar eclipse solve and search;
- global solar eclipse solve and search;
- local solar-eclipse circumstances for a geographic observer;
- solar Besselian elements, route rows, route curves, and local boundary helpers.

All entry points use the caller's `NativeCalcContext`, so results depend on the ephemeris routes, apparent-position options, Delta-T model, eclipse shadow model, and radius models configured in that context. Before calling these APIs, the global ephemeris runtime must be initialized with data that covers the requested date range.

## Sources And Algorithms

### Shared Search Seeds

Both lunar and solar eclipse searches use Jean Meeus, *Astronomical Algorithms*, 2nd ed., chapter 52 as the lunation pre-filter:

- lunar eclipses use the chapter 52 argument-of-latitude threshold for full moons;
- solar eclipses use the corresponding node-distance threshold for new moons;
- Meeus formulas provide cheap approximate maximum-eclipse times before the runtime evaluates full ephemeris geometry.

This pre-filter only generates candidate lunations and initial times. It does not determine final results. Final classification, magnitude, contact times, and route geometry come from the configured ephemeris runtime. Local solar-eclipse search performs observer-local probing, Besselian seeding, and topocentric exact contact refinement after global candidates have been found.

### Lunar Eclipses

The lunar-eclipse runtime uses three-dimensional shadow-axis geometry. The older
`sxwnl` angular-coordinate implementation remains only as a regression oracle
and is not linked into the production runtime.

At a candidate full moon, let `M` and `S` be the apparent Earth-to-Moon and
Earth-to-Sun vectors in the true ecliptic frame of date. The anti-solar shadow
axis and the Moon's perpendicular displacement from it are:

```text
u = -S / |S|
s = M dot u
q = M - s * u
```

The solver defines greatest eclipse as the minimum of `q dot q`, so greatest is
the instant when the Moon's center is physically closest to Earth's shadow axis.
Earth's umbral and penumbral radii are recomputed at the Moon's axial distance
`s`; the contact boundary is therefore allowed to change as the Moon moves
through the shadow cone.

Contact solving takes five full apparent-geometry samples across a twelve-hour
window centered on greatest eclipse. It fits separate cubic least-squares
polynomials to the three components of `q` and to each contact-boundary radius,
then solves `q(t) dot q(t) - R(t)^2 = 0`. With the Kaguya TLL1 lunar-limb model,
each applicable contact receives one final Newton correction evaluated against
the exact apparent geometry.

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

Runtime comparison, last checked 2026-08-10:

| Event | PMO JD UT | Taiyin JD UT | Taiyin - PMO |
| --- | ---: | ---: | ---: |
| `P1` | `2460926.143680556` | `2460926.143706255` | `+2.22s` |
| `U1` | `2460926.185277778` | `2460926.185258877` | `-1.63s` |
| `U2` | `2460926.229444444` | `2460926.229429345` | `-1.30s` |
| `Greatest` | `2460926.258194444` | `2460926.258208147` | `+1.18s` |
| `U3` | `2460926.286944444` | `2460926.286998820` | `+4.70s` |
| `U4` | `2460926.331180556` | `2460926.331178352` | `-0.19s` |
| `P4` | `2460926.372638889` | `2460926.372648492` | `+0.83s` |

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

PMO table precision is `0.1` minute, so each PMO time has about `±3s` rounding
uncertainty. With the Chauvenet/Almanac preset, six of the seven circular-limb
times are within that publication precision; `U3` differs by `+4.70s`. Across
all seven times, circular-limb MAE is `1.72s`, RMSE is `2.19s`, and the maximum
absolute difference is `4.70s`.

With the bundled Kaguya TLL1 lunar-limb model enabled, the PMO errors become:

| Event | Circular-limb error | TLL1-limb error |
| --- | ---: | ---: |
| `P1` | `+2.22s` | `+1.07s` |
| `U1` | `-1.63s` | `-2.38s` |
| `U2` | `-1.30s` | `-1.71s` |
| `Greatest` | `+1.18s` | `+1.18s` |
| `U3` | `+4.70s` | `+4.77s` |
| `U4` | `-0.19s` | `-2.10s` |
| `P4` | `+0.83s` | `-3.53s` |

Across all seven times, the TLL1 run has MAE `2.39s`, RMSE `2.69s`, and maximum
absolute difference `4.77s`. TLL1 is a direction-dependent physical limb model,
not a tuning switch for this rounded PMO table; the publication does not state
that its contact values use Kaguya topography. The final exact Newton correction
limits the measured TLL1 contact interpolation error to `0.021s` against direct
runtime geometry.

### Global Solar Eclipses

The global solar-eclipse solver generates candidates from the Meeus new-moon pre-filter, then searches directly on instantaneous Sun/Moon vectors. It does not construct a Besselian polynomial. Greatest eclipse minimizes the squared norm of the three-dimensional perpendicular from the geocenter to the Sun-Moon shadow axis. For a circular lunar limb, every cone generator is intersected analytically with the WGS84 Earth ellipsoid: substituting the generator line into the ellipsoid produces one quadratic equation. Global `P1/P4` are the times when the maximum forward-generator discriminant is zero, i.e. when the penumbral cone is tangent to Earth. Center-line begin/end similarly solve the shadow-axis/ellipsoid discriminant.

Each event initializes one apparent-correction window around the new-moon seed and reuses it throughout greatest and contact refinement. Search evaluations deliberately skip UT/GAST and the Besselian `mu` angle. Earth rotation changes the geographic longitude of a shadow intersection, but it cannot change whether a circular cone intersects an axisymmetric oblate Earth. A complete Earth-fixed transform is therefore evaluated only after greatest-eclipse convergence when a geographic location is requested.

Global searches also apply their requested eclipse-kind filter immediately
after greatest and preliminary classification. A candidate that cannot match
is returned to the search loop before geographic location, contact roots, or
hybrid refinement are computed. Accepted events still complete all requested
outputs. Preliminary central total/annular events are retained for a
hybrid-only filter because the final hybrid classification is known only after
central-kind refinement.

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

Runtime comparison, last checked 2026-08-10:

| Event | PMO JD UT | Taiyin JD UT | Taiyin - PMO |
| --- | ---: | ---: | ---: |
| `P1` | `2460409.154317129` | `2460409.154337802` | `+1.786s` |
| `C1` | `2460409.194432870` | `2460409.194446871` | `+1.209s` |
| `Greatest` | `2460409.262037037` | `2460409.262039739` | `+0.233s` |
| `C4` | `2460409.329490741` | `2460409.329500663` | `+0.858s` |
| `P4` | `2460409.369687500` | `2460409.369682533` | `-0.429s` |

With Kaguya TLL1 enabled and the WGS84 tangent point solved jointly, the
affected `P1/P4` errors become `-0.047s` and `-0.396s`. Both are closer to this
PMO table than the circular-limb results. `C1`, greatest eclipse, and `C4` are
axis/Earth geometry and remain unchanged.

Greatest-location comparison:

| Quantity | PMO value | Taiyin value | Taiyin - PMO |
| --- | ---: | ---: | ---: |
| Greatest latitude | `25.285000°` | `25.289609°` | `+0.004609°` |
| Greatest longitude | `-104.143333°` | `-104.147999°` | `-0.004665°` |

Global `P1/P4` use direct time bracketing around greatest eclipse; they do not
depend on Besselian roots. For the circular limb, the final scalar is the maximum normalized discriminant among the
penumbral cone generators after analytic intersection with the WGS84 ellipsoid.
The scalar is negative before contact, zero at tangency, and positive while the
forward cone intersects Earth. With TLL1 enabled, the final solver instead
minimizes the direction-dependent profiled penumbral margin jointly over the
ellipsoid. The cheap projected-ellipse scalar remains only a seed; it is not the
final contact model.

At greatest eclipse, `penumbral_margin_km` is derived from that same final
geometry. For the circular limb it is the signed clearance in the
WGS84 ellipsoid-normalized radial metric; with TLL1 it is the signed profiled
surface clearance. In both cases a negative value means that the penumbra
intersects Earth, so the diagnostic cannot contradict the event kind.

After an event and its physical time bounds are known, route and map APIs may
construct a Besselian polynomial as an event-local acceleration layer. This
keeps repeated route-row evaluation cheap without making event discovery or
global contact semantics depend on the polynomial fit.

The direct-cone regression includes the grazing partial eclipse of
1935-01-05. NASA lists it as the smallest partial eclipse in the 1901–2000
catalog, with magnitude about `0.00126`. The former projected-radius rejection
missed this event; the cone/ellipsoid discriminant retains it and reproduces
the catalog greatest time `05:35:46 TD`:
`https://eclipse.gsfc.nasa.gov/SEdecade/SEdecade1931.html`.

#### 2026-08-12 PMO Global Solar Eclipse Oracle

Purple Mountain Observatory has published public material for the `2026年8月12日日全食` total solar eclipse:

- page: `https://pmo.cas.cn/xwdt2019/kpdt2019/202512/t20251231_8093683.html`
- route attachment: `https://pmo.cas.cn/xwdt2019/kpdt2019/202512/P020251231400816723831.txt`
- overview attachment: `https://pmo.cas.cn/xwdt2019/kpdt2019/202512/P020260624590816163664.txt`

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

Runtime comparison, last checked 2026-08-10:

| Event / quantity | Taiyin - PMO |
| --- | ---: |
| `P1` | `+1.532s` |
| `C1` | `+1.389s` |
| `Greatest` | `+0.629s` |
| `C4` | `+0.531s` |
| `P4` | `+0.196s` |
| Greatest latitude | `+0.001996°` |
| Greatest longitude | `+0.012161°` |
| Total duration | `-2.677s` |
| Path width | `-7.555 km` |

With Kaguya TLL1 enabled and the WGS84 tangent point solved jointly, `P1/P4`
change from `+1.532s / +0.196s` to `-2.269s / -0.467s`. Both remain within the
rough `±3s` publication resolution of the whole-second PMO table, but an
individual topographic contact is not guaranteed to move closer to a rounded
table value. Across all ten global times in the 2024 and 2026 PMO cases, MAE
changes from `0.88s / 1.03s` (circular-limb MAE/RMSE) to `0.80s / 1.02s`
(TLL1-limb MAE/RMSE).

This fixture mainly covers high-latitude North Atlantic / European path geometry outside the 2024 North American total-eclipse case. The PMO overview is public almanac material: contact times are listed to whole seconds, and path quantities are rounded at about the `0.1` arcminute / kilometer level. It is therefore a second/kilometer-level sanity oracle, not a higher-precision source for internal geometric constants. `path_width_km` is the local transverse path-width estimate along the center-line normal. Route-table generation intersects that normal with the closed core-path polygon, including the sunrise/sunset horizon caps; this avoids the singular extrapolation of a north/south limit near path emergence and disappearance. It is not the ground arc distance between the same-instant north and south limits.

The PMO five-second [2026 route table](https://pmo.cas.cn/xwdt2019/kpdt2019/202512/P020251231400816723831.txt) is also checked as a 1,059-row differential oracle over the complete-path rows from `17:02:05` through `18:30:15` UT. Coordinate errors below are great-circle angular separations; duration and width columns use absolute error. Last checked 2026-08-11 with the OPM2 major-bodies 600y data and the smooth circular-limb route model:

| Metric | Pre-refactor route | Current closed-path route |
| --- | ---: | ---: |
| Northern limit MAE | `0.03565°` | `0.03565°` |
| Center line MAE | `0.01147°` | `0.01147°` |
| Southern limit MAE | `0.01121°` | `0.01121°` |
| Central duration MAE | `1.896s` | `1.896s` |
| Path-width MAE | `4.00 km` | `2.74 km` |
| Path-width p95 | `11.55 km` | `5.72 km` |
| Path-width maximum | `82.73 km` | `6.33 km` |

The geometry rewrite therefore leaves the route coordinates and duration materially unchanged while removing the endpoint-width singularity. The first/last complete current widths are `282.1 / 306.3 km`, versus PMO `280.2 / 300.1 km`. The width is measured by intersecting the center-line normal with the closed core-path polygon, including the sunrise/sunset horizon caps; it is not the ground arc between same-instant north and south limits.

Limit coordinates themselves are much more ill-conditioned at a path cusp. The first northern-limit row has `0.785°` angular error even though the all-row center-line MAE is only `0.0115°`; that northern branch moves by more than a degree over the next five seconds as it emerges from the horizon cap. This value is retained as a model-sensitive diagnostic rather than corrected with an empirical time or coordinate offset.

With Kaguya TLL1 enabled for the same 1,059 rows, the current north/center/south MAEs are `0.03089° / 0.01147° / 0.07192°`, duration MAE is `0.719s`, and path-width MAE/p95/maximum are `8.23 / 13.85 / 14.56 km`. This mixed comparison must not be read as a ranking of the physical limb models: the PMO publication does not identify a Kaguya topographic profile and its route widths are closer to Taiyin's smooth almanac-radius convention. TLL1 remains an explicit direction-dependent lunar-topography model, not an oracle-tuning switch.

### Local Solar Eclipses

Local solar-eclipse routines use Taiyin's Besselian seeds, topocentric apparent
Sun/Moon geometry, and observer visibility model for a geographic longitude,
latitude, and height. The final contact and greatest-eclipse refinements do not
call the archived compatibility implementation.

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

Local solar APIs use a geometric (unrefracted) sunrise/sunset visibility window
by default. Set `TAIYIN_ECLIPSE_LOCAL_REFRACTION` to use the context's apparent,
refracted solar rise/set window instead. This option only decides whether the
eclipse reaches the observer's visibility window and which sunrise/sunset
instants contribute their magnitude; it does not change the maximum-eclipse
instant, magnitude at maximum, obscuration, or topocentric central geometry.

`TAIYIN_ECLIPSE_LOCAL_STRICT_METEOROLOGY` is valid only together with
`TAIYIN_ECLIPSE_LOCAL_REFRACTION`. It prohibits standard-atmosphere fallback,
so the caller must supply valid atmosphere fields. A refracted local request
without atmosphere data likewise needs the context to opt into
`TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK`; otherwise it returns
`TAIYIN_ERROR_INVALID_ARGUMENT`. Both local visibility options are rejected by
global solar and lunar eclipse APIs rather than silently changing their
semantics.

### Solar Route And Besselian Helpers

Route APIs expose lower-level path products:

- `compute_solar_besselian_elements_tt` computes Besselian elements for one instant;
- `compute_solar_besselian_polynomial_tt` samples and fits a polynomial over a time span;
- `compute_solar_eclipse_where_*` returns lightweight instantaneous global geometry: the center line and core/penumbral north/south limits;
- `compute_solar_eclipse_route_row_*` returns a route row near one instant;
- `compute_solar_eclipse_route_*` samples rows over an interval;
- `compute_solar_eclipse_route_curves_*` returns route and limit curve points;
- `compute_solar_eclipse_route_product_*` returns the core north/south limits and a convenience core-path polygon;
- `compute_solar_eclipse_route_map_product_*` closes core, penumbral, and half-magnitude layers into map-product polygons, using sunrise/sunset maximum boundaries when one physical wide-limit curve does not exist;
- `compute_local_solar_eclipse_boundary_*` computes a local boundary near a given point and time.

`compute_solar_eclipse_where_*` is the inexpensive choice when a map needs only
one epoch's center/core/penumbra geometry. It deliberately omits half-magnitude
limits, transverse width, duration, and numerical center-line refinement. Use
`compute_solar_eclipse_route_row_*` when those route-table metrics are needed.

These functions are for map/path generation and diagnostics. A simple "is there an eclipse near this date?" query does not need them.

Route curves and route products are derived from Taiyin route rows. They include the geometrically applicable center line, penumbral limits, core limits, and half-magnitude limits sampled through the configured ephemeris runtime. Polygon longitudes are kept with an unwrapped longitude field so antimeridian-crossing envelopes can be rendered without tearing; callers may normalize individual points after projection. These APIs return numeric geometry products, not a finished map layer: downstream code is still responsible for map projection, thinning, segmentation, styling, and tile/viewport clipping.

The default route-curve sampling density is
`TAIYIN_SOLAR_ROUTE_DEFAULT_SAMPLE_COUNT` samples across the source route span.
The `_with_options` variants of
`compute_solar_eclipse_route_curves_*`,
`compute_solar_eclipse_route_product_*`, and
`compute_solar_eclipse_route_map_product_*` accept an explicit
`route_sample_count` in the documented min/max range. This controls exported
curve and polygon point density only; it does not change the ephemeris,
Delta T, radius, shadow, or lunar-limb models.

The default smooth route retains the established parameterized topology scan
for branch identity, horizon stitching, and valid/invalid transitions. Its
accepted circular-cone generators intersect the WGS84 ellipsoid analytically
by the same line-substitution quadratic verified by the native shadow-geometry
kernel; the north/south core, penumbral, and half-magnitude boundaries are not
intersections with a spherical Earth or a direction-dependent replacement
radius. At a transition, Taiyin bisects adjacent samples instead of using a
one-step linear endpoint estimate; this prevents reversed endpoint order at
high latitudes. Center-line and core-limit transition intervals are also
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

For interval route-row APIs, `step_minutes` advances on the time scale named by
the function: `_tt` samples a TT grid and `_ut` samples a UT grid. Both endpoints
are inclusive and the exact end epoch is emitted once; when the requested span
is not an integer number of steps, only the final interval is shorter. UT grid
epochs are converted individually to TT for ephemeris geometry, so a small
change in Delta T across the interval cannot create a second near-duplicate end
row.

Internal ephemeris positions are computed for TT instants by deriving TDB through the context's TDB model. Every current `*_at_ut` eclipse variant interprets its input and output as UT1 and uses the context's Delta-T model for conversion; it never switches to UTC merely because EOP data are loaded. Use TT when comparing against ephemeris-time tables and UT1 for Earth-rotation-facing results. A future UTC-specific entry point must be named explicitly rather than changing this contract.

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
  with the TLL1 model loaded by the global runtime;
- `TAIYIN_ECLIPSE_LOCAL_REFRACTION`: for local solar APIs only, use a refracted
  sunrise/sunset visibility window instead of the default geometric one;
- `TAIYIN_ECLIPSE_LOCAL_STRICT_METEOROLOGY`: for refracted local solar APIs
  only, require supplied atmosphere data and forbid standard-atmosphere fallback.

Lunar-limb correction is opt-in and requires a globally loaded model. See
[`lunar_limb_model.md`](lunar_limb_model.md) for loading, lifetime, coverage,
and corrected-contact semantics.

By default, eclipse calculation uses the built-in apparent-position route. Light-time, annual aberration, gravitational deflection, Shapiro delay, frame model, TDB model, Delta-T model, and related settings are therefore part of Sun/Moon geometry, Besselian seeds, correction windows, and contact refinement. The default context should use the recommended apparent-position convention; this is the configuration to state first when comparing against public almanacs, PMO/NASA material, or Swiss-style oracles.

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

These entry points are for geographic observers. They read the observer longitude, latitude, and height from `NativeCalcContext::observer_location`; if the context has no observer location, they return `TAIYIN_ERROR_INVALID_ARGUMENT`. Their existing `uint64_t flags` parameter accepts the local visibility options described above. `search_next_local_solar_eclipse_*` scans global solar-eclipse candidates first, then computes local circumstances and applies the requested kind filter to the observer-local result. For example, a global total solar eclipse that is only partial at the observer will not be returned by a `TAIYIN_ECLIPSE_TOTAL` local search.

### Solar Path Products

```cpp
compute_solar_besselian_elements_tt(...)
compute_solar_besselian_polynomial_tt(...)
evaluate_solar_besselian_polynomial(...)
compute_solar_eclipse_where_tt(...)
compute_solar_eclipse_where_ut(...)
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

### Complete Runnable Example

[`examples/eclipse_search.cpp`](../examples/eclipse_search.cpp) is a complete
program rather than an isolated fragment. It initializes packaged ephemeris
data, searches for lunar and solar eclipses, calculates local circumstances for
Dallas, and produces the core, penumbral, and half-magnitude map polygons for
the 2024-04-08 eclipse.

Build and run it from the repository root:

```sh
cmake -S . -B build
cmake --build build --target example_eclipse_search
./build/example_eclipse_search /path/to/taiyin/data
```

The data-root argument is optional. The program tries the argument first, then
`TAIYIN_DATA_ROOT`, then `./data`. A successful run with the bundled 600-year
data prints output similar to:

```text
Next total lunar eclipse: 2025-03-14 06:58:46.7 UT, umbral magnitude=1.183066
Next total solar eclipse: 2024-04-08 18:17:20.2 UT at 25.28961, -104.14800
Dallas local maximum: 2024-04-08 18:42:39.0 UT, magnitude=1.057150, Sun altitude=64.617 deg
Route map: 627 polygon points; 260 core, 184 penumbral, 183 half-magnitude
```

Exact values depend on the selected ephemeris data and context models.

### Choosing An Entry Point

| Task | Entry point |
| --- | --- |
| Evaluate the lunation near a known date | `solve_lunar_eclipse_at_ut` / `solve_solar_eclipse_at_ut` |
| Find the next or previous matching event | `search_next_lunar_eclipse_ut` / `search_next_solar_eclipse_ut` |
| Find all matching events in an interval | `search_lunar_eclipses_ut` / `search_solar_eclipses_ut` |
| Calculate circumstances at the context observer | `solve_local_solar_eclipse_at_ut` / `search_next_local_lunar_eclipse_ut` |
| Sample rows over an explicit interval | `compute_solar_eclipse_route_ut` |
| Generate separate center/boundary curves | `compute_solar_eclipse_route_curves_ut_with_options` |
| Generate closed map polygons | `compute_solar_eclipse_route_map_product_ut_with_options` |

The `_ut` functions accept and return UT. The corresponding `_tt` functions
use TT. Do not pass a scalar `double` to either interface: public eclipse APIs
use `SplitJulianDate` so sub-second resolution is retained when the integer
Julian day is large. Construct one directly from a calendar date:

```cpp
SplitJulianDate start_ut;
if (!julian_day_split({2024, 1, 1, 0, 0, 0.0}, &start_ut)) {
    // Invalid calendar input.
}
```

For a forward search, pass a kind bit or an OR of kinds. Pass `0` to accept all
kinds, or add `TAIYIN_ECLIPSE_BACKWARD` to `flags` for a backward search:

```cpp
SolarEclipseResultUt eclipse = {};
EphemerisEvalDiagnostic diagnostic = {};
Status status = search_next_solar_eclipse_ut(
    &context,
    start_ut,
    TAIYIN_ECLIPSE_TOTAL | TAIYIN_ECLIPSE_ANNULAR,
    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
    &eclipse,
    &diagnostic);
```

`TAIYIN_STATUS_OK` means a matching search result was produced. For a `solve_*`
call, it means evaluation succeeded; check `result.kind` because the nearby
lunation may have `TAIYIN_ECLIPSE_NONE`. On failure, `status_name(status)` and
`EphemerisEvalDiagnostic` provide the first useful error information.

### Local Circumstances

Local entry points read their observer from `NativeCalcContext`. Longitude is
east-positive, latitude is north-positive, and height is in metres:

```cpp
NativeCalcContext local_context = context;
native_context_set_observer_location(
    &local_context,
    native_observer_location_degrees(-96.7970, 32.7767, 131.0));

LocalSolarEclipseResultUt local = {};
Status status = solve_local_solar_eclipse_at_ut(
    &local_context,
    eclipse.maximum_jd_ut,
    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
    &local,
    &diagnostic);
```

Without an observer location, local APIs return
`TAIYIN_ERROR_INVALID_ARGUMENT`. Local lunar results additionally expose
`visibility_flags`; test `TAIYIN_ECLIPSE_MAXIMUM_VISIBLE` to determine whether
the Moon is above the selected horizon at greatest eclipse.

### Route Map Polygons And Buffer Sizing

Variable-size route APIs use a two-call contract. First pass `nullptr` and zero
capacity to obtain the required count, then allocate and call again:

```cpp
SolarEclipseRouteProductSummary summary = {};
size_t point_count = 0;
Status status = compute_solar_eclipse_route_map_product_ut_with_options(
    &context, eclipse.maximum_jd_ut, 0, 128,
    nullptr, 0, &point_count, &summary, &diagnostic);

std::vector<SolarEclipseRouteProductPoint> points(point_count);
if (status == TAIYIN_STATUS_OK) {
    status = compute_solar_eclipse_route_map_product_ut_with_options(
        &context, eclipse.maximum_jd_ut, 0, 128,
        points.data(), points.size(), &point_count, &summary, &diagnostic);
    points.resize(point_count);
}
```

`route_sample_count` controls path sampling and must be between
`TAIYIN_SOLAR_ROUTE_MIN_SAMPLE_COUNT` and
`TAIYIN_SOLAR_ROUTE_MAX_SAMPLE_COUNT`; the overload without options uses
`TAIYIN_SOLAR_ROUTE_DEFAULT_SAMPLE_COUNT`. If a non-null buffer is too small,
the function returns `TAIYIN_ERROR_OUT_OF_MEMORY` and still reports the required
count and summary.

The map-product array stores the closed core polygon first, then the penumbral
polygon, then the half-magnitude polygon. Use the three `*_polygon_point_count`
fields in `SolarEclipseRouteProductSummary` to split the array; do not infer
boundaries from coordinates. `longitude_deg` is normalized for ordinary map
display, while `unwrapped_longitude_deg` preserves continuity across the
antimeridian.

## Validation Notes

Public regression tests use several source types:

- PMO public material: 2024 and 2026 global solar eclipse contacts/greatest location, 2025 total lunar eclipse contacts;
- NASA eclipse catalogs / decade tables: greatest time, classification, magnitude, and duration;
- NASA city-table values: minute-level local solar-eclipse sanity checks;
- archived `sxwnl` fixtures: migration comparisons for the superseded eclipse
  implementations; they are test-only and are not production dependencies;
- OPM2/SPK data comparison tests: verify ephemeris reading and route composition.

PMO/NASA and similar public sources are preferred as behavior baselines. Old TypeScript fixtures are mainly migration regressions and diagnostic tables; unless a row is explicitly tied to a public source such as PMO/NASA, it is not treated as an authoritative eclipse oracle.

Local solar eclipses have multiple layers of tests: fixed Mazatlan/New York regressions, Dallas NASA city-table minute-level checks, observer-local kind filtering, truepos paths, and sunrise/sunset visible-partial samples. These tests cover classification, contacts, local search filtering, and visibility markers. More published local-circumstances tables are needed before claiming second-level external accuracy for arbitrary local contacts.

Swiss Ephemeris comparisons can be useful for compatibility or model-difference analysis, but they are not a license-neutral public oracle and not the baseline for the public test suite.

## Current Boundaries

- Public API is not frozen before the first release; struct fields and flags may still change as validation proceeds.
- Eclipse results depend on the ephemeris route, apparent options, TDB and Delta-T models, shadow model, and Moon-radius model in `NativeCalcContext`. Comparisons should report those settings. Current `_at_ut` eclipse APIs always mean UT1.
- Solar route curves are numeric map/path products. Downstream users usually still need projection, thinning, segmentation, and map rendering.
- Local solar-eclipse APIs compute geometric visibility and contact circumstances. They do not model weather, clouds, terrain obstruction, equipment, or human visual effects.
- Lunar-eclipse contacts, durations, and magnitudes depend strongly on shadow model and Moon-radius model. Without stating the model, second-level or percentage differences between sources should not be treated directly as errors.
- NASA HTML lunar catalogs do not provide individual `P1/U1/U2/U3/U4/P4` contact times. They provide greatest time, magnitudes, and phase durations. Individual contact oracles require sources that publish those contacts explicitly.
