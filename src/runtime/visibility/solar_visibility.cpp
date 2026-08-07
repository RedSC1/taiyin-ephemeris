#include "runtime/visibility/solar_visibility_internal.h"

#include "runtime/eclipse/eclipse_time.h"
#include "runtime/apparent/fast_apparent.h"
#include "runtime/core/native_context_checks.h"
#include "runtime/visibility/visibility_angle_search_internal.h"
#include "runtime/visibility/visibility_math_internal.h"
#include "runtime/visibility/visibility_sampling_internal.h"

#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/internal/body_disc_radius.h"
#include "taiyin/observer.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/observed_position.h"

#include <algorithm>
#include <cmath>

namespace taiyin {
namespace runtime {
namespace {

bool split_tdb_from_tt(
    const SplitJulianDate& jd_tt,
    double tdb_minus_tt,
    SplitJulianDate* out
) noexcept {
    return add_seconds_to_split_jd(jd_tt, tdb_minus_tt, out);
}

SplitJulianDate invalid_jd() noexcept {
    return SplitJulianDate(0, NAN);
}

constexpr double kFixedSunDistanceAu = 1.0;
constexpr double kDefaultCoarseStepDays = 2.0 / 24.0;
constexpr double kDefaultRootToleranceDays = 1.0e-10;
constexpr double kDefaultResidualToleranceRad = 1.0e-10;
constexpr double kSolarRiseSetFast2MaxAbsLatitudeDeg = 65.0;

FastApparentOptions solar_visibility_window_options() noexcept {
    FastApparentOptions options;
    options.frame = FAST_APPARENT_TRUE_EQUATOR_OF_DATE;
    options.with_velocity = false;
    options.true_position = false;
    return options;
}

Status sample_solar_declination_tt(
    const NativeCalcContext* context,
    const SplitJulianDate& jd_tt,
    FastApparentCorrectionSeries* corrections,
    const FastApparentCorrectionConfig* correction_config,
    double* out_dec_rad,
    EphemerisEvalDiagnostic* diagnostic
) {
    if (!context || !out_dec_rad) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double tdb_minus_tt = dispatch::eval_tdb(
        context->model_context.tdb_model_id, jd_tt, 0);
    SplitJulianDate jd_tdb;
    if (!split_tdb_from_tt(jd_tt, tdb_minus_tt, &jd_tdb)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    FastApparentOptions options = solar_visibility_window_options();
    FastApparentCorrectionEpochSample correction_sample;
    if (corrections) {
        if (!correction_config) return TAIYIN_ERROR_INVALID_ARGUMENT;
        Status st = get_fast_correction(
            context, TAIYIN_BODY_SUN, 0, options, *correction_config,
            jd_tt, corrections, diagnostic, &correction_sample);
        if (st != TAIYIN_STATUS_OK) return st;
        options.correction_sample = &correction_sample;
    }
    CartesianState sun;
    Status st = eval_fast_apparent_body_tdb(
        context, jd_tdb, jd_tt, TAIYIN_BODY_SUN, options, &sun, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    *out_dec_rad = std::atan2(
        sun.position_au.z,
        std::hypot(sun.position_au.x, sun.position_au.y));
    return TAIYIN_STATUS_OK;
}

bool valid_event_kind(int event_kind) noexcept {
    return event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_RISE
        || event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_SET;
}

bool valid_transit_event_kind(int event_kind) noexcept {
    return event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_UPPER_TRANSIT
        || event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_LOWER_TRANSIT;
}

bool valid_limb_kind(int limb_kind) noexcept {
    return limb_kind == TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER
        || limb_kind == TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER
        || limb_kind == TAIYIN_SOLAR_VISIBILITY_LIMB_LOWER;
}

int crossing_direction_for_event(int event_kind) noexcept {
    return event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_RISE
        ? TAIYIN_VISIBILITY_CROSSING_RISING
        : TAIYIN_VISIBILITY_CROSSING_SETTING;
}

VisibilityAltitudeSearchSpec base_solar_spec(
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    double target_altitude_rad
) noexcept {
    VisibilityAltitudeSearchSpec spec;
    spec.body_id = TAIYIN_BODY_SUN;
    spec.crossing_direction = crossing_direction_for_event(event_kind);
    spec.start_jd_ut = start_jd_ut;
    spec.end_jd_ut = end_jd_ut;
    spec.target_altitude_rad = target_altitude_rad;
    spec.physical_radius_km = ::taiyin::internal::body_disc_radius_km(
        TAIYIN_BODY_SUN, ::taiyin::internal::BodyDiscRadiusConvention::MeanPhysical);
    spec.coarse_step_days = kDefaultCoarseStepDays;
    spec.root_tolerance_days = kDefaultRootToleranceDays;
    spec.residual_tolerance_rad = kDefaultResidualToleranceRad;
    spec.observed_flags = 0u;
    return spec;
}

bool resolve_public_solar_visibility_flags(
    uint64_t input_flags,
    uint64_t* out_internal_flags
) noexcept {
    if (!out_internal_flags) return false;
    if ((input_flags & TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION) != 0u
        && (input_flags & TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION) != 0u) {
        return false;
    }
    const uint64_t known_flags =
        TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION
        | TAIYIN_SOLAR_VISIBILITY_FLAG_FIXED_DISC_SIZE
        | TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION
        | TAIYIN_SOLAR_VISIBILITY_STRICT_METEOROLOGY;
    if ((input_flags & ~known_flags) != 0u) return false;
    uint64_t flags = input_flags
        & ~static_cast<uint64_t>(TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION);
    if ((input_flags & TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION) == 0u) {
        flags |= TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION;
    }
    *out_internal_flags = flags;
    return true;
}

SplitJulianDate local_mean_time_target_tt(
    const SplitJulianDate& center_jd_tt,
    double longitude_deg,
    double fraction_of_day
) noexcept {
    SplitJulianDate local_center;
    if (!add_days_to_split_jd(center_jd_tt, longitude_deg / 360.0, &local_center)) {
        return invalid_jd();
    }
    const int64_t local_day = local_center.day_number
        + static_cast<int64_t>(std::floor(local_center.day_fraction - fraction_of_day + 0.5));
    return SplitJulianDate(local_day, fraction_of_day) - longitude_deg / 360.0;
}

double normalize_signed_radians(double value) noexcept {
    double out = std::fmod(value + TAIYIN_PI, TAIYIN_TWO_PI);
    if (out < 0.0) out += TAIYIN_TWO_PI;
    return out - TAIYIN_PI;
}

double topocentric_altitude_rad(
    const Vector3& topocentric_equatorial_au,
    double local_sidereal_rad,
    double latitude_rad
) noexcept {
    const double sin_lst = std::sin(local_sidereal_rad);
    const double cos_lst = std::cos(local_sidereal_rad);
    const double sin_lat = std::sin(latitude_rad);
    const double cos_lat = std::cos(latitude_rad);
    const double east = -sin_lst * topocentric_equatorial_au.x + cos_lst * topocentric_equatorial_au.y;
    const double north = -sin_lat * cos_lst * topocentric_equatorial_au.x
        - sin_lat * sin_lst * topocentric_equatorial_au.y
        + cos_lat * topocentric_equatorial_au.z;
    const double up = cos_lat * cos_lst * topocentric_equatorial_au.x
        + cos_lat * sin_lst * topocentric_equatorial_au.y
        + sin_lat * topocentric_equatorial_au.z;
    return std::atan2(up, std::sqrt(east * east + north * north));
}

struct SolarRiseSetFast2Sample {
    double residual_rad = NAN;
    double slope_rad_per_day = NAN;
};

Status solar_rise_set_event_altitude(
    const NativeCalcContext* context,
    const Vector3& topocentric,
    double center_altitude_rad,
    int limb_kind,
    uint64_t solar_visibility_flags,
    double physical_radius_km,
    double* out_event_altitude_rad
) noexcept {
    if (!context || !out_event_altitude_rad || !std::isfinite(center_altitude_rad)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    double event_altitude = center_altitude_rad;
    if (limb_kind != TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER) {
        const double distance_au = (solar_visibility_flags & TAIYIN_SOLAR_VISIBILITY_FLAG_FIXED_DISC_SIZE) != 0u
            ? kFixedSunDistanceAu
            : std::sqrt(topocentric.x * topocentric.x + topocentric.y * topocentric.y + topocentric.z * topocentric.z);
        const double radius = visibility_angular_radius_rad(physical_radius_km, distance_au);
        if (!std::isfinite(radius)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        event_altitude = limb_kind == TAIYIN_SOLAR_VISIBILITY_LIMB_LOWER
            ? center_altitude_rad - radius
            : center_altitude_rad + radius;
    }
    if ((solar_visibility_flags & TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION) != 0u) {
        const uint64_t observed_flags =
            (solar_visibility_flags & TAIYIN_SOLAR_VISIBILITY_STRICT_METEOROLOGY) != 0u
                ? TAIYIN_OBSERVED_STRICT_METEOROLOGY
                : 0u;
        const Status st = visibility_apply_refraction_from_context(
            context, event_altitude, observed_flags, &event_altitude);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    *out_event_altitude_rad = event_altitude;
    return TAIYIN_STATUS_OK;
}

Status sample_solar_rise_set_fast2_no_window(
    const NativeCalcContext* context,
    const SplitJulianDate& jd_tt,
    double lon_rad,
    double lat_rad,
    double height_m,
    int limb_kind,
    double horizon_altitude_rad,
    uint64_t solar_visibility_flags,
    double physical_radius_km,
    SolarRiseSetFast2Sample* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = SolarRiseSetFast2Sample();
    const double tdb_minus_tt = dispatch::eval_tdb(
        context->model_context.tdb_model_id, jd_tt, 0);
    SplitJulianDate jd_tdb;
    if (!split_tdb_from_tt(jd_tt, tdb_minus_tt, &jd_tdb)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    FastApparentOptions options = solar_visibility_window_options();
    CartesianState sun;
    Status st = eval_fast_apparent_body_tdb(
        context,
        jd_tdb,
        jd_tt,
        TAIYIN_BODY_SUN,
        options,
        &sun,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    SplitJulianDate jd_ut;
    st = eclipse_tt_to_ut(*context, jd_tt, &jd_ut, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    double gast = 0.0;
    if (!gast_model_rad(
            context->model_context.precession_model_id,
            context->model_context.nutation_model_id,
            jd_ut,
            jd_tt,
            &gast)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    const Vector3 observer = observer_geocentric_simple_position_au(
        lon_rad,
        lat_rad,
        height_m,
        jd_ut,
        jd_tt);
    const Vector3 topocentric = topocentric_position_au(sun.position_au, observer);
    const double ra = std::atan2(topocentric.y, topocentric.x);
    const double dec = std::atan2(topocentric.z, std::hypot(topocentric.x, topocentric.y));
    const double hour_angle = normalize_signed_radians(gast + lon_rad - ra);
    const double sin_lat = std::sin(lat_rad);
    const double cos_lat = std::cos(lat_rad);
    const double sin_dec = std::sin(dec);
    const double cos_dec = std::cos(dec);
    const double sin_alt = sin_lat * sin_dec + cos_lat * cos_dec * std::cos(hour_angle);
    const double altitude = std::asin(std::max(-1.0, std::min(1.0, sin_alt)));
    const double cos_alt = std::max(1e-12, std::cos(altitude));
    const double slope = -TAIYIN_TWO_PI * cos_lat * cos_dec * std::sin(hour_angle) / cos_alt;
    if (!std::isfinite(altitude) || !std::isfinite(slope)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;

    double event_altitude = NAN;
    st = solar_rise_set_event_altitude(
        context,
        topocentric,
        altitude,
        limb_kind,
        solar_visibility_flags,
        physical_radius_km,
        &event_altitude);
    if (st != TAIYIN_STATUS_OK) return st;

    out->residual_rad = event_altitude - horizon_altitude_rad;
    double event_slope = slope;
    if ((solar_visibility_flags & TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION) != 0u) {
        constexpr double kDerivativeStepRad = 1.0e-5;
        double event_altitude_lo = NAN;
        double event_altitude_hi = NAN;
        st = solar_rise_set_event_altitude(
            context,
            topocentric,
            altitude - kDerivativeStepRad,
            limb_kind,
            solar_visibility_flags,
            physical_radius_km,
            &event_altitude_lo);
        if (st != TAIYIN_STATUS_OK) return st;
        st = solar_rise_set_event_altitude(
            context,
            topocentric,
            altitude + kDerivativeStepRad,
            limb_kind,
            solar_visibility_flags,
            physical_radius_km,
            &event_altitude_hi);
        if (st != TAIYIN_STATUS_OK) return st;
        const double event_altitude_derivative =
            (event_altitude_hi - event_altitude_lo) / (2.0 * kDerivativeStepRad);
        event_slope *= event_altitude_derivative;
    }
    if (!std::isfinite(event_slope)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    out->slope_rad_per_day = event_slope;
    return TAIYIN_STATUS_OK;
}

int analytic_solar_rise_set_seed_from_noon_tt(
    const SplitJulianDate& local_noon_jd_tt,
    double latitude_rad,
    double horizon_altitude_rad,
    int event_kind,
    SplitJulianDate* out_seed
) noexcept {
    if (!out_seed) return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_NOT_FOUND;
    *out_seed = invalid_jd();
    const double tropical_year_days = DAYS_PER_TROPICAL_YEAR;
    double day_of_year = std::fmod(local_noon_jd_tt - SPLIT_JD_J2000, tropical_year_days);
    if (day_of_year < 0.0) day_of_year += tropical_year_days;
    const double phase = (day_of_year - 80.0) / tropical_year_days * TAIYIN_TWO_PI;
    const double solar_declination = 0.409092804222 * std::sin(phase);
    const double denominator = std::cos(latitude_rad) * std::cos(solar_declination);
    if (!std::isfinite(denominator) || std::fabs(denominator) < 1e-12) {
        return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_NOT_FOUND;
    }
    const double cos_hour_angle = (
        std::sin(horizon_altitude_rad)
        - std::sin(latitude_rad) * std::sin(solar_declination)) / denominator;
    if (!std::isfinite(cos_hour_angle)) {
        return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_NOT_FOUND;
    }
    if (cos_hour_angle < -1.0) {
        return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE;
    }
    if (cos_hour_angle > 1.0) {
        return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW;
    }
    const double b = TAIYIN_TWO_PI * (day_of_year - 81.0) / 364.0;
    const double equation_of_time_min = 9.87 * std::sin(2.0 * b)
        - 7.53 * std::cos(b)
        - 1.5 * std::sin(b);
    const double mean_noon_hour = 12.0 - equation_of_time_min / 60.0;
    const double hour_angle_hours = std::acos(cos_hour_angle) * 12.0 / TAIYIN_PI;
    const double event_hour = event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_RISE
        ? mean_noon_hour - hour_angle_hours
        : mean_noon_hour + hour_angle_hours;
    *out_seed = local_noon_jd_tt + (event_hour - 12.0) / 24.0;
    return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES;
}

}  // namespace

SolarVisibilityEventResult::SolarVisibilityEventResult() noexcept
    : altitude_state(TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_NOT_FOUND),
      crossing_direction(TAIYIN_SOLAR_VISIBILITY_CROSSING_ANY),
      jd_ut(invalid_jd()),
      residual_rad(NAN),
      min_residual_rad(NAN),
      max_residual_rad(NAN),
      min_residual_jd_ut(invalid_jd()),
      max_residual_jd_ut(invalid_jd()),
      sample_count(0),
      refine_count(0) {}

SolarRiseSetFastResult::SolarRiseSetFastResult() noexcept
    : altitude_state(TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_NOT_FOUND),
      rise_jd_tt(invalid_jd()),
      set_jd_tt(invalid_jd()),
      sample_count(0),
      refine_count(0) {}

SolarTransitFastResult::SolarTransitFastResult() noexcept
    : transit_jd_tt(invalid_jd()),
      altitude_rad(NAN),
      azimuth_rad(NAN),
      sample_count(0),
      refine_count(0) {}

namespace {

int public_altitude_state(int internal_state) noexcept {
    switch (internal_state) {
    case TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES:
        return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES;
    case TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE:
        return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE;
    case TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW:
        return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW;
    case TAIYIN_VISIBILITY_ALTITUDE_STATE_TANGENT:
        return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_TANGENT;
    default:
        return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_NOT_FOUND;
    }
}

int public_crossing_direction(int internal_direction) noexcept {
    switch (internal_direction) {
    case TAIYIN_VISIBILITY_CROSSING_RISING:
        return TAIYIN_SOLAR_VISIBILITY_CROSSING_RISING;
    case TAIYIN_VISIBILITY_CROSSING_SETTING:
        return TAIYIN_SOLAR_VISIBILITY_CROSSING_SETTING;
    default:
        return TAIYIN_SOLAR_VISIBILITY_CROSSING_ANY;
    }
}

void copy_result(
    const VisibilityAltitudeSearchResult& src,
    SolarVisibilityEventResult* dst
) noexcept {
    if (!dst) return;
    dst->altitude_state = public_altitude_state(src.altitude_state);
    dst->crossing_direction = public_crossing_direction(src.crossing_direction);
    dst->jd_ut = src.jd_ut;
    dst->residual_rad = src.residual_rad;
    dst->min_residual_rad = src.min_residual_rad;
    dst->max_residual_rad = src.max_residual_rad;
    dst->min_residual_jd_ut = src.min_residual_jd_ut;
    dst->max_residual_jd_ut = src.max_residual_jd_ut;
    dst->sample_count = src.sample_count;
    dst->refine_count = src.refine_count;
}

Status sample_solar_horizontal_tt(
    const NativeCalcContext* context,
    const SplitJulianDate& jd_tt,
    double longitude_rad,
    double latitude_rad,
    double height_m,
    FastApparentCorrectionSeries* corrections,
    const FastApparentCorrectionConfig* correction_config,
    double* out_hour_angle_rad,
    double* out_altitude_rad,
    double* out_azimuth_rad,
    EphemerisEvalDiagnostic* diagnostic
) {
    if (!out_hour_angle_rad || !out_altitude_rad || !out_azimuth_rad) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double tdb_minus_tt = dispatch::eval_tdb(
        context->model_context.tdb_model_id, jd_tt, 0);
    SplitJulianDate jd_tdb;
    if (!split_tdb_from_tt(jd_tt, tdb_minus_tt, &jd_tdb)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    FastApparentOptions options = solar_visibility_window_options();
    FastApparentCorrectionEpochSample correction_sample;
    if (corrections) {
        if (!correction_config) return TAIYIN_ERROR_INVALID_ARGUMENT;
        Status st = get_fast_correction(
            context, TAIYIN_BODY_SUN, 0, options, *correction_config,
            jd_tt, corrections, diagnostic, &correction_sample);
        if (st != TAIYIN_STATUS_OK) return st;
        options.correction_sample = &correction_sample;
    }
    CartesianState sun;
    Status st = eval_fast_apparent_body_tdb(
        context,
        jd_tdb,
        jd_tt,
        TAIYIN_BODY_SUN,
        options,
        &sun,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    SplitJulianDate jd_ut;
    st = eclipse_tt_to_ut(*context, jd_tt, &jd_ut, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    double gast = 0.0;
    if (!gast_model_rad(
            context->model_context.precession_model_id,
            context->model_context.nutation_model_id,
            jd_ut,
            jd_tt,
            &gast)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    const Vector3 observer = observer_geocentric_simple_position_au(
        longitude_rad,
        latitude_rad,
        height_m,
        jd_ut,
        jd_tt);
    const Vector3 topocentric = topocentric_position_au(sun.position_au, observer);
    const double ra = std::atan2(topocentric.y, topocentric.x);
    const double hour_angle = normalize_signed_radians(gast + longitude_rad - ra);
    const HorizontalCoordinates horizontal = topocentric_position_to_horizontal(
        topocentric,
        normalize_radians(gast + longitude_rad),
        latitude_rad);
    const double altitude = horizontal.altitude_rad;
    const double azimuth = horizontal.azimuth_rad;
    if (!std::isfinite(hour_angle) || !std::isfinite(altitude) || !std::isfinite(azimuth)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out_hour_angle_rad = hour_angle;
    *out_altitude_rad = altitude;
    *out_azimuth_rad = azimuth;
    return TAIYIN_STATUS_OK;
}

double transit_base_target_rad(int event_kind) noexcept {
    return event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_UPPER_TRANSIT ? 0.0 : TAIYIN_PI;
}

struct SolarTransitSampleData {
    const NativeCalcContext* context;
};

Status sample_solar_hour_angle_ut(
    const void* user_data,
    const SplitJulianDate& jd_ut,
    double reference_hour_angle_rad,
    bool has_reference,
    double* out_hour_angle_rad,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_hour_angle_rad) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out_hour_angle_rad = NAN;
    const SolarTransitSampleData* data = static_cast<const SolarTransitSampleData*>(user_data);
    if (!data || !data->context) return TAIYIN_ERROR_INVALID_ARGUMENT;
    double altitude = 0.0;
    double azimuth = 0.0;
    double hour_angle = 0.0;
    double distance = 0.0;
    const Status st = visibility_sample_body_center_horizontal_ut(
        data->context,
        TAIYIN_BODY_SUN,
        jd_ut,
        0u,
        &altitude,
        &azimuth,
        &hour_angle,
        &distance,
        0,
        0,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    double continuous_hour_angle = hour_angle;
    if (has_reference) {
        continuous_hour_angle = reference_hour_angle_rad
            + normalize_signed_radians(hour_angle - reference_hour_angle_rad);
    }
    if (!std::isfinite(continuous_hour_angle)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    *out_hour_angle_rad = continuous_hour_angle;
    (void)altitude;
    (void)azimuth;
    (void)distance;
    return TAIYIN_STATUS_OK;
}

}  // namespace

Status compute_solar_transit_fast_tt(
    const NativeCalcContext* context,
    const SplitJulianDate& center_jd_tt,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    SolarTransitFastResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = SolarTransitFastResult();
    if (!context || !out || !split_julian_date_is_finite(center_jd_tt)
        || !std::isfinite(longitude_deg) || !std::isfinite(latitude_deg)
        || latitude_deg < -90.0 || latitude_deg > 90.0
        || !std::isfinite(height_m)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const SplitJulianDate transit_center_tt = local_mean_time_target_tt(
        center_jd_tt, longitude_deg, 0.0);

    const FastApparentOptions window_options = solar_visibility_window_options();
    FastApparentCorrectionConfig correction_config;
    correction_config.initial_half_days = 3.0 / 24.0;
    correction_config.sample_step_days = 3.0 / 24.0;
    FastApparentCorrectionSeries corrections;

    const double lon_rad = longitude_deg * TAIYIN_DEG_TO_RAD;
    const double lat_rad = latitude_deg * TAIYIN_DEG_TO_RAD;
    NativeCalcContext location_context = *context;
    const Status location_status = native_context_set_observer_location(
        &location_context,
        native_observer_location_degrees(longitude_deg, latitude_deg, height_m));
    if (location_status != TAIYIN_STATUS_OK) return location_status;
    auto sample = [&](const SplitJulianDate& jd_tt, double reference_hour_angle, bool has_reference,
                      double* hour_angle, double* altitude, double* azimuth) noexcept -> Status {
        Status st = sample_solar_horizontal_tt(
            context,
            jd_tt,
            lon_rad,
            lat_rad,
            height_m,
            &corrections,
            &correction_config,
            hour_angle,
            altitude,
            azimuth,
            diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        if (has_reference) {
            *hour_angle = reference_hour_angle + normalize_signed_radians(*hour_angle - reference_hour_angle);
        }
        ++out->sample_count;
        return TAIYIN_STATUS_OK;
    };

    auto sample_exact = [&](const SplitJulianDate& jd_tt, double reference_hour_angle, bool has_reference,
                            double* hour_angle, double* altitude, double* azimuth) noexcept -> Status {
        SplitJulianDate jd_ut;
        Status st = eclipse_tt_to_ut(location_context, jd_tt, &jd_ut, nullptr, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        double distance = 0.0;
        st = visibility_sample_body_center_horizontal_ut(
            &location_context,
            TAIYIN_BODY_SUN,
            jd_ut,
            0u,
            altitude,
            azimuth,
            hour_angle,
            &distance,
            nullptr,
            nullptr,
            diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        if (has_reference) {
            *hour_angle = reference_hour_angle + normalize_signed_radians(*hour_angle - reference_hour_angle);
        }
        ++out->sample_count;
        return TAIYIN_STATUS_OK;
    };

    const double predictor_step = 1.0 / 24.0;
    double h_minus = 0.0;
    double h_mid = 0.0;
    double h_plus = 0.0;
    double ignored_altitude = 0.0;
    double ignored_azimuth = 0.0;
    Status st = sample(
        transit_center_tt - predictor_step,
        0.0,
        false,
        &h_minus,
        &ignored_altitude,
        &ignored_azimuth);
    if (st != TAIYIN_STATUS_OK) return st;
    st = sample(
        transit_center_tt,
        h_minus,
        true,
        &h_mid,
        &ignored_altitude,
        &ignored_azimuth);
    if (st != TAIYIN_STATUS_OK) return st;
    st = sample(
        transit_center_tt + predictor_step,
        h_mid,
        true,
        &h_plus,
        &ignored_altitude,
        &ignored_azimuth);
    if (st != TAIYIN_STATUS_OK) return st;

    const double a = (h_plus + h_minus - 2.0 * h_mid) / (2.0 * predictor_step * predictor_step);
    const double b = (h_plus - h_minus) / (2.0 * predictor_step);
    const double c = h_mid;
    double seed_dt = NAN;
    if (std::isfinite(a) && std::isfinite(b) && std::isfinite(c) && std::fabs(b) > 1e-12) {
        seed_dt = -c / b;
        if (std::fabs(a) > 1e-12) {
            const double discriminant = b * b - 4.0 * a * c;
            if (discriminant >= 0.0) {
                const double sqrt_discriminant = std::sqrt(discriminant);
                const double r0 = (-b - sqrt_discriminant) / (2.0 * a);
                const double r1 = (-b + sqrt_discriminant) / (2.0 * a);
                if (std::fabs(r0) < std::fabs(seed_dt)) seed_dt = r0;
                if (std::fabs(r1) < std::fabs(seed_dt)) seed_dt = r1;
            }
        }
    }
    if (std::isfinite(seed_dt) && std::fabs(seed_dt) <= 0.25) {
        SplitJulianDate previous_jd = transit_center_tt + seed_dt - 1.0 / 1440.0;
        SplitJulianDate refined_jd = transit_center_tt + seed_dt;
        double previous_hour_angle = 0.0;
        double current_hour_angle = 0.0;
        bool ok = true;
        st = sample_exact(
            previous_jd,
            h_mid,
            true,
            &previous_hour_angle,
            &ignored_altitude,
            &ignored_azimuth);
        if (st != TAIYIN_STATUS_OK) ok = false;
        if (ok) {
            st = sample_exact(
                refined_jd,
                previous_hour_angle,
                true,
                &current_hour_angle,
                &ignored_altitude,
                &ignored_azimuth);
            if (st != TAIYIN_STATUS_OK) ok = false;
        }
        for (int i = 0; i < 5; ++i) {
            if (!ok || std::fabs(current_hour_angle) < 1e-8) break;
            const double denom = current_hour_angle - previous_hour_angle;
            if (!std::isfinite(denom) || std::fabs(denom) < 1e-12) {
                ok = false;
                break;
            }
            const SplitJulianDate next_jd = refined_jd
                - current_hour_angle * (refined_jd - previous_jd) / denom;
            ++out->refine_count;
            if (!split_julian_date_is_finite(next_jd)
                || std::fabs(next_jd - transit_center_tt) > 0.25) {
                ok = false;
                break;
            }
            previous_jd = refined_jd;
            previous_hour_angle = current_hour_angle;
            refined_jd = next_jd;
            st = sample_exact(
                refined_jd,
                previous_hour_angle,
                true,
                &current_hour_angle,
                &ignored_altitude,
                &ignored_azimuth);
            if (st != TAIYIN_STATUS_OK) ok = false;
        }
        if (ok) {
            double hour_angle = 0.0;
            st = sample_exact(
                refined_jd,
                current_hour_angle,
                true,
                &hour_angle,
                &out->altitude_rad,
                &out->azimuth_rad);
            if (st == TAIYIN_STATUS_OK && std::fabs(hour_angle) < 1e-8) {
                out->transit_jd_tt = refined_jd;
                return TAIYIN_STATUS_OK;
            }
        }
    }

    const double step = 1.0 / 24.0;
    const SplitJulianDate search_start = transit_center_tt - 0.25;
    const SplitJulianDate search_end = transit_center_tt + 0.25;
    SplitJulianDate previous_jd = search_start;
    double previous_hour_angle = 0.0;
    st = sample(
        previous_jd,
        0.0,
        false,
        &previous_hour_angle,
        &ignored_altitude,
        &ignored_azimuth);
    if (st != TAIYIN_STATUS_OK) return st;
    bool found = false;
    SplitJulianDate lo_jd = invalid_jd();
    SplitJulianDate hi_jd = invalid_jd();
    double lo_hour_angle = NAN;
    double hi_hour_angle = NAN;
    SplitJulianDate jd = previous_jd + step;
    for (;;) {
        const SplitJulianDate current_jd = std::min(search_end, jd);
        double current_hour_angle = 0.0;
        st = sample(
            current_jd,
            previous_hour_angle,
            true,
            &current_hour_angle,
            &ignored_altitude,
            &ignored_azimuth);
        if (st != TAIYIN_STATUS_OK) return st;
        if ((previous_hour_angle <= 0.0 && current_hour_angle >= 0.0)
            || (previous_hour_angle >= 0.0 && current_hour_angle <= 0.0)) {
            lo_jd = previous_jd;
            hi_jd = current_jd;
            lo_hour_angle = previous_hour_angle;
            hi_hour_angle = current_hour_angle;
            found = true;
            break;
        }
        previous_jd = current_jd;
        previous_hour_angle = current_hour_angle;
        if (!(current_jd < search_end)) break;
        const SplitJulianDate next_jd = jd + step;
        jd = next_jd > jd ? next_jd : search_end;
    }

    if (found) {
        for (int i = 0; i < 24; ++i) {
            const SplitJulianDate mid_jd = lo_jd + 0.5 * (hi_jd - lo_jd);
            double mid_hour_angle = 0.0;
            st = sample(
                mid_jd,
                0.5 * (lo_hour_angle + hi_hour_angle),
                true,
                &mid_hour_angle,
                &ignored_altitude,
                &ignored_azimuth);
            if (st != TAIYIN_STATUS_OK) return st;
            ++out->refine_count;
            if ((lo_hour_angle <= 0.0 && mid_hour_angle >= 0.0)
                || (lo_hour_angle >= 0.0 && mid_hour_angle <= 0.0)) {
                hi_jd = mid_jd;
                hi_hour_angle = mid_hour_angle;
            } else {
                lo_jd = mid_jd;
                lo_hour_angle = mid_hour_angle;
            }
        }
        out->transit_jd_tt = lo_jd + 0.5 * (hi_jd - lo_jd);
        double hour_angle = 0.0;
        st = sample(
            out->transit_jd_tt,
            0.0,
            false,
            &hour_angle,
            &out->altitude_rad,
            &out->azimuth_rad);
        if (st != TAIYIN_STATUS_OK) return st;
        return TAIYIN_STATUS_OK;
    }

    SplitJulianDate start_ut;
    SplitJulianDate end_ut;
    st = eclipse_tt_to_ut(location_context, center_jd_tt - 0.5, &start_ut, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    st = eclipse_tt_to_ut(location_context, center_jd_tt + 0.5, &end_ut, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    SolarVisibilityEventResult transit_result;
    st = search_solar_transit_ut(
        &location_context,
        start_ut,
        end_ut,
        TAIYIN_SOLAR_VISIBILITY_EVENT_UPPER_TRANSIT,
        &transit_result,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    if (!split_julian_date_is_finite(transit_result.jd_ut)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    st = eclipse_ut_to_tt(location_context, transit_result.jd_ut, &out->transit_jd_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    double hour_angle = 0.0;
    return sample(
        out->transit_jd_tt,
        0.0,
        false,
        &hour_angle,
        &out->altitude_rad,
        &out->azimuth_rad);
}

Status compute_solar_rise_set_fast_tt(
    const NativeCalcContext* context,
    const SplitJulianDate& center_jd_tt,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    int limb_kind,
    double horizon_altitude_rad,
    uint64_t solar_visibility_flags,
    SolarRiseSetFastResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = SolarRiseSetFastResult();
    uint64_t resolved_visibility_flags = 0u;
    if (!resolve_public_solar_visibility_flags(
            solar_visibility_flags, &resolved_visibility_flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (!context || !out || !split_julian_date_is_finite(center_jd_tt)
        || !std::isfinite(longitude_deg) || !std::isfinite(latitude_deg)
        || latitude_deg < -90.0 || latitude_deg > 90.0
        || !std::isfinite(height_m) || !std::isfinite(horizon_altitude_rad)
        || !valid_limb_kind(limb_kind)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    NativeCalcContext location_context = *context;
    const Status location_status = native_context_set_observer_location(
        &location_context,
        native_observer_location_degrees(longitude_deg, latitude_deg, height_m));
    if (location_status != TAIYIN_STATUS_OK) return location_status;
    context = &location_context;
    if ((resolved_visibility_flags & TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION) != 0u
        && (resolved_visibility_flags & TAIYIN_SOLAR_VISIBILITY_STRICT_METEOROLOGY) != 0u) {
        NativeAtmosphere atmosphere;
        if (!native_context_resolve_refraction_atmosphere(*context, false, &atmosphere)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
    }
    const double physical_radius_km = ::taiyin::internal::body_disc_radius_km(
        TAIYIN_BODY_SUN, ::taiyin::internal::BodyDiscRadiusConvention::MeanPhysical);
    SplitJulianDate center_ut;
    Status st = eclipse_tt_to_ut(*context, center_jd_tt, &center_ut, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    const SplitJulianDate search_start_ut = center_ut - 0.5;
    const SplitJulianDate search_end_ut = center_ut + 0.5;
    SplitJulianDate search_start_tt;
    SplitJulianDate search_end_tt;
    st = eclipse_ut_to_tt(*context, search_start_ut, &search_start_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    st = eclipse_ut_to_tt(*context, search_end_ut, &search_end_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    // Julian-date fractions are measured from noon: .75 is local 06:00 and .25 is local 18:00.
    const SplitJulianDate rise_center_tt = local_mean_time_target_tt(
        center_jd_tt, longitude_deg, 0.75);
    const SplitJulianDate set_center_tt = local_mean_time_target_tt(
        center_jd_tt, longitude_deg, 0.25);
    const bool high_latitude_fallback = std::fabs(latitude_deg) > kSolarRiseSetFast2MaxAbsLatitudeDeg;

    const double lon_rad = longitude_deg * TAIYIN_DEG_TO_RAD;
    const double lat_rad = latitude_deg * TAIYIN_DEG_TO_RAD;
    auto solve_fast2_event = [&](int event_kind, SplitJulianDate* out_jd_tt) noexcept -> Status {
        if (!out_jd_tt) return TAIYIN_ERROR_INVALID_ARGUMENT;
        *out_jd_tt = invalid_jd();
        const double local_offset_days = longitude_deg / 360.0;
        const SplitJulianDate origin_jd_tt = search_start_tt;
        const SplitJulianDate local_origin = origin_jd_tt + local_offset_days;
        const int64_t origin_noon_index = local_origin.day_number
            + static_cast<int64_t>(std::floor(local_origin.day_fraction + 0.5));
        auto seed_for_noon_index = [&](int64_t noon_index, SplitJulianDate* out_seed) noexcept -> int {
            if (!out_seed) return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_NOT_FOUND;
            *out_seed = invalid_jd();
            const SplitJulianDate local_noon_tt = SplitJulianDate(noon_index, 0.0)
                - local_offset_days;
            return analytic_solar_rise_set_seed_from_noon_tt(
                local_noon_tt,
                lat_rad,
                horizon_altitude_rad,
                event_kind,
                out_seed);
        };
        auto try_seed = [&](const SplitJulianDate& seed, SplitJulianDate* root_out) noexcept -> Status {
            if (!root_out) return TAIYIN_ERROR_INVALID_ARGUMENT;
            *root_out = invalid_jd();
            if (!split_julian_date_is_finite(seed)) return TAIYIN_STATUS_OK;
            SplitJulianDate t = seed;
            const int iteration_count =
                (resolved_visibility_flags & TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION) != 0u
                    ? 3
                    : 2;
            for (int i = 0; i < iteration_count; ++i) {
                SolarRiseSetFast2Sample sample;
                Status sample_status = sample_solar_rise_set_fast2_no_window(
                    context,
                    t,
                    lon_rad,
                    lat_rad,
                    height_m,
                    limb_kind,
                    horizon_altitude_rad,
                    resolved_visibility_flags,
                    physical_radius_km,
                    &sample,
                    diagnostic);
                if (sample_status != TAIYIN_STATUS_OK) return sample_status;
                ++out->sample_count;
                const bool direction_ok = event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_RISE
                    ? sample.slope_rad_per_day > 0.2
                    : sample.slope_rad_per_day < -0.2;
                if (!direction_ok || std::fabs(sample.slope_rad_per_day) < 0.2) {
                    return TAIYIN_STATUS_OK;
                }
                const SplitJulianDate next_t = t - sample.residual_rad / sample.slope_rad_per_day;
                if (!split_julian_date_is_finite(next_t)
                    || next_t < search_start_tt - 0.05
                    || next_t > search_end_tt + 0.05
                    || std::fabs(next_t - t) > 0.25) {
                    return TAIYIN_STATUS_OK;
                }
                if (i == iteration_count - 1) {
                    *root_out = next_t;
                    return TAIYIN_STATUS_OK;
                }
                t = next_t;
            }
            return TAIYIN_STATUS_OK;
        };
        auto solve_noon_root = [&](int64_t noon_index, SplitJulianDate* root_out) noexcept -> Status {
            if (!root_out) return TAIYIN_ERROR_INVALID_ARGUMENT;
            *root_out = invalid_jd();
            SplitJulianDate seed = invalid_jd();
            const int estimated_state = seed_for_noon_index(noon_index, &seed);
            if (estimated_state != TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES
                || !split_julian_date_is_finite(seed)) {
                return TAIYIN_STATUS_OK;
            }
            return try_seed(seed, root_out);
        };
        auto accept_if_valid = [&](const SplitJulianDate& root) noexcept -> bool {
            if (!(root >= search_start_tt && root <= search_end_tt)) return false;
            if (!split_julian_date_is_finite(*out_jd_tt) || root < *out_jd_tt) {
                *out_jd_tt = root;
            }
            return true;
        };
        SplitJulianDate seed0 = invalid_jd();
        const int seed0_state = seed_for_noon_index(origin_noon_index, &seed0);
        if (seed0_state != TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES
            || !split_julian_date_is_finite(seed0)) {
            return TAIYIN_STATUS_OK;
        }
        constexpr double kFast2PastSeedGuardDays = 1.0 / 24.0;
        const bool seed_before_origin = seed0 < origin_jd_tt;
        const bool near_origin_past_seed = seed_before_origin
            && origin_jd_tt - seed0 <= kFast2PastSeedGuardDays;

        auto solve_and_accept = [&](int64_t noon_index, SplitJulianDate* root_out) noexcept -> Status {
            SplitJulianDate root = invalid_jd();
            const Status solve_status = solve_noon_root(noon_index, &root);
            if (solve_status != TAIYIN_STATUS_OK) return solve_status;
            if (root_out) *root_out = root;
            accept_if_valid(root);
            return TAIYIN_STATUS_OK;
        };

        if (seed_before_origin && !near_origin_past_seed) {
            SplitJulianDate root1 = invalid_jd();
            st = solve_and_accept(origin_noon_index + 1, &root1);
            if (st != TAIYIN_STATUS_OK) return st;
            return TAIYIN_STATUS_OK;
        }

        SplitJulianDate root0 = invalid_jd();
        st = solve_and_accept(origin_noon_index, &root0);
        if (st != TAIYIN_STATUS_OK) return st;
        if (near_origin_past_seed
            || (split_julian_date_is_finite(root0) && root0 < search_start_tt)) {
            SplitJulianDate root1 = invalid_jd();
            st = solve_and_accept(origin_noon_index + 1, &root1);
            if (st != TAIYIN_STATUS_OK) return st;
        }
        return TAIYIN_STATUS_OK;
    };

    if (std::fabs(latitude_deg) <= kSolarRiseSetFast2MaxAbsLatitudeDeg) {
        SplitJulianDate rise_fast2 = invalid_jd();
        SplitJulianDate set_fast2 = invalid_jd();
        st = solve_fast2_event(TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, &rise_fast2);
        if (st != TAIYIN_STATUS_OK) return st;
        st = solve_fast2_event(TAIYIN_SOLAR_VISIBILITY_EVENT_SET, &set_fast2);
        if (st != TAIYIN_STATUS_OK) return st;
        if (split_julian_date_is_finite(rise_fast2)
            || split_julian_date_is_finite(set_fast2)) {
            out->rise_jd_tt = rise_fast2;
            out->set_jd_tt = set_fast2;
            out->altitude_state = TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES;
            return TAIYIN_STATUS_OK;
        }
        out->sample_count = 0;
        out->refine_count = 0;
    }

    const FastApparentOptions window_options = solar_visibility_window_options();
    FastApparentCorrectionConfig correction_config;
    correction_config.initial_half_days = 3.0 / 24.0;
    correction_config.sample_step_days = 3.0 / 24.0;
    FastApparentCorrectionSeries corrections;
    FastApparentCorrectionConfig active_correction_config = correction_config;
    bool rise_window = false;
    bool set_window = false;
    if (!high_latitude_fallback) {
        FastApparentCorrectionEpochSample sample;
        rise_window = get_fast_correction(
            context, TAIYIN_BODY_SUN, 0, window_options, active_correction_config,
            rise_center_tt, &corrections, diagnostic, &sample) == TAIYIN_STATUS_OK;
        set_window = get_fast_correction(
            context, TAIYIN_BODY_SUN, 0, window_options, active_correction_config,
            set_center_tt, &corrections, diagnostic, &sample) == TAIYIN_STATUS_OK;
    }

    constexpr int kPolarWindowCount = 4;
    if (high_latitude_fallback) {
        const double interval_days = search_end_tt - search_start_tt;
        const double window_days = interval_days / static_cast<double>(kPolarWindowCount);
        const double window_half_days = 0.5 * window_days + 5.0 / 1440.0;
        FastApparentCorrectionConfig polar_config = correction_config;
        polar_config.initial_half_days = window_half_days;
        active_correction_config = polar_config;
        corrections = FastApparentCorrectionSeries();
        for (int i = 0; i < kPolarWindowCount; ++i) {
            const SplitJulianDate center = search_start_tt
                + (static_cast<double>(i) + 0.5) * window_days;
            FastApparentCorrectionEpochSample sample;
            get_fast_correction(
                context, TAIYIN_BODY_SUN, 0, window_options, active_correction_config,
                center, &corrections, diagnostic, &sample);
        }
    }

    auto sampled_crossing_slope = [&](const SplitJulianDate& jd_tt, double* out_slope) -> Status {
        if (!out_slope) return TAIYIN_ERROR_INVALID_ARGUMENT;
        *out_slope = NAN;
        double dec = 0.0;
        const Status st = sample_solar_declination_tt(
            context, jd_tt, &corrections, &active_correction_config, &dec, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        const double denominator = std::cos(lat_rad) * std::cos(dec);
        if (!std::isfinite(denominator) || std::fabs(denominator) < 1e-12) return TAIYIN_STATUS_OK;
        const double cos_hour_angle = (
            std::sin(horizon_altitude_rad)
            - std::sin(lat_rad) * std::sin(dec)) / denominator;
        if (std::isfinite(cos_hour_angle) && std::fabs(cos_hour_angle) <= 1.0) {
            const double sin_hour_angle = std::sqrt(std::max(0.0, 1.0 - cos_hour_angle * cos_hour_angle));
            *out_slope = TAIYIN_TWO_PI * std::fabs(denominator) * sin_hour_angle;
        }
        return TAIYIN_STATUS_OK;
    };
    double rise_crossing_slope = NAN;
    double set_crossing_slope = NAN;
    if (rise_window) {
        st = sampled_crossing_slope(rise_center_tt, &rise_crossing_slope);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    if (set_window) {
        st = sampled_crossing_slope(set_center_tt, &set_crossing_slope);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    auto residual_at = [&](const SplitJulianDate& jd_tt, bool exact, double* value) -> Status {
        const double tdb_minus_tt = dispatch::eval_tdb(
            context->model_context.tdb_model_id, jd_tt, 0);
        SplitJulianDate jd_tdb;
        if (!split_tdb_from_tt(jd_tt, tdb_minus_tt, &jd_tdb)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        FastApparentOptions options = solar_visibility_window_options();
        FastApparentCorrectionEpochSample correction_sample;
        if (!exact) {
            st = get_fast_correction(
                context, TAIYIN_BODY_SUN, 0, options, active_correction_config,
                jd_tt, &corrections, diagnostic, &correction_sample);
            if (st != TAIYIN_STATUS_OK) return st;
            options.correction_sample = &correction_sample;
        }
        CartesianState sun;
        st = eval_fast_apparent_body_tdb(
            context,
            jd_tdb,
            jd_tt,
            TAIYIN_BODY_SUN,
            options,
            &sun,
            diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        SplitJulianDate jd_ut;
        st = eclipse_tt_to_ut(*context, jd_tt, &jd_ut, nullptr, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        double gast = 0.0;
        if (!gast_model_rad(
                context->model_context.precession_model_id,
                context->model_context.nutation_model_id,
                jd_ut,
                jd_tt,
                &gast)) {
            return TAIYIN_ERROR_UNSUPPORTED;
        }
        const Vector3 observer = observer_geocentric_simple_position_au(
            lon_rad,
            lat_rad,
            height_m,
            jd_ut,
            jd_tt);
        const Vector3 topocentric = topocentric_position_au(sun.position_au, observer);
        const double altitude = topocentric_altitude_rad(topocentric, normalize_radians(gast + lon_rad), lat_rad);
        if (!std::isfinite(altitude)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        double event_altitude = NAN;
        st = solar_rise_set_event_altitude(
            context,
            topocentric,
            altitude,
            limb_kind,
            resolved_visibility_flags,
            physical_radius_km,
            &event_altitude);
        if (st != TAIYIN_STATUS_OK) return st;
        ++out->sample_count;
        *value = event_altitude - horizon_altitude_rad;
        return TAIYIN_STATUS_OK;
    };
    auto bisect_at = [&](SplitJulianDate lo, SplitJulianDate hi, double flo, bool exact,
            SplitJulianDate* out_jd_tt) -> Status {
        for (int i = 0; i < 24; ++i) {
            const SplitJulianDate mid = lo + 0.5 * (hi - lo);
            double fm = 0.0;
            const Status st = residual_at(mid, exact, &fm);
            if (st != TAIYIN_STATUS_OK) return st;
            ++out->refine_count;
            if ((flo <= 0.0 && fm > 0.0) || (flo >= 0.0 && fm < 0.0)) {
                hi = mid;
            } else {
                lo = mid;
                flo = fm;
            }
        }
        *out_jd_tt = lo + 0.5 * (hi - lo);
        return TAIYIN_STATUS_OK;
    };
    auto solve_fast_bracket = [&](const SplitJulianDate& lo, const SplitJulianDate& hi,
        double flo, double fhi,
            SplitJulianDate* out_jd_tt) -> Status {
        if (!out_jd_tt) return TAIYIN_ERROR_INVALID_ARGUMENT;
        SplitJulianDate left = lo;
        SplitJulianDate right = hi;
        double f_left = flo;
        double f_right = fhi;
        SplitJulianDate root = left + 0.5 * (right - left);
        for (int i = 0; i < 8; ++i) {
            const double denom = f_right - f_left;
            if (!std::isfinite(denom) || std::fabs(denom) < 1e-14) break;
            root = right - f_right * (right - left) / denom;
            if (!split_julian_date_is_finite(root)
                || root <= left + 1e-12
                || root >= right - 1e-12) {
                root = left + 0.5 * (right - left);
            }
            double f_root = 0.0;
            const Status st = residual_at(root, false, &f_root);
            if (st != TAIYIN_STATUS_OK) return st;
            ++out->refine_count;
            if (std::fabs(f_root) < 1e-8 || std::fabs(right - left) < 1e-9) {
                *out_jd_tt = root;
                return TAIYIN_STATUS_OK;
            }
            if ((f_left <= 0.0 && f_root >= 0.0) || (f_left >= 0.0 && f_root <= 0.0)) {
                right = root;
                f_right = f_root;
            } else {
                left = root;
                f_left = f_root;
            }
        }
        *out_jd_tt = root;
        return TAIYIN_STATUS_OK;
    };

    auto validate_fast_root = [&](int event_kind, const SplitJulianDate& root) -> Status {
        if (!split_julian_date_is_finite(root)) return TAIYIN_ERROR_INVALID_ARGUMENT;
        constexpr double kValidationStepDays = 15.0 / 86400.0;
        const SplitJulianDate lo = std::max(search_start_tt, root - kValidationStepDays);
        const SplitJulianDate hi = std::min(search_end_tt, root + kValidationStepDays);
        if (!(hi > lo)) return TAIYIN_STATUS_OK;
        double flo = 0.0;
        double fhi = 0.0;
        Status st = residual_at(lo, true, &flo);
        if (st != TAIYIN_STATUS_OK) return st;
        st = residual_at(hi, true, &fhi);
        if (st != TAIYIN_STATUS_OK) return st;
        constexpr double kValidationResidualToleranceRad = kDefaultResidualToleranceRad;
        if (event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_RISE) {
            return flo < -kValidationResidualToleranceRad && fhi > kValidationResidualToleranceRad
                ? TAIYIN_STATUS_OK
                : TAIYIN_EVENT_ERROR_NOT_FOUND;
        }
        return flo > kValidationResidualToleranceRad && fhi < -kValidationResidualToleranceRad
            ? TAIYIN_STATUS_OK
            : TAIYIN_EVENT_ERROR_NOT_FOUND;
    };
    auto shallow_analytic_crossing = [](double crossing_slope_rad_per_day) noexcept -> bool {
        return !std::isfinite(crossing_slope_rad_per_day) || crossing_slope_rad_per_day < 1.0;
    };

    auto try_fast_seed = [&](int event_kind, const SplitJulianDate& seed,
        SplitJulianDate* out_jd_tt) -> Status {
        if (!out_jd_tt) return TAIYIN_ERROR_INVALID_ARGUMENT;
        if (!split_julian_date_is_finite(seed)
            || seed < search_start_tt - 0.25 || seed > search_end_tt + 0.25) {
            return TAIYIN_STATUS_OK;
        }

        const double widths[] = {
            5.0 / 1440.0,
            15.0 / 1440.0,
            1.0 / 24.0,
            2.0 / 24.0,
            4.0 / 24.0,
            6.0 / 24.0,
        };
        for (double width : widths) {
            const SplitJulianDate lo = std::max(search_start_tt, seed - width);
            const SplitJulianDate hi = std::min(search_end_tt, seed + width);
            if (!(hi > lo)) continue;
            double flo = 0.0;
            double fhi = 0.0;
            Status st = residual_at(lo, false, &flo);
            if (st != TAIYIN_STATUS_OK) return st;
            st = residual_at(hi, false, &fhi);
            if (st != TAIYIN_STATUS_OK) return st;
            const bool bracket = event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_RISE
                ? (flo <= 0.0 && fhi >= 0.0)
                : (flo >= 0.0 && fhi <= 0.0);
            if (!bracket) continue;
            SplitJulianDate root;
            st = solve_fast_bracket(lo, hi, flo, fhi, &root);
            if (st != TAIYIN_STATUS_OK) return st;
            const double abs_lat_deg = std::fabs(latitude_deg);
            const bool validate_root = abs_lat_deg > 66.0
                || shallow_analytic_crossing(
                    event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_RISE
                        ? rise_crossing_slope
                        : set_crossing_slope);
            if (validate_root) {
                st = validate_fast_root(event_kind, root);
                if (st == TAIYIN_EVENT_ERROR_NOT_FOUND) continue;
                if (st != TAIYIN_STATUS_OK) return st;
            }
            if (!split_julian_date_is_finite(*out_jd_tt) || root < *out_jd_tt) {
                *out_jd_tt = root;
            }
            return TAIYIN_STATUS_OK;
        }
        return TAIYIN_STATUS_OK;
    };
    const SplitJulianDate local_center = center_jd_tt + longitude_deg / 360.0;
    const int64_t local_midnight_day = local_center.day_number
        + static_cast<int64_t>(std::floor(local_center.day_fraction - 0.5));
    const SplitJulianDate base_local_midnight_tt = SplitJulianDate(local_midnight_day, 0.5)
        - longitude_deg / 360.0;
    auto local_time_jd = [&](double hour) noexcept -> SplitJulianDate {
        return base_local_midnight_tt + hour / 24.0;
    };
    auto analytic_seed = [&](int event_kind, SplitJulianDate* out_seed) noexcept -> int {
        if (!out_seed) return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_NOT_FOUND;
        *out_seed = invalid_jd();
        const double tropical_year_days = DAYS_PER_TROPICAL_YEAR;
        double day_of_year = std::fmod(center_jd_tt - SPLIT_JD_J2000, tropical_year_days);
        if (day_of_year < 0.0) day_of_year += tropical_year_days;
        const double phase = (day_of_year - 80.0) / tropical_year_days * TAIYIN_TWO_PI;
        const double solar_declination = 0.409092804222 * std::sin(phase);
        const double denominator = std::cos(lat_rad) * std::cos(solar_declination);
        if (!std::isfinite(denominator) || std::fabs(denominator) < 1e-12) {
            return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_NOT_FOUND;
        }
        const double cos_hour_angle = (
            std::sin(horizon_altitude_rad)
            - std::sin(lat_rad) * std::sin(solar_declination)) / denominator;
        if (!std::isfinite(cos_hour_angle)) {
            return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_NOT_FOUND;
        }
        if (cos_hour_angle < -1.0) {
            return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE;
        }
        if (cos_hour_angle > 1.0) {
            return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW;
        }

        const double b = TAIYIN_TWO_PI * (day_of_year - 81.0) / 364.0;
        const double equation_of_time_min = 9.87 * std::sin(2.0 * b)
            - 7.53 * std::cos(b)
            - 1.5 * std::sin(b);
        const double mean_noon_hour = 12.0 - equation_of_time_min / 60.0;
        const double hour_angle_hours = std::acos(cos_hour_angle) * 12.0 / TAIYIN_PI;
        const double event_hour = event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_RISE
            ? mean_noon_hour - hour_angle_hours
            : mean_noon_hour + hour_angle_hours;
        *out_seed = local_time_jd(event_hour);
        return TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES;
    };
    auto try_predict_event = [&](int event_kind, SplitJulianDate* out_jd_tt) -> Status {
        if (!out_jd_tt) return TAIYIN_ERROR_INVALID_ARGUMENT;
        *out_jd_tt = invalid_jd();

        SplitJulianDate seed = invalid_jd();
        const int estimated_state = analytic_seed(event_kind, &seed);
        if (estimated_state == TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES) {
            const double offsets[] = {0.0, -1.0, 1.0};
            for (double offset : offsets) {
                const Status seed_status = try_fast_seed(event_kind, seed + offset, out_jd_tt);
                if (seed_status != TAIYIN_STATUS_OK) return seed_status;
            }
            if (split_julian_date_is_finite(*out_jd_tt)) return TAIYIN_STATUS_OK;
        } else if (estimated_state == TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE
                   || estimated_state == TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW) {
            return TAIYIN_STATUS_OK;
        }

        const double h0 = event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_RISE ? 5.0 : 18.0;
        const double h1 = event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_RISE ? 6.0 : 19.0;
        const SplitJulianDate t0 = local_time_jd(h0);
        const SplitJulianDate t1 = local_time_jd(h1);
        double f0 = 0.0;
        double f1 = 0.0;
        Status st = residual_at(t0, false, &f0);
        if (st != TAIYIN_STATUS_OK) return st;
        st = residual_at(t1, false, &f1);
        if (st != TAIYIN_STATUS_OK) return st;
        const double slope = f1 - f0;
        const bool direction_ok = event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_RISE
            ? slope > 0.0
            : slope < 0.0;
        if (!direction_ok || !std::isfinite(slope) || std::fabs(slope) < 1e-8) {
            return TAIYIN_STATUS_OK;
        }
        seed = t0 - f0 * (t1 - t0) / slope;
        return try_fast_seed(event_kind, seed, out_jd_tt);
    };

    if (!high_latitude_fallback) {
        st = try_predict_event(TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, &out->rise_jd_tt);
        if (st != TAIYIN_STATUS_OK) return st;
        st = try_predict_event(TAIYIN_SOLAR_VISIBILITY_EVENT_SET, &out->set_jd_tt);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    const bool shallow_rise = shallow_analytic_crossing(rise_crossing_slope);
    const bool shallow_set = shallow_analytic_crossing(set_crossing_slope);
    if (split_julian_date_is_finite(out->rise_jd_tt)
        && split_julian_date_is_finite(out->set_jd_tt)
        && !shallow_rise
        && !shallow_set) {
        out->altitude_state = TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES;
        return TAIYIN_STATUS_OK;
    }

    if ((split_julian_date_is_finite(out->rise_jd_tt)
            || split_julian_date_is_finite(out->set_jd_tt))
        && !shallow_rise
        && !shallow_set) {
        out->altitude_state = TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES;
    } else {
        out->rise_jd_tt = invalid_jd();
        out->set_jd_tt = invalid_jd();
        const bool exact_fast_scan = !high_latitude_fallback && (shallow_rise || shallow_set);
        const double step = 0.01;
        SplitJulianDate previous_jd = search_start_tt;
        double previous_value = 0.0;
        st = residual_at(previous_jd, exact_fast_scan, &previous_value);
        if (st != TAIYIN_STATUS_OK) return st;
        double min_value = previous_value;
        double max_value = previous_value;
        SplitJulianDate jd = previous_jd + step;
        for (;;) {
            const SplitJulianDate current_jd = std::min(search_end_tt, jd);
            double current_value = 0.0;
            st = residual_at(current_jd, exact_fast_scan, &current_value);
            if (st != TAIYIN_STATUS_OK) return st;
            min_value = std::min(min_value, current_value);
            max_value = std::max(max_value, current_value);
            const double scan_tolerance = exact_fast_scan ? kDefaultResidualToleranceRad : 0.0;
            if (!split_julian_date_is_finite(out->rise_jd_tt)
                && previous_value < -scan_tolerance
                && current_value > scan_tolerance
                && current_value > previous_value) {
                SplitJulianDate root_jd_tt;
                st = bisect_at(previous_jd, current_jd, previous_value, exact_fast_scan, &root_jd_tt);
                if (st != TAIYIN_STATUS_OK) return st;
                st = validate_fast_root(TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, root_jd_tt);
                if (st == TAIYIN_STATUS_OK) {
                    out->rise_jd_tt = root_jd_tt;
                } else if (st != TAIYIN_EVENT_ERROR_NOT_FOUND) {
                    return st;
                }
            }
            if (!split_julian_date_is_finite(out->set_jd_tt)
                && previous_value > scan_tolerance
                && current_value < -scan_tolerance
                && current_value < previous_value) {
                SplitJulianDate root_jd_tt;
                st = bisect_at(previous_jd, current_jd, previous_value, exact_fast_scan, &root_jd_tt);
                if (st != TAIYIN_STATUS_OK) return st;
                st = validate_fast_root(TAIYIN_SOLAR_VISIBILITY_EVENT_SET, root_jd_tt);
                if (st == TAIYIN_STATUS_OK) {
                    out->set_jd_tt = root_jd_tt;
                } else if (st != TAIYIN_EVENT_ERROR_NOT_FOUND) {
                    return st;
                }
            }
            previous_jd = current_jd;
            previous_value = current_value;
            if (!(current_jd < search_end_tt)) break;
            const SplitJulianDate next_jd = jd + step;
            jd = next_jd > jd ? next_jd : search_end_tt;
        }

        if (split_julian_date_is_finite(out->rise_jd_tt)
            || split_julian_date_is_finite(out->set_jd_tt)) {
            out->altitude_state = TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES;
        } else if (min_value > 0.0) {
            out->altitude_state = TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE;
        } else if (max_value < 0.0) {
            out->altitude_state = TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW;
        } else {
            out->altitude_state = TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_TANGENT;
        }
    }

    return TAIYIN_STATUS_OK;
}

Status search_solar_rise_set_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int limb_kind,
    uint64_t solar_visibility_flags,
    SolarVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_solar_rise_set_at_horizon_ut(
        context,
        start_jd_ut,
        end_jd_ut,
        event_kind,
        limb_kind,
        0.0,
        solar_visibility_flags,
        out,
        diagnostic);
}

Status search_solar_rise_set_at_horizon_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int limb_kind,
    double horizon_altitude_rad,
    uint64_t solar_visibility_flags,
    SolarVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = SolarVisibilityEventResult();
    if (!context || !out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    uint64_t internal_flags = 0u;
    if (!resolve_public_solar_visibility_flags(solar_visibility_flags, &internal_flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    VisibilityAltitudeSearchResult internal_result;
    const Status st = solar_visibility_search_rise_set_at_horizon_ut(
        context,
        start_jd_ut,
        end_jd_ut,
        event_kind,
        limb_kind,
        horizon_altitude_rad,
        internal_flags,
        &internal_result,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    copy_result(internal_result, out);
    return TAIYIN_STATUS_OK;
}

Status search_solar_twilight_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int twilight_kind,
    SolarVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = SolarVisibilityEventResult();
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    VisibilityAltitudeSearchResult internal_result;
    const Status st = solar_visibility_search_twilight_ut(
        context,
        start_jd_ut,
        end_jd_ut,
        event_kind,
        twilight_kind,
        &internal_result,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    copy_result(internal_result, out);
    return TAIYIN_STATUS_OK;
}

Status search_solar_transit_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    SolarVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = SolarVisibilityEventResult();
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    VisibilityAltitudeSearchResult internal_result;
    const Status st = solar_visibility_search_transit_ut(
        context,
        start_jd_ut,
        end_jd_ut,
        event_kind,
        &internal_result,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    copy_result(internal_result, out);
    return TAIYIN_STATUS_OK;
}

Status solar_visibility_search_rise_set_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int limb_kind,
    uint64_t solar_visibility_flags,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return solar_visibility_search_rise_set_at_horizon_ut(
        context,
        start_jd_ut,
        end_jd_ut,
        event_kind,
        limb_kind,
        0.0,
        solar_visibility_flags,
        out,
        diagnostic);
}

Status solar_visibility_search_rise_set_at_horizon_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int limb_kind,
    double horizon_altitude_rad,
    uint64_t solar_visibility_flags,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!valid_event_kind(event_kind)
        || !valid_limb_kind(limb_kind)
        || !std::isfinite(horizon_altitude_rad)
        || (solar_visibility_flags
            & ~(TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION
                | TAIYIN_SOLAR_VISIBILITY_FLAG_FIXED_DISC_SIZE
                | TAIYIN_SOLAR_VISIBILITY_STRICT_METEOROLOGY)) != 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    VisibilityAltitudeSearchSpec spec = base_solar_spec(start_jd_ut, end_jd_ut, event_kind, horizon_altitude_rad);
    const bool use_refraction = (solar_visibility_flags & TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION) != 0u;
    const bool fixed_disc_size = (solar_visibility_flags & TAIYIN_SOLAR_VISIBILITY_FLAG_FIXED_DISC_SIZE) != 0u;
    if (use_refraction
        && (solar_visibility_flags & TAIYIN_SOLAR_VISIBILITY_STRICT_METEOROLOGY) != 0u) {
        NativeAtmosphere atmosphere;
        if (!context || !native_context_resolve_refraction_atmosphere(*context, false, &atmosphere)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
    }
    if (fixed_disc_size) {
        spec.angular_radius_distance_au = kFixedSunDistanceAu;
    }
    if (limb_kind == TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER) {
        spec.residual_mode = TAIYIN_VISIBILITY_RESIDUAL_CENTER_ALTITUDE;
        spec.observed_flags = use_refraction ? TAIYIN_OBSERVED_REFRACTION : 0u;
    } else if (limb_kind == TAIYIN_SOLAR_VISIBILITY_LIMB_LOWER) {
        spec.residual_mode = use_refraction
            ? TAIYIN_VISIBILITY_RESIDUAL_APPARENT_LOWER_LIMB
            : TAIYIN_VISIBILITY_RESIDUAL_TRUE_LOWER_LIMB;
    } else {
        spec.residual_mode = use_refraction
            ? TAIYIN_VISIBILITY_RESIDUAL_APPARENT_UPPER_LIMB
            : TAIYIN_VISIBILITY_RESIDUAL_TRUE_UPPER_LIMB;
    }
    if ((solar_visibility_flags & TAIYIN_SOLAR_VISIBILITY_STRICT_METEOROLOGY) != 0u) {
        spec.observed_flags |= TAIYIN_OBSERVED_STRICT_METEOROLOGY;
    }
    return visibility_search_altitude_interval_ut(context, spec, out, diagnostic);
}

Status solar_visibility_search_twilight_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int twilight_kind,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!valid_event_kind(event_kind)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double target_altitude = visibility_solar_twilight_altitude_rad(twilight_kind);
    if (!std::isfinite(target_altitude)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    VisibilityAltitudeSearchSpec spec = base_solar_spec(start_jd_ut, end_jd_ut, event_kind, target_altitude);
    spec.residual_mode = TAIYIN_VISIBILITY_RESIDUAL_CENTER_ALTITUDE;
    spec.observed_flags = 0u;
    spec.physical_radius_km = 0.0;
    return visibility_search_altitude_interval_ut(context, spec, out, diagnostic);
}

Status solar_visibility_search_transit_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context
        || !out
        || !valid_transit_event_kind(event_kind)
        || !split_julian_date_is_finite(start_jd_ut)
        || !split_julian_date_is_finite(end_jd_ut)
        || !(end_jd_ut > start_jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SolarTransitSampleData data = { context };
    VisibilityAngleTargetSearchSpec spec;
    spec.start_jd_ut = start_jd_ut;
    spec.end_jd_ut = end_jd_ut;
    spec.base_target_rad = transit_base_target_rad(event_kind);
    spec.coarse_step_days = kDefaultCoarseStepDays;
    spec.root_tolerance_days = kDefaultRootToleranceDays;
    spec.residual_tolerance_rad = kDefaultResidualToleranceRad;
    spec.sample = sample_solar_hour_angle_ut;
    spec.user_data = &data;
    return visibility_search_continuous_angle_target_ut(spec, out, diagnostic);
}

}  // namespace runtime
}  // namespace taiyin
