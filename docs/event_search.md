# Event Search

Status: Current
Last reviewed: 2026-07-01
Primary header: `include/taiyin/runtime/event_search.h`

Event search is Taiyin runtime's low-level numeric root-solving capability. It only answers "when does this angular condition become true?" It does not directly generate almanacs, solar-term tables, astrology event objects, or visualization results.

## Scope

Current public APIs cover:

- solar longitude crossing;
- lunar longitude crossing;
- arbitrary body longitude crossing;
- relative longitude crossing between two bodies;
- lunar phase, i.e. `Moon - Sun = phase`;
- exact aspects, i.e. hits against one or more aspect separations;
- longitude stations, where longitude speed is zero;
- minimum three-dimensional angular separation between two bodies.

All these functions use the caller's `NativeCalcContext`. Observer, time model, precession/nutation model, route rule, data-source selection, and ordinary `calc_position_*` behavior therefore stay consistent. Event search does not bypass the context and does not swap in a separate data source.

## Flags

Every event-search entry point accepts a `uint64_t flags`:

```text
low 32 bits:  native position flags
high 32 bits: event-search option flags
```

The low 32 bits are passed to the ordinary position path. Longitude, relative-longitude, lunar-phase, exact-aspect, and station searches add:

```text
TAIYIN_NATIVE_POSITION_SPEED
TAIYIN_NATIVE_POSITION_RADIANS
```

Callers do not need to enable those two flags manually. Three-dimensional separation and greatest-elongation searches internally sample with XYZ + SPEED. These output modes are still managed by the search function and cannot be requested by callers:

```text
TAIYIN_NATIVE_POSITION_XYZ
TAIYIN_NATIVE_POSITION_EQUATORIAL
```

The only public event-search option flag today is:

```text
TAIYIN_EVENT_SEARCH_REVERSE
```

It is used only by single-event estimate searches such as `search_solar_longitude_*` and `search_moon_longitude_*`. Interval searches already define direction through `start_jd` and `end_jd`; they do not accept reverse. Passing high 32-bit search options to interval searches returns unsupported.

## Timescales

UT entry points use the ordinary UT position path:

```cpp
search_solar_longitude_ut(...)
search_body_aspect_crossings_ut(...)
```

TT entry points receive TT and derive TDB internally from the `NativeCalcContext` TDB model:

```cpp
search_solar_longitude_tt(...)
search_body_aspect_crossings_tt(...)
```

Public event-search APIs do not ask callers to pass both TT and TDB. Pass UT or TT according to the function name.

## Output Conventions

Single-event functions return one JD:

```cpp
double jd = 0.0;
Status status = search_solar_longitude_ut(
    &context,
    target_longitude_rad,
    estimate_jd_ut,
    flags,
    &jd,
    &diagnostic);
```

Interval functions use caller-provided output arrays:

```cpp
double events[16];
size_t event_count = 0;
Status status = search_body_longitude_crossings_ut(
    &context,
    body_id,
    target_longitude_rad,
    start_jd_ut,
    end_jd_ut,
    max_step_days,
    flags,
    events,
    16,
    &event_count,
    &diagnostic);
```

`event_count` reports the number of events actually written. If capacity is insufficient, the function returns a capacity/memory error. It does not silently truncate and return success. Station and exact-aspect APIs have optional additional output arrays:

```text
station:      out_longitude_rad
exact-aspect: out_target_aspect_rad
```

These additional arrays may be `nullptr`.

## Longitude Search

### Solar And Lunar Single Events

```cpp
search_solar_longitude_ut(...)
search_solar_longitude_tt(...)
search_moon_longitude_ut(...)
search_moon_longitude_tt(...)
```

These functions find one longitude crossing near `estimate_jd`. They search forward by default; with `TAIYIN_EVENT_SEARCH_REVERSE`, they search backward.

They are suitable for "next/previous solar longitude" and "next/previous lunar longitude." They are not intended for general planets, because retrograde bodies can cross the same longitude multiple times near stations.

### Arbitrary Body Interval Search

```cpp
search_body_longitude_crossings_ut(...)
search_body_longitude_crossings_tt(...)
search_body_longitude_crossings_auto_step_ut(...)
search_body_longitude_crossings_auto_step_tt(...)
```

These functions search all events in `[start_jd, end_jd]` satisfying:

```text
body longitude - target longitude = 0
```

The explicit-step form accepts `max_step_days`. The auto-step form uses `recommended_longitude_search_step_days(body_id)`.

The algorithm samples by step first, then refines sign-change brackets with safeguarded Newton/bisection. Step is a search hint, not a mathematical proof; if the step is too large, paired roots near a station or tangent roots can still be missed.

## Station Search

```cpp
search_body_longitude_stations_ut(...)
search_body_longitude_stations_tt(...)
search_body_longitude_stations_auto_step_ut(...)
search_body_longitude_stations_auto_step_tt(...)
```

These functions search:

```text
d(longitude) / dt = 0
```

They return only station JD and optional station longitude. They do not label "retrograde begins," "retrograde ends," "direct station," or "retrograde station." If an application needs those labels, it can evaluate longitude speed before and after the returned JD.

### Reference Event: Mercury Station On 2003-08-28

The following reference event shows station search near Mercury's retrograde boundary. The formal retrograde start is the instant after the station where longitude speed becomes negative. In Beijing civil time, Mercury was still direct during the afternoon and crossed the station in the evening.

| Event | Reference / Result | UTC | Beijing Time | Delta vs Taiyin | Notes |
| --- | ---: | --- | --- | ---: | --- |
| Mercury station, turning retrograde | Swiss Ephemeris 2.10.03 | 2003-08-28 13:41:22.175 | 2003-08-28 21:41:22.175 | +0.96 s | second-level reference |
| Mercury station, turning retrograde | Taiyin OPM2 | 2003-08-28 13:41:23 | 2003-08-28 21:41:23 | current result | Taiyin output |
| Mercury station range | JPL Horizons DE441 | 2003-08-28 13:40-13:42 | 2003-08-28 21:40-21:42 | within range | `ObsEcLon` apparent ecliptic-of-date minute table has a peak plateau |

The same date also carries the 2003 Mars opposition reference:

| Event | Reference / Result | UTC | Beijing Time | Delta vs Taiyin | Notes |
| --- | ---: | --- | --- | ---: | --- |
| Mars opposition | Swiss Ephemeris 2.10.03 | 2003-08-28 17:58:47.166 | 2003-08-29 01:58:47.166 | -0.006 s | Mars/Sun apparent longitude difference of 180 degrees |
| Mars opposition | Taiyin OPM2 | 2003-08-28 17:58:47 | 2003-08-29 01:58:47 | current result | Taiyin output |
| Mars opposition | SEDS reference | 2003-08-28 17:58:49 | 2003-08-29 01:58:49 | -1.84 s | published reference |

## Relative Longitude, Lunar Phase, And Exact Aspects

Relative longitude crossing:

```cpp
search_body_aspect_crossings_ut(...)
search_body_aspect_crossings_tt(...)
search_body_aspect_crossings_auto_step_ut(...)
search_body_aspect_crossings_auto_step_tt(...)
```

It solves:

```text
body_a longitude - body_b longitude - aspect = 0
```

Lunar phase wrappers:

```cpp
search_lunar_phase_crossings_ut(...)
search_lunar_phase_crossings_tt(...)
search_lunar_phase_crossings_default_step_ut(...)
search_lunar_phase_crossings_default_step_tt(...)
```

Lunar phase is simply `Moon - Sun = phase`. The `default_step` versions use the built-in lunar-phase step and are suitable for common searches such as new moon, first quarter, full moon, and last quarter.

Exact aspects:

```cpp
search_body_exact_aspects_ut(...)
search_body_exact_aspects_tt(...)
search_body_exact_aspects_auto_step_ut(...)
search_body_exact_aspects_auto_step_tt(...)
```

The function accepts one or more aspect separations. 0 degrees and 180 degrees generate one target each; other separations expand to two directions. For example, 120 degrees expands to 120 and 240 degrees.

Exact-aspect search performs two kinds of detection:

```text
sign-change crossing
relative-station tangent hit
```

This covers ordinary crossings and local extrema that exactly touch an aspect. Returned `out_target_aspect_rad` is the hit target angle, not an aspect name.

## Mercury/Venus Greatest Elongation

Greatest-elongation search currently targets Mercury and Venus. It uses the caller's `NativeCalcContext`, route rule, output frame, and native position flags to evaluate the three-dimensional planet-Sun elongation as seen by the observer. The result is classified as eastern or western elongation.

Internally, the solver fixes `center_id` to the Sun. This does not move the observer to the Sun; it makes the Sun the ephemeris evaluation origin. The elongation geometry remains:

```text
angle(observer -> body, observer -> Sun)
```

For example, under the default geocentric semantics the apparent/native pipeline evaluates:

```text
body -> Sun
observer(Earth) -> Sun
Sun -> Sun
```

and then forms:

```text
observer -> body = body/Sun - observer/Sun
observer -> Sun  = Sun/Sun  - observer/Sun
```

This keeps OPM2 and the built-in semi-analytical model on stable routes. The
semi-analytical Mercury/Venus/major-planet data are Sun-centered; if the solver
simply inherited an arbitrary caller-supplied `center_id`, some routes could be
unavailable or forced through extra fallback. The fixed center here is an
intermediate evaluation origin and does not change the final observer semantics.

The current solver intentionally uses a conservative path:

```text
scan samples -> compute elongation rate and its derivative from position/velocity/acceleration -> bracket rate=0 -> guarded Newton/bisection refine
```

The refine step uses the acceleration exposed through `calc_state_*()` to build a Newton candidate. It does not branch on whether the acceleration is native or finite-difference-derived; if the candidate is finite, remains inside the current bracket, and continues shrinking the bracket, it is accepted. Otherwise the solver falls back to bisection.

Acceleration can therefore speed convergence, but correctness does not depend on an unguarded Newton step; bracketing and bisection remain in place to avoid leaving the search interval or jumping across the root.

This entry point returns only a real `elongation rate = 0` greatest-elongation event that was bracketed inside the caller range. If the range does not contain an eastern/western greatest-elongation stationary point, it returns `TAIYIN_EVENT_ERROR_NOT_FOUND`; it does not report an arbitrary interval maximum as a greatest-elongation event.

The following table compares greatest-elongation search with references derived from JPL Horizons apparent vectors. Each JPL value is computed from geocentric apparent vectors for the planet and the Sun, then quadratically fitted from seven samples around the Taiyin result.

| Case | JPL JD UT | Taiyin JD UT | Taiyin - JPL Time | JPL Elongation | Taiyin - JPL Angle |
| --- | ---: | ---: | ---: | ---: | ---: |
| Mercury eastern, 2024-03 | 2460394.440334700 | 2460394.440334365 | -0.029 s | 18.701601185 deg | +0.0025" |
| Mercury western, 2024-05 | 2460440.395385454 | 2460440.395385969 | +0.044 s | 26.365604784 deg | +0.0021" |
| Venus eastern, 2023-06 | 2460099.958895525 | 2460099.958895225 | -0.026 s | 45.399231306 deg | +0.0020" |
| Venus western, 2023-10 | 2460241.468374033 | 2460241.468373870 | -0.014 s | 46.413181097 deg | +0.0017" |

The public regression suite also runs these searches through the independent
built-in semi-analytical route and checks that it finds the same event class and
remains close to the OPM2 result. Its underlying held-out DE441 accuracy is
documented in [Built-in Semi-Analytical Ephemeris](semi_analytic_ephemeris.md).

## Minimum Angular Separation

Minimum-separation entry points:

```cpp
search_minimum_angular_separation_ut(...)
search_minimum_angular_separation_tt(...)
```

These functions search for the minimum three-dimensional angle between two body direction vectors. This differs from the longitude difference used by `search_body_aspect_*()`: two bodies can share longitude while still being separated in latitude. The API is therefore a low-level primitive for appulses, occultations, transits, and closest-approach workflows; event names, conjunction/orb rules, and display semantics belong in upper layers.

The current solver uses the same angular-separation kinematics helper as greatest elongation: it first brackets a local minimum with `separation_rate`, then uses `separation_acceleration` to build a guarded Newton candidate. If the candidate is not finite or leaves the bracket, the solver continues with bisection. If no sign-change bracket is found, it falls back to value minimization near the best sampled point.

The following table gives a Sun-Moon minimum-separation reference near the 2024-04-08 solar eclipse. The reference is not a JPL event-table row; it is derived from JPL Horizons apparent vectors:

```text
Moon(301), Sun(10), center=Earth geocenter
EPHEM_TYPE=VECTORS
TIME_TYPE=UT, VEC_CORR=LT+S
TLIST covers 2024-04-08 18:15:00..18:20:00 UTC at 10-second sampling cadence
read Moon and Sun apparent vectors separately
compute the three-dimensional angle between the vectors locally
fit a quadratic through seven samples around the minimum sample to obtain the reference minimum
```

| Event | Reference / Result | UTC | JD UT | Minimum Separation | Delta vs Taiyin |
| --- | ---: | --- | ---: | ---: | ---: |
| Sun-Moon minimum angular separation | JPL Horizons DE441 vector | 2024-04-08 18:17:20.494 | 2460409.262042756 | 0.3476802575 deg | +0.013 s, -0.0000074" |
| Sun-Moon minimum angular separation | Taiyin OPM2 | 2024-04-08 18:17:20.507 | 2460409.262042910 | 0.3476802555 deg | current result |

## Mercury/Venus Solar Transits

Solar-transit entry point:

```cpp
search_next_solar_transit_ut(...)
search_next_local_solar_transit_ut(...)
compute_local_solar_transit_ut(...)
```

`search_next_solar_transit_ut()` searches forward from a supplied UT start time for the next Mercury/Venus transit from a global geocentric viewpoint. It returns greatest-transit time, minimum angular separation, apparent solar and planetary radii, and T1/T2/T3/T4 contact times. Pass `TAIYIN_EVENT_SEARCH_REVERSE` to search backward for the previous event. This entry point rejects topocentric native flags and `NativeCalcContext` values that already contain a topocentric observer, so its result remains a global geocentric event.

`search_next_local_solar_transit_ut()` does not require the geocentric result to already be a confirmed transit. It walks Mercury/Venus inferior-conjunction cycles from the supplied start time, then solves the supplied observer's topocentric minimum separation and contact times directly; this keeps grazing local events from being filtered out by a geocentric-only transit test. The result contains geocentric candidate fields, local topocentric T1/T2/greatest/T3/T4, solar altitude and azimuth at those instants, visibility bits, and sunrise/sunset during the transit interval. `compute_local_solar_transit_ut()` is the companion for callers that already have a `SolarTransitSearchResult` or candidate result; it does not re-search candidates and only fills local topocentric contact and visibility fields. Local entries default to refracted apparent altitude and require atmosphere fields in the context. Pass `TAIYIN_EVENT_SEARCH_NO_REFRACTION` for true-altitude visibility.

The search uses a staged filter:

```text
k candidate -> empirical conjunction seed -> inferior-distance filter -> node/latitude gate -> apparent minimum separation -> contact root solve
```

The first stage locates candidate inferior-conjunction cycles with a Mercury/Venus `k` index. The seed uses DE441-fitted empirical corrections: a mean inferior-conjunction period plus a slow polynomial and Poisson-style terms of the form `t^n sin(2πfk)` / `t^n cos(2πfk)`. This seed only narrows the initial search window and does not define event timing. The current empirical window is `±3 day` for Mercury and `±1 day` for Venus. If the empirical small window does not recover a suitable inferior conjunction, the search falls back to the conservative mean-`k` wide window. The next stage refines longitude conjunction near the candidate in an internal canonical ecliptic frame and rejects superior conjunctions by requiring the observer-body distance to be smaller than the observer-Sun distance. The node-style prefilter mirrors the role of the eclipse `F` filter: if the planet's ecliptic latitude near inferior conjunction exceeds the conservative `2 deg` threshold, the candidate is treated as definitely not a transit.

Solar-transit search does not treat the caller's `NativeCalcContext::apparent_options.output_frame_id` as candidate-search semantics. Candidate conjunction and the latitude gate use true ecliptic-of-date internally, or mean ecliptic-of-date when `TAIYIN_NATIVE_POSITION_NONUT` is passed. Transit confirmation remains the job of the three-dimensional minimum-separation and contact root solvers.

`tools/validate_solar_transit_seed_de441.py` is an offline DE441 validation tool for the `jd -> k` bootstrap, empirical seed coverage, and conservative mean-`k` fallback window. In the default sampled validation, the worst empirical-seed error against DE441 inferior conjunction roots is about `2.38 day` for Mercury and `0.33 day` for Venus, both inside the production small windows. The tool follows the actual BSP coverage before sampling `k` values so ephemeris-file boundaries are not confused with search failures.

The latitude gate is a necessary-condition filter, not an event confirmation. In other words, `abs(latitude) > 2 deg` means definitely no transit, while `abs(latitude) <= 2 deg` only means the candidate may still transit. Transit confirmation remains the job of the Sun-body three-dimensional minimum separation and contact root solver. The threshold is guarded by DE441 hard-scan regression cases: the tests compare the `k` search against a `k`-independent three-dimensional minimum-separation scan near ancient, mid-range, modern, and future DE441 epochs.

The final stage searches the Sun-body three-dimensional apparent minimum separation only near surviving candidates. Contacts are roots of `separation(t) - (solar_radius(t) ± body_radius(t)) = 0`; the apparent radii are recomputed at each sampled JD rather than being frozen at greatest transit.

The first version supports only Mercury and Venus. Outer planets cannot transit the Sun from Earth and are rejected as invalid arguments. The local transit entry is not a general occultation framework; lunar occultations, planet-star occultations, and satellite transits belong in later occultation/appulse APIs.

The current regression suite covers the 2006-11-08 Mercury transit, the 2019-11-11 Mercury transit, and the 2004-06-08 Venus transit. Mercury references come from the NASA Eclipse Web Site's *Seven Century Catalog of Mercury Transits: 1601 CE to 2300 CE*; Venus references come from the *Six Millennium Catalog of Venus Transits: 2000 BCE to 4000 CE*. These catalogs list geocentric UT contact times and greatest transit rounded to minutes, plus minimum center separation rounded to 0.1 arcsec. These values are used as external authoritative sanity checks, not as second-level contact oracles.

References:

- [NASA Mercury Transit Catalog](https://eclipse.gsfc.nasa.gov/transit/catalog/MercuryCatalog.html)
- [NASA Venus Transit Catalog](https://eclipse.gsfc.nasa.gov/transit/catalog/VenusCatalog.html)

| Event | Item | NASA Reference | Taiyin OPM2 | Note |
| --- | --- | ---: | ---: | --- |
| Mercury 2006 | Contact I | 2006-11-08 19:12 UT | 19:12:04.2 UT | within NASA minute-level reference |
| Mercury 2006 | Contact II | 2006-11-08 19:14 UT | 19:13:57.1 UT | within NASA minute-level reference |
| Mercury 2006 | Greatest transit | 2006-11-08 21:41 UT | 21:41:04.2 UT | within NASA minute-level reference |
| Mercury 2006 | Contact III | 2006-11-09 00:08 UT | 00:08:16.1 UT | within NASA minute-level reference |
| Mercury 2006 | Contact IV | 2006-11-09 00:10 UT | 00:10:09.0 UT | within NASA minute-level reference |
| Mercury 2006 | Minimum center separation | 422.9" | 422.9144" | consistent with NASA 0.1" reference |
| Mercury 2019 | Contact I | 2019-11-11 12:35 UT | 12:35:26.8 UT | within NASA minute-level reference |
| Mercury 2019 | Contact II | 2019-11-11 12:37 UT | 12:37:08.2 UT | within NASA minute-level reference |
| Mercury 2019 | Greatest transit | 2019-11-11 15:20 UT | 15:19:48.0 UT | within NASA minute-level reference |
| Mercury 2019 | Contact III | 2019-11-11 18:02 UT | 18:02:32.9 UT | within NASA minute-level reference |
| Mercury 2019 | Contact IV | 2019-11-11 18:04 UT | 18:04:14.3 UT | within NASA minute-level reference |
| Mercury 2019 | Minimum center separation | 75.9" | 75.9351" | consistent with NASA 0.1" reference |
| Venus 2004 | Contact I | 2004-06-08 05:13 UT | 05:13:33.8 UT | within NASA minute-level reference |
| Venus 2004 | Contact II | 2004-06-08 05:33 UT | 05:33:05.0 UT | within NASA minute-level reference |
| Venus 2004 | Greatest transit | 2004-06-08 08:20 UT | 08:19:44.2 UT | within NASA minute-level reference |
| Venus 2004 | Contact III | 2004-06-08 11:07 UT | 11:06:37.9 UT | within NASA minute-level reference |
| Venus 2004 | Contact IV | 2004-06-08 11:26 UT | 11:25:54.7 UT | within NASA minute-level reference |
| Venus 2004 | Minimum center separation | 626.9" | 626.8902" | consistent with NASA 0.1" reference |

The 2019 Mercury transit is also covered by a second-level JPL Horizons apparent-vector oracle. It validates that Taiyin and the JPL-derived geometry agree at sub-second and sub-milliarcsecond scale. The `Taiyin OPM2` agreement column is sensitive to UT1/UTC, Delta T, apparent-vector definitions, solar/planetary radius conventions, and OPM2-vs-DE441 data differences; it should not be read as an absolute physical error. Generation settings:

```text
Mercury(199), Sun(10), center=Earth geocenter
EPHEM_TYPE=VECTORS
TIME_TYPE=UT, VEC_CORR=LT+S
TLIST covers 2019-11-11 12:30:00..18:10:00 UTC at roughly 10-second cadence
read Mercury and Sun apparent vectors separately
solve contact residuals using Taiyin's current apparent-radius convention
fit greatest transit from seven samples around minimum angular separation
```

| Item | JPL vector-derived oracle | Taiyin OPM2 Agreement |
| --- | ---: | ---: |
| Contact I | 2019-11-11 12:35:26.985 UT | within 1 s |
| Contact II | 2019-11-11 12:37:08.361 UT | within 1 s |
| Greatest transit | 2019-11-11 15:19:48.114 UT | within 1 s |
| Contact III | 2019-11-11 18:02:33.101 UT | within 1 s |
| Contact IV | 2019-11-11 18:04:14.493 UT | within 1 s |
| Minimum center separation | 75.9351759" | within 0.01" |

## Step Selection

Explicit-step APIs offer the most control. Callers should choose `max_step_days` according to event type, time window, and target body speed.

Convenience functions provide built-in suggestions:

```cpp
recommended_longitude_search_step_days(body_id)
recommended_aspect_search_step_days(body_a_id, body_b_id)
```

Current built-in hints are conservative:

```text
Moon       0.25 day
Mercury   0.5 day
Venus     1.0 day
Mars      1.0 day
Sun       2.0 days
Jupiter   2.0 days
Saturn    2.0 days
Uranus    3.0 days
Neptune   3.0 days
Pluto     3.0 days
unknown   0.5 day
```

`recommended_aspect_search_step_days()` takes the smaller suggestion from the two bodies. For custom bodies with unusual motion, prefer the explicit-step APIs.

## Error Behavior

Event search reports failure through `Status` and optional `EphemerisEvalDiagnostic`.

Common cases:

- null pointers, invalid time windows, invalid step, and invalid capacity return argument errors;
- requesting XYZ or equatorial output returns unsupported;
- passing event-search option flags to interval search returns unsupported;
- no root in the requested window returns `TAIYIN_EVENT_ERROR_NOT_FOUND`;
- data coverage gaps, missing composite components, or missing routes are normalized to `TAIYIN_EVENT_ERROR_NOT_FOUND`, with the diagnostic retaining lower-level failure information;
- insufficient output-array capacity returns a capacity/memory error.

Search functions do not silently switch coordinate modes, data sources, or route rules. If the selected context and flags cannot compute the target body within the requested window, event search returns a failure status.

## Test Coverage

Public `test_event_search` covers:

- solar and lunar longitude crossing;
- UT and TT entry points;
- reverse Sun search;
- solar longitude oracle;
- no-event and capacity behavior for bounded longitude search;
- auto-step longitude/aspect wrappers;
- lunar phase wrapper consistency with generic Moon-Sun aspect search;
- exact-aspect direction expansion and tangent exact-aspect detection;
- minimum Sun/Moon angular separation sanity and OPM2/semi-analytical comparison;
- synthetic and real longitude stations;
- route-rule fixed/no-fallback behavior;
- data-boundary and component coverage-gap termination;
- rejecting reverse bounded search;
- rejecting XYZ/equatorial output modes.

Compatibility comparison tests can live in private tests or external repositories. They are useful for developing compatibility behavior, but are not part of the public runtime contract.

## Extension Boundaries

`event_search.h` provides composable primitive event-search capabilities. Calendar, visibility, and astrology extensions can add names, classifications, display fields, and domain rules on top of those results.

Examples already expressible with current primitive search and suitable for named entry points in higher-level modules:

- Solar terms: search solar longitude targets, then attach solar-term names in a calendar module.
- Lunar phases: search `Moon - Sun` phase, then attach names such as new moon, full moon, first quarter, and last quarter in a calendar module.
- Retrograde events: search stations, then let the upper layer use speeds before and after the JD to label direct station, retrograde station, retrograde start, or retrograde end.
- Named aspects: search exact aspects, then let an astrology extension apply orb, name, display, applying/separating rules.

Directions better placed in astronomy/calendar extension layers:

- multi-day rise/set, meridian transit, day length, and twilight tables;
- visibility windows for the Sun, Moon, planets, and stars;
- heliacal rising/setting, first visibility, evening/morning visibility, acronychal/cosmical events, and similar traditional visibility events;
- planetary phenomena such as phase angle, elongation, illuminated fraction, apparent diameter, and magnitude;
- Mercury/Venus greatest eastern/western elongations;
- batch lunar star-catalog scans, mutual planetary occultations, and minimum separation/appulse for Moon-planet conjunctions;
- Mercury/Venus transits of the Sun;
- calendar-oriented local solar/lunar eclipse visibility tables, observer summaries, and batch queries.

Directions better placed in astrology extension layers:

- zodiac sign ingress, house ingress, ASC/MC, and house-system events;
- planetary returns, transit-to-natal, synastry/composite events;
- named aspects with orb, applying/separating status, and interpretation semantics;
- combust, under beams, cazimi, and other solar-distance rules;
- dignity/debility, lunar mansions, void-of-course Moon, electional rules;
- Arabic parts, midpoints, Lilith variants, virtual points, and other synthetic chart points.

These extensions can reuse runtime longitude, relative longitude, station, visibility, eclipse, and future occultation capabilities while providing APIs closer to their use cases.

## Implementation Priorities

Future feature priority is guided by the public capability surface already provided by Swiss Ephemeris. "Guided by" means feature coverage and user workflow, not copying Swiss C ABI, flag details, or error strings into Taiyin core.

| Priority | Area | SwissEph capability reference | Planned Taiyin shape |
|---:|---|---|---|
| P0 | Existing event-search closure | `swe_solcross*`, `swe_mooncross*`, `swe_helio_cross*` | Stabilize low-level solar/lunar longitude, generic longitude, relative longitude, lunar phase, station, and exact-aspect searches; add reference and boundary tests. |
| P0 | Eclipse closure | `swe_sol_eclipse_*`, `swe_lun_eclipse_*` | Continue documenting and testing existing solar eclipse, lunar eclipse, local solar eclipse, path/contact, and local lunar-eclipse visibility features with reference cases, boundary cases, and performance coverage. |
| P1 | Rise/set, meridian transit, visibility tables | `swe_rise_trans`, `swe_rise_trans_true_hor`, `swe_azalt`, `swe_azalt_rev` | Existing solar, lunar, and planetary rise/set/transit primitives should be documented and strengthened with more custom-horizon oracles, generic body visibility only if it reduces duplication, multi-day tables, and more high-latitude boundaries. |
| P1 | Fixed stars and star visibility | `swe_fixstar*`, `swe_fixstar*_ut`, `swe_fixstar*_mag`, `swe_rise_trans` with star | Build on the current star catalog/API to add fixed-star rise/set, meridian transit, circumpolar/no-event handling, magnitude output, and reference tests. |
| P1 | Planetary phenomena | `swe_pheno`, `swe_pheno_ut` | Scalar APIs already cover phase angle, elongation, illuminated fraction, apparent diameter, empirical magnitude, and Moon horizontal parallax; next add more oracles and reuse these results for greatest elongation and morning/evening visibility. |
| P2 | Mercury/Venus greatest elongation and related phenomena | SwissEph users usually derive this from `swe_pheno*` plus search | Provide eastern/western greatest elongation search and tests, first for Mercury/Venus; current search uses guarded Newton/bisection refinement, before possibly generalizing the same strategy to angular-separation extrema. |
| P2 | Occultations, transits, and minimum separation | `swe_lun_occult_*`, solar transit/occultation workflows | Guarded Newton/bisection minimum angular separation primitive, Mercury/Venus transits, and first lunar fixed-star / solar-system-body next-search APIs with maximum/begin/end/contact and basic local visibility summaries are available; next strengthen specified-target lunar occultation seed/refine, `where`-style visibility regions, and more oracle coverage. Batch star-catalog scanning is not a SwissEph single-target API parity item and is deferred to a later catalog/almanac layer. |
| P2 | Nodes, apsides, and orbital quantities | `swe_nod_aps*`, `swe_get_orbital_elements`, `swe_orbit_max_min_true_distance` | Generic osculating elements, physical node/apsis searches, and the optional extension's mean/true lunar node plus Delaunay-mean and osculating lunar apogee are available. Natural/interpolated apogee and school-specific synthetic points remain extension work. |
| P3 | Heliacal and traditional visibility | `swe_heliacal_ut`, `swe_heliacal_pheno_ut`, `swe_vis_limit_mag`, `swe_heliacal_angle`, `swe_topo_arcus_visionis` | Point-source morning/evening first/last search is available with Belokrylov (2011) and Schaefer (1993) profiles. Next: artificial sky glow, crescent-specific lunar visibility, traditional event aliases, and external behavior oracles. |
| P3 | House and astrology-event extensions | `swe_houses*`, `swe_house_pos`, `swe_gauquelin_sector` | Implement ASC/MC, house systems, sign/house ingress, returns, transit-to-natal, and Gauquelin sector in an astrology extension. |

The core ordering principle is to implement reliable observables first, then event wrappers: positions, horizontal coordinates, phenomena, angular separations, and visibility decisions should be stable before greatest elongation, occultation, transit, heliacal events, and astrology events.
