#include "taiyin/runtime/occultation_search.h"

#include "runtime/eclipse/eclipse_time.h"
#include "runtime/eclipse/solar_eclipse_sxwnl.h"
#include "runtime/occultation/sxwnl_occultation_ext.h"

#include "taiyin/angle.h"
#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/internal/body_disc_radius.h"
#include "taiyin/physical_constants.h"
#include "taiyin/lunar_limb_tll1.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/lunar_limb.h"
#include "taiyin/runtime/observed_position.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/runtime/star_position.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace taiyin {
namespace runtime {
namespace {

const double MOON_RADIUS_KM = 1737.4;
const int OCCULTATION_MAX_CANDIDATES = 2048;
const double OCCULTATION_SEED_WINDOW_DAYS = 0.65;
const double OCCULTATION_MIN_SEED_ADVANCE_DAYS = 0.75;
const double MOON_MEAN_SIDEREAL_RATE_RAD_PER_DAY = TAIYIN_TWO_PI / 27.321661547;
const double MOON_MAX_ECLIPTIC_LATITUDE_RAD = 5.35 * TAIYIN_DEG_TO_RAD;
const double GEOCENTRIC_ECLIPTIC_MARGIN_RAD = 0.45 * TAIYIN_DEG_TO_RAD;
const double LOCAL_ECLIPTIC_MARGIN_RAD = 1.55 * TAIYIN_DEG_TO_RAD;
const double STAR_PROPER_MOTION_LATITUDE_SAFETY_RAD = 10.0 * TAIYIN_DEG_TO_RAD;
const double OCCULTATION_CONTACT_SCAN_STEP_DAYS = 10.0 / 1440.0;
const double OCCULTATION_CONTACT_TOLERANCE_DAYS = 1.0e-9;
const double OCCULTATION_CONTACT_NEWTON_STEP_DAYS = 0.5 / 86400.0;
const int OCCULTATION_CONTACT_NEWTON_ITERATIONS = 4;
const double OCCULTATION_LIMB_EXTREMUM_HALF_WINDOW_DAYS = 10.0 / 1440.0;
const int OCCULTATION_LIMB_EXTREMUM_SCAN_INTERVALS = 32;
const int OCCULTATION_LIMB_EXTREMUM_REFINE_ITERATIONS = 20;
const double OCCULTATION_VISIBILITY_SCAN_STEP_DAYS = 5.0 / 1440.0;
const uint32_t SUPPORTED_OCCULTATION_POSITION_FLAGS =
    TAIYIN_NATIVE_POSITION_TRUEPOS
    | TAIYIN_NATIVE_POSITION_ASTROMETRIC
    | TAIYIN_NATIVE_POSITION_NO_ABERR
    | TAIYIN_NATIVE_POSITION_NO_GDEFL;
const uint64_t OCCULTATION_TYPE_FILTER_FLAGS =
    TAIYIN_OCCULTATION_FILTER_PARTIAL
    | TAIYIN_OCCULTATION_FILTER_TOTAL
    | TAIYIN_OCCULTATION_FILTER_GRAZING
    | TAIYIN_OCCULTATION_FILTER_CENTRAL
    | TAIYIN_OCCULTATION_FILTER_NONCENTRAL;
const uint64_t SUPPORTED_OCCULTATION_SEARCH_FLAGS =
    static_cast<uint64_t>(SUPPORTED_OCCULTATION_POSITION_FLAGS)
    | TAIYIN_OCCULTATION_SEARCH_BACKWARD
    | TAIYIN_OCCULTATION_SEARCH_ONE_CANDIDATE
    | TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION
    | OCCULTATION_TYPE_FILTER_FLAGS;
const uint64_t SUPPORTED_OCCULTATION_WHERE_FLAGS =
    static_cast<uint64_t>(SUPPORTED_OCCULTATION_POSITION_FLAGS)
    | TAIYIN_OCCULTATION_VISIBILITY_REFRACTION;

SplitJulianDate invalid_jd() noexcept {
    return SplitJulianDate(0, std::numeric_limits<double>::quiet_NaN());
}

struct OccultationSample {
    SplitJulianDate jd_ut;
    double separation_rad;
    double separation_rate_rad_per_day;
    double separation_acceleration_rad_per_day2;
    double moon_radius_rad;
    double moon_radius_rate_rad_per_day;
    double target_radius_rad;
    double target_radius_rate_rad_per_day;
    double margin_rad;
    Vector3 observer_to_moon_au;
    Vector3 observer_to_target_au;

    OccultationSample() noexcept
        : jd_ut(invalid_jd()),
          separation_rad(NAN),
          separation_rate_rad_per_day(NAN),
          separation_acceleration_rad_per_day2(NAN),
          moon_radius_rad(NAN),
          moon_radius_rate_rad_per_day(NAN),
          target_radius_rad(0.0),
          target_radius_rate_rad_per_day(NAN),
          margin_rad(NAN),
          observer_to_moon_au{NAN, NAN, NAN},
          observer_to_target_au{NAN, NAN, NAN} {}
};

enum OccultationContactBoundary {
    OCCULTATION_CONTACT_OUTER,
    OCCULTATION_CONTACT_INNER,
};

struct EclipticOccultationSeedSample {
    SplitJulianDate jd_ut;
    double moon_lon_rad;
    double moon_lat_rad;
    double moon_lon_rate_rad_per_day;
    double target_lon_rad;
    double target_lat_rad;
    double target_lon_rate_rad_per_day;

    EclipticOccultationSeedSample() noexcept
        : jd_ut(invalid_jd()),
          moon_lon_rad(NAN),
          moon_lat_rad(NAN),
          moon_lon_rate_rad_per_day(NAN),
          target_lon_rad(NAN),
          target_lat_rad(NAN),
          target_lon_rate_rad_per_day(0.0) {}
};

bool context_has_observer(const NativeCalcContext& context) noexcept {
    return context.fields.has(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION)
        || context.fields.has(TAIYIN_NATIVE_FIELD_TOPOCENTRIC_OFFSET);
}

bool finite_vector3(const Vector3& value) noexcept {
    return std::isfinite(value.x)
        && std::isfinite(value.y)
        && std::isfinite(value.z);
}

double occultation_contact_margin(
    const OccultationSample& sample,
    OccultationContactBoundary boundary
) noexcept {
    if (boundary == OCCULTATION_CONTACT_INNER) {
        return std::fabs(sample.moon_radius_rad - sample.target_radius_rad) - sample.separation_rad;
    }
    return sample.margin_rad;
}

uint32_t occultation_type_flags_from_geometry(
    double separation_rad,
    double moon_radius_rad,
    double target_radius_rad
) noexcept {
    if (!std::isfinite(separation_rad)
        || !std::isfinite(moon_radius_rad)
        || !std::isfinite(target_radius_rad)
        || moon_radius_rad < 0.0
        || target_radius_rad < 0.0) {
        return 0u;
    }
    const double outer_margin = moon_radius_rad + target_radius_rad - separation_rad;
    if (!(outer_margin >= 0.0)) {
        return 0u;
    }

    uint32_t flags = 0u;
    const double inner_margin = std::fabs(moon_radius_rad - target_radius_rad) - separation_rad;
    if (inner_margin >= 0.0) {
        flags |= moon_radius_rad >= target_radius_rad
            ? TAIYIN_OCCULTATION_TYPE_TOTAL
            : TAIYIN_OCCULTATION_TYPE_PARTIAL;
    } else {
        flags |= TAIYIN_OCCULTATION_TYPE_PARTIAL;
    }

    const double scale = std::max(1.0, std::max(moon_radius_rad, target_radius_rad));
    const double grazing_tolerance = 1.0e-12 * scale;
    if (std::fabs(outer_margin) <= grazing_tolerance
        || std::fabs(inner_margin) <= grazing_tolerance) {
        flags |= TAIYIN_OCCULTATION_TYPE_GRAZING;
    }
    return flags;
}

uint32_t occultation_type_flags_from_sample(const OccultationSample& sample) noexcept {
    return occultation_type_flags_from_geometry(
        sample.separation_rad,
        sample.moon_radius_rad,
        sample.target_radius_rad);
}

double circle_overlap_area(
    double r1,
    double r2,
    double d
) noexcept {
    if (!std::isfinite(r1) || !std::isfinite(r2) || !std::isfinite(d)
        || !(r1 > 0.0) || !(r2 > 0.0) || d < 0.0) {
        return NAN;
    }
    if (d >= r1 + r2) return 0.0;
    if (d <= std::fabs(r1 - r2)) {
        const double r = std::min(r1, r2);
        return M_PI * r * r;
    }
    double c1 = (d * d + r1 * r1 - r2 * r2) / (2.0 * d * r1);
    double c2 = (d * d + r2 * r2 - r1 * r1) / (2.0 * d * r2);
    if (c1 < -1.0) c1 = -1.0;
    if (c1 > 1.0) c1 = 1.0;
    if (c2 < -1.0) c2 = -1.0;
    if (c2 > 1.0) c2 = 1.0;
    const double a1 = std::acos(c1);
    const double a2 = std::acos(c2);
    const double area = r1 * r1 * a1
        + r2 * r2 * a2
        - 0.5 * std::sqrt(std::max(
            0.0,
            (-d + r1 + r2) * (d + r1 - r2) * (d - r1 + r2) * (d + r1 + r2)));
    return area;
}

LunarOccultationPhenomena phenomena_from_sample(const OccultationSample& sample) noexcept {
    LunarOccultationPhenomena out;
    out.angular_distance_rad = sample.separation_rad;
    if (std::isfinite(sample.moon_radius_rad) && sample.moon_radius_rad > 0.0
        && std::isfinite(sample.target_radius_rad) && sample.target_radius_rad >= 0.0) {
        out.diameter_ratio = sample.target_radius_rad > 0.0
            ? sample.moon_radius_rad / sample.target_radius_rad
            : 0.0;
    }
    if (std::isfinite(sample.margin_rad)
        && std::isfinite(sample.moon_radius_rad)
        && std::isfinite(sample.target_radius_rad)) {
        if (sample.target_radius_rad > 0.0) {
            out.magnitude = (sample.moon_radius_rad + sample.target_radius_rad - sample.separation_rad)
                / (2.0 * sample.target_radius_rad);

            const double overlap = circle_overlap_area(
                sample.moon_radius_rad,
                sample.target_radius_rad,
                sample.separation_rad);
            const double target_area = M_PI * sample.target_radius_rad * sample.target_radius_rad;
            if (std::isfinite(overlap) && target_area > 0.0) {
                const double occulted_fraction = overlap / target_area;
                const bool total_or_annular =
                    sample.separation_rad <= std::fabs(sample.moon_radius_rad - sample.target_radius_rad);
                out.obscuration = total_or_annular
                    ? (sample.moon_radius_rad * sample.moon_radius_rad)
                        / (sample.target_radius_rad * sample.target_radius_rad)
                    : occulted_fraction;
                out.occulted_fraction = occulted_fraction;
                if (out.occulted_fraction < 0.0) out.occulted_fraction = 0.0;
                if (out.occulted_fraction > 1.0) out.occulted_fraction = 1.0;
            }
        } else {
            const double covered = sample.margin_rad >= 0.0 ? 1.0 : 0.0;
            out.magnitude = covered;
            out.obscuration = covered;
            out.occulted_fraction = covered;
        }
    }
    return out;
}

uint32_t occultation_position_flags(uint64_t flags) noexcept {
    return static_cast<uint32_t>(flags & TAIYIN_OCCULTATION_POSITION_FLAGS_MASK);
}

bool occultation_type_filter_matches(uint64_t flags, uint32_t type_flags) noexcept {
    const uint64_t filter = flags & OCCULTATION_TYPE_FILTER_FLAGS;
    if (filter == 0u) return true;
    if ((filter & TAIYIN_OCCULTATION_FILTER_PARTIAL) != 0u
        && (type_flags & TAIYIN_OCCULTATION_TYPE_PARTIAL) != 0u) {
        return true;
    }
    if ((filter & TAIYIN_OCCULTATION_FILTER_TOTAL) != 0u
        && (type_flags & TAIYIN_OCCULTATION_TYPE_TOTAL) != 0u) {
        return true;
    }
    if ((filter & TAIYIN_OCCULTATION_FILTER_GRAZING) != 0u
        && (type_flags & TAIYIN_OCCULTATION_TYPE_GRAZING) != 0u) {
        return true;
    }
    if ((filter & TAIYIN_OCCULTATION_FILTER_CENTRAL) != 0u
        && (type_flags & TAIYIN_OCCULTATION_TYPE_CENTRAL) != 0u) {
        return true;
    }
    return (filter & TAIYIN_OCCULTATION_FILTER_NONCENTRAL) != 0u
        && (type_flags & TAIYIN_OCCULTATION_TYPE_NONCENTRAL) != 0u;
}

uint32_t native_flags_from_occultation_flags(uint64_t flags, bool topocentric) noexcept {
    uint32_t native_flags =
        TAIYIN_NATIVE_POSITION_RADIANS
        | TAIYIN_NATIVE_POSITION_SPEED;
    const uint32_t position_flags = occultation_position_flags(flags);
    if ((position_flags & TAIYIN_NATIVE_POSITION_TRUEPOS) != 0u) {
        native_flags |= TAIYIN_NATIVE_POSITION_TRUEPOS;
    }
    if ((position_flags & TAIYIN_NATIVE_POSITION_ASTROMETRIC) != 0u) {
        native_flags |= TAIYIN_NATIVE_POSITION_ASTROMETRIC;
    }
    if ((position_flags & TAIYIN_NATIVE_POSITION_NO_ABERR) != 0u) {
        native_flags |= TAIYIN_NATIVE_POSITION_NO_ABERR;
    }
    if ((position_flags & TAIYIN_NATIVE_POSITION_NO_GDEFL) != 0u) {
        native_flags |= TAIYIN_NATIVE_POSITION_NO_GDEFL;
    }
    if (topocentric) {
        native_flags |= TAIYIN_NATIVE_POSITION_TOPOCENTRIC;
    }
    return native_flags;
}

uint32_t observed_flags_from_occultation_flags(uint64_t flags, bool topocentric) noexcept {
    uint32_t observed_flags = 0u;
    const uint32_t position_flags = occultation_position_flags(flags);
    if ((position_flags & TAIYIN_NATIVE_POSITION_TRUEPOS) != 0u) {
        observed_flags |= TAIYIN_OBSERVED_TRUEPOS;
    }
    if ((position_flags & TAIYIN_NATIVE_POSITION_ASTROMETRIC) != 0u) {
        observed_flags |= TAIYIN_OBSERVED_ASTROMETRIC;
    }
    if ((position_flags & TAIYIN_NATIVE_POSITION_NO_ABERR) != 0u) {
        observed_flags |= TAIYIN_OBSERVED_NO_ABERR;
    }
    if ((position_flags & TAIYIN_NATIVE_POSITION_NO_GDEFL) != 0u) {
        observed_flags |= TAIYIN_OBSERVED_NO_GDEFL;
    }
    if (topocentric) {
        observed_flags |= TAIYIN_OBSERVED_TOPOCENTRIC;
    }
    return observed_flags;
}

uint32_t observed_horizontal_flags_from_visibility_flags(uint64_t flags) noexcept {
    uint32_t observed_flags = TAIYIN_OBSERVED_TOPOCENTRIC | TAIYIN_OBSERVED_HORIZONTAL;
    if ((flags & TAIYIN_OCCULTATION_VISIBILITY_REFRACTION) != 0u) {
        observed_flags |= TAIYIN_OBSERVED_REFRACTION;
    }
    return observed_flags;
}

uint64_t where_visibility_flags(uint64_t flags) noexcept {
    return flags & TAIYIN_OCCULTATION_VISIBILITY_REFRACTION;
}

const HorizontalCoordinates& selected_horizontal_coordinates(
    const ObservedPosition& observed,
    uint64_t visibility_flags
) noexcept {
    return (visibility_flags & TAIYIN_OCCULTATION_VISIBILITY_REFRACTION) != 0u
        ? observed.refracted_horizontal
        : observed.horizontal;
}

uint32_t sample_visibility_flags(
    const HorizontalCoordinates& moon,
    const HorizontalCoordinates& target,
    const HorizontalCoordinates& sun
) noexcept {
    uint32_t flags = 0u;
    if (std::isfinite(moon.altitude_rad) && moon.altitude_rad >= 0.0) {
        flags |= TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_MOON_ABOVE_HORIZON;
    }
    if (std::isfinite(target.altitude_rad) && target.altitude_rad >= 0.0) {
        flags |= TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_TARGET_ABOVE_HORIZON;
    }
    if (std::isfinite(sun.altitude_rad) && sun.altitude_rad < 0.0) {
        flags |= TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_SUN_BELOW_HORIZON;
    }
    return flags;
}

void accumulate_visibility_flags(
    const LunarOccultationLocalVisibilitySample& sample,
    bool maximum,
    LunarOccultationLocalVisibility* out
) noexcept {
    if (!out || !sample.valid) return;
    if ((sample.visibility_flags & TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_GEOMETRICALLY_VISIBLE)
        == TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_GEOMETRICALLY_VISIBLE) {
        out->visibility_flags |= TAIYIN_OCCULTATION_VISIBILITY_HAS_VISIBLE_SAMPLE;
        if (maximum) {
            out->visibility_flags |= TAIYIN_OCCULTATION_VISIBILITY_MAXIMUM_VISIBLE;
        }
    }
    if ((sample.visibility_flags & TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_DARK_SKY_VISIBLE)
        == TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_DARK_SKY_VISIBLE) {
        out->visibility_flags |= TAIYIN_OCCULTATION_VISIBILITY_HAS_DARK_SAMPLE;
        if (maximum) {
            out->visibility_flags |= TAIYIN_OCCULTATION_VISIBILITY_MAXIMUM_DARK;
        }
    }
}

SplitJulianDate occultation_event_interval_start(
    const LunarStarOccultationSearchResult& occultation
) noexcept {
    if (split_julian_date_is_finite(occultation.first_contact_jd_ut)) return occultation.first_contact_jd_ut;
    if (split_julian_date_is_finite(occultation.begin_jd_ut)) return occultation.begin_jd_ut;
    return split_julian_date_is_finite(occultation.jd_ut)
        ? occultation.jd_ut - OCCULTATION_SEED_WINDOW_DAYS
        : invalid_jd();
}

SplitJulianDate occultation_event_interval_end(
    const LunarStarOccultationSearchResult& occultation
) noexcept {
    if (split_julian_date_is_finite(occultation.fourth_contact_jd_ut)) return occultation.fourth_contact_jd_ut;
    if (split_julian_date_is_finite(occultation.end_jd_ut)) return occultation.end_jd_ut;
    return split_julian_date_is_finite(occultation.jd_ut)
        ? occultation.jd_ut + OCCULTATION_SEED_WINDOW_DAYS
        : invalid_jd();
}

double target_altitude_scalar(const LunarOccultationLocalVisibilitySample& sample) noexcept {
    return sample.valid ? sample.target_altitude_rad : NAN;
}

double geometric_visibility_scalar(const LunarOccultationLocalVisibilitySample& sample) noexcept {
    if (!sample.valid
        || !std::isfinite(sample.moon_altitude_rad)
        || !std::isfinite(sample.target_altitude_rad)) {
        return NAN;
    }
    return std::min(sample.moon_altitude_rad, sample.target_altitude_rad);
}

double dark_visibility_scalar(const LunarOccultationLocalVisibilitySample& sample) noexcept {
    const double geometric = geometric_visibility_scalar(sample);
    if (!std::isfinite(geometric) || !std::isfinite(sample.sun_altitude_rad)) return NAN;
    return std::min(geometric, -sample.sun_altitude_rad);
}

double clamp_unit(double value) noexcept {
    if (value < -1.0) return -1.0;
    if (value > 1.0) return 1.0;
    return value;
}

void spherical_to_unit(double lon, double lat, double out[3]) noexcept {
    const double clat = std::cos(lat);
    out[0] = clat * std::cos(lon);
    out[1] = clat * std::sin(lon);
    out[2] = std::sin(lat);
}

double angular_separation_rad(
    double lon_a,
    double lat_a,
    double lon_b,
    double lat_b
) noexcept {
    double a[3];
    double b[3];
    spherical_to_unit(lon_a, lat_a, a);
    spherical_to_unit(lon_b, lat_b, b);
    const double dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    return std::acos(clamp_unit(dot));
}

sxwnl::solar::Vec3 equatorial_llr_km_from_spherical(const double position[6]) noexcept {
    return {
        position[0],
        position[1],
        position[2] * TAIYIN_AU_KM,
    };
}

Status eval_body_equatorial_llr_km_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_ut,
    uint64_t flags,
    sxwnl::solar::Vec3* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    NativeCalcContext geocentric = *context;
    native_context_set_geocentric_observer(&geocentric, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH);
    geocentric.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE;
    const uint32_t position_flags =
        native_flags_from_occultation_flags(flags, false) | TAIYIN_NATIVE_POSITION_EQUATORIAL;
    double position[6] = {};
    const Status status = calc_position_ut(
        &geocentric,
        body_id,
        jd_ut,
        position_flags,
        position,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!std::isfinite(position[0]) || !std::isfinite(position[1])
        || !std::isfinite(position[2]) || !(position[2] > 0.0)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out = equatorial_llr_km_from_spherical(position);
    return TAIYIN_STATUS_OK;
}

Status eval_star_equatorial_llr_km_ut(
    const NativeCalcContext* context,
    const char* star_key,
    SplitJulianDate jd_ut,
    uint64_t flags,
    sxwnl::solar::Vec3* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !star_key || star_key[0] == '\0' || !out || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    NativeCalcContext geocentric = *context;
    native_context_set_geocentric_observer(&geocentric, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH);
    geocentric.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE;
    const uint32_t position_flags =
        native_flags_from_occultation_flags(flags, false) | TAIYIN_NATIVE_POSITION_EQUATORIAL;
    double position[6] = {};
    const Status status = calc_star_position_ut(
        &geocentric,
        star_key,
        jd_ut,
        position_flags,
        position,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!std::isfinite(position[0]) || !std::isfinite(position[1])
        || !std::isfinite(position[2]) || !(position[2] > 0.0)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out = equatorial_llr_km_from_spherical(position);
    return TAIYIN_STATUS_OK;
}

Status gast_for_occultation_where(
    const NativeCalcContext& context,
    SplitJulianDate jd_ut,
    double* out_gast_rad,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_gast_rad || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate jd_tt = invalid_jd();
    Status status = eclipse_ut_to_tt(context, jd_ut, &jd_tt, nullptr, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!gast_model_rad(
            context.model_context.precession_model_id,
            context.model_context.nutation_model_id,
            jd_ut,
            jd_tt,
            out_gast_rad)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    return TAIYIN_STATUS_OK;
}

Status eval_ecliptic_star_seed_sample(
    const NativeCalcContext* context,
    const char* star_key,
    SplitJulianDate jd_ut,
    uint64_t flags,
    bool topocentric,
    EclipticOccultationSeedSample* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !star_key || star_key[0] == '\0' || !out || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    NativeCalcContext ecliptic_context = *context;
    ecliptic_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    const uint32_t position_flags = native_flags_from_occultation_flags(flags, topocentric);

    double moon[6] = {};
    Status status = calc_position_ut(
        &ecliptic_context,
        TAIYIN_BODY_MOON,
        jd_ut,
        position_flags,
        moon,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    double star[6] = {};
    status = calc_star_position_ut(
        &ecliptic_context,
        star_key,
        jd_ut,
        position_flags,
        star,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    if (!std::isfinite(moon[0]) || !std::isfinite(moon[1]) || !std::isfinite(moon[3])
        || !std::isfinite(star[0]) || !std::isfinite(star[1])) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    out->jd_ut = jd_ut;
    out->moon_lon_rad = normalize_radians(moon[0]);
    out->moon_lat_rad = moon[1];
    out->moon_lon_rate_rad_per_day = moon[3];
    out->target_lon_rad = normalize_radians(star[0]);
    out->target_lat_rad = star[1];
    out->target_lon_rate_rad_per_day = std::isfinite(star[3]) ? star[3] : 0.0;
    return TAIYIN_STATUS_OK;
}

Status eval_ecliptic_body_seed_sample(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_ut,
    uint64_t flags,
    bool topocentric,
    EclipticOccultationSeedSample* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    NativeCalcContext ecliptic_context = *context;
    ecliptic_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    const uint32_t position_flags = native_flags_from_occultation_flags(flags, topocentric);

    double moon[6] = {};
    Status status = calc_position_ut(
        &ecliptic_context,
        TAIYIN_BODY_MOON,
        jd_ut,
        position_flags,
        moon,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    double target[6] = {};
    status = calc_position_ut(
        &ecliptic_context,
        body_id,
        jd_ut,
        position_flags,
        target,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    if (!std::isfinite(moon[0]) || !std::isfinite(moon[1]) || !std::isfinite(moon[3])
        || !std::isfinite(target[0]) || !std::isfinite(target[1]) || !std::isfinite(target[3])) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    out->jd_ut = jd_ut;
    out->moon_lon_rad = normalize_radians(moon[0]);
    out->moon_lat_rad = moon[1];
    out->moon_lon_rate_rad_per_day = moon[3];
    out->target_lon_rad = normalize_radians(target[0]);
    out->target_lat_rad = target[1];
    out->target_lon_rate_rad_per_day = target[3];
    return TAIYIN_STATUS_OK;
}

SplitJulianDate estimate_next_lunar_longitude_seed(
    const EclipticOccultationSeedSample& sample,
    bool backward
) noexcept {
    double rate = sample.moon_lon_rate_rad_per_day - sample.target_lon_rate_rad_per_day;
    if (!std::isfinite(rate) || std::fabs(rate) < 0.05) {
        rate = MOON_MEAN_SIDEREAL_RATE_RAD_PER_DAY;
    }
    double delta = normalize_signed_radians(sample.target_lon_rad - sample.moon_lon_rad);
    const double direction = backward ? -1.0 : 1.0;
    const double wrap_step = (direction * rate > 0.0) ? TAIYIN_TWO_PI : -TAIYIN_TWO_PI;
    double dt = delta / rate;
    for (int i = 0; i < 4 && direction * dt <= 1.0e-6; ++i) {
        delta += wrap_step;
        dt = delta / rate;
    }
    if (!std::isfinite(dt) || direction * dt <= 1.0e-6) {
        return invalid_jd();
    }
    return sample.jd_ut + dt;
}

bool is_valid_lunar_body_occultation_target(int body_id) noexcept {
    return body_id != TAIYIN_BODY_MOON
        && body_id != TAIYIN_BODY_EARTH
        && body_id != TAIYIN_BODY_EMB
        && body_id != TAIYIN_BODY_SUN
        && body_id != TAIYIN_BODY_SSB;
}

int physical_lunar_occultation_body(int body_id) noexcept {
    switch (body_id) {
    case TAIYIN_BODY_MERCURY_BARYCENTER:
        return TAIYIN_BODY_MERCURY;
    case TAIYIN_BODY_VENUS_BARYCENTER:
        return TAIYIN_BODY_VENUS;
    case TAIYIN_BODY_MARS_BARYCENTER:
        return TAIYIN_BODY_MARS;
    case TAIYIN_BODY_JUPITER_BARYCENTER:
        return TAIYIN_BODY_JUPITER;
    case TAIYIN_BODY_SATURN_BARYCENTER:
        return TAIYIN_BODY_SATURN;
    case TAIYIN_BODY_URANUS_BARYCENTER:
        return TAIYIN_BODY_URANUS;
    case TAIYIN_BODY_NEPTUNE_BARYCENTER:
        return TAIYIN_BODY_NEPTUNE;
    case TAIYIN_BODY_PLUTO_BARYCENTER:
        return TAIYIN_BODY_PLUTO;
    default:
        return body_id;
    }
}

bool valid_lunar_body_occultation_target(
    int body_id,
    double radius_km
) noexcept {
    return is_valid_lunar_body_occultation_target(body_id)
        && std::isfinite(radius_km)
        && radius_km >= 0.0;
}

double standard_lunar_body_occultation_radius_km(
    int body_id
) noexcept {
    double radius_km = ::taiyin::internal::body_disc_radius_km(
        physical_lunar_occultation_body(body_id),
        ::taiyin::internal::BodyDiscRadiusConvention::MeanPhysical);
    if (!std::isfinite(radius_km)) {
        radius_km = 0.0;
    }
    return radius_km;
}

Status eval_lunar_star_occultation_sample(
    const NativeCalcContext* context,
    const char* star_key,
    SplitJulianDate jd_ut,
    uint64_t flags,
    bool topocentric,
    OccultationSample* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !star_key || star_key[0] == '\0' || !out || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const uint32_t observed_flags = observed_flags_from_occultation_flags(flags, topocentric);
    const int moon_id = TAIYIN_BODY_MOON;
    ObservedPosition moon;
    Status status = calc_observed_ut(
        context,
        jd_ut,
        &moon_id,
        1,
        observed_flags,
        &moon,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    ObservedPosition star;
    status = calc_observed_star_ut(
        context,
        star_key,
        jd_ut,
        observed_flags,
        &star,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    if (!std::isfinite(moon.apparent.longitude_rad)
        || !std::isfinite(moon.apparent.latitude_rad)
        || !std::isfinite(moon.apparent.distance_au)
        || !(moon.apparent.distance_au > 0.0)
        || !std::isfinite(star.apparent.longitude_rad)
        || !std::isfinite(star.apparent.latitude_rad)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    const double moon_distance_km = moon.apparent.distance_au * TAIYIN_AU_KM;
    if (!std::isfinite(moon_distance_km) || !(moon_distance_km > MOON_RADIUS_KM)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    out->jd_ut = jd_ut;
    out->separation_rad = angular_separation_rad(
        moon.apparent.longitude_rad,
        moon.apparent.latitude_rad,
        star.apparent.longitude_rad,
        star.apparent.latitude_rad);
    out->moon_radius_rad = std::asin(MOON_RADIUS_KM / moon_distance_km);
    out->target_radius_rad = 0.0;
    out->margin_rad = out->moon_radius_rad + out->target_radius_rad - out->separation_rad;
    out->observer_to_moon_au = moon.apparent.apparent_state.position_au;
    out->observer_to_target_au = star.apparent.apparent_state.position_au;
    if (!std::isfinite(out->separation_rad)
        || !std::isfinite(out->moon_radius_rad)
        || !std::isfinite(out->target_radius_rad)
        || !std::isfinite(out->margin_rad)
        || !finite_vector3(out->observer_to_moon_au)
        || !finite_vector3(out->observer_to_target_au)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    return TAIYIN_STATUS_OK;
}

Status eval_lunar_body_occultation_sample(
    const NativeCalcContext* context,
    int body_id,
    double target_radius_km,
    SplitJulianDate jd_ut,
    uint64_t flags,
    bool topocentric,
    OccultationSample* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_ut)
        || !valid_lunar_body_occultation_target(body_id, target_radius_km)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const uint32_t observed_flags = observed_flags_from_occultation_flags(flags, topocentric);
    const int body_ids[2] = { TAIYIN_BODY_MOON, body_id };
    ObservedPosition observed[2];
    EphemerisEvalDiagnostic diagnostics[2];
    const Status status = calc_observed_ut(
        context,
        jd_ut,
        body_ids,
        2,
        observed_flags,
        observed,
        diagnostics);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) *diagnostic = diagnostics[0].status != TAIYIN_STATUS_OK ? diagnostics[0] : diagnostics[1];
        return status;
    }
    if (diagnostic) *diagnostic = diagnostics[1];

    const ObservedPosition& moon = observed[0];
    const ObservedPosition& target = observed[1];
    if (!std::isfinite(moon.apparent.longitude_rad)
        || !std::isfinite(moon.apparent.latitude_rad)
        || !std::isfinite(moon.apparent.distance_au)
        || !(moon.apparent.distance_au > 0.0)
        || !std::isfinite(target.apparent.longitude_rad)
        || !std::isfinite(target.apparent.latitude_rad)
        || !std::isfinite(target.apparent.distance_au)
        || !(target.apparent.distance_au > 0.0)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    const double moon_distance_km = moon.apparent.distance_au * TAIYIN_AU_KM;
    if (!std::isfinite(moon_distance_km) || !(moon_distance_km > MOON_RADIUS_KM)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const double target_distance_km = target.apparent.distance_au * TAIYIN_AU_KM;
    if (!std::isfinite(target_radius_km) || !(target_radius_km >= 0.0)
        || !std::isfinite(target_distance_km) || !(target_distance_km > target_radius_km)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    out->jd_ut = jd_ut;
    out->separation_rad = angular_separation_rad(
        moon.apparent.longitude_rad,
        moon.apparent.latitude_rad,
        target.apparent.longitude_rad,
        target.apparent.latitude_rad);
    out->moon_radius_rad = std::asin(MOON_RADIUS_KM / moon_distance_km);
    out->target_radius_rad = target_radius_km > 0.0 ? std::asin(target_radius_km / target_distance_km) : 0.0;
    out->margin_rad = out->moon_radius_rad + out->target_radius_rad - out->separation_rad;
    out->observer_to_moon_au = moon.apparent.apparent_state.position_au;
    out->observer_to_target_au = target.apparent.apparent_state.position_au;
    if (!std::isfinite(out->separation_rad)
        || !std::isfinite(out->moon_radius_rad)
        || !std::isfinite(out->target_radius_rad)
        || !std::isfinite(out->margin_rad)
        || !finite_vector3(out->observer_to_moon_au)
        || !finite_vector3(out->observer_to_target_au)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    return TAIYIN_STATUS_OK;
}

Status apply_lunar_limb_to_occultation_sample(
    const NativeCalcContext* context,
    uint64_t flags,
    OccultationSample* sample,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !sample || !split_julian_date_is_finite(sample->jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if ((flags & TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION) == 0u) {
        return TAIYIN_STATUS_OK;
    }
    // At exact center alignment the target-facing position angle is undefined.
    // Keep the smooth maximum radius; contact samples have a defined direction.
    if (std::isfinite(sample->separation_rad) && sample->separation_rad <= 1.0e-10) {
        return TAIYIN_STATUS_OK;
    }

    const double moon_distance_au = vector3_norm(sample->observer_to_moon_au);
    const double target_distance_au = vector3_norm(sample->observer_to_target_au);
    if (!std::isfinite(moon_distance_au) || !(moon_distance_au > 0.0)
        || !std::isfinite(target_distance_au) || !(target_distance_au > 0.0)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    Vector3 limb_direction;
    Status status = apparent_limb_direction_toward_target(
        sample->observer_to_moon_au,
        sample->observer_to_target_au,
        false,
        &limb_direction);
    if (status != TAIYIN_STATUS_OK) return status;

    SplitJulianDate jd_tt = invalid_jd();
    status = eclipse_ut_to_tt(*context, sample->jd_ut, &jd_tt, nullptr, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    double lunar_limb_radius_m = NAN;
    status = eval_lunar_limb_radius_from_apparent_frame_m(
        context,
        jd_tt,
        (occultation_position_flags(flags) & TAIYIN_NATIVE_POSITION_TRUEPOS) != 0u,
        TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE,
        vector3_negate(sample->observer_to_moon_au),
        limb_direction,
        &lunar_limb_radius_m);
    if (status != TAIYIN_STATUS_OK) return status;

    const double moon_distance_m = moon_distance_au * TAIYIN_AU_KM * 1000.0;
    if (!std::isfinite(lunar_limb_radius_m) || !(lunar_limb_radius_m > 0.0)
        || !std::isfinite(moon_distance_m) || !(moon_distance_m > lunar_limb_radius_m)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    sample->moon_radius_rad = std::asin(lunar_limb_radius_m / moon_distance_m);
    sample->margin_rad = sample->moon_radius_rad
        + sample->target_radius_rad
        - sample->separation_rad;
    if (!std::isfinite(sample->moon_radius_rad) || !std::isfinite(sample->margin_rad)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    return TAIYIN_STATUS_OK;
}

struct StarOccultationTarget {
    const char* star_key;

    bool valid() const noexcept {
        return star_key && star_key[0] != '\0';
    }

    int kind() const noexcept {
        return TAIYIN_OCCULTATION_KIND_LUNAR_STAR;
    }

    Status can_be_lunar_occulted(
        const NativeCalcContext* context,
        SplitJulianDate jd_ut,
        uint64_t flags,
        bool topocentric,
        bool* out_possible,
        EphemerisEvalDiagnostic* diagnostic
    ) const noexcept {
        if (!context || !valid() || !out_possible || !split_julian_date_is_finite(jd_ut)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        *out_possible = false;

        EclipticOccultationSeedSample sample;
        const Status status = eval_ecliptic_star_seed_sample(
            context,
            star_key,
            jd_ut,
            flags,
            false,
            &sample,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        if (!std::isfinite(sample.target_lat_rad)) {
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }
        const double margin = topocentric ? LOCAL_ECLIPTIC_MARGIN_RAD : GEOCENTRIC_ECLIPTIC_MARGIN_RAD;
        *out_possible = std::fabs(sample.target_lat_rad)
            <= MOON_MAX_ECLIPTIC_LATITUDE_RAD + margin + STAR_PROPER_MOTION_LATITUDE_SAFETY_RAD;
        return TAIYIN_STATUS_OK;
    }

    Status eval_ecliptic(
        const NativeCalcContext* context,
        SplitJulianDate jd_ut,
        uint64_t flags,
        bool topocentric,
        EclipticOccultationSeedSample* out,
        EphemerisEvalDiagnostic* diagnostic
    ) const noexcept {
        return eval_ecliptic_star_seed_sample(context, star_key, jd_ut, flags, topocentric, out, diagnostic);
    }

    Status eval_occultation(
        const NativeCalcContext* context,
        SplitJulianDate jd_ut,
        uint64_t flags,
        bool topocentric,
        OccultationSample* out,
        EphemerisEvalDiagnostic* diagnostic
    ) const noexcept {
        return eval_lunar_star_occultation_sample(context, star_key, jd_ut, flags, topocentric, out, diagnostic);
    }

    Status eval_equatorial_llr_km(
        const NativeCalcContext* context,
        SplitJulianDate jd_ut,
        uint64_t flags,
        sxwnl::solar::Vec3* out,
        EphemerisEvalDiagnostic* diagnostic
    ) const noexcept {
        return eval_star_equatorial_llr_km_ut(context, star_key, jd_ut, flags, out, diagnostic);
    }
};

struct BodyOccultationTarget {
    int body_id;
    double radius_km;

    bool valid() const noexcept {
        return valid_lunar_body_occultation_target(body_id, radius_km);
    }

    int kind() const noexcept {
        return TAIYIN_OCCULTATION_KIND_LUNAR_BODY;
    }

    Status can_be_lunar_occulted(
        const NativeCalcContext*,
        SplitJulianDate,
        uint64_t,
        bool,
        bool* out_possible,
        EphemerisEvalDiagnostic*
    ) const noexcept {
        if (!valid() || !out_possible) return TAIYIN_ERROR_INVALID_ARGUMENT;
        *out_possible = true;
        return TAIYIN_STATUS_OK;
    }

    Status eval_ecliptic(
        const NativeCalcContext* context,
        SplitJulianDate jd_ut,
        uint64_t flags,
        bool topocentric,
        EclipticOccultationSeedSample* out,
        EphemerisEvalDiagnostic* diagnostic
    ) const noexcept {
        return eval_ecliptic_body_seed_sample(
            context, body_id, jd_ut, flags, topocentric, out, diagnostic);
    }

    Status eval_occultation(
        const NativeCalcContext* context,
        SplitJulianDate jd_ut,
        uint64_t flags,
        bool topocentric,
        OccultationSample* out,
        EphemerisEvalDiagnostic* diagnostic
    ) const noexcept {
        return eval_lunar_body_occultation_sample(
            context, body_id, radius_km, jd_ut, flags, topocentric, out, diagnostic);
    }

    Status eval_equatorial_llr_km(
        const NativeCalcContext* context,
        SplitJulianDate jd_ut,
        uint64_t flags,
        sxwnl::solar::Vec3* out,
        EphemerisEvalDiagnostic* diagnostic
    ) const noexcept {
        return eval_body_equatorial_llr_km_ut(
            context, body_id, jd_ut, flags, out, diagnostic);
    }
};

template <typename Target>
Status eval_occultation_derivative_sample(
    const NativeCalcContext* context,
    const Target& target,
    SplitJulianDate jd_ut,
    uint64_t flags,
    bool topocentric,
    OccultationSample* out,
    int* evaluation_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !target.valid() || !out || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double h = 1.0e-3;
    OccultationSample minus;
    OccultationSample center;
    OccultationSample plus;
    Status status = target.eval_occultation(
        context,
        jd_ut - h,
        flags,
        topocentric,
        &minus,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = target.eval_occultation(
        context,
        jd_ut,
        flags,
        topocentric,
        &center,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = target.eval_occultation(
        context,
        jd_ut + h,
        flags,
        topocentric,
        &plus,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (evaluation_count) *evaluation_count += 3;

    center.separation_rate_rad_per_day =
        (plus.separation_rad - minus.separation_rad) / (2.0 * h);
    center.separation_acceleration_rad_per_day2 =
        (plus.separation_rad - 2.0 * center.separation_rad + minus.separation_rad) / (h * h);
    center.moon_radius_rate_rad_per_day =
        (plus.moon_radius_rad - minus.moon_radius_rad) / (2.0 * h);
    center.target_radius_rate_rad_per_day =
        (plus.target_radius_rad - minus.target_radius_rad) / (2.0 * h);
    *out = center;
    return TAIYIN_STATUS_OK;
}

template <typename Target>
Status refine_lunar_longitude_seed(
    const NativeCalcContext* context,
    const Target& target,
    SplitJulianDate seed_jd_ut,
    uint64_t flags,
    bool topocentric,
    SplitJulianDate* out_jd_ut,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !target.valid() || !out_jd_ut || !split_julian_date_is_finite(seed_jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    SplitJulianDate t = seed_jd_ut;
    for (int i = 0; i < 4; ++i) {
        EclipticOccultationSeedSample sample;
        const Status status = target.eval_ecliptic(context, t, flags, topocentric, &sample, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        const double residual = normalize_signed_radians(sample.moon_lon_rad - sample.target_lon_rad);
        double rate = sample.moon_lon_rate_rad_per_day - sample.target_lon_rate_rad_per_day;
        if (!std::isfinite(rate) || std::fabs(rate) < 0.05) {
            rate = MOON_MEAN_SIDEREAL_RATE_RAD_PER_DAY;
        }
        double correction = -residual / rate;
        if (correction > OCCULTATION_SEED_WINDOW_DAYS) correction = OCCULTATION_SEED_WINDOW_DAYS;
        if (correction < -OCCULTATION_SEED_WINDOW_DAYS) correction = -OCCULTATION_SEED_WINDOW_DAYS;
        t += correction;
        if (!split_julian_date_is_finite(t)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        if (std::fabs(correction) < 1.0e-5) break;
    }

    *out_jd_ut = t;
    return TAIYIN_STATUS_OK;
}

template <typename Target>
Status minimize_lunar_separation(
    const NativeCalcContext* context,
    const Target& target,
    SplitJulianDate a,
    SplitJulianDate b,
    uint64_t flags,
    bool topocentric,
    OccultationSample* out,
    int* iteration_count,
    int* evaluation_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !target.valid() || !out
        || !split_julian_date_is_finite(a) || !split_julian_date_is_finite(b) || !(a < b)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    Status status = TAIYIN_STATUS_OK;
    if (target.kind() == TAIYIN_OCCULTATION_KIND_LUNAR_BODY) {
        SplitJulianDate t = a + 0.5 * (b - a);
        OccultationSample sample;
        status = eval_occultation_derivative_sample(
            context,
            target,
            t,
            flags,
            topocentric,
            &sample,
            evaluation_count,
            diagnostic);
        if (status == TAIYIN_STATUS_OK) {
            int iterations = 0;
            for (; iterations < 8; ++iterations) {
                if (!std::isfinite(sample.separation_rate_rad_per_day)
                    || !std::isfinite(sample.separation_acceleration_rad_per_day2)
                    || std::fabs(sample.separation_acceleration_rad_per_day2) <= 1.0e-14) {
                    break;
                }
                const double correction =
                    -sample.separation_rate_rad_per_day / sample.separation_acceleration_rad_per_day2;
                const SplitJulianDate next_t = t + correction;
                if (!split_julian_date_is_finite(next_t)
                    || next_t <= a
                    || next_t >= b
                    || std::fabs(correction) > 0.75 * (b - a)) {
                    break;
                }

                OccultationSample next_sample;
                status = eval_occultation_derivative_sample(
                    context,
                    target,
                    next_t,
                    flags,
                    topocentric,
                    &next_sample,
                    evaluation_count,
                    diagnostic);
                if (status != TAIYIN_STATUS_OK) return status;
                if (!(next_sample.separation_rad <= sample.separation_rad)) {
                    break;
                }

                t = next_t;
                sample = next_sample;
                if (std::fabs(correction) < 1.0e-9
                    || std::fabs(sample.separation_rate_rad_per_day) < 1.0e-12) {
                    if (iteration_count) *iteration_count += iterations + 1;
                    *out = sample;
                    return TAIYIN_STATUS_OK;
                }
            }
            if (iteration_count) *iteration_count += iterations;
        }
    }

    const double inv_phi = 0.61803398874989484820;
    SplitJulianDate x1 = b - inv_phi * (b - a);
    SplitJulianDate x2 = a + inv_phi * (b - a);
    OccultationSample f1;
    OccultationSample f2;
    status = target.eval_occultation(context, x1, flags, topocentric, &f1, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = target.eval_occultation(context, x2, flags, topocentric, &f2, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (evaluation_count) *evaluation_count += 2;

    int iterations = 0;
    for (; iterations < 48 && (b - a) > 1.0e-8; ++iterations) {
        if (f1.separation_rad <= f2.separation_rad) {
            b = x2;
            x2 = x1;
            f2 = f1;
            x1 = b - inv_phi * (b - a);
            status = target.eval_occultation(context, x1, flags, topocentric, &f1, diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
        } else {
            a = x1;
            x1 = x2;
            f1 = f2;
            x2 = a + inv_phi * (b - a);
            status = target.eval_occultation(context, x2, flags, topocentric, &f2, diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
        }
        if (evaluation_count) ++(*evaluation_count);
    }
    if (iteration_count) *iteration_count += iterations;

    *out = (f1.separation_rad <= f2.separation_rad) ? f1 : f2;
    return TAIYIN_STATUS_OK;
}

template <typename Target>
Status eval_lunar_occultation_contact_sample(
    const NativeCalcContext* context,
    const Target& target,
    SplitJulianDate jd_ut,
    uint64_t flags,
    bool topocentric,
    OccultationSample* out,
    int* evaluation_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    Status status = target.eval_occultation(
        context, jd_ut, flags, topocentric, out, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (evaluation_count) ++(*evaluation_count);
    return apply_lunar_limb_to_occultation_sample(context, flags, out, diagnostic);
}

template <typename Target>
Status maximize_lunar_occultation_margin(
    const NativeCalcContext* context,
    const Target& target,
    const OccultationSample& smooth_minimum,
    SplitJulianDate bracket_start,
    SplitJulianDate bracket_end,
    uint64_t flags,
    bool topocentric,
    OccultationSample* out,
    int* evaluation_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !target.valid() || !out
        || !split_julian_date_is_finite(smooth_minimum.jd_ut)
        || !split_julian_date_is_finite(bracket_start)
        || !split_julian_date_is_finite(bracket_end)
        || !(bracket_start < bracket_end)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = smooth_minimum;
    if ((flags & TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION) == 0u) {
        return TAIYIN_STATUS_OK;
    }

    Status status = apply_lunar_limb_to_occultation_sample(
        context, flags, out, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    // Most longitude conjunctions are far outside the lunar disk. Use the
    // largest radius representable by the loaded TLL1 model as a conservative
    // gate before paying for a local corrected-margin search.
    const Tll1LunarLimbModel* model = global_lunar_limb_model();
    if (model && model->header) {
        const double maximum_radius_m = model->header->reference_radius_m
            + static_cast<double>(std::numeric_limits<int16_t>::max())
                * model->header->offset_scale_m;
        const double moon_distance_m = vector3_norm(smooth_minimum.observer_to_moon_au)
            * TAIYIN_AU_KM * 1000.0;
        if (std::isfinite(maximum_radius_m) && maximum_radius_m > 0.0
            && std::isfinite(moon_distance_m) && moon_distance_m > maximum_radius_m) {
            // The 0.1% distance allowance is far larger than lunar range
            // variation across the 20-minute extremum window.
            const double maximum_radius_rad = std::asin(std::min(
                1.0, maximum_radius_m * 1.001 / moon_distance_m));
            const double maximum_margin_rad = maximum_radius_rad
                + smooth_minimum.target_radius_rad - smooth_minimum.separation_rad;
            if (std::isfinite(maximum_margin_rad) && maximum_margin_rad < 0.0) {
                return TAIYIN_STATUS_OK;
            }
        }
    }

    const SplitJulianDate scan_start = std::max(
        bracket_start, smooth_minimum.jd_ut - OCCULTATION_LIMB_EXTREMUM_HALF_WINDOW_DAYS);
    const SplitJulianDate scan_end = std::min(
        bracket_end, smooth_minimum.jd_ut + OCCULTATION_LIMB_EXTREMUM_HALF_WINDOW_DAYS);
    if (!(scan_end > scan_start)) return TAIYIN_STATUS_OK;

    OccultationSample samples[OCCULTATION_LIMB_EXTREMUM_SCAN_INTERVALS + 1];
    size_t best_index = 0;
    OccultationSample best = *out;
    for (int i = 0; i <= OCCULTATION_LIMB_EXTREMUM_SCAN_INTERVALS; ++i) {
        const double fraction = static_cast<double>(i)
            / static_cast<double>(OCCULTATION_LIMB_EXTREMUM_SCAN_INTERVALS);
        const SplitJulianDate jd_ut = scan_start + (scan_end - scan_start) * fraction;
        if (std::fabs(jd_ut - smooth_minimum.jd_ut) <= 1.0e-12) {
            samples[i] = *out;
        } else {
            status = eval_lunar_occultation_contact_sample(
                context,
                target,
                jd_ut,
                flags,
                topocentric,
                &samples[i],
                evaluation_count,
                diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
        }
        if (i == 0 || samples[i].margin_rad > best.margin_rad) {
            best = samples[i];
            best_index = static_cast<size_t>(i);
        }
    }

    if (best_index == 0
        || best_index == static_cast<size_t>(OCCULTATION_LIMB_EXTREMUM_SCAN_INTERVALS)) {
        *out = best;
        return TAIYIN_STATUS_OK;
    }

    const double inv_phi = 0.61803398874989484820;
    SplitJulianDate a = samples[best_index - 1].jd_ut;
    SplitJulianDate b = samples[best_index + 1].jd_ut;
    SplitJulianDate x1 = b - inv_phi * (b - a);
    SplitJulianDate x2 = a + inv_phi * (b - a);
    OccultationSample f1;
    OccultationSample f2;
    status = eval_lunar_occultation_contact_sample(
        context, target, x1, flags, topocentric, &f1, evaluation_count, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = eval_lunar_occultation_contact_sample(
        context, target, x2, flags, topocentric, &f2, evaluation_count, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    for (int iteration = 0;
         iteration < OCCULTATION_LIMB_EXTREMUM_REFINE_ITERATIONS
             && (b - a) > OCCULTATION_CONTACT_TOLERANCE_DAYS;
         ++iteration) {
        if (f1.margin_rad >= f2.margin_rad) {
            b = x2;
            x2 = x1;
            f2 = f1;
            x1 = b - inv_phi * (b - a);
            status = eval_lunar_occultation_contact_sample(
                context, target, x1, flags, topocentric, &f1, evaluation_count, diagnostic);
        } else {
            a = x1;
            x1 = x2;
            f1 = f2;
            x2 = a + inv_phi * (b - a);
            status = eval_lunar_occultation_contact_sample(
                context, target, x2, flags, topocentric, &f2, evaluation_count, diagnostic);
        }
        if (status != TAIYIN_STATUS_OK) return status;
    }
    if (f1.margin_rad > best.margin_rad) best = f1;
    if (f2.margin_rad > best.margin_rad) best = f2;
    *out = best;
    return TAIYIN_STATUS_OK;
}

template <typename Target>
Status solve_bracketed_lunar_occultation_contact(
    const NativeCalcContext* context,
    const Target& target,
    SplitJulianDate inside_jd,
    SplitJulianDate outside_jd,
    OccultationContactBoundary boundary,
    uint64_t flags,
    bool topocentric,
    SplitJulianDate* out_jd_ut,
    int* iteration_count,
    int* evaluation_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !target.valid() || !out_jd_ut
        || !split_julian_date_is_finite(inside_jd) || !split_julian_date_is_finite(outside_jd)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    OccultationSample inside;
    Status status = eval_lunar_occultation_contact_sample(
        context, target, inside_jd, flags, topocentric, &inside, evaluation_count, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    OccultationSample outside;
    status = eval_lunar_occultation_contact_sample(
        context, target, outside_jd, flags, topocentric, &outside, evaluation_count, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    const double inside_margin = occultation_contact_margin(inside, boundary);
    const double outside_margin = occultation_contact_margin(outside, boundary);
    if (!(inside_margin >= 0.0) || !(outside_margin <= 0.0)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    SplitJulianDate inside_t = inside_jd;
    SplitJulianDate outside_t = outside_jd;
    int iterations = 0;
    for (; iterations < 64
         && std::fabs(outside_t - inside_t) > OCCULTATION_CONTACT_TOLERANCE_DAYS;
         ++iterations) {
        const SplitJulianDate lo = std::min(inside_t, outside_t);
        const SplitJulianDate hi = std::max(inside_t, outside_t);
        SplitJulianDate candidate_t = invalid_jd();
        const double margin = occultation_contact_margin(inside, boundary);
        const double outside_margin = occultation_contact_margin(outside, boundary);
        const double denominator = margin - outside_margin;
        if (std::isfinite(margin)
            && std::isfinite(outside_margin)
            && std::fabs(denominator) > 1.0e-16) {
            const SplitJulianDate secant_t = inside_t
                - margin * (inside_t - outside_t) / denominator;
            if (split_julian_date_is_finite(secant_t) && secant_t > lo && secant_t < hi) {
                candidate_t = secant_t;
            }
        }
        if (!split_julian_date_is_finite(candidate_t)) {
            candidate_t = lo + 0.5 * (hi - lo);
        }

        OccultationSample sample;
        status = eval_lunar_occultation_contact_sample(
            context, target, candidate_t, flags, topocentric, &sample, evaluation_count, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        const double sample_margin = occultation_contact_margin(sample, boundary);
        if (sample_margin >= 0.0) {
            inside_t = candidate_t;
            inside = sample;
        } else {
            outside_t = candidate_t;
            outside = sample;
        }

        if ((flags & TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION) != 0u
            && iterations < OCCULTATION_CONTACT_NEWTON_ITERATIONS) {
            const SplitJulianDate new_lo = std::min(inside_t, outside_t);
            const SplitJulianDate new_hi = std::max(inside_t, outside_t);
            const double derivative_step = std::min(
                OCCULTATION_CONTACT_NEWTON_STEP_DAYS,
                0.2 * std::min(candidate_t - lo, hi - candidate_t));
            if (std::isfinite(derivative_step)
                && derivative_step > 0.0
                && candidate_t - derivative_step > lo
                && candidate_t + derivative_step < hi) {
                OccultationSample minus;
                OccultationSample plus;
                status = eval_lunar_occultation_contact_sample(
                    context,
                    target,
                    candidate_t - derivative_step,
                    flags,
                    topocentric,
                    &minus,
                    evaluation_count,
                    diagnostic);
                if (status != TAIYIN_STATUS_OK) return status;
                status = eval_lunar_occultation_contact_sample(
                    context,
                    target,
                    candidate_t + derivative_step,
                    flags,
                    topocentric,
                    &plus,
                    evaluation_count,
                    diagnostic);
                if (status != TAIYIN_STATUS_OK) return status;
                const double slope = (
                    occultation_contact_margin(plus, boundary)
                    - occultation_contact_margin(minus, boundary))
                    / (2.0 * derivative_step);
                if (std::isfinite(slope) && std::fabs(slope) > 1.0e-12) {
                    const SplitJulianDate newton_t = candidate_t - sample_margin / slope;
                    const double guard = 0.05 * (new_hi - new_lo);
                    if (split_julian_date_is_finite(newton_t)
                        && newton_t > new_lo + guard
                        && newton_t < new_hi - guard) {
                        OccultationSample newton_sample;
                        status = eval_lunar_occultation_contact_sample(
                            context,
                            target,
                            newton_t,
                            flags,
                            topocentric,
                            &newton_sample,
                            evaluation_count,
                            diagnostic);
                        if (status != TAIYIN_STATUS_OK) return status;
                        const double newton_margin = occultation_contact_margin(
                            newton_sample, boundary);
                        if (std::isfinite(newton_margin)
                            && std::fabs(newton_margin) < std::fabs(sample_margin)) {
                            if (newton_margin >= 0.0) {
                                inside_t = newton_t;
                                inside = newton_sample;
                            } else {
                                outside_t = newton_t;
                                outside = newton_sample;
                            }
                        }
                    }
                }
            }
        }
    }
    if (iteration_count) *iteration_count += iterations;
    *out_jd_ut = inside_t + 0.5 * (outside_t - inside_t);
    return TAIYIN_STATUS_OK;
}

template <typename Target>
Status find_lunar_occultation_contact(
    const NativeCalcContext* context,
    const Target& target,
    const OccultationSample& maximum,
    double direction,
    OccultationContactBoundary boundary,
    uint64_t flags,
    bool topocentric,
    SplitJulianDate* out_jd_ut,
    int* iteration_count,
    int* evaluation_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !target.valid() || !out_jd_ut
        || !split_julian_date_is_finite(maximum.jd_ut)
        || !std::isfinite(occultation_contact_margin(maximum, boundary))
        || !(occultation_contact_margin(maximum, boundary) >= 0.0)
        || !(direction == -1.0 || direction == 1.0)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    SplitJulianDate inside_jd = maximum.jd_ut;
    double scan_start_offset = OCCULTATION_CONTACT_SCAN_STEP_DAYS;
    const double maximum_margin = occultation_contact_margin(maximum, boundary);
    if (std::isfinite(maximum_margin)
        && std::isfinite(maximum.separation_acceleration_rad_per_day2)
        && maximum_margin >= 0.0
        && maximum.separation_acceleration_rad_per_day2 > 1.0e-14) {
        double trial_offset = std::sqrt(
            2.0 * maximum_margin / maximum.separation_acceleration_rad_per_day2);
        if (std::isfinite(trial_offset) && trial_offset > 0.0) {
            trial_offset = std::max(
                OCCULTATION_CONTACT_SCAN_STEP_DAYS,
                std::min(OCCULTATION_SEED_WINDOW_DAYS, 1.15 * trial_offset));
            for (int attempt = 0;
                 attempt < 6 && trial_offset <= OCCULTATION_SEED_WINDOW_DAYS + 1.0e-12;
                 ++attempt) {
                const SplitJulianDate candidate_jd = maximum.jd_ut + direction * trial_offset;
                OccultationSample sample;
                const Status status = eval_lunar_occultation_contact_sample(
                    context,
                    target,
                    candidate_jd,
                    flags,
                    topocentric,
                    &sample,
                    evaluation_count,
                    diagnostic);
                if (status != TAIYIN_STATUS_OK) return status;
                if (occultation_contact_margin(sample, boundary) <= 0.0) {
                    return solve_bracketed_lunar_occultation_contact(
                        context,
                        target,
                        inside_jd,
                        candidate_jd,
                        boundary,
                        flags,
                        topocentric,
                        out_jd_ut,
                        iteration_count,
                        evaluation_count,
                        diagnostic);
                }
                inside_jd = candidate_jd;
                scan_start_offset = trial_offset + OCCULTATION_CONTACT_SCAN_STEP_DAYS;
                trial_offset *= 1.45;
            }
        }
    }

    for (double offset = scan_start_offset;
         offset <= OCCULTATION_SEED_WINDOW_DAYS + 1.0e-12;
         offset += OCCULTATION_CONTACT_SCAN_STEP_DAYS) {
        const SplitJulianDate candidate_jd = maximum.jd_ut + direction * offset;
        OccultationSample sample;
        const Status status = eval_lunar_occultation_contact_sample(
            context,
            target,
            candidate_jd,
            flags,
            topocentric,
            &sample,
            evaluation_count,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        if (occultation_contact_margin(sample, boundary) <= 0.0) {
            return solve_bracketed_lunar_occultation_contact(
                context,
                target,
                inside_jd,
                candidate_jd,
                boundary,
                flags,
                topocentric,
                out_jd_ut,
                iteration_count,
                evaluation_count,
                diagnostic);
        }
        inside_jd = candidate_jd;
    }

    return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

void set_not_found_diagnostic(
    EphemerisEvalDiagnostic* diagnostic,
    SplitJulianDate jd_ut
) noexcept {
    if (!diagnostic) return;
    *diagnostic = EphemerisEvalDiagnostic();
    diagnostic->status = TAIYIN_EVENT_ERROR_NOT_FOUND;
    diagnostic->target_id = TAIYIN_BODY_MOON;
    diagnostic->jd_tdb = jd_ut;
}

Status fill_lunar_occultation_body_visibility_sample(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_ut,
    uint64_t visibility_flags,
    LunarOccultationLocalVisibilitySample* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !is_valid_lunar_body_occultation_target(body_id)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out = LunarOccultationLocalVisibilitySample();
    if (!split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_STATUS_OK;
    }

    const uint32_t observed_flags = observed_horizontal_flags_from_visibility_flags(visibility_flags);
    const int body_ids[3] = { TAIYIN_BODY_MOON, body_id, TAIYIN_BODY_SUN };
    ObservedPosition observed[3];
    EphemerisEvalDiagnostic diagnostics[3];
    const Status status = calc_observed_ut(
        context,
        jd_ut,
        body_ids,
        3,
        observed_flags,
        observed,
        diagnostics);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) {
            *diagnostic = diagnostics[0].status != TAIYIN_STATUS_OK
                ? diagnostics[0]
                : (diagnostics[1].status != TAIYIN_STATUS_OK ? diagnostics[1] : diagnostics[2]);
        }
        return status;
    }
    if (diagnostic) *diagnostic = diagnostics[1];

    const HorizontalCoordinates& moon = selected_horizontal_coordinates(observed[0], visibility_flags);
    const HorizontalCoordinates& target = selected_horizontal_coordinates(observed[1], visibility_flags);
    const HorizontalCoordinates& sun = selected_horizontal_coordinates(observed[2], visibility_flags);
    if (!std::isfinite(moon.altitude_rad) || !std::isfinite(moon.azimuth_rad)
        || !std::isfinite(target.altitude_rad) || !std::isfinite(target.azimuth_rad)
        || !std::isfinite(sun.altitude_rad) || !std::isfinite(sun.azimuth_rad)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    out->valid = 1;
    out->jd_ut = jd_ut;
    out->moon_altitude_rad = moon.altitude_rad;
    out->moon_azimuth_rad = moon.azimuth_rad;
    out->target_altitude_rad = target.altitude_rad;
    out->target_azimuth_rad = target.azimuth_rad;
    out->sun_altitude_rad = sun.altitude_rad;
    out->sun_azimuth_rad = sun.azimuth_rad;
    out->visibility_flags = sample_visibility_flags(moon, target, sun);
    return TAIYIN_STATUS_OK;
}

Status fill_lunar_occultation_star_visibility_sample(
    const NativeCalcContext* context,
    const char* star_key,
    SplitJulianDate jd_ut,
    uint64_t visibility_flags,
    LunarOccultationLocalVisibilitySample* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !star_key || star_key[0] == '\0') {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out = LunarOccultationLocalVisibilitySample();
    if (!split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_STATUS_OK;
    }

    const uint32_t observed_flags = observed_horizontal_flags_from_visibility_flags(visibility_flags);
    const int body_ids[2] = { TAIYIN_BODY_MOON, TAIYIN_BODY_SUN };
    ObservedPosition bodies[2];
    EphemerisEvalDiagnostic body_diagnostics[2];
    Status status = calc_observed_ut(
        context,
        jd_ut,
        body_ids,
        2,
        observed_flags,
        bodies,
        body_diagnostics);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) {
            *diagnostic = body_diagnostics[0].status != TAIYIN_STATUS_OK
                ? body_diagnostics[0]
                : body_diagnostics[1];
        }
        return status;
    }

    ObservedPosition star;
    status = calc_observed_star_ut(
        context,
        star_key,
        jd_ut,
        observed_flags,
        &star,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    const HorizontalCoordinates& moon = selected_horizontal_coordinates(bodies[0], visibility_flags);
    const HorizontalCoordinates& sun = selected_horizontal_coordinates(bodies[1], visibility_flags);
    const HorizontalCoordinates& target = selected_horizontal_coordinates(star, visibility_flags);
    if (!std::isfinite(moon.altitude_rad) || !std::isfinite(moon.azimuth_rad)
        || !std::isfinite(target.altitude_rad) || !std::isfinite(target.azimuth_rad)
        || !std::isfinite(sun.altitude_rad) || !std::isfinite(sun.azimuth_rad)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    out->valid = 1;
    out->jd_ut = jd_ut;
    out->moon_altitude_rad = moon.altitude_rad;
    out->moon_azimuth_rad = moon.azimuth_rad;
    out->target_altitude_rad = target.altitude_rad;
    out->target_azimuth_rad = target.azimuth_rad;
    out->sun_altitude_rad = sun.altitude_rad;
    out->sun_azimuth_rad = sun.azimuth_rad;
    out->visibility_flags = sample_visibility_flags(moon, target, sun);
    return TAIYIN_STATUS_OK;
}

typedef double (*LocalVisibilityScalarFn)(const LunarOccultationLocalVisibilitySample& sample);

template <typename EvalVisibilitySampleFn>
Status eval_local_visibility_scalar(
    EvalVisibilitySampleFn eval_visibility_sample,
    SplitJulianDate jd_ut,
    LocalVisibilityScalarFn scalar_fn,
    double* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out || !scalar_fn || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = NAN;
    LunarOccultationLocalVisibilitySample sample;
    const Status status = eval_visibility_sample(jd_ut, &sample, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    *out = scalar_fn(sample);
    if (!std::isfinite(*out)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    return TAIYIN_STATUS_OK;
}

template <typename EvalVisibilitySampleFn>
Status bisect_local_visibility_crossing(
    EvalVisibilitySampleFn eval_visibility_sample,
    SplitJulianDate a,
    double fa,
    SplitJulianDate b,
    double fb,
    LocalVisibilityScalarFn scalar_fn,
    SplitJulianDate* out_jd_ut,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_jd_ut || !split_julian_date_is_finite(a) || !std::isfinite(fa)
        || !split_julian_date_is_finite(b) || !std::isfinite(fb) || !scalar_fn) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_jd_ut = invalid_jd();
    SplitJulianDate lo = a;
    SplitJulianDate hi = b;
    double flo = fa;
    double fhi = fb;
    if (lo > hi) {
        std::swap(lo, hi);
        std::swap(flo, fhi);
    }
    if ((flo >= 0.0 && fhi >= 0.0) || (flo < 0.0 && fhi < 0.0)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    for (int iter = 0; iter < 54 && (hi - lo) > OCCULTATION_CONTACT_TOLERANCE_DAYS; ++iter) {
        const SplitJulianDate mid = lo + 0.5 * (hi - lo);
        double fm = NAN;
        const Status status = eval_local_visibility_scalar(
            eval_visibility_sample,
            mid,
            scalar_fn,
            &fm,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        if ((flo >= 0.0 && fm >= 0.0) || (flo < 0.0 && fm < 0.0)) {
            lo = mid;
            flo = fm;
        } else {
            hi = mid;
            fhi = fm;
        }
    }
    *out_jd_ut = lo + 0.5 * (hi - lo);
    return TAIYIN_STATUS_OK;
}

template <typename EvalVisibilitySampleFn>
Status find_visibility_intervals(
    EvalVisibilitySampleFn eval_visibility_sample,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    LocalVisibilityScalarFn scalar_fn,
    LunarOccultationVisibilityInterval* intervals,
    int max_intervals,
    int* out_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!intervals || !out_count || max_intervals <= 0 || !scalar_fn
        || !split_julian_date_is_finite(start_jd_ut) || !split_julian_date_is_finite(end_jd_ut) || !(start_jd_ut < end_jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_count = 0;
    for (int i = 0; i < max_intervals; ++i) {
        intervals[i] = LunarOccultationVisibilityInterval();
    }

    SplitJulianDate previous_t = start_jd_ut;
    double previous_value = NAN;
    Status status = eval_local_visibility_scalar(
        eval_visibility_sample,
        previous_t,
        scalar_fn,
        &previous_value,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    bool inside = previous_value >= 0.0;
    SplitJulianDate current_begin = inside ? start_jd_ut : invalid_jd();
    if (inside) {
        current_begin = start_jd_ut;
    }

    for (SplitJulianDate t = std::min(
             end_jd_ut, start_jd_ut + OCCULTATION_VISIBILITY_SCAN_STEP_DAYS);
         t <= end_jd_ut;
         t = std::min(end_jd_ut, t + OCCULTATION_VISIBILITY_SCAN_STEP_DAYS)) {
        if (!(t > previous_t)) break;
        double value = NAN;
        status = eval_local_visibility_scalar(eval_visibility_sample, t, scalar_fn, &value, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;

        if (!inside && previous_value < 0.0 && value >= 0.0) {
            SplitJulianDate begin = invalid_jd();
            status = bisect_local_visibility_crossing(
                eval_visibility_sample,
                previous_t,
                previous_value,
                t,
                value,
                scalar_fn,
                &begin,
                diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
            current_begin = begin;
            inside = true;
        } else if (inside && previous_value >= 0.0 && value < 0.0) {
            SplitJulianDate end = invalid_jd();
            status = bisect_local_visibility_crossing(
                eval_visibility_sample,
                previous_t,
                previous_value,
                t,
                value,
                scalar_fn,
                &end,
                diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
            if (*out_count < max_intervals
                && split_julian_date_is_finite(current_begin)
                && split_julian_date_is_finite(end)) {
                LunarOccultationVisibilityInterval& interval = intervals[*out_count];
                interval.valid = 1;
                interval.begin_jd_ut = current_begin;
                interval.end_jd_ut = end;
                ++(*out_count);
            }
            current_begin = invalid_jd();
            inside = false;
        }

        if (t >= end_jd_ut) break;
        previous_t = t;
        previous_value = value;
    }

    if (inside) {
        if (*out_count < max_intervals && split_julian_date_is_finite(current_begin)) {
            LunarOccultationVisibilityInterval& interval = intervals[*out_count];
            interval.valid = 1;
            interval.begin_jd_ut = current_begin;
            interval.end_jd_ut = end_jd_ut;
            ++(*out_count);
        }
    }
    return TAIYIN_STATUS_OK;
}

void mirror_first_interval(
    const LunarOccultationVisibilityInterval* intervals,
    int count,
    SplitJulianDate* out_begin_jd_ut,
    SplitJulianDate* out_end_jd_ut
) noexcept {
    if (!out_begin_jd_ut || !out_end_jd_ut) return;
    *out_begin_jd_ut = invalid_jd();
    *out_end_jd_ut = invalid_jd();
    if (intervals && count > 0 && intervals[0].valid) {
        *out_begin_jd_ut = intervals[0].begin_jd_ut;
        *out_end_jd_ut = intervals[0].end_jd_ut;
    }
}

template <typename EvalVisibilitySampleFn>
Status find_local_target_rise_set(
    EvalVisibilitySampleFn eval_visibility_sample,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    SplitJulianDate* out_rise_jd_ut,
    SplitJulianDate* out_set_jd_ut,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_rise_jd_ut || !out_set_jd_ut
        || !split_julian_date_is_finite(start_jd_ut) || !split_julian_date_is_finite(end_jd_ut) || !(start_jd_ut < end_jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_rise_jd_ut = invalid_jd();
    *out_set_jd_ut = invalid_jd();

    SplitJulianDate previous_t = start_jd_ut;
    double previous_value = NAN;
    Status status = eval_local_visibility_scalar(
        eval_visibility_sample,
        previous_t,
        target_altitude_scalar,
        &previous_value,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    for (SplitJulianDate t = std::min(
             end_jd_ut, start_jd_ut + OCCULTATION_VISIBILITY_SCAN_STEP_DAYS);
         t <= end_jd_ut;
         t = std::min(end_jd_ut, t + OCCULTATION_VISIBILITY_SCAN_STEP_DAYS)) {
        if (!(t > previous_t)) break;
        double value = NAN;
        status = eval_local_visibility_scalar(
            eval_visibility_sample,
            t,
            target_altitude_scalar,
            &value,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;

        if (!split_julian_date_is_finite(*out_rise_jd_ut) && previous_value < 0.0 && value >= 0.0) {
            status = bisect_local_visibility_crossing(
                eval_visibility_sample,
                previous_t,
                previous_value,
                t,
                value,
                target_altitude_scalar,
                out_rise_jd_ut,
                diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
        } else if (!split_julian_date_is_finite(*out_set_jd_ut) && previous_value >= 0.0 && value < 0.0) {
            status = bisect_local_visibility_crossing(
                eval_visibility_sample,
                previous_t,
                previous_value,
                t,
                value,
                target_altitude_scalar,
                out_set_jd_ut,
                diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
        }
        if (split_julian_date_is_finite(*out_rise_jd_ut) && split_julian_date_is_finite(*out_set_jd_ut)) {
            return TAIYIN_STATUS_OK;
        }
        if (t >= end_jd_ut) break;
        previous_t = t;
        previous_value = value;
    }
    return TAIYIN_STATUS_OK;
}

template <typename EvalVisibilitySampleFn>
Status fill_lunar_occultation_visibility_intervals(
    const LunarStarOccultationSearchResult* occultation,
    EvalVisibilitySampleFn eval_visibility_sample,
    LunarOccultationLocalVisibility* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!occultation || !out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    const SplitJulianDate start = occultation_event_interval_start(*occultation);
    const SplitJulianDate end = occultation_event_interval_end(*occultation);
    if (!split_julian_date_is_finite(start)
        || !split_julian_date_is_finite(end) || !(start < end)) {
        return TAIYIN_STATUS_OK;
    }

    EphemerisEvalDiagnostic scratch;
    Status status = find_local_target_rise_set(
        eval_visibility_sample,
        start,
        end,
        &out->target_rise_jd_ut,
        &out->target_set_jd_ut,
        &scratch);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) *diagnostic = scratch;
        return status;
    }

    status = find_visibility_intervals(
        eval_visibility_sample,
        start,
        end,
        geometric_visibility_scalar,
        out->visible_intervals,
        TAIYIN_OCCULTATION_MAX_VISIBILITY_INTERVALS,
        &out->visible_interval_count,
        &scratch);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) *diagnostic = scratch;
        return status;
    }
    mirror_first_interval(
        out->visible_intervals,
        out->visible_interval_count,
        &out->visible_begin_jd_ut,
        &out->visible_end_jd_ut);
    if (out->visible_interval_count > 0) {
        out->visibility_flags |= TAIYIN_OCCULTATION_VISIBILITY_HAS_VISIBLE_INTERVAL;
    }

    status = find_visibility_intervals(
        eval_visibility_sample,
        start,
        end,
        dark_visibility_scalar,
        out->dark_visible_intervals,
        TAIYIN_OCCULTATION_MAX_VISIBILITY_INTERVALS,
        &out->dark_visible_interval_count,
        &scratch);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) *diagnostic = scratch;
        return status;
    }
    mirror_first_interval(
        out->dark_visible_intervals,
        out->dark_visible_interval_count,
        &out->dark_visible_begin_jd_ut,
        &out->dark_visible_end_jd_ut);
    if (out->dark_visible_interval_count > 0) {
        out->visibility_flags |= TAIYIN_OCCULTATION_VISIBILITY_HAS_DARK_INTERVAL;
    }
    return TAIYIN_STATUS_OK;
}

template <typename EvalTargetLlrFn>
Status eval_occultation_center_line(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint64_t flags,
    EvalTargetLlrFn eval_target_llr,
    sxwnl_ext::occultation::Boundary* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = sxwnl_ext::occultation::Boundary();

    sxwnl::solar::Vec3 moon;
    Status status = eval_body_equatorial_llr_km_ut(
        context,
        TAIYIN_BODY_MOON,
        jd_ut,
        flags,
        &moon,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    sxwnl::solar::Vec3 target;
    status = eval_target_llr(jd_ut, &target, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    double gast = 0.0;
    status = gast_for_occultation_where(*context, jd_ut, &gast, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    *out = sxwnl_ext::occultation::lineEar_llr(moon, target, gast);
    return TAIYIN_STATUS_OK;
}

template <typename EvalTargetLlrFn>
Status eval_occultation_nbj(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint64_t flags,
    EvalTargetLlrFn eval_target_llr,
    sxwnl_ext::occultation::NbjResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = sxwnl_ext::occultation::NbjResult();

    sxwnl::solar::Vec3 moon;
    Status status = eval_body_equatorial_llr_km_ut(
        context,
        TAIYIN_BODY_MOON,
        jd_ut,
        flags,
        &moon,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    sxwnl::solar::Vec3 target;
    status = eval_target_llr(jd_ut, &target, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    double gast = 0.0;
    status = gast_for_occultation_where(*context, jd_ut, &gast, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    sxwnl_ext::occultation::ZbState state;
    sxwnl_ext::occultation::zb0(moon, target, gast, &state);
    *out = sxwnl_ext::occultation::nbj(state);
    return TAIYIN_STATUS_OK;
}

template <typename EvalTargetLlrFn>
Status find_occultation_center_line_edge(
    const NativeCalcContext* context,
    const LunarStarOccultationSearchResult* occultation,
    uint64_t flags,
    EvalTargetLlrFn eval_target_llr,
    double direction,
    SplitJulianDate* out_jd_ut,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !occultation || !out_jd_ut
        || !split_julian_date_is_finite(occultation->jd_ut)
        || !(direction == -1.0 || direction == 1.0)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_jd_ut = invalid_jd();

    // C1/C4 are Moon-target disk contacts, not the ingress/egress of the
    // Moon-target center line on the Earth. Keep the where-path scan independent
    // from contact times so real search results do not truncate central paths.
    const double span_days = OCCULTATION_SEED_WINDOW_DAYS;

    sxwnl_ext::occultation::Boundary inside;
    Status status = eval_occultation_center_line(
        context,
        occultation->jd_ut,
        flags,
        eval_target_llr,
        &inside,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    SplitJulianDate inside_t = occultation->jd_ut;
    SplitJulianDate outside_t = invalid_jd();
    constexpr double kScanStepDays = 5.0 / 1440.0;
    for (double offset = kScanStepDays; offset <= span_days + 1.0e-12; offset += kScanStepDays) {
        const SplitJulianDate candidate_t = occultation->jd_ut + direction * offset;
        sxwnl_ext::occultation::Boundary sample;
        EphemerisEvalDiagnostic scratch;
        status = eval_occultation_center_line(
            context,
            candidate_t,
            flags,
            eval_target_llr,
            &sample,
            &scratch);
        if (status != TAIYIN_STATUS_OK) return status;
        if (!sample.valid) {
            outside_t = candidate_t;
            break;
        }
        inside_t = candidate_t;
    }
    if (!split_julian_date_is_finite(outside_t)) {
        return TAIYIN_STATUS_OK;
    }

    constexpr double kEdgeTimeToleranceDays = 1.0e-12;
    for (int iter = 0;
         iter < 48 && std::fabs(outside_t - inside_t) > kEdgeTimeToleranceDays;
         ++iter) {
        const SplitJulianDate mid = inside_t + 0.5 * (outside_t - inside_t);
        sxwnl_ext::occultation::Boundary sample;
        EphemerisEvalDiagnostic scratch;
        status = eval_occultation_center_line(
            context,
            mid,
            flags,
            eval_target_llr,
            &sample,
            &scratch);
        if (status != TAIYIN_STATUS_OK) return status;
        if (sample.valid) {
            inside_t = mid;
        } else {
            outside_t = mid;
        }
    }

    // The midpoint can fall on the invalid side after the final geometry is
    // reevaluated. Return the last sample that was explicitly valid instead.
    *out_jd_ut = inside_t;
    return TAIYIN_STATUS_OK;
}

template <typename EvalTargetLlrFn>
Status fill_occultation_center_line_path(
    const NativeCalcContext* context,
    uint64_t flags,
    EvalTargetLlrFn eval_target_llr,
    SplitJulianDate begin_jd_ut,
    SplitJulianDate end_jd_ut,
    LunarOccultationWhereResult* out
) noexcept {
    if (!context || !out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    if (!split_julian_date_is_finite(begin_jd_ut) || !split_julian_date_is_finite(end_jd_ut) || !(begin_jd_ut < end_jd_ut)) {
        return TAIYIN_STATUS_OK;
    }

    out->center_line_path_count = 0;
    out->center_line_min_longitude_deg = NAN;
    out->center_line_max_longitude_deg = NAN;
    out->center_line_min_latitude_deg = NAN;
    out->center_line_max_latitude_deg = NAN;
    out->center_line_path_distance_km = NAN;

    double previous_lon_rad = NAN;
    double previous_lat_rad = NAN;
    double distance_km = 0.0;
    for (int i = 0; i < TAIYIN_OCCULTATION_WHERE_MAX_PATH_POINTS; ++i) {
        const double fraction = TAIYIN_OCCULTATION_WHERE_MAX_PATH_POINTS == 1
            ? 0.0
            : static_cast<double>(i) / static_cast<double>(TAIYIN_OCCULTATION_WHERE_MAX_PATH_POINTS - 1);
        const SplitJulianDate jd_ut = begin_jd_ut + fraction * (end_jd_ut - begin_jd_ut);
        sxwnl_ext::occultation::Boundary point;
        EphemerisEvalDiagnostic scratch;
        const Status status = eval_occultation_center_line(
            context,
            jd_ut,
            flags,
            eval_target_llr,
            &point,
            &scratch);
        if (status != TAIYIN_STATUS_OK || !point.valid) {
            continue;
        }

        LunarOccultationWherePathPoint& slot = out->center_line_path[out->center_line_path_count];
        slot.valid = 1;
        slot.jd_ut = jd_ut;
        slot.longitude_deg = point.longitude_rad * TAIYIN_RAD_TO_DEG;
        slot.latitude_deg = point.latitude_rad * TAIYIN_RAD_TO_DEG;
        slot.height_m = 0.0;
        ++out->center_line_path_count;

        if (!std::isfinite(out->center_line_min_longitude_deg)) {
            out->center_line_min_longitude_deg = slot.longitude_deg;
            out->center_line_max_longitude_deg = slot.longitude_deg;
            out->center_line_min_latitude_deg = slot.latitude_deg;
            out->center_line_max_latitude_deg = slot.latitude_deg;
        } else {
            out->center_line_min_longitude_deg = std::min(out->center_line_min_longitude_deg, slot.longitude_deg);
            out->center_line_max_longitude_deg = std::max(out->center_line_max_longitude_deg, slot.longitude_deg);
            out->center_line_min_latitude_deg = std::min(out->center_line_min_latitude_deg, slot.latitude_deg);
            out->center_line_max_latitude_deg = std::max(out->center_line_max_latitude_deg, slot.latitude_deg);
        }
        if (std::isfinite(previous_lon_rad) && std::isfinite(previous_lat_rad)) {
            const double step_km = sxwnl_ext::occultation::surface_distance_km(
                previous_lon_rad,
                previous_lat_rad,
                point.longitude_rad,
                point.latitude_rad);
            if (std::isfinite(step_km)) distance_km += step_km;
        }
        previous_lon_rad = point.longitude_rad;
        previous_lat_rad = point.latitude_rad;
    }

    if (out->center_line_path_count > 1) {
        out->center_line_path_distance_km = distance_km;
    }
    return TAIYIN_STATUS_OK;
}

double normalize_longitude_rad(double longitude_rad) noexcept {
    return std::atan2(std::sin(longitude_rad), std::cos(longitude_rad));
}

double signed_longitude_delta_rad(double a, double b) noexcept {
    return normalize_longitude_rad(a - b);
}

double unwrap_longitude_deg(double reference_deg, double longitude_deg) noexcept {
    double out = longitude_deg;
    while (out - reference_deg > 180.0) out -= 360.0;
    while (out - reference_deg < -180.0) out += 360.0;
    return out;
}

void offset_geodetic_approx(
    double longitude_rad,
    double latitude_rad,
    double east_km,
    double north_km,
    double* out_longitude_rad,
    double* out_latitude_rad
) noexcept {
    const double earth_radius_km = 6378.1366;
    double lat = latitude_rad + north_km / earth_radius_km;
    const double limit = 0.5 * M_PI - 1.0e-8;
    if (lat > limit) lat = limit;
    if (lat < -limit) lat = -limit;
    const double cos_lat = std::max(1.0e-6, std::fabs(std::cos(lat)));
    const double lon = longitude_rad + east_km / (earth_radius_km * cos_lat);
    if (out_longitude_rad) *out_longitude_rad = normalize_longitude_rad(lon);
    if (out_latitude_rad) *out_latitude_rad = lat;
}

Status set_local_observer_from_radians(
    const NativeCalcContext* context,
    double longitude_rad,
    double latitude_rad,
    NativeCalcContext* out
) noexcept {
    if (!context || !out || !std::isfinite(longitude_rad) || !std::isfinite(latitude_rad)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = *context;
    native_context_set_geocentric_observer(out, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH);
    return native_context_set_observer_location(
        out,
        native_observer_location_degrees(
            longitude_rad * TAIYIN_RAD_TO_DEG,
            latitude_rad * TAIYIN_RAD_TO_DEG,
            0.0));
}

template <typename EvalOccultationSampleFn>
Status eval_margin_at_ground_offset(
    const NativeCalcContext* context,
    double center_longitude_rad,
    double center_latitude_rad,
    double normal_east,
    double normal_north,
    double signed_distance_km,
    SplitJulianDate jd_ut,
    EvalOccultationSampleFn eval_occultation_sample,
    double* out_margin_rad
) noexcept {
    if (!context || !out_margin_rad || !std::isfinite(signed_distance_km)
        || !std::isfinite(normal_east) || !std::isfinite(normal_north)
        || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_margin_rad = NAN;
    double lon = NAN;
    double lat = NAN;
    offset_geodetic_approx(
        center_longitude_rad,
        center_latitude_rad,
        normal_east * signed_distance_km,
        normal_north * signed_distance_km,
        &lon,
        &lat);
    NativeCalcContext local;
    Status status = set_local_observer_from_radians(context, lon, lat, &local);
    if (status != TAIYIN_STATUS_OK) return status;
    OccultationSample sample;
    status = eval_occultation_sample(&local, jd_ut, &sample);
    if (status != TAIYIN_STATUS_OK) return status;
    *out_margin_rad = sample.margin_rad;
    return std::isfinite(*out_margin_rad)
        ? TAIYIN_STATUS_OK
        : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

template <typename EvalOccultationSampleFn>
Status find_outer_limit_from_center(
    const NativeCalcContext* context,
    double center_longitude_rad,
    double center_latitude_rad,
    double normal_east,
    double normal_north,
    int side,
    SplitJulianDate jd_ut,
    EvalOccultationSampleFn eval_occultation_sample,
    LunarOccultationWherePathPoint* out
) noexcept {
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out = LunarOccultationWherePathPoint();
    double center_margin = NAN;
    Status status = eval_margin_at_ground_offset(
        context,
        center_longitude_rad,
        center_latitude_rad,
        normal_east,
        normal_north,
        0.0,
        jd_ut,
        eval_occultation_sample,
        &center_margin);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!(center_margin >= 0.0)) return TAIYIN_STATUS_OK;

    double low = 0.0;
    double high = 250.0;
    double high_margin = NAN;
    for (int i = 0; i < 6; ++i) {
        status = eval_margin_at_ground_offset(
            context,
            center_longitude_rad,
            center_latitude_rad,
            normal_east,
            normal_north,
            static_cast<double>(side) * high,
            jd_ut,
            eval_occultation_sample,
            &high_margin);
        if (status != TAIYIN_STATUS_OK) return status;
        if (high_margin <= 0.0) break;
        low = high;
        high *= 2.0;
    }
    if (!(high_margin <= 0.0)) {
        return TAIYIN_STATUS_OK;
    }

    for (int i = 0; i < 16; ++i) {
        const double mid = 0.5 * (low + high);
        double mid_margin = NAN;
        status = eval_margin_at_ground_offset(
            context,
            center_longitude_rad,
            center_latitude_rad,
            normal_east,
            normal_north,
            static_cast<double>(side) * mid,
            jd_ut,
            eval_occultation_sample,
            &mid_margin);
        if (status != TAIYIN_STATUS_OK) return status;
        if (mid_margin >= 0.0) {
            low = mid;
        } else {
            high = mid;
        }
    }

    double lon = NAN;
    double lat = NAN;
    offset_geodetic_approx(
        center_longitude_rad,
        center_latitude_rad,
        normal_east * static_cast<double>(side) * high,
        normal_north * static_cast<double>(side) * high,
        &lon,
        &lat);
    out->valid = 1;
    out->jd_ut = jd_ut;
    out->longitude_deg = lon * TAIYIN_RAD_TO_DEG;
    out->latitude_deg = lat * TAIYIN_RAD_TO_DEG;
    out->height_m = 0.0;
    return TAIYIN_STATUS_OK;
}

void append_polygon_point(
    const LunarOccultationWherePathPoint& source,
    LunarOccultationWhereResult* out
) noexcept {
    if (!out || !source.valid
        || out->visible_region_polygon_count >= TAIYIN_OCCULTATION_WHERE_MAX_POLYGON_POINTS) {
        return;
    }
    LunarOccultationWherePathPoint& point =
        out->visible_region_polygon[out->visible_region_polygon_count];
    point = source;
    if (out->visible_region_polygon_count > 0) {
        const double previous =
            out->visible_region_polygon[out->visible_region_polygon_count - 1].longitude_deg;
        point.longitude_deg = unwrap_longitude_deg(previous, point.longitude_deg);
    }
    if (!std::isfinite(out->visible_region_min_longitude_deg)) {
        out->visible_region_min_longitude_deg = point.longitude_deg;
        out->visible_region_max_longitude_deg = point.longitude_deg;
        out->visible_region_min_latitude_deg = point.latitude_deg;
        out->visible_region_max_latitude_deg = point.latitude_deg;
    } else {
        out->visible_region_min_longitude_deg = std::min(out->visible_region_min_longitude_deg, point.longitude_deg);
        out->visible_region_max_longitude_deg = std::max(out->visible_region_max_longitude_deg, point.longitude_deg);
        out->visible_region_min_latitude_deg = std::min(out->visible_region_min_latitude_deg, point.latitude_deg);
        out->visible_region_max_latitude_deg = std::max(out->visible_region_max_latitude_deg, point.latitude_deg);
    }
    ++out->visible_region_polygon_count;
}

void fill_occultation_visible_region_polygon(LunarOccultationWhereResult* out) noexcept {
    if (!out) return;
    out->visible_region_polygon_count = 0;
    out->visible_region_min_longitude_deg = NAN;
    out->visible_region_max_longitude_deg = NAN;
    out->visible_region_min_latitude_deg = NAN;
    out->visible_region_max_latitude_deg = NAN;
    if (out->outer_limit_path_count < 2) return;
    for (int i = 0; i < out->outer_limit_path_count; ++i) {
        append_polygon_point(out->outer_north_path[i], out);
    }
    for (int i = out->outer_limit_path_count - 1; i >= 0; --i) {
        append_polygon_point(out->outer_south_path[i], out);
    }
}

template <typename EvalOccultationSampleFn>
Status fill_occultation_outer_limit_path(
    const NativeCalcContext* context,
    EvalOccultationSampleFn eval_occultation_sample,
    LunarOccultationWhereResult* out
) noexcept {
    if (!context || !out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    out->outer_limit_path_count = 0;
    out->outer_limit_mean_width_km = NAN;
    out->outer_limit_max_width_km = NAN;
    if (out->center_line_hits_earth == 0 || out->center_line_path_count < 3) {
        return TAIYIN_STATUS_OK;
    }

    double width_sum = 0.0;
    double width_max = 0.0;
    int width_count = 0;
    for (int i = 0; i < out->center_line_path_count; ++i) {
        const LunarOccultationWherePathPoint& center = out->center_line_path[i];
        if (!center.valid) continue;
        const int prev_i = std::max(0, i - 1);
        const int next_i = std::min(out->center_line_path_count - 1, i + 1);
        if (prev_i == next_i) continue;
        const LunarOccultationWherePathPoint& prev = out->center_line_path[prev_i];
        const LunarOccultationWherePathPoint& next = out->center_line_path[next_i];
        if (!prev.valid || !next.valid) continue;

        const double lon = center.longitude_deg * TAIYIN_DEG_TO_RAD;
        const double lat = center.latitude_deg * TAIYIN_DEG_TO_RAD;
        const double prev_lon = prev.longitude_deg * TAIYIN_DEG_TO_RAD;
        const double next_lon = next.longitude_deg * TAIYIN_DEG_TO_RAD;
        const double prev_lat = prev.latitude_deg * TAIYIN_DEG_TO_RAD;
        const double next_lat = next.latitude_deg * TAIYIN_DEG_TO_RAD;
        const double east = signed_longitude_delta_rad(next_lon, prev_lon) * std::cos(lat);
        const double north = next_lat - prev_lat;
        const double tangent = std::hypot(east, north);
        if (!(tangent > 0.0) || !std::isfinite(tangent)) continue;

        double normal_east = -north / tangent;
        double normal_north = east / tangent;

        LunarOccultationWherePathPoint north_point;
        Status status = find_outer_limit_from_center(
            context,
            lon,
            lat,
            normal_east,
            normal_north,
            +1,
            center.jd_ut,
            eval_occultation_sample,
            &north_point);
        if (status != TAIYIN_STATUS_OK) return status;
        LunarOccultationWherePathPoint south_point;
        status = find_outer_limit_from_center(
            context,
            lon,
            lat,
            normal_east,
            normal_north,
            -1,
            center.jd_ut,
            eval_occultation_sample,
            &south_point);
        if (status != TAIYIN_STATUS_OK) return status;
        if (!north_point.valid || !south_point.valid) continue;

        const int out_i = out->outer_limit_path_count;
        if (out_i >= TAIYIN_OCCULTATION_WHERE_MAX_PATH_POINTS) break;
        out->outer_north_path[out_i] = north_point;
        out->outer_south_path[out_i] = south_point;
        ++out->outer_limit_path_count;

        const double width_km = sxwnl_ext::occultation::surface_distance_km(
            north_point.longitude_deg * TAIYIN_DEG_TO_RAD,
            north_point.latitude_deg * TAIYIN_DEG_TO_RAD,
            south_point.longitude_deg * TAIYIN_DEG_TO_RAD,
            south_point.latitude_deg * TAIYIN_DEG_TO_RAD);
        if (std::isfinite(width_km)) {
            width_sum += width_km;
            width_max = std::max(width_max, width_km);
            ++width_count;
        }
    }
    if (width_count > 0) {
        out->outer_limit_mean_width_km = width_sum / static_cast<double>(width_count);
        out->outer_limit_max_width_km = width_max;
    }
    fill_occultation_visible_region_polygon(out);
    return TAIYIN_STATUS_OK;
}

template <typename EvalTargetLlrFn, typename EvalOccultationSampleFn, typename EvalVisibilitySampleFn>
Status compute_lunar_occultation_where_impl(
    const NativeCalcContext* context,
    int expected_kind,
    const LunarStarOccultationSearchResult* occultation,
    uint64_t flags,
    EvalTargetLlrFn eval_target_llr,
    EvalOccultationSampleFn eval_occultation_sample,
    EvalVisibilitySampleFn eval_visibility_sample,
    LunarOccultationWhereResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !occultation || !out
        || occultation->kind != expected_kind
        || !split_julian_date_is_finite(occultation->jd_ut)
        || (flags & ~SUPPORTED_OCCULTATION_WHERE_FLAGS) != 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = LunarOccultationWhereResult();
    out->jd_ut = occultation->jd_ut;
    out->height_m = 0.0;

    sxwnl_ext::occultation::NbjResult where_geometry;
    Status status = eval_occultation_nbj(
        context,
        occultation->jd_ut,
        flags,
        eval_target_llr,
        &where_geometry,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    const sxwnl_ext::occultation::Boundary best = where_geometry.center_line_hits_earth != 0
        ? where_geometry.pp0
        : where_geometry.pp1;
    if (!best.valid
        || !std::isfinite(best.longitude_rad)
        || !std::isfinite(best.latitude_rad)) {
        out->center_line_hits_earth = 0;
        set_not_found_diagnostic(diagnostic, occultation->jd_ut);
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    NativeCalcContext local = *context;
    native_context_set_geocentric_observer(&local, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH);
    status = native_context_set_observer_location(
        &local,
        native_observer_location_degrees(
            best.longitude_rad * TAIYIN_RAD_TO_DEG,
            best.latitude_rad * TAIYIN_RAD_TO_DEG,
            0.0));
    if (status != TAIYIN_STATUS_OK) return status;

    OccultationSample topocentric;
    status = eval_occultation_sample(&local, occultation->jd_ut, &topocentric);
    if (status != TAIYIN_STATUS_OK) return status;

    LunarOccultationLocalVisibilitySample sample;
    status = eval_visibility_sample(&local, &sample);
    if (status != TAIYIN_STATUS_OK) return status;

    out->center_line_hits_earth = where_geometry.center_line_hits_earth;
    if (where_geometry.center_line_hits_earth != 0) {
        status = find_occultation_center_line_edge(
            context,
            occultation,
            flags,
            eval_target_llr,
            -1.0,
            &out->center_line_begin_jd_ut,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        status = find_occultation_center_line_edge(
            context,
            occultation,
            flags,
            eval_target_llr,
            1.0,
            &out->center_line_end_jd_ut,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        status = fill_occultation_center_line_path(
            context,
            flags,
            eval_target_llr,
            out->center_line_begin_jd_ut,
            out->center_line_end_jd_ut,
            out);
        if (status != TAIYIN_STATUS_OK) return status;
        status = fill_occultation_outer_limit_path(
            context,
            eval_occultation_sample,
            out);
        if (status != TAIYIN_STATUS_OK) return status;
    }

    out->type_flags = occultation_type_flags_from_sample(topocentric)
        | (where_geometry.center_line_hits_earth != 0
            ? TAIYIN_OCCULTATION_TYPE_CENTRAL
            : TAIYIN_OCCULTATION_TYPE_NONCENTRAL);
    out->longitude_deg = best.longitude_rad * TAIYIN_RAD_TO_DEG;
    out->latitude_deg = best.latitude_rad * TAIYIN_RAD_TO_DEG;
    out->separation_rad = topocentric.separation_rad;
    out->moon_radius_rad = topocentric.moon_radius_rad;
    out->target_radius_rad = topocentric.target_radius_rad;
    out->margin_rad = topocentric.margin_rad;
    out->phenomena = phenomena_from_sample(topocentric);
    out->local_sample = sample;
    out->visibility_flags = sample.visibility_flags;
    return TAIYIN_STATUS_OK;
}

template <typename Target>
Status occultation_centrality_type_flags(
    const NativeCalcContext* context,
    const Target& target,
    SplitJulianDate jd_ut,
    uint64_t flags,
    uint32_t* out_flags,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !target.valid() || !out_flags || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    sxwnl::solar::Vec3 moon;
    Status status = eval_body_equatorial_llr_km_ut(
        context,
        TAIYIN_BODY_MOON,
        jd_ut,
        flags,
        &moon,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    sxwnl::solar::Vec3 target_llr;
    status = target.eval_equatorial_llr_km(
        context,
        jd_ut,
        flags,
        &target_llr,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    double gast = 0.0;
    status = gast_for_occultation_where(*context, jd_ut, &gast, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    const sxwnl_ext::occultation::Boundary center =
        sxwnl_ext::occultation::lineEar_llr(moon, target_llr, gast);
    *out_flags = center.valid
        && std::isfinite(center.longitude_rad)
        && std::isfinite(center.latitude_rad)
        ? TAIYIN_OCCULTATION_TYPE_CENTRAL
        : TAIYIN_OCCULTATION_TYPE_NONCENTRAL;
    return TAIYIN_STATUS_OK;
}

template <typename Target>
Status search_next_lunar_occultation_impl_ut(
    const NativeCalcContext* context,
    const Target& target,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    bool topocentric,
    LunarStarOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = LunarStarOccultationSearchResult();
    if (!context || !target.valid() || !out
        || !split_julian_date_is_finite(jd_start_ut)
        || (flags & ~SUPPORTED_OCCULTATION_SEARCH_FLAGS) != 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (topocentric && !context_has_observer(*context)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if ((flags & TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION) != 0u
        && !global_lunar_limb_model()) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    bool possible = false;
    Status status = target.can_be_lunar_occulted(
        context,
        jd_start_ut,
        flags,
        topocentric,
        &possible,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!possible) {
        set_not_found_diagnostic(diagnostic, jd_start_ut);
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    const bool backward = (flags & TAIYIN_OCCULTATION_SEARCH_BACKWARD) != 0u;
    const bool one_candidate = (flags & TAIYIN_OCCULTATION_SEARCH_ONE_CANDIDATE) != 0u;
    const double direction = backward ? -1.0 : 1.0;
    const double latitude_gate_rad = topocentric ? LOCAL_ECLIPTIC_MARGIN_RAD : GEOCENTRIC_ECLIPTIC_MARGIN_RAD;
    SplitJulianDate probe_jd = jd_start_ut - direction * OCCULTATION_SEED_WINDOW_DAYS;
    for (int i = 0; i < OCCULTATION_MAX_CANDIDATES; ++i) {
        EclipticOccultationSeedSample seed_sample;
        status = target.eval_ecliptic(
            context,
            probe_jd,
            flags,
            false,
            &seed_sample,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;

        const SplitJulianDate seed_jd = estimate_next_lunar_longitude_seed(seed_sample, backward);
        if (!split_julian_date_is_finite(seed_jd)) {
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }

        SplitJulianDate refined_seed = invalid_jd();
        status = refine_lunar_longitude_seed(
            context,
            target,
            seed_jd,
            flags,
            false,
            &refined_seed,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;

        ++out->candidate_count;
        out->candidate_jd_ut = refined_seed;
        SplitJulianDate next_probe_jd = refined_seed + direction * OCCULTATION_MIN_SEED_ADVANCE_DAYS;
        if (!split_julian_date_is_finite(next_probe_jd)
            || direction * (next_probe_jd - probe_jd) <= 1.0e-6) {
            next_probe_jd = probe_jd + direction * OCCULTATION_MIN_SEED_ADVANCE_DAYS;
        }
        out->next_search_jd_ut = next_probe_jd;

        EclipticOccultationSeedSample candidate_sample;
        status = target.eval_ecliptic(
            context,
            refined_seed,
            flags,
            false,
            &candidate_sample,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;

        if (std::isfinite(candidate_sample.moon_lat_rad)
            && std::isfinite(candidate_sample.target_lat_rad)
            && std::fabs(candidate_sample.moon_lat_rad - candidate_sample.target_lat_rad) <= latitude_gate_rad) {
            OccultationSample polished;
            const SplitJulianDate bracket_start = refined_seed - OCCULTATION_SEED_WINDOW_DAYS;
            const SplitJulianDate bracket_end = refined_seed + OCCULTATION_SEED_WINDOW_DAYS;
            status = minimize_lunar_separation(
                context,
                target,
                bracket_start,
                bracket_end,
                flags,
                topocentric,
                &polished,
                &out->iteration_count,
                &out->evaluation_count,
                diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
            OccultationSample event_sample;
            status = maximize_lunar_occultation_margin(
                context,
                target,
                polished,
                bracket_start,
                bracket_end,
                flags,
                topocentric,
                &event_sample,
                &out->evaluation_count,
                diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
            if (((backward && event_sample.jd_ut < jd_start_ut)
                    || (!backward && event_sample.jd_ut > jd_start_ut))
                && event_sample.margin_rad >= 0.0) {
                uint32_t type_flags = occultation_type_flags_from_sample(event_sample);
                uint32_t centrality_flags = 0u;
                EphemerisEvalDiagnostic centrality_diagnostic;
                const Status centrality_status = occultation_centrality_type_flags(
                    context,
                    target,
                    event_sample.jd_ut,
                    flags,
                    &centrality_flags,
                    &centrality_diagnostic);
                if (centrality_status == TAIYIN_STATUS_OK) {
                    type_flags |= centrality_flags;
                } else {
                    type_flags |= TAIYIN_OCCULTATION_TYPE_CENTRALITY_UNAVAILABLE;
                }
                if (!occultation_type_filter_matches(flags, type_flags)) {
                    if (one_candidate) {
                        set_not_found_diagnostic(diagnostic, jd_start_ut);
                        return TAIYIN_EVENT_ERROR_NOT_FOUND;
                    }
                    probe_jd = next_probe_jd;
                    continue;
                }

                SplitJulianDate first_contact_jd_ut = invalid_jd();
                SplitJulianDate second_contact_jd_ut = invalid_jd();
                SplitJulianDate third_contact_jd_ut = invalid_jd();
                SplitJulianDate fourth_contact_jd_ut = invalid_jd();
                status = find_lunar_occultation_contact(
                    context,
                    target,
                    event_sample,
                    -1.0,
                    OCCULTATION_CONTACT_OUTER,
                    flags,
                    topocentric,
                    &first_contact_jd_ut,
                    &out->iteration_count,
                    &out->evaluation_count,
                    diagnostic);
                if (status != TAIYIN_STATUS_OK) return status;
                status = find_lunar_occultation_contact(
                    context,
                    target,
                    event_sample,
                    1.0,
                    OCCULTATION_CONTACT_OUTER,
                    flags,
                    topocentric,
                    &fourth_contact_jd_ut,
                    &out->iteration_count,
                    &out->evaluation_count,
                    diagnostic);
                if (status != TAIYIN_STATUS_OK) return status;
                if (event_sample.target_radius_rad > 0.0
                    && occultation_contact_margin(event_sample, OCCULTATION_CONTACT_INNER) >= 0.0) {
                    status = find_lunar_occultation_contact(
                        context,
                        target,
                        event_sample,
                        -1.0,
                        OCCULTATION_CONTACT_INNER,
                        flags,
                        topocentric,
                        &second_contact_jd_ut,
                        &out->iteration_count,
                        &out->evaluation_count,
                        diagnostic);
                    if (status != TAIYIN_STATUS_OK) return status;
                    status = find_lunar_occultation_contact(
                        context,
                        target,
                        event_sample,
                        1.0,
                        OCCULTATION_CONTACT_INNER,
                        flags,
                        topocentric,
                        &third_contact_jd_ut,
                        &out->iteration_count,
                        &out->evaluation_count,
                        diagnostic);
                    if (status != TAIYIN_STATUS_OK) return status;
                }
                out->kind = target.kind();
                out->type_flags = type_flags;
                out->jd_ut = event_sample.jd_ut;
                out->begin_jd_ut = first_contact_jd_ut;
                out->end_jd_ut = fourth_contact_jd_ut;
                out->first_contact_jd_ut = first_contact_jd_ut;
                out->second_contact_jd_ut = second_contact_jd_ut;
                out->third_contact_jd_ut = third_contact_jd_ut;
                out->fourth_contact_jd_ut = fourth_contact_jd_ut;
                out->separation_rad = event_sample.separation_rad;
                out->moon_radius_rad = event_sample.moon_radius_rad;
                out->target_radius_rad = event_sample.target_radius_rad;
                out->margin_rad = event_sample.margin_rad;
                out->phenomena = phenomena_from_sample(event_sample);
                return TAIYIN_STATUS_OK;
            }
        }

        if (one_candidate) {
            set_not_found_diagnostic(diagnostic, jd_start_ut);
            return TAIYIN_EVENT_ERROR_NOT_FOUND;
        }
        probe_jd = next_probe_jd;
    }

    set_not_found_diagnostic(diagnostic, jd_start_ut);
    return TAIYIN_EVENT_ERROR_NOT_FOUND;
}

}  // namespace

LunarOccultationPhenomena::LunarOccultationPhenomena() noexcept
    : angular_distance_rad(NAN),
      diameter_ratio(NAN),
      magnitude(NAN),
      obscuration(NAN),
      occulted_fraction(NAN) {}

LunarOccultationVisibilityInterval::LunarOccultationVisibilityInterval() noexcept
    : valid(0),
      begin_jd_ut(invalid_jd()),
      end_jd_ut(invalid_jd()) {}

LunarStarOccultationSearchResult::LunarStarOccultationSearchResult() noexcept
    : kind(TAIYIN_OCCULTATION_KIND_NONE),
      type_flags(0u),
      jd_ut(invalid_jd()),
      begin_jd_ut(invalid_jd()),
      end_jd_ut(invalid_jd()),
      first_contact_jd_ut(invalid_jd()),
      second_contact_jd_ut(invalid_jd()),
      third_contact_jd_ut(invalid_jd()),
      fourth_contact_jd_ut(invalid_jd()),
      separation_rad(NAN),
      moon_radius_rad(NAN),
      target_radius_rad(NAN),
      margin_rad(NAN),
      phenomena(),
      candidate_jd_ut(invalid_jd()),
      next_search_jd_ut(invalid_jd()),
      candidate_count(0),
      iteration_count(0),
      evaluation_count(0) {}

LunarOccultationLocalVisibilitySample::LunarOccultationLocalVisibilitySample() noexcept
    : valid(0),
      jd_ut(invalid_jd()),
      moon_altitude_rad(NAN),
      moon_azimuth_rad(NAN),
      target_altitude_rad(NAN),
      target_azimuth_rad(NAN),
      sun_altitude_rad(NAN),
      sun_azimuth_rad(NAN),
      visibility_flags(0u) {}

LunarOccultationLocalVisibility::LunarOccultationLocalVisibility() noexcept
    : first_contact(),
      second_contact(),
      maximum(),
      third_contact(),
      fourth_contact(),
      target_rise_jd_ut(invalid_jd()),
      target_set_jd_ut(invalid_jd()),
      visible_begin_jd_ut(invalid_jd()),
      visible_end_jd_ut(invalid_jd()),
      dark_visible_begin_jd_ut(invalid_jd()),
      dark_visible_end_jd_ut(invalid_jd()),
      visible_interval_count(0),
      visible_intervals(),
      dark_visible_interval_count(0),
      dark_visible_intervals(),
      visibility_flags(0u) {}

LunarOccultationWherePathPoint::LunarOccultationWherePathPoint() noexcept
    : valid(0),
      jd_ut(invalid_jd()),
      longitude_deg(NAN),
      latitude_deg(NAN),
      height_m(NAN) {}

LunarOccultationWhereResult::LunarOccultationWhereResult() noexcept
    : center_line_hits_earth(0),
      type_flags(0u),
      jd_ut(invalid_jd()),
      center_line_begin_jd_ut(invalid_jd()),
      center_line_end_jd_ut(invalid_jd()),
      center_line_path_count(0),
      center_line_path(),
      center_line_min_longitude_deg(NAN),
      center_line_max_longitude_deg(NAN),
      center_line_min_latitude_deg(NAN),
      center_line_max_latitude_deg(NAN),
      center_line_path_distance_km(NAN),
      outer_limit_path_count(0),
      outer_north_path(),
      outer_south_path(),
      outer_limit_mean_width_km(NAN),
      outer_limit_max_width_km(NAN),
      visible_region_polygon_count(0),
      visible_region_polygon(),
      visible_region_min_longitude_deg(NAN),
      visible_region_max_longitude_deg(NAN),
      visible_region_min_latitude_deg(NAN),
      visible_region_max_latitude_deg(NAN),
      longitude_deg(NAN),
      latitude_deg(NAN),
      height_m(NAN),
      separation_rad(NAN),
      moon_radius_rad(NAN),
      target_radius_rad(NAN),
      margin_rad(NAN),
      phenomena(),
      local_sample(),
      visibility_flags(0u) {}

Status search_next_geocentric_lunar_star_occultation_ut(
    const NativeCalcContext* context,
    const char* star_key,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    LunarStarOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const StarOccultationTarget target = { star_key };
    return search_next_lunar_occultation_impl_ut(
        context,
        target,
        jd_start_ut,
        flags,
        false,
        out,
        diagnostic);
}

Status search_next_local_lunar_star_occultation_ut(
    const NativeCalcContext* context,
    const char* star_key,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    LunarStarOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const StarOccultationTarget target = { star_key };
    return search_next_lunar_occultation_impl_ut(
        context,
        target,
        jd_start_ut,
        flags,
        true,
        out,
        diagnostic);
}

Status search_next_geocentric_lunar_body_occultation_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    LunarBodyOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_next_geocentric_lunar_body_occultation_ut(
        context,
        body_id,
        standard_lunar_body_occultation_radius_km(body_id),
        jd_start_ut,
        flags,
        out,
        diagnostic);
}

Status search_next_geocentric_lunar_body_occultation_ut(
    const NativeCalcContext* context,
    int body_id,
    double target_radius_km,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    LunarBodyOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const BodyOccultationTarget target = { body_id, target_radius_km };
    return search_next_lunar_occultation_impl_ut(
        context,
        target,
        jd_start_ut,
        flags,
        false,
        out,
        diagnostic);
}

Status search_next_local_lunar_body_occultation_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    LunarBodyOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_next_local_lunar_body_occultation_ut(
        context,
        body_id,
        standard_lunar_body_occultation_radius_km(body_id),
        jd_start_ut,
        flags,
        out,
        diagnostic);
}

Status search_next_local_lunar_body_occultation_ut(
    const NativeCalcContext* context,
    int body_id,
    double target_radius_km,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    LunarBodyOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const BodyOccultationTarget target = { body_id, target_radius_km };
    return search_next_lunar_occultation_impl_ut(
        context,
        target,
        jd_start_ut,
        flags,
        true,
        out,
        diagnostic);
}

Status compute_lunar_star_occultation_local_visibility_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const LunarStarOccultationSearchResult* occultation,
    uint64_t visibility_flags,
    LunarOccultationLocalVisibility* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !star_key || star_key[0] == '\0' || !occultation || !out
        || occultation->kind != TAIYIN_OCCULTATION_KIND_LUNAR_STAR
        || (visibility_flags & ~TAIYIN_OCCULTATION_VISIBILITY_REFRACTION) != 0u
        || !context_has_observer(*context)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = LunarOccultationLocalVisibility();

    Status status = fill_lunar_occultation_star_visibility_sample(
        context, star_key, occultation->first_contact_jd_ut, visibility_flags,
        &out->first_contact, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = fill_lunar_occultation_star_visibility_sample(
        context, star_key, occultation->second_contact_jd_ut, visibility_flags,
        &out->second_contact, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = fill_lunar_occultation_star_visibility_sample(
        context, star_key, occultation->jd_ut, visibility_flags,
        &out->maximum, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = fill_lunar_occultation_star_visibility_sample(
        context, star_key, occultation->third_contact_jd_ut, visibility_flags,
        &out->third_contact, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = fill_lunar_occultation_star_visibility_sample(
        context, star_key, occultation->fourth_contact_jd_ut, visibility_flags,
        &out->fourth_contact, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    accumulate_visibility_flags(out->first_contact, false, out);
    accumulate_visibility_flags(out->second_contact, false, out);
    accumulate_visibility_flags(out->maximum, true, out);
    accumulate_visibility_flags(out->third_contact, false, out);
    accumulate_visibility_flags(out->fourth_contact, false, out);
    auto eval_visibility_sample = [&](SplitJulianDate jd_ut, LunarOccultationLocalVisibilitySample* sample, EphemerisEvalDiagnostic* diag) noexcept -> Status {
        return fill_lunar_occultation_star_visibility_sample(
            context,
            star_key,
            jd_ut,
            visibility_flags,
            sample,
            diag);
    };
    status = fill_lunar_occultation_visibility_intervals(
        occultation,
        eval_visibility_sample,
        out,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    return TAIYIN_STATUS_OK;
}

Status compute_lunar_body_occultation_local_visibility_ut(
    const NativeCalcContext* context,
    int body_id,
    const LunarBodyOccultationSearchResult* occultation,
    uint64_t visibility_flags,
    LunarOccultationLocalVisibility* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !is_valid_lunar_body_occultation_target(body_id) || !occultation || !out
        || occultation->kind != TAIYIN_OCCULTATION_KIND_LUNAR_BODY
        || (visibility_flags & ~TAIYIN_OCCULTATION_VISIBILITY_REFRACTION) != 0u
        || !context_has_observer(*context)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = LunarOccultationLocalVisibility();

    Status status = fill_lunar_occultation_body_visibility_sample(
        context, body_id, occultation->first_contact_jd_ut, visibility_flags,
        &out->first_contact, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = fill_lunar_occultation_body_visibility_sample(
        context, body_id, occultation->second_contact_jd_ut, visibility_flags,
        &out->second_contact, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = fill_lunar_occultation_body_visibility_sample(
        context, body_id, occultation->jd_ut, visibility_flags,
        &out->maximum, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = fill_lunar_occultation_body_visibility_sample(
        context, body_id, occultation->third_contact_jd_ut, visibility_flags,
        &out->third_contact, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = fill_lunar_occultation_body_visibility_sample(
        context, body_id, occultation->fourth_contact_jd_ut, visibility_flags,
        &out->fourth_contact, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    accumulate_visibility_flags(out->first_contact, false, out);
    accumulate_visibility_flags(out->second_contact, false, out);
    accumulate_visibility_flags(out->maximum, true, out);
    accumulate_visibility_flags(out->third_contact, false, out);
    accumulate_visibility_flags(out->fourth_contact, false, out);
    auto eval_visibility_sample = [&](SplitJulianDate jd_ut, LunarOccultationLocalVisibilitySample* sample, EphemerisEvalDiagnostic* diag) noexcept -> Status {
        return fill_lunar_occultation_body_visibility_sample(
            context,
            body_id,
            jd_ut,
            visibility_flags,
            sample,
            diag);
    };
    status = fill_lunar_occultation_visibility_intervals(
        occultation,
        eval_visibility_sample,
        out,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    return TAIYIN_STATUS_OK;
}

Status compute_lunar_star_occultation_where_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const LunarStarOccultationSearchResult* occultation,
    uint64_t flags,
    LunarOccultationWhereResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!star_key || star_key[0] == '\0') {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    auto eval_target_llr = [&](SplitJulianDate jd_ut, sxwnl::solar::Vec3* target, EphemerisEvalDiagnostic* diag) noexcept -> Status {
        return eval_star_equatorial_llr_km_ut(
            context,
            star_key,
            jd_ut,
            flags,
            target,
            diag);
    };
    auto eval_occultation_sample = [&](NativeCalcContext* local, SplitJulianDate jd_ut, OccultationSample* sample) noexcept -> Status {
        return eval_lunar_star_occultation_sample(
            local,
            star_key,
            jd_ut,
            flags,
            true,
            sample,
            diagnostic);
    };
    auto eval_visibility_sample = [&](
        NativeCalcContext* local,
        LunarOccultationLocalVisibilitySample* sample
    ) noexcept -> Status {
        return fill_lunar_occultation_star_visibility_sample(
            local,
            star_key,
            occultation->jd_ut,
            where_visibility_flags(flags),
            sample,
            diagnostic);
    };
    return compute_lunar_occultation_where_impl(
        context,
        TAIYIN_OCCULTATION_KIND_LUNAR_STAR,
        occultation,
        flags,
        eval_target_llr,
        eval_occultation_sample,
        eval_visibility_sample,
        out,
        diagnostic);
}

Status compute_lunar_body_occultation_where_ut(
    const NativeCalcContext* context,
    int body_id,
    const LunarBodyOccultationSearchResult* occultation,
    uint64_t flags,
    LunarOccultationWhereResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return compute_lunar_body_occultation_where_ut(
        context,
        body_id,
        standard_lunar_body_occultation_radius_km(body_id),
        occultation,
        flags,
        out,
        diagnostic);
}

Status compute_lunar_body_occultation_where_ut(
    const NativeCalcContext* context,
    int body_id,
    double target_radius_km,
    const LunarBodyOccultationSearchResult* occultation,
    uint64_t flags,
    LunarOccultationWhereResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!valid_lunar_body_occultation_target(body_id, target_radius_km)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    auto eval_target_llr = [&](SplitJulianDate jd_ut, sxwnl::solar::Vec3* target, EphemerisEvalDiagnostic* diag) noexcept -> Status {
        return eval_body_equatorial_llr_km_ut(
            context,
            body_id,
            jd_ut,
            flags,
            target,
            diag);
    };
    auto eval_occultation_sample = [&](NativeCalcContext* local, SplitJulianDate jd_ut, OccultationSample* sample) noexcept -> Status {
        return eval_lunar_body_occultation_sample(
            local,
            body_id,
            target_radius_km,
            jd_ut,
            flags,
            true,
            sample,
            diagnostic);
    };
    auto eval_visibility_sample = [&](
        NativeCalcContext* local,
        LunarOccultationLocalVisibilitySample* sample
    ) noexcept -> Status {
        return fill_lunar_occultation_body_visibility_sample(
            local,
            body_id,
            occultation->jd_ut,
            where_visibility_flags(flags),
            sample,
            diagnostic);
    };
    return compute_lunar_occultation_where_impl(
        context,
        TAIYIN_OCCULTATION_KIND_LUNAR_BODY,
        occultation,
        flags,
        eval_target_llr,
        eval_occultation_sample,
        eval_visibility_sample,
        out,
        diagnostic);
}

}  // namespace runtime
}  // namespace taiyin
