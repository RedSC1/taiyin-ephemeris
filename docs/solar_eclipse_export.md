# Solar Eclipse Raw JSON Export

`taiyin_solar_eclipse_export` is a command-line tool under `tools/`. It writes
global solar-eclipse events, route curves, and region polygons already computed
by the Taiyin runtime to versioned JSON for map, almanac, and static-site
generators.

It is not a second eclipse solver or a web-product layer. The tool does not
recompute center lines or boundaries; all values come from the public eclipse
search and route-product APIs.

## Build And Run

```bash
cmake -S . -B build
cmake --build build --target taiyin_solar_eclipse_export

./build/taiyin_solar_eclipse_export \
  --data-root data \
  --start 2026-01-01 \
  --end 2027-01-01 \
  --route-samples 400 \
  --output forecast.json
```

Omit `--output`, or pass `--output -`, to write JSON to standard output.
`--route-samples N` controls the route curve and polygon point density. The
default is `400`; higher values make large-scale map paths smoother at larger
JSON size and export cost. It does not change ephemeris or eclipse-model
accuracy.

The default uses a smooth mean lunar limb. Pass a TLL1 model explicitly to
apply it to contact times and route boundaries:

```bash
./build/taiyin_solar_eclipse_export \
  --data-root data \
  --lunar-limb data/lunar-limb/kaguya_lalt_16ppd.tll1 \
  --start 2026-08-01 \
  --end 2026-09-01 \
  --output forecast-tll1.json
```

Merely placing a TLL1 file under the data root does not change results.
`--lunar-limb` explicitly enables the correction. A load failure is reported
as an error and never silently falls back to the smooth limb.

The range can also be supplied as Julian days:

```bash
./build/taiyin_solar_eclipse_export \
  --data-root data \
  --start-jd-ut 2461041.5 \
  --end-jd-ut 2461406.5
```

The start is inclusive and the end is exclusive. Calendar dates strictly use
`YYYY-MM-DD` from `0001-01-01` through `9999-12-31`. The calendar convention
matches `taiyin::julian_day()`: Gregorian dates begin on 1582-10-15, earlier
dates use the Julian calendar, and the missing dates 1582-10-05 through
1582-10-14 are rejected.

## JSON Contract

The top-level shape is:

```json
{
  "schema": "taiyin.solar-eclipse-forecast",
  "schema_version": 1,
  "time_scale": {},
  "query": {},
  "models": {},
  "events": []
}
```

`schema_version` controls the field contract. Consumers should validate both
`schema` and `schema_version` instead of inferring the format from a sample.

Each event contains:

- a stable `event_id` derived from the greatest-eclipse calendar date;
- numeric `kind_flags` and readable `kind` names;
- greatest-eclipse time, predicted Delta T, and greatest location;
- P1, C1, greatest, C4, and P4 times;
- axis distance, penumbral/core radii, and geometry margins;
- center, core, partial-contact, sunrise/sunset maximum, penumbral, and half-magnitude route curves;
- core, penumbral, and half-magnitude polygons;
- route counts, plus geographic bounds and an antimeridian flag when a closed
  polygon is available.

`route_product.available` reports whether route curves or polygons were
returned. `route_product.polygon_available` separately reports whether a
closed polygon is present.

A noncentral partial eclipse exports the physically present one-sided
penumbral limit. It also exports a half-magnitude limit when the eclipse reaches
magnitude 0.5, so its route product is available. The other side of the
complete visibility region is closed by sunrise, sunset, and contact-time
horizon geometry; it is not a second penumbral limit. The exporter preserves
that sunrise/sunset maximum boundary and uses it to close the penumbral polygon.
It does the same for the half-magnitude polygon when magnitude 0.5 is reached.
Empty half-magnitude curves and polygons are valid for a shallow partial eclipse.

For a central eclipse, a closed core, penumbral, or half-magnitude polygon is
generated when the corresponding boundary contains enough points; wide layers
use a sunrise/sunset maximum boundary when one physical limit does not exist.
`polygon_available` means that at least one layer was generated. Consumers
should inspect each `*_polygon_point_count` and polygon array for per-layer
availability. If no layer can be closed, `polygon_available` is `false` and
`polygon_reason` describes the absence.

Central smooth routes export `curves.core_begin_horizon` and
`curves.core_end_horizon` when the core-radius horizon intersections can be
solved. The core polygon uses these curves rather than directly joining the
north/south endpoints; if either curve cannot be solved, the core polygon is
left empty. Penumbral and half-magnitude polygons close with their own sunrise/sunset maximum boundaries
and every physically present limit, using refined endpoints at solution
transitions.

Times or geometry values that do not apply to an event are JSON `null`. The
output never contains `NaN` or `Infinity`.

## Time Scale

The first version explicitly uses predicted UT1 from an estimated Delta T:

```json
"time_scale": {
  "name": "UT1_ESTIMATED"
}
```

`jd_ut` and `calendar_ut` therefore represent predicted UT1. Future civil UTC
also depends on DUT1 that has not yet been observed. A web presentation may
label second-level display values as approximate UTC, but the data layer should
not claim that the two scales are identical.

## Model Boundary

The output declares the actual model and flags used. The default mode is:

```json
"models": {
  "earth_surface": "wgs84_sea_level",
  "terrain": "none",
  "lunar_limb": "smooth_mean",
  "lunar_limb_correction_enabled": false,
  "lunar_limb_source_id": null,
  "lunar_limb_generation": null,
  "eclipse_search_flags": 8589934592,
  "eclipse_route_flags": 0,
  "route_sample_count": 400
}
```

With `--lunar-limb`, `lunar_limb` is `tll1`, the correction flag is `true`, and
the TLL1 header's `source_id` and `generation` are included. Search flags apply
the model to eclipse contacts, while route flags apply it to non-center route
limits and their polygons. The center line remains unchanged because it is
defined by the shadow axis.

These fields mean the exporter uses the WGS84 sea-level ellipsoid and either
the smooth mean limb or the selected TLL1 model:

- no Earth DEM or actual observer elevation;
- no mountains, buildings, or real horizon profile;
- no Kaguya TLL1 correction by default;
- contact times and route edges use TLL1 together only when `--lunar-limb` is
  supplied explicitly.

The event, curve, and polygon organization does not depend on the selected
lunar-limb model.

## Downstream Responsibility

A separate almanac or map repository should own:

- GeoJSON conversion and antimeridian splitting;
- projection, simplification, and styling;
- SVG, PNG, and interactive maps;
- city, administrative-region, population, and weather data;
- GitHub Pages and scheduled Actions.

Downstream code should not reimplement eclipse geometry. Taiyin's raw JSON is
the astronomical result; map projects transform, present, and combine product
data around it.
