# Orbital Events

Status: Current
Primary header: `include/taiyin/runtime/orbital_events.h`

This module provides geometric, two-body osculating quantities and searches
for physical node and apsis events. It is not a Lilith, mean-node, true-node,
or house-system API.

## Supported Bodies And Centers

The center is intentionally fixed by the body ID:

| Body | Center |
| --- | --- |
| Moon | Earth |
| Earth, EMB, major-planet centers, major-planet barycenters | Sun |

The Sun and Solar System Barycenter are rejected. Major-planet barycenters are
valid orbital targets; they describe system-barycenter motion around the Sun.

## Geometric State Contract

All functions use a geometric relative state at one epoch:

```text
r = body(t) - fixed_center(t)
v = d(r) / dt
```

The implementation forces a geometric ICRF relative state. Light-time,
aberration, gravitational deflection, and a local topocentric observer are not
meaningful parts of a physical orbit, and are not accepted as caller flags. The
underlying ephemeris route, time models, and optional barycenter-approximation
flag still come from `NativeCalcContext`.

`reference_frame_id` selects the orientation used for the reported angles and
plane crossing. It accepts `TAIYIN_APPARENT_FRAME_ICRF`,
`TAIYIN_APPARENT_FRAME_J2000_MEAN_EQUATOR`,
`TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC`,
`TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE`,
`TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE`,
`TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE`,
`TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE`, and
`TAIYIN_APPARENT_FRAME_CIRS`. The existing apparent-frame pipeline owns the
ICRF-to-frame transform, including the J2000 frame bias.
Semi-major axis, eccentricity, distances, and energy continue to use the
underlying inertial state, rather than the apparent rotation rate of an
of-date reference frame.

## Osculating Elements

```cpp
calc_body_osculating_orbit_ut(...)
calc_body_osculating_orbit_tt(...)
```

`BodyOsculatingOrbit` contains the standard instantaneous conic quantities:
semi-major axis, eccentricity, inclination, longitude of ascending node,
argument of periapsis, true/mean anomaly, current distance, periapsis and
apoapsis distances, and the corresponding two-body period.

The elements are derived from the current position and velocity plus the
combined primary/target gravitational parameter. Taiyin uses the NAIF DE440 GM
constants for the supported primary/body pairs. They describe the conic tangent
to the current n-body trajectory; they must not be treated as a long-term orbit
propagator. This is the same state-to-osculating-elements contract described by
[NAIF SPICE `oscelt_c`](https://naif.jpl.nasa.gov/pub/naif/toolkit_docs/C/cspice/oscelt_c.html)
and [JPL Horizons](https://ssd.jpl.nasa.gov/horizons/manual.html).

Near-circular and near-coplanar states make periapsis direction or node
longitude intrinsically ill-conditioned. In the exactly degenerate case, the
corresponding angular field is normalized to zero rather than inventing a
direction.

## Epoch Reference Points

```cpp
calc_body_orbit_reference_points_ut(...)
calc_body_orbit_reference_points_tt(...)
```

`BodyOrbitReferencePoints` evaluates the same instantaneous osculating orbit
once and returns its ascending node, descending node, periapsis, apoapsis, and
second focus. Every point includes Cartesian position relative to the fixed
physical center, longitude/right-ascension direction, latitude, and distance
in the selected reference frame.

The model is explicitly
`TAIYIN_BODY_ORBIT_REFERENCE_POINTS_OSCULATING`. These points describe the
conic fitted at the requested epoch; they are not the next times at which the
perturbed physical body reaches those locations. Use the search entry points
below for physical event times. Mean-element school conventions and
barycentric-osculating compatibility conventions are not silently substituted.

## Apsis Search

```cpp
search_next_body_apsis_ut(..., TAIYIN_BODY_APSIS_PERICENTER, ...)
search_next_body_apsis_ut(..., TAIYIN_BODY_APSIS_APOCENTER, ...)
```

The search solves the physical radial condition:

```text
f(t)  = r(t) . v(t) = 0
f'(t) = v(t) . v(t) + r(t) . a(t)
```

For a forward search, pericenter crosses from negative to positive and
apocenter from positive to negative. `TAIYIN_ORBITAL_EVENT_REVERSE` searches
the prior occurrence. A search begun exactly at an event advances to the next
or previous occurrence rather than returning the input time.

The current osculating mean anomaly provides a local seed. Taiyin validates it
against the selected ephemeris, refines with safeguarded Newton steps, falls
back to secant steps when needed, and finally uses a bracketed fallback scan
over a little more than one local osculating period. The seed is an optimization,
not a long-term event calendar.

## Reference-Plane Node Search

```cpp
search_next_body_plane_node_ut(..., TAIYIN_BODY_NODE_ASCENDING,
                               reference_frame_id, ...)
search_next_body_plane_node_ut(..., TAIYIN_BODY_NODE_DESCENDING,
                               reference_frame_id, ...)
```

These functions search crossings of the selected frame's XY plane:

```text
f(t) = z_reference_frame(t) = 0
```

An ascending node changes from negative to positive reference latitude; a
descending node changes from positive to negative. The result's
`reference_plane_angle_rad` is expressed in that same frame: it is a longitude
in an ecliptic frame and a right-ascension direction in an equatorial frame.
`ICRF` means its equatorial XY plane. In the current apparent-frame convention,
mean and true ecliptic of date share the same ecliptic plane for this crossing:
true-of-date changes the ecliptic origin (and therefore the reported longitude)
through nutation, but not the node instant.

## Flags And Timescales

The low 32 bits accept only:

```text
TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX
```

The high-word search option is:

```text
TAIYIN_ORBITAL_EVENT_REVERSE
```

UT and TT entry points follow the ordinary Taiyin UT/TT-to-TDB conversion path.
The `jd` field of a search result uses the scale named by its entry point.
