# Body Phenomena

Status: Current
Last reviewed: 2026-07-02
Main header: `include/taiyin/runtime/phenomena.h`

The phenomena API computes body phenomena quantities, comparable to the common outputs of SwissEph `swe_pheno_ut()`, without exposing a magic `attr[20]` array.

Current entry points:

```cpp
Status calc_body_phenomena_ut(
    const NativeCalcContext* context,
    int body_id,
    double jd_ut,
    uint64_t flags,
    BodyPhenomena* out,
    EphemerisEvalDiagnostic* diagnostic);

Status calc_body_phenomena_tt(
    const NativeCalcContext* context,
    int body_id,
    double jd_tt,
    uint64_t flags,
    BodyPhenomena* out,
    EphemerisEvalDiagnostic* diagnostic);
```

The low 32 bits of `flags` are native position flags. No phenomena-specific high-bit flags are defined yet; non-zero high bits return unsupported. The implementation forces XYZ output internally when computing angular quantities.

## Minimal Usage

The API does not require phenomena-specific configuration. Initialize the runtime, prepare a normal `NativeCalcContext`, then pass a body id, JD, and native position flags:

```cpp
#include "taiyin/body_id.h"
#include "taiyin/runtime/phenomena.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

taiyin::runtime::EphemerisRuntimeConfig config;
config.data_root = "data";
config.load_packaged_data = true;
taiyin::runtime::initialize_global_ephemeris_runtime(config);

taiyin::runtime::NativeCalcContext context;
taiyin::runtime::native_context_set_geocentric_observer(
    &context,
    taiyin::TAIYIN_BODY_EARTH,
    taiyin::TAIYIN_BODY_EARTH);
taiyin::runtime::native_context_use_solar_deflector(&context);
context.apparent_options.flags =
    taiyin::TAIYIN_APPARENT_LIGHT_TIME
    | taiyin::TAIYIN_APPARENT_ABERRATION
    | taiyin::TAIYIN_APPARENT_DEFLECTION;

const double jd_ut = taiyin::julian_day({2024, 4, 8, 18, 0, 0.0});
taiyin::runtime::BodyPhenomena moon;
taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
const taiyin::Status status = taiyin::runtime::calc_body_phenomena_ut(
    &context,
    taiyin::TAIYIN_BODY_MOON,
    jd_ut,
    0u,
    &moon,
    &diagnostic);

if (status == taiyin::TAIYIN_STATUS_OK) {
    // moon.phase_angle_rad
    // moon.illuminated_fraction
    // moon.solar_elongation_rad
    // moon.apparent_diameter_rad
    // moon.apparent_magnitude
    // moon.horizontal_parallax_rad
}
```

Pass `0u` for `flags` if the current `context` apparent configuration is enough. Pass the same native position flags as a position calculation when the phenomena output should match that calculation route. Do not pass `TAIYIN_APPARENT_*` bits to this `flags` parameter; apparent-correction switches belong on `context.apparent_options.flags`.

`TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX` is available when the caller explicitly accepts a major-planet barycenter as a body approximation. With this flag, position evaluation first tries the requested physical body; if the active route has no body route or a coverage/component gap, Mars, Jupiter, Saturn, Uranus, Neptune, and Pluto may retry through the matching barycenter. Mercury and Venus already alias to their barycenters in the built-in rules because their packaged routes use that conventional identity. Earth and Moon are not covered by this approximation flag; they use the dedicated EMB/Moon composition route. On a successful approximation, `diagnostic.target_id` remains the requested body id and `diagnostic.component_target_id` records the barycenter actually used.

Because the approximation path is strict-first, a route that structurally lacks body/COB data can pay for one failed body attempt before the barycenter retry on each call. This keeps the semantics simple and prevents future body data from being skipped silently; high-frequency searches can add a dedicated route-level optimization later if needed.

## Output Fields

`BodyPhenomena` currently contains:

| Field | Meaning |
| --- | --- |
| `phase_angle_rad` | Phase angle, i.e. observer-body-Sun angle. |
| `illuminated_fraction` | Illuminated fraction, computed as `(1 + cos(phase_angle)) / 2`. |
| `solar_elongation_rad` | Solar elongation, i.e. body-Sun angle as seen by the observer. |
| `apparent_diameter_rad` | Apparent disc diameter from the built-in conventional radius table and observer-body distance. |
| `apparent_magnitude` | Apparent visual magnitude from empirical apparent-magnitude models. |
| `horizontal_parallax_rad` | Geocentric horizontal parallax for the Moon; `NaN` for other bodies. |

Angles are radians. `illuminated_fraction` is dimensionless in `0..1`; `apparent_magnitude` is the usual astronomical apparent magnitude.

`illuminated_fraction` is an ideal-sphere geometric quantity: it reports how much of the visible disc area is sunlit, not how bright the disc appears. Near half Moon the geometric fraction is close to `0.5`, but the Moon is not half as bright as full Moon because albedo patterns, opposition surge, libration, passband response, and terrain scattering affect photometry.

## Current Boundaries

- `apparent_magnitude` is an empirical photometry model, not a strict radiative-transfer or surface-physics model.
- The current magnitude model covers Sun, Moon, Mercury, Venus, Mars, Jupiter, Saturn, Uranus, Neptune, and Pluto. Mercury, Venus, Mars, Jupiter, Saturn, Uranus, and Neptune use the empirical formulas and branch rules from Mallama & Hilton (2018); the Sun scales a mean V-band solar apparent magnitude by distance; the Moon uses an Astronomical-Almanac-style before/after full Moon phase model with a thin-crescent continuity fallback; Pluto currently uses the H-G phase function with `H=-0.55` and `G=0.15`. See [`phenomena_magnitude_models.md`](phenomena_magnitude_models.md) for details.
- The Moon magnitude is currently an almanac-grade empirical model, similar in scope to ordinary SwissEph `swe_pheno*()` phenomena output. It is not a ROLO lunar irradiance model and does not model observing passbands, lunar libration, or terrain-dependent high-precision lunar photometry.
- The Mars model needs the Mars body position plus sub-observer/sub-solar longitude derived from IAU rotation elements. If the active route rule can only provide the Mars barycenter and not the Mars body, the API returns the normal no-route status by default. Passing `TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX` opts into the less strict barycenter approximation.
- Saturn currently returns the default ring-included magnitude. The ring-included formula in the paper is defined for the small geocentric phase-angle range; outside that range Taiyin does not fabricate a globe-only result.
- Uranus magnitude includes the planetographic sub-latitude term; tests load the Uranus COB slice so the body route is present.
- The built-in radius table covers Sun, Moon, Mercury, Venus, Mars, Jupiter, Saturn, Uranus, Neptune, and Pluto.
- Barycenter IDs do not have physical radii and currently return unsupported.
- `horizontal_parallax_rad` currently implements only the Moon's geocentric horizontal parallax; SwissEph `SEFLG_TOPOCTR` topocentric parallax semantics are not implemented yet.
- Quantities use the provided `NativeCalcContext` and native position flags, so route rules, observer settings, apparent corrections, frame models, and ordinary position calculation stay aligned.

## SwissEph Relationship

SwissEph `swe_pheno_ut()` returns:

```text
attr[0] phase angle
attr[1] illuminated fraction
attr[2] elongation
attr[3] apparent diameter
attr[4] apparent magnitude
attr[5] Moon horizontal parallax
```

Taiyin currently aligns the common meaning of `attr[0..4]` and covers the Moon geocentric horizontal parallax corresponding to `attr[5]`.

Tests use fixed SwissEph-generated oracles to check that output semantics stay in the same range as a common reference implementation. Because Taiyin uses its own OPM2/runtime apparent chain, default apparent semantics are not bit-for-bit SwissEph compatible; tolerances are set for reference alignment, not ABI/compat-level parity.
