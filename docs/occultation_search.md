# Occultation Search

Status: Current
Last reviewed: 2026-07-05

Primary header: `include/taiyin/runtime/occultation_search.h`

The occultation module provides lunar fixed-star and lunar solar-system-body occultation search APIs. It is designed for the workflow "from this time, find the next or previous lunar occultation of this specified target." The API follows Taiyin's own context, flag, and result-struct conventions.

## Public Entries

```cpp
Status search_next_geocentric_lunar_star_occultation_ut(
    const NativeCalcContext* context,
    const char* star_key,
    double jd_start_ut,
    uint64_t flags,
    LunarStarOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status search_next_local_lunar_star_occultation_ut(
    const NativeCalcContext* context,
    const char* star_key,
    double jd_start_ut,
    uint64_t flags,
    LunarStarOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status search_next_geocentric_lunar_body_occultation_ut(
    const NativeCalcContext* context,
    int body_id,
    double target_radius_km,
    double jd_start_ut,
    uint64_t flags,
    LunarBodyOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status search_next_local_lunar_body_occultation_ut(
    const NativeCalcContext* context,
    int body_id,
    double target_radius_km,
    double jd_start_ut,
    uint64_t flags,
    LunarBodyOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status compute_lunar_star_occultation_local_visibility_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const LunarStarOccultationSearchResult* occultation,
    uint64_t visibility_flags,
    LunarOccultationLocalVisibility* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status compute_lunar_body_occultation_local_visibility_ut(
    const NativeCalcContext* context,
    int body_id,
    const LunarBodyOccultationSearchResult* occultation,
    uint64_t visibility_flags,
    LunarOccultationLocalVisibility* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status compute_lunar_star_occultation_where_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const LunarStarOccultationSearchResult* occultation,
    uint64_t flags,
    LunarOccultationWhereResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status compute_lunar_body_occultation_where_ut(
    const NativeCalcContext* context,
    int body_id,
    double target_radius_km,
    const LunarBodyOccultationSearchResult* occultation,
    uint64_t flags,
    LunarOccultationWhereResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;
```

`star_key` is resolved through the loaded TSC1/TSF1 star catalogs. Applications should load a catalog first with `add_global_tsc1_star_catalog()` or `add_global_tsf1_star_catalog()`.

`body_id` is a Taiyin/NAIF-style solar-system target ID and `target_radius_km` is its explicit circular apparent-disc radius for the search and `where` entries. A zero radius explicitly means a point target. The core runtime never estimates a small-body diameter from absolute magnitude or albedo. Local visibility only samples horizontal coordinates, so it needs `body_id` but not a radius. The Moon, Earth, EMB, Sun, and SSB are not valid lunar-body occultation targets; solar occultation belongs to the eclipse API.

The same four lunar-body entries also have a convenience overload without `target_radius_km`. It obtains the existing mean physical radius for known major bodies from Taiyin's shared disc-radius registry; unknown body IDs remain point targets. New integrations that have physical target metadata should pass the radius directly.

`search_next_geocentric_lunar_star_occultation_ut()` and `search_next_geocentric_lunar_body_occultation_ut()` test whether the Moon and the target overlap in geocentric apparent direction.

`search_next_local_lunar_star_occultation_ut()` and `search_next_local_lunar_body_occultation_ut()` use observer/topocentric information from `NativeCalcContext` for the final topocentric test. If the context has neither observer location nor topocentric offset, it returns `TAIYIN_ERROR_INVALID_ARGUMENT`. New local APIs do not take longitude/latitude as separate parameters.

`compute_lunar_*_occultation_local_visibility_ut()` does not search again. It takes an existing lunar occultation result, samples local horizontal coordinates at C1/C2/maximum/C3/C4, and returns Moon, target, and Sun altitude/azimuth plus visibility bits. It also requires an observer in the context.

`compute_lunar_*_occultation_where_ut()` also does not search again. It takes an existing lunar occultation event from either a geocentric search or a local/topocentric search; the `where` entry only uses the event kind and maximum-occultation time. It projects the Moon-target center line onto the Earth with sxwnl-style line-Earth geometry. If the center line hits the Earth, the result contains the center-line point, sampled center-line path, outer-contact limit band, and closed polygon. If the center line misses the Earth, the result contains a first-version best-observer point for a noncentral occultation.

## Capability Boundary

This module currently prioritizes specified-target lunar occultation workflows, not whole-catalog almanac scanning.

The Taiyin side currently covers:

- search the next or previous geocentric lunar occultation of a specified fixed star or solar-system body target from a supplied UT start;
- search the next or previous local/topocentric lunar occultation of a specified fixed star or solar-system body target from a supplied UT start;
- return maximum-occultation / minimum-separation time and contact times;
- return event type bits from the maximum-occultation and center-line geometry, including total, partial, grazing, central, and noncentral cases;
- sample Moon/target/Sun altitude and azimuth at contacts and maximum for an existing local occultation result;
- compute a first-version `where` center-line / best-observer point for an existing lunar occultation event, including noncentral best-observer points when the center line misses the Earth.

The remaining work is mainly:

- bulletin-grade `where` surface regions, such as lunar-limb-topography and terrain-corrected north/south limits;
- fuller local visibility products, such as associating rise/set events with interval indexes and generating almanac-facing summaries;
- more public-prediction oracle coverage, especially boundary and grazing cases.

Whole-catalog lunar occultation scanning is better treated as a later almanac/catalog layer feature, not as the current core-runtime target. If added later, it should start with star prefiltering, time-window management, and output-shape design instead of simply looping this next-search API over every star.

## Flags

Occultation APIs use a 64-bit flag word. The low 32 bits are native-position
modifiers shared with `calc_position_*`; the high 32 bits are occultation
search or visibility options:

```cpp
TAIYIN_OCCULTATION_POSITION_FLAGS_MASK
TAIYIN_OCCULTATION_OPTION_FLAGS_MASK

TAIYIN_OCCULTATION_SEARCH_TRUEPOS
TAIYIN_OCCULTATION_SEARCH_BACKWARD
TAIYIN_OCCULTATION_SEARCH_ONE_CANDIDATE
TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION

TAIYIN_OCCULTATION_FILTER_PARTIAL
TAIYIN_OCCULTATION_FILTER_TOTAL
TAIYIN_OCCULTATION_FILTER_GRAZING
TAIYIN_OCCULTATION_FILTER_CENTRAL
TAIYIN_OCCULTATION_FILTER_NONCENTRAL

TAIYIN_OCCULTATION_VISIBILITY_REFRACTION
```

Search is forward by default. Set `TAIYIN_OCCULTATION_SEARCH_BACKWARD` to find the previous event before `jd_start_ut`.

`TAIYIN_OCCULTATION_SEARCH_TRUEPOS` is a compatibility alias for the low-bit native `TAIYIN_NATIVE_POSITION_TRUEPOS` flag. Set it to use true/geometric positions, disabling light-time, aberration, deflection, and related apparent corrections. The flag affects both candidate seeding and final separation testing. The search entries also accept `TAIYIN_NATIVE_POSITION_ASTROMETRIC`, `TAIYIN_NATIVE_POSITION_NO_ABERR`, and `TAIYIN_NATIVE_POSITION_NO_GDEFL`. Output-shape native flags such as `XYZ`, `EQUATORIAL`, `RADIANS`, `SPEED`, and `TOPOCENTRIC` are rejected by the occultation search API because this result shape is fixed.

Set `TAIYIN_OCCULTATION_SEARCH_ONE_CANDIDATE` to probe only the first lunar-passage candidate around `jd_start_ut`. If that candidate is not an occultation or does not match the requested filters, the function returns `TAIYIN_EVENT_ERROR_NOT_FOUND` and fills `candidate_jd_ut`, `next_search_jd_ut`, and `candidate_count` so a caller-managed scanner can continue from the suggested next start.

Set `TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION` to replace the smooth lunar radius with the direction-dependent TLL1 silhouette for final event classification and contact times. Candidate generation and minimum-separation search remain on the smooth model. The smooth event solution seeds a corrected-residual contact scan; the solver brackets each corrected root, attempts finite-difference Newton steps only when they remain inside the bracket and reduce the residual, and otherwise falls back to safeguarded secant/bisection. A globally loaded TLL1 model is required, otherwise the search returns `TAIYIN_ERROR_UNSUPPORTED`. Loading a model without this flag does not change results.

Type filter bits restrict which classified events are accepted. Without filter bits, any lunar occultation type is accepted. With one or more filter bits, the search only returns an event whose final `type_flags` intersects the selected filters; otherwise normal search continues to the next candidate. In one-candidate mode, a filter mismatch stops after that candidate.

`TAIYIN_OCCULTATION_SEARCH_BACKWARD` and `TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION` are only valid for `search_next_*` entries. `compute_lunar_*_occultation_where_ut()` evaluates geometry at an already known event time and does not yet apply TLL1 to its surface limits, so it accepts only `TAIYIN_OCCULTATION_SEARCH_TRUEPOS` and `TAIYIN_OCCULTATION_VISIBILITY_REFRACTION`.

`TAIYIN_OCCULTATION_VISIBILITY_REFRACTION` is used only by the local visibility summary. By default, visibility samples use geometric horizontal altitude. With this bit set, the returned horizontal coordinates are refracted; callers must configure atmosphere in the context when requesting refraction.

## Result

```cpp
struct LunarStarOccultationSearchResult {
    int kind;
    uint32_t type_flags;
    double jd_ut;
    double begin_jd_ut;
    double end_jd_ut;
    double first_contact_jd_ut;
    double second_contact_jd_ut;
    double third_contact_jd_ut;
    double fourth_contact_jd_ut;
    double separation_rad;
    double moon_radius_rad;
    double target_radius_rad;
    double margin_rad;
    double candidate_jd_ut;
    double next_search_jd_ut;
    int candidate_count;
    int iteration_count;
    int evaluation_count;
};
```

`jd_ut` is the maximum-occultation / minimum-separation time between the Moon and the target.

`first_contact_jd_ut` through `fourth_contact_jd_ut` are the C1-C4 contact times. C1/C4 solve `moon_radius_rad + target_radius_rad - separation_rad = 0`; C2/C3 solve `abs(moon_radius_rad - target_radius_rad) - separation_rad = 0`. `begin_jd_ut` is a compatibility alias for `first_contact_jd_ut`, and `end_jd_ut` is a compatibility alias for `fourth_contact_jd_ut`. Fixed stars are treated as point targets, so `target_radius_rad = 0` and C2/C3 remain NaN. Solar-system body targets use a mean physical radius and the current observer-target distance to compute an apparent target radius.

`margin_rad = moon_radius_rad + target_radius_rad - separation_rad`. When it is nonnegative, the apparent lunar disk overlaps the apparent target disk and the search returns `TAIYIN_STATUS_OK`. If no event is found, the function returns `TAIYIN_EVENT_ERROR_NOT_FOUND` and `kind` remains `TAIYIN_OCCULTATION_KIND_NONE`.

`candidate_jd_ut` records the last refined lunar-passage candidate that was probed. `next_search_jd_ut` is the next suggested start epoch for caller-managed scanning. `candidate_count` records how many candidates were probed in this call. These fields are especially useful with `TAIYIN_OCCULTATION_SEARCH_ONE_CANDIDATE`.

`type_flags` uses traditional event classification bits:

```cpp
TAIYIN_OCCULTATION_TYPE_PARTIAL
TAIYIN_OCCULTATION_TYPE_TOTAL
TAIYIN_OCCULTATION_TYPE_ANNULAR
TAIYIN_OCCULTATION_TYPE_GRAZING
TAIYIN_OCCULTATION_TYPE_CENTRAL
TAIYIN_OCCULTATION_TYPE_NONCENTRAL
TAIYIN_OCCULTATION_TYPE_CENTRALITY_UNAVAILABLE
```

Plain lunar fixed-star/body search results fill `PARTIAL`, `TOTAL`, and `GRAZING` from the apparent radii and minimum separation at maximum occultation, and try to fill `CENTRAL` or `NONCENTRAL` from center-line Earth-intersection geometry at the same epoch. `TAIYIN_OCCULTATION_TYPE_ANNULAR` remains a reserved type bit, but this non-solar lunar occultation search does not return it. Center-line classification depends on the current model context being able to evaluate GAST; if that model combination is unsupported, the search still returns the event and contact times and sets `CENTRALITY_UNAVAILABLE` instead of failing the whole event. `compute_lunar_*_occultation_where_ut()` uses the same center-line geometry and additionally returns the center-line / best-observer longitude and latitude; as a dedicated where entry, it returns an error when that geometry cannot be evaluated. Fixed stars are treated as point targets, so they usually classify as `TOTAL` or grazing.

## Local Visibility Summary

```cpp
struct LunarOccultationLocalVisibilitySample {
    int valid;
    double jd_ut;
    double moon_altitude_rad;
    double moon_azimuth_rad;
    double target_altitude_rad;
    double target_azimuth_rad;
    double sun_altitude_rad;
    double sun_azimuth_rad;
    uint32_t visibility_flags;
};

struct LunarOccultationPhenomena {
    double angular_distance_rad;
    double diameter_ratio;
    double magnitude;
    double obscuration;
    double occulted_fraction;
};

struct LunarOccultationVisibilityInterval {
    int valid;
    double begin_jd_ut;
    double end_jd_ut;
};

struct LunarOccultationLocalVisibility {
    LunarOccultationLocalVisibilitySample first_contact;
    LunarOccultationLocalVisibilitySample second_contact;
    LunarOccultationLocalVisibilitySample maximum;
    LunarOccultationLocalVisibilitySample third_contact;
    LunarOccultationLocalVisibilitySample fourth_contact;
    double target_rise_jd_ut;
    double target_set_jd_ut;
    double visible_begin_jd_ut;
    double visible_end_jd_ut;
    double dark_visible_begin_jd_ut;
    double dark_visible_end_jd_ut;
    int visible_interval_count;
    LunarOccultationVisibilityInterval visible_intervals[TAIYIN_OCCULTATION_MAX_VISIBILITY_INTERVALS];
    int dark_visible_interval_count;
    LunarOccultationVisibilityInterval dark_visible_intervals[TAIYIN_OCCULTATION_MAX_VISIBILITY_INTERVALS];
    uint32_t visibility_flags;
};

struct LunarOccultationWherePathPoint {
    int valid;
    double jd_ut;
    double longitude_deg;
    double latitude_deg;
    double height_m;
};

struct LunarOccultationWhereResult {
    int center_line_hits_earth;
    uint32_t type_flags;
    double jd_ut;
    double center_line_begin_jd_ut;
    double center_line_end_jd_ut;
    int center_line_path_count;
    LunarOccultationWherePathPoint center_line_path[TAIYIN_OCCULTATION_WHERE_MAX_PATH_POINTS];
    double center_line_min_longitude_deg;
    double center_line_max_longitude_deg;
    double center_line_min_latitude_deg;
    double center_line_max_latitude_deg;
    double center_line_path_distance_km;
    int outer_limit_path_count;
    LunarOccultationWherePathPoint outer_north_path[TAIYIN_OCCULTATION_WHERE_MAX_PATH_POINTS];
    LunarOccultationWherePathPoint outer_south_path[TAIYIN_OCCULTATION_WHERE_MAX_PATH_POINTS];
    double outer_limit_mean_width_km;
    double outer_limit_max_width_km;
    int visible_region_polygon_count;
    LunarOccultationWherePathPoint visible_region_polygon[TAIYIN_OCCULTATION_WHERE_MAX_POLYGON_POINTS];
    double visible_region_min_longitude_deg;
    double visible_region_max_longitude_deg;
    double visible_region_min_latitude_deg;
    double visible_region_max_latitude_deg;
    double longitude_deg;
    double latitude_deg;
    double height_m;
    double separation_rad;
    double moon_radius_rad;
    double target_radius_rad;
    double margin_rad;
    LunarOccultationPhenomena phenomena;
    LunarOccultationLocalVisibilitySample local_sample;
    uint32_t visibility_flags;
};
```

`center_line_begin_jd_ut` and `center_line_end_jd_ut` are the first-version time
bounds where the Moon-target center line intersects the Earth around `jd_ut`.
If the event is noncentral, or the helper cannot bracket an edge with the
current event/window, the field is left as `NaN`.

`center_line_path` samples the same center-line intersection between
`center_line_begin_jd_ut` and `center_line_end_jd_ut` with a fixed-size summary
array. `center_line_*_longitude_deg`, `center_line_*_latitude_deg`, and
`center_line_path_distance_km` summarize that sampled center line. This is a
center-line path product, not a full visibility polygon or north/south limit
product. Noncentral occultations have no center-line ground path, so
`center_line_path_count` remains `0` and `longitude_deg` / `latitude_deg`
identify the best-observer point.

`outer_north_path` and `outer_south_path` are the first-version outer-contact
limit-band summary. For each sampled center-line point, Taiyin evaluates the
current topocentric occultation geometry along the local normal and solves for
the two ground points where `margin_rad = 0`. `outer_limit_mean_width_km` and
`outer_limit_max_width_km` summarize the approximate width between those paired
boundary points. These fields are generated only when the center line intersects
the Earth; noncentral events keep `outer_limit_path_count == 0`. This is not a
lunar-limb-topography corrected limit and not a complete polygon product.

`visible_region_polygon` closes `outer_north_path` with the reversed
`outer_south_path` into a first-version visibility polygon. Longitudes are
unwrapped against adjacent points, so a path crossing the `-180/180` meridian
does not tear the polygon envelope apart. Callers that need normalized
longitudes can normalize individual points after reading them. This polygon is
still derived from the outer-contact limit band above; it does not include lunar
limb topography or terrain elevation.

`LunarOccultationPhenomena` is an attr-style phenomena summary:
`angular_distance_rad` is the Moon-target center separation. `diameter_ratio`,
`magnitude`, and `obscuration` follow the traditional lunar-occultation
convention where possible: `diameter_ratio` is lunar apparent diameter divided
by target apparent diameter, `magnitude` is the fraction of the target diameter
covered by the Moon, and `obscuration` follows the corresponding two-disc area
quantity. For small planetary targets these values can be greater than 1. Fixed
stars are treated as point targets. `occulted_fraction` is Taiyin's normalized
0..1 covered-target fraction for callers that want a bounded quantity.

`LunarOccultationWhereResult::height_m` is currently always `0.0`. The center-line / best-observer point is evaluated at sea level; terrain elevation is not queried or estimated by the current `where` geometry.

Each sample uses these bits:

```cpp
TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_MOON_ABOVE_HORIZON
TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_TARGET_ABOVE_HORIZON
TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_SUN_BELOW_HORIZON
```

`TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_GEOMETRICALLY_VISIBLE` is the combined mask for Moon and target above the horizon. `TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_DARK_SKY_VISIBLE` also requires the Sun below the horizon.

The aggregate result uses these bits:

```cpp
TAIYIN_OCCULTATION_VISIBILITY_HAS_VISIBLE_SAMPLE
TAIYIN_OCCULTATION_VISIBILITY_MAXIMUM_VISIBLE
TAIYIN_OCCULTATION_VISIBILITY_HAS_DARK_SAMPLE
TAIYIN_OCCULTATION_VISIBILITY_MAXIMUM_DARK
TAIYIN_OCCULTATION_VISIBILITY_HAS_VISIBLE_INTERVAL
TAIYIN_OCCULTATION_VISIBILITY_HAS_DARK_INTERVAL
```

Fixed stars are point targets, so C2/C3 are not physical contacts and their samples have `valid = 0`. Body targets return C1-C4 samples when inner contacts exist.

`target_rise_jd_ut` and `target_set_jd_ut` are target-altitude horizon crossings
inside the occultation interval when such crossings exist. If the target stays
above or below the horizon throughout the event, the corresponding field remains
`NaN`. `visible_begin_jd_ut` / `visible_end_jd_ut` describe the first continuous
sub-interval where both the Moon and target are above the horizon.
`dark_visible_begin_jd_ut` / `dark_visible_end_jd_ut` add the requirement that
the Sun is below the horizon. These local intervals are computed with the same
horizontal coordinates selected by `TAIYIN_OCCULTATION_VISIBILITY_REFRACTION`.
`visible_intervals[]` and `dark_visible_intervals[]` store all intervals found
by the scan. The legacy begin/end fields mirror the first interval for simple
callers.

## Search Algorithm

The current implementation does not scan the whole search horizon with a fixed small step, and it is not a minute-by-minute brute-force search over future decades. The public entry is a next-event search: it first generates a small set of candidates, then refines the geometry around each candidate.

1. Compute geocentric true-ecliptic longitude for the Moon and the target near the start time.
2. Estimate the next longitude conjunction seed from the relative lunar/target longitude rate.
3. Refine the seed with a few longitude-conjunction iterations. After each candidate, the next probe epoch recomputes lunar/target longitude and rate; the search does not keep adding one start-epoch mean period forever.
4. Filter candidates using the Moon/star ecliptic latitude difference.
5. For surviving candidates, minimize the three-dimensional angular separation around the seed.
6. If the minimum separation is less than or equal to the sum of lunar and target apparent radii, solve the two outer contact roots around maximum; for body targets fully covered at maximum, also solve the two inner contact roots and return the event.

The local entry still generates seeds from geocentric ecliptic coordinates and uses a wider latitude gate to account for possible topocentric parallax shifts. The final result is recomputed and tested with local/topocentric observed directions.

This keeps seed/gate logic as candidate generation only; final geometry is not tied to the coarse model.

### Why Can It Miss If It Refines?

The hard numerical search happens only around candidate seeds: once a candidate survives the latitude gate, the code minimizes the three-dimensional angular separation inside that local window and then bisects the contact roots. It does not densely scan the full search horizon.

Possible misses therefore come from candidate generation, not from the local minimum solver:

- The closest approach can be offset from the time when lunar longitude equals target longitude. Local/topocentric occultations can shift further because lunar parallax is large.
- If the seed is too far from the real closest approach, the fixed local window may not contain the true minimum.
- If the latitude gate rejects a candidate too early, the target may still enter the occultation band nearby because of topocentric parallax, planetary motion, or proper motion.
- Fixed stars have proper motion. TSC1/TSF1 evaluation propagates the star at each sample epoch, but the seed/gate layer still must avoid long-horizon rejection based only on the start epoch.

The current implementation is suitable for near-modern, specified-target next-searches and regression tests. Long-range searches need a stronger candidate layer, such as eclipse/transit-style `k` candidates, conservative F/latitude filtering, and bracket sizes validated against true-ephemeris brute-force scans. Whole-catalog scanning belongs to a higher almanac/catalog layer and is not the next step for this API.

## Test Coverage

`test_occultation_search` covers:

- synthetic TSF1 stars placed on the Moon direction at selected epochs;
- a multi-date synthetic grid searched from 20 days before the planted occultation, guarding against dense-scan fallback;
- packaged TSC1 smoke coverage using `data/stars/catalogs/stars-fixed-traditional.tsc1` and low-ecliptic-latitude bright stars;
- external-reference oracle coverage: Antares, Spica, Regulus, and Aldebaran local lunar fixed-star maximum, ingress, and egress fixtures, plus Mercury/Venus/Mars/Jupiter/Saturn local lunar-body maximum and C1-C4 fixtures; Jupiter/Saturn exercise COB body composition, while Mars uses a Mars-barycenter approximation because the current packaged COB set does not include a Mars offset file;
- `where` coverage for Antares and Mercury center-line locations, outer-contact limit bands, closed polygons, plus Spica and Saturn noncentral best-observer points, compared with external-reference fixtures;
- local visibility summary coverage for Antares and Mercury local occultations, including finite altitude/azimuth samples, multi-interval visibility lists, and missing-observer rejection;
- major-body smoke coverage that searches Mercury/Venus/Mars/Jupiter/Saturn for one real lunar-body event and verifies invalid Moon/Sun targets are rejected;
- Mercury/Venus lunar-body seed boundaries: starts just before maximum, after ingress, and backward from after the event all recover the same occultation;
- missing-star error propagation;
- the local API requirement that observer/topocentric data must come from `NativeCalcContext`.
- opt-in TLL1 contact correction for geocentric body and local fixed-star events, including missing-model rejection and disabled-model transparency.

## Current Limits

The first version returns maximum-occultation / minimum-separation time; body targets return C1-C4 contacts, and point-star targets return C1/C4. It also returns basic occultation type bits and provides a `where` center-line / best-observer point: central events return a center-line point and sampled path, while noncentral events return a best-observer point. It does not yet provide:

- planetary oblateness, Saturn's rings, or stellar angular diameters; body targets currently use mean physical radius as the target apparent radius;
- magnitude limits, solar elongation limits, or lunar-altitude limits;
- bulletin-grade `where` surface regions; the current polygon closes the outer-contact limit band but does not yet apply TLL1 lunar-limb topography, terrain elevation, or higher-order boundary smoothing;
- local visibility intervals are returned as lists, but rise/set events are not yet associated with specific interval indexes and there is no almanac-style textual summary;
- batch scanning over an entire star catalog; this is deferred to a later catalog/almanac layer design;
- mutual planetary occultations, satellite transits, or a general occultation/appulse framework;
- second-level external oracle comparison against published occultation predictions.

Those capabilities should build on the current next-event API once it is stable. Near-term occultation work should keep focusing on specified-target behavior and oracle coverage; batch star-catalog scanning is deferred.
