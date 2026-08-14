#include "taiyin/runtime/observed_position.h"

#include "runtime/core/native_context_checks.h"
#include "runtime/core/time_scale_diagnostic.h"

#include "taiyin/angle.h"
#include "taiyin/dispatch.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/internal/eop.h"
#include "taiyin/physical_constants.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

#include <cmath>
#include <mutex>

namespace taiyin {
namespace runtime {
namespace {

const uint64_t SUPPORTED_OBSERVED_FLAGS =
    TAIYIN_OBSERVED_CALCULATION_FLAGS_MASK
    | TAIYIN_OBSERVED_HORIZONTAL
    | TAIYIN_OBSERVED_REFRACTION
    | TAIYIN_OBSERVED_STRICT_METEOROLOGY;

void clear_observed(ObservedPosition* out, size_t count) noexcept {
    if (!out) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        out[i] = ObservedPosition();
    }
}

void set_diagnostic(
    EphemerisEvalDiagnostic* diagnostic,
    Status status,
    int target_id,
    int center_id,
    const SplitJulianDate& jd_tdb
) noexcept {
    if (!diagnostic) {
        return;
    }
    *diagnostic = EphemerisEvalDiagnostic();
    diagnostic->status = status;
    diagnostic->target_id = target_id;
    diagnostic->center_id = center_id;
    diagnostic->frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
    diagnostic->jd_tdb = jd_tdb;
}

void set_diagnostics(
    EphemerisEvalDiagnostic* diagnostics,
    size_t count,
    const int* body_ids,
    Status status,
    int center_id,
    const SplitJulianDate& jd_tdb
) noexcept {
    if (!diagnostics) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        set_diagnostic(diagnostics + i, status, body_ids ? body_ids[i] : 0, center_id, jd_tdb);
    }
}

uint32_t observed_flags_to_apparent_flags(
    uint64_t observed_flags,
    uint32_t context_flags
) noexcept {
    uint32_t flags = context_flags | TAIYIN_APPARENT_SPHERICAL;
    flags &= ~TAIYIN_APPARENT_ACCELERATION;
    if ((observed_flags & TAIYIN_OBSERVED_SPEED) != 0u) {
        flags |= TAIYIN_APPARENT_VELOCITY;
    } else {
        flags &= ~TAIYIN_APPARENT_VELOCITY;
    }
    if ((observed_flags & TAIYIN_OBSERVED_TOPOCENTRIC) != 0u) {
        flags |= TAIYIN_APPARENT_TOPOCENTRIC;
    } else {
        flags &= ~TAIYIN_APPARENT_TOPOCENTRIC;
    }
    if ((observed_flags & TAIYIN_OBSERVED_TRUEPOS) != 0u) {
        flags &= ~(TAIYIN_APPARENT_LIGHT_TIME
            | TAIYIN_APPARENT_ABERRATION
            | TAIYIN_APPARENT_DEFLECTION
            | TAIYIN_APPARENT_SHAPIRO_DELAY);
    } else if ((observed_flags & TAIYIN_OBSERVED_ASTROMETRIC) != 0u) {
        flags |= TAIYIN_APPARENT_LIGHT_TIME;
        flags &= ~(TAIYIN_APPARENT_ABERRATION
            | TAIYIN_APPARENT_DEFLECTION
            | TAIYIN_APPARENT_SHAPIRO_DELAY);
    } else {
        if ((observed_flags & TAIYIN_OBSERVED_NO_ABERR) != 0u) {
            flags &= ~TAIYIN_APPARENT_ABERRATION;
        }
        if ((observed_flags & TAIYIN_OBSERVED_NO_GDEFL) != 0u) {
            flags &= ~TAIYIN_APPARENT_DEFLECTION;
        }
    }
    return flags;
}

bool apparent_flags_need_deflectors(uint32_t apparent_flags) noexcept {
    return (apparent_flags & (
        TAIYIN_APPARENT_ABERRATION
        | TAIYIN_APPARENT_DEFLECTION
        | TAIYIN_APPARENT_SHAPIRO_DELAY)) != 0u;
}

struct ModelTopocentricMatrixData {
    int precession_model_id;
    int nutation_model_id;
    SplitJulianDate epoch_jd_ut1;
    SplitJulianDate epoch_jd_tt;
};

bool eval_true_equator_topocentric_to_icrf_matrix(
    const SplitJulianDate& jd_tt,
    const void* data,
    Matrix3x3* out_matrix
) noexcept {
    if (!data || !out_matrix) {
        return false;
    }
    const ModelTopocentricMatrixData* eval_data = static_cast<const ModelTopocentricMatrixData*>(data);
    SplitJulianDate jd_ut1;
    if (!add_days_to_split_jd(
            eval_data->epoch_jd_ut1,
            days_between_split_jd(eval_data->epoch_jd_tt, jd_tt),
            &jd_ut1)) {
        return false;
    }

    Matrix3x3 precession;
    double mean_obliquity = 0.0;
    NutationAngles nutation;
    double sidereal = 0.0;
    if (!dispatch::eval_precession(eval_data->precession_model_id, jd_tt, 0, &precession, &mean_obliquity)
        || !dispatch::eval_nutation(eval_data->nutation_model_id, jd_tt, 0, &nutation)
        || !gast_model_rad(eval_data->precession_model_id, eval_data->nutation_model_id, jd_ut1, jd_tt, &sidereal)) {
        return false;
    }
    nutation.mean_obliquity_rad = mean_obliquity;
    nutation.true_obliquity_rad = mean_obliquity + nutation.deps_rad;

    const Matrix3x3 true_to_icrf = matrix3x3_transpose(matrix3x3_multiply(nutation_matrix(nutation), precession));
    *out_matrix = matrix3x3_multiply(true_to_icrf, earth_rotation_matrix(sidereal));
    return true;
}

bool eval_model_sidereal(
    const AstroModelContext& model_context,
    const SplitJulianDate& jd_ut1,
    const SplitJulianDate& jd_tt,
    double step_days,
    double* out_sidereal,
    double* out_sidereal_rate
) noexcept {
    if (!out_sidereal || !out_sidereal_rate) {
        return false;
    }
    return gast_model_rad(
        model_context.precession_model_id,
        model_context.nutation_model_id,
        jd_ut1,
        jd_tt,
        out_sidereal)
        && gast_rate_model_rad_per_day(
            model_context.precession_model_id,
            model_context.nutation_model_id,
            jd_tt,
            0.0,
            0.0,
            step_days,
            out_sidereal_rate);
}

Status set_true_equator_model_topocentric_observer(
    NativeCalcContext* context,
    const NativeObserverLocation& location,
    const SplitJulianDate& jd_ut1,
    const SplitJulianDate& jd_tt
) noexcept {
    if (!context) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double step_days = context->apparent_options.matrix_derivative_step_days;
    const ModelTopocentricMatrixData eval_data = {
        context->model_context.precession_model_id,
        context->model_context.nutation_model_id,
        jd_ut1,
        jd_tt,
    };
    Matrix3x3 to_icrf;
    Matrix3x3 to_icrf_dot;
    Matrix3x3 to_icrf_ddot;
    if (!eval_true_equator_topocentric_to_icrf_matrix(jd_tt, &eval_data, &to_icrf)
        || !matrix_derivative_central(&eval_true_equator_topocentric_to_icrf_matrix, &eval_data, jd_tt, step_days, &to_icrf_dot)
        || !matrix_second_derivative_central(&eval_true_equator_topocentric_to_icrf_matrix, &eval_data, jd_tt, step_days, &to_icrf_ddot)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    const Vector3 ecef_au = vector3_scale(
        geodetic_to_ecef_m(location.longitude_rad, location.latitude_rad, location.height_m),
        1.0 / TAIYIN_AU_M);
    CartesianState offset;
    offset.position_au = matrix3x3_multiply_vector(to_icrf, ecef_au);
    offset.velocity_au_per_day = matrix3x3_multiply_vector(to_icrf_dot, ecef_au);
    offset.acceleration_au_per_day2 = matrix3x3_multiply_vector(to_icrf_ddot, ecef_au);

    const Status status = native_context_set_topocentric_observer_offset(context, offset);
    if (status == TAIYIN_STATUS_OK) {
        context->observer_location = location;
        context->fields.set(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION);
    }
    return status;
}

Status resolve_ut_scales(
    const NativeCalcContext& context,
    const SplitJulianDate& jd_ut,
    SplitJulianDate* out_jd_tt,
    SplitJulianDate* out_jd_tdb
) noexcept {
    if (!out_jd_tt || !out_jd_tdb || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    NativeTimeScaleCache& cache = context.time_scale_cache;
    std::lock_guard<std::recursive_mutex> cache_lock(cache.mutex);
    const uint64_t dispatch_generation = dispatch::model_registry_generation();
    if (cache.ut1_valid
        && cache.ut1_dispatch_generation == dispatch_generation
        && cache.jd_ut1 == jd_ut
        && cache.delta_t_model_id == context.delta_t_model_id
        && cache.ephemeris_family_id == context.ephemeris_family_id
        && cache.tdb_model_id == context.model_context.tdb_model_id) {
        *out_jd_tt = cache.ut1_jd_tt;
        *out_jd_tdb = cache.ut1_jd_tdb;
        return TAIYIN_STATUS_OK;
    }
    const double delta_t_seconds = dispatch::eval_delta_t_with_ephemeris_correction(
        context.delta_t_model_id,
        context.ephemeris_family_id,
        jd_ut,
        0,
        0);
    SplitJulianDate jd_tt;
    if (!ut1_to_tt_split_jd(jd_ut, delta_t_seconds, &jd_tt)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    const double tdb_minus_tt_seconds = dispatch::eval_tdb(context.model_context.tdb_model_id, jd_tt, 0);
    SplitJulianDate jd_tdb;
    if (!add_seconds_to_split_jd(jd_tt, tdb_minus_tt_seconds, &jd_tdb)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    cache.tt_valid = true;
    cache.tt_dispatch_generation = dispatch_generation;
    cache.jd_tt = jd_tt;
    cache.tdb_model_id = context.model_context.tdb_model_id;
    cache.jd_tdb = jd_tdb;
    cache.ut1_valid = true;
    cache.ut1_dispatch_generation = dispatch_generation;
    cache.jd_ut1 = jd_ut;
    cache.delta_t_model_id = context.delta_t_model_id;
    cache.ephemeris_family_id = context.ephemeris_family_id;
    cache.delta_t_seconds = delta_t_seconds;
    cache.ut1_jd_tt = jd_tt;
    cache.ut1_jd_tdb = jd_tdb;
    *out_jd_tt = jd_tt;
    *out_jd_tdb = jd_tdb;
    return TAIYIN_STATUS_OK;
}

bool same_calendar_datetime(
    const CalendarDateTime& lhs,
    const CalendarDateTime& rhs
) noexcept {
    return lhs.year == rhs.year
        && lhs.month == rhs.month
        && lhs.day == rhs.day
        && lhs.hour == rhs.hour
        && lhs.minute == rhs.minute
        && lhs.second == rhs.second;
}

Status apply_celestial_pole_offset_from_eop(
    NativeCalcContext* context,
    const SplitJulianDate& jd_utc
) noexcept {
    const internal::EarthOrientationTable* eop_table = global_earth_orientation_table();
    if (!context || !eop_table) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    internal::EarthOrientationSample eop;
    internal::EarthOrientationRates rates;
    internal::EarthRotationDerivatives derivatives;
    if (!internal::interpolate_earth_orientation(eop_table, jd_utc, &eop)
        || !internal::derive_earth_orientation_rates(eop_table, jd_utc, &rates, &derivatives)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    (void)derivatives;
    return native_context_set_celestial_pole_offset(
        context,
        eop.dx_rad,
        eop.dy_rad,
        rates.dx_rate_rad_per_day,
        rates.dy_rate_rad_per_day);
}

Status calc_observed_resolved_scales(
    const NativeCalcContext* context,
    const SplitJulianDate& diagnostic_jd,
    const SplitJulianDate& jd_ut1,
    const SplitJulianDate& jd_tt,
    const SplitJulianDate& jd_tdb,
    bool precise_topocentric,
    const SplitJulianDate& jd_utc,
    const int* body_ids,
    size_t body_count,
    uint64_t flags,
    ObservedPosition* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept {
    clear_observed(out, body_count);
    if (body_count == 0) {
        return TAIYIN_STATUS_OK;
    }
    if (!context || !body_ids || !out || body_count > TAIYIN_MAJOR_BODY_COUNT
        || !split_julian_date_is_finite(diagnostic_jd)
        || !split_julian_date_is_finite(jd_ut1)
        || !split_julian_date_is_finite(jd_tt)
        || !split_julian_date_is_finite(jd_tdb)) {
        set_diagnostics(diagnostics, body_count, body_ids, TAIYIN_ERROR_INVALID_ARGUMENT, context ? context->observer_id : 0, diagnostic_jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if ((flags & ~SUPPORTED_OBSERVED_FLAGS) != 0u) {
        set_diagnostics(diagnostics, body_count, body_ids, TAIYIN_ERROR_UNSUPPORTED, context->observer_id, diagnostic_jd);
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    const bool want_topocentric = (flags & TAIYIN_OBSERVED_TOPOCENTRIC) != 0u;
    const bool want_horizontal = (flags & (TAIYIN_OBSERVED_HORIZONTAL | TAIYIN_OBSERVED_REFRACTION)) != 0u;
    const bool want_refraction = (flags & TAIYIN_OBSERVED_REFRACTION) != 0u;
    if (want_topocentric && context->observer_id != TAIYIN_BODY_EARTH) {
        set_diagnostics(
            diagnostics,
            body_count,
            body_ids,
            TAIYIN_ERROR_UNSUPPORTED,
            context->observer_id,
            diagnostic_jd);
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    if (want_horizontal && !want_topocentric) {
        set_diagnostics(diagnostics, body_count, body_ids, TAIYIN_ERROR_INVALID_ARGUMENT, context->observer_id, diagnostic_jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (want_horizontal
        && (!context->fields.has(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION)
            || !native_observer_location_is_finite(context->observer_location))) {
        set_diagnostics(diagnostics, body_count, body_ids, TAIYIN_ERROR_INVALID_ARGUMENT, context->observer_id, diagnostic_jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    RefractionModel refraction_model = RefractionModel::Bennett;
    NativeAtmosphere refraction_atmosphere;
    if (want_refraction) {
        const bool allow_standard_fallback = (context->atmosphere_policy_flags
            & TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK) != 0u
            && (flags & TAIYIN_OBSERVED_STRICT_METEOROLOGY) == 0u;
        if (!native_context_resolve_refraction_atmosphere(
                *context, allow_standard_fallback, &refraction_atmosphere)
            || !native_refraction_model_from_id(context->refraction_model_id, &refraction_model)) {
            set_diagnostics(diagnostics, body_count, body_ids, TAIYIN_ERROR_INVALID_ARGUMENT, context->observer_id, diagnostic_jd);
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
    }

    NativeCalcContext scratch = *context;
    scratch.apparent_options.model_context = &scratch.model_context;
    if (want_topocentric) {
        if (scratch.fields.has(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION)
            && native_observer_location_is_finite(scratch.observer_location)) {
            Status observer_status = TAIYIN_STATUS_OK;
            if (precise_topocentric) {
                observer_status = native_context_set_precise_topocentric_observer(
                    &scratch,
                    scratch.observer_location,
                    jd_utc,
                    jd_tt);
            } else {
                observer_status = set_true_equator_model_topocentric_observer(
                    &scratch,
                    scratch.observer_location,
                    jd_ut1,
                    jd_tt);
            }
            if (observer_status != TAIYIN_STATUS_OK) {
                set_diagnostics(diagnostics, body_count, body_ids, observer_status, context->observer_id, jd_tdb);
                return observer_status;
            }
        } else if (!scratch.fields.has(TAIYIN_NATIVE_FIELD_TOPOCENTRIC_OFFSET)
            || !native_cartesian_state_is_finite(scratch.apparent_options.observer_offset)) {
            set_diagnostics(diagnostics, body_count, body_ids, TAIYIN_ERROR_INVALID_ARGUMENT, context->observer_id, jd_tdb);
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
    }

    const uint32_t apparent_flags = observed_flags_to_apparent_flags(flags, scratch.apparent_options.flags);
    if ((apparent_flags & TAIYIN_APPARENT_SHAPIRO_DELAY) != 0u
        && (apparent_flags & TAIYIN_APPARENT_LIGHT_TIME) == 0u) {
        set_diagnostics(diagnostics, body_count, body_ids, TAIYIN_ERROR_INVALID_ARGUMENT, context->observer_id, jd_tdb);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (apparent_flags_need_deflectors(apparent_flags)
        && !scratch.fields.has(TAIYIN_NATIVE_FIELD_DEFLECTORS)) {
        set_diagnostics(diagnostics, body_count, body_ids, TAIYIN_ERROR_INVALID_ARGUMENT, context->observer_id, jd_tdb);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    ApparentDeflector empty_deflector;
    ApparentOptions options = scratch.apparent_options;
    options.flags = apparent_flags;
    options.output_frame_id = TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE;
    options.model_context = &scratch.model_context;
    if (!scratch.fields.has(TAIYIN_NATIVE_FIELD_DEFLECTORS)) {
        options.deflectors = &empty_deflector;
        options.deflector_count = 0;
        options.solar_deflector_index = -1;
    }

    MajorBodyApparentBatchRequest apparent_request;
    apparent_request.jd_tdb = jd_tdb;
    apparent_request.jd_tt = jd_tt;
    apparent_request.observer_id = scratch.observer_id;
    apparent_request.center_id = scratch.center_id;
    apparent_request.body_ids = body_ids;
    apparent_request.body_count = body_count;
    apparent_request.position_flags = static_cast<uint32_t>(
        flags & TAIYIN_OBSERVED_CALCULATION_FLAGS_MASK);
    apparent_request.options = &options;

    MajorBodyApparentBatchResult apparent_result;
    EphemerisEvalDiagnostic batch_diagnostic;
    const Status apparent_status = eval_global_major_body_apparent_batch(
        apparent_request,
        &apparent_result,
        &batch_diagnostic);
    if (apparent_status != TAIYIN_STATUS_OK) {
        set_diagnostics(diagnostics, body_count, body_ids, apparent_status, context->observer_id, jd_tdb);
        if (diagnostics) {
            diagnostics[0] = batch_diagnostic;
        }
        return apparent_status;
    }

    double local_sidereal = 0.0;
    double local_sidereal_rate = 0.0;
    if (want_horizontal) {
        double sidereal = 0.0;
        if (!eval_model_sidereal(
                scratch.model_context,
                jd_ut1,
                jd_tt,
                options.matrix_derivative_step_days,
                &sidereal,
                &local_sidereal_rate)) {
            set_diagnostics(diagnostics, body_count, body_ids, TAIYIN_ERROR_UNSUPPORTED, context->observer_id, jd_tdb);
            return TAIYIN_ERROR_UNSUPPORTED;
        }
        local_sidereal = normalize_radians(sidereal + scratch.observer_location.longitude_rad);
    }

    for (size_t i = 0; i < apparent_result.body_count; ++i) {
        out[i] = ObservedPosition();
        out[i].body_id = apparent_result.bodies[i].body_id;
        out[i].status = apparent_result.bodies[i].status;
        out[i].diagnostic = apparent_result.bodies[i].diagnostic;
        out[i].apparent = apparent_result.bodies[i];
        if (diagnostics) {
            diagnostics[i] = apparent_result.bodies[i].diagnostic;
        }

        if (want_horizontal) {
            out[i].horizontal = topocentric_position_to_horizontal(
                apparent_result.bodies[i].apparent_state.position_au,
                local_sidereal,
                scratch.observer_location.latitude_rad);
            out[i].refracted_horizontal = out[i].horizontal;
            if ((flags & TAIYIN_OBSERVED_SPEED) != 0u) {
                topocentric_velocity_to_horizontal_rates(
                    apparent_result.bodies[i].apparent_state.position_au,
                    apparent_result.bodies[i].apparent_state.velocity_au_per_day,
                    local_sidereal,
                    local_sidereal_rate,
                    scratch.observer_location.latitude_rad,
                    &out[i].horizontal_rates);
                out[i].refracted_horizontal_rates = out[i].horizontal_rates;
            }
        }

        if (want_refraction) {
            out[i].refracted_horizontal = refract_horizontal_coordinates(
                out[i].horizontal,
                refraction_atmosphere.pressure_mbar,
                refraction_atmosphere.temperature_celsius,
                refraction_atmosphere.relative_humidity,
                refraction_atmosphere.wavelength_micrometer,
                refraction_model);
            if ((flags & TAIYIN_OBSERVED_SPEED) != 0u) {
                out[i].refracted_horizontal_rates = refract_horizontal_rates(
                    out[i].horizontal,
                    out[i].horizontal_rates,
                    refraction_atmosphere.pressure_mbar,
                    refraction_atmosphere.temperature_celsius,
                    refraction_atmosphere.relative_humidity,
                    refraction_atmosphere.wavelength_micrometer,
                    refraction_model);
            }
        }
    }
    return TAIYIN_STATUS_OK;
}

}  // namespace

ObservedPosition::ObservedPosition() noexcept
    : body_id(0),
      status(TAIYIN_STATUS_OK),
      diagnostic(),
      apparent(),
      horizontal(),
      horizontal_rates(),
      refracted_horizontal(),
      refracted_horizontal_rates() {}

Status calc_observed_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& jd_ut,
    const int* body_ids,
    size_t body_count,
    uint64_t flags,
    ObservedPosition* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept {
    SplitJulianDate jd_tt;
    SplitJulianDate jd_tdb;
    if (context && split_julian_date_is_finite(jd_ut)) {
        const Status time_status = resolve_ut_scales(*context, jd_ut, &jd_tt, &jd_tdb);
        if (time_status != TAIYIN_STATUS_OK) {
            clear_observed(out, body_count);
            set_diagnostics(diagnostics, body_count, body_ids, time_status, context->observer_id, jd_ut);
            return time_status;
        }
    }
    return calc_observed_resolved_scales(
        context,
        jd_ut,
        jd_ut,
        jd_tt,
        jd_tdb,
        false,
        jd_ut,
        body_ids,
        body_count,
        flags,
        out,
        diagnostics);
}

Status calc_observed_utc(
    const NativeCalcContext* context,
    const CalendarDateTime& datetime_utc,
    const int* body_ids,
    size_t body_count,
    uint64_t flags,
    ObservedPosition* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept {
    SplitJulianDate jd_utc;
    if (!julian_day_split(datetime_utc, &jd_utc)) {
        clear_observed(out, body_count);
        set_diagnostics(
            diagnostics,
            body_count,
            body_ids,
            TAIYIN_ERROR_INVALID_ARGUMENT,
            context ? context->observer_id : 0,
            SplitJulianDate());
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    clear_observed(out, body_count);
    if (body_count == 0) {
        return TAIYIN_STATUS_OK;
    }
    if (!context || !body_ids || !out || body_count > TAIYIN_MAJOR_BODY_COUNT) {
        set_diagnostics(diagnostics, body_count, body_ids, TAIYIN_ERROR_INVALID_ARGUMENT, context ? context->observer_id : 0, jd_utc);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const internal::EarthOrientationTable* eop_table = global_earth_orientation_table();
    PreciseTimeScales scales;
    TimeScaleDiagnostic time_diagnostic;
    NativeTimeScaleCache& cache = context->time_scale_cache;
    NativeCalcContext scratch = *context;
    scratch.apparent_options.model_context = &scratch.model_context;
    std::lock_guard<std::recursive_mutex> cache_lock(cache.mutex);
    const uint64_t dispatch_generation = dispatch::model_registry_generation();
    const bool time_cache_hit = cache.utc_valid
        && cache.utc_dispatch_generation == dispatch_generation
        && same_calendar_datetime(cache.datetime_utc, datetime_utc)
        && cache.allow_utc_out_of_range_estimate
            == context->allow_utc_out_of_range_estimate
        && cache.utc_tdb_model_id == context->model_context.tdb_model_id
        && cache.utc_delta_t_model_id == context->delta_t_model_id
        && cache.utc_ephemeris_family_id == context->ephemeris_family_id
        && cache.eop_table == eop_table;
    Status cpo_status = TAIYIN_STATUS_OK;
    if (time_cache_hit) {
        scales = cache.utc_scales;
        time_diagnostic = cache.utc_diagnostic;
        if (cache.has_celestial_pole_offset) {
            cpo_status = native_context_set_celestial_pole_offset(
                &scratch,
                cache.celestial_pole_offset_dx_rad,
                cache.celestial_pole_offset_dy_rad,
                cache.celestial_pole_offset_dx_rate_rad_per_day,
                cache.celestial_pole_offset_dy_rate_rad_per_day);
        }
    } else {
        TimeScaleOptions options;
        options.allow_utc_out_of_range_estimate =
            context->allow_utc_out_of_range_estimate;
        options.tdb_model_id = context->model_context.tdb_model_id;
        options.delta_t_model_id = context->delta_t_model_id;
        options.ephemeris_family_id = context->ephemeris_family_id;
        options.leap_second_table = builtin_leap_second_table();
        if (!make_time_scales_from_utc(
                datetime_utc,
                eop_table,
                &options,
                &scales,
                &time_diagnostic)) {
            const Status status = precise_time_failure_status(time_diagnostic);
            set_diagnostics(
                diagnostics,
                body_count,
                body_ids,
                status,
                context->observer_id,
                jd_utc);
            for (size_t i = 0; diagnostics && i < body_count; ++i) {
                copy_time_scale_diagnostic(diagnostics + i, time_diagnostic);
            }
            return status;
        }
        cpo_status = time_diagnostic.used_eop
            ? apply_celestial_pole_offset_from_eop(&scratch, scales.jd_utc)
            : TAIYIN_STATUS_OK;
        if (cpo_status == TAIYIN_STATUS_OK) {
            cache.utc_valid = true;
            cache.utc_dispatch_generation = dispatch_generation;
            cache.datetime_utc = datetime_utc;
            cache.allow_utc_out_of_range_estimate =
                context->allow_utc_out_of_range_estimate;
            cache.utc_tdb_model_id = context->model_context.tdb_model_id;
            cache.utc_delta_t_model_id = context->delta_t_model_id;
            cache.utc_ephemeris_family_id = context->ephemeris_family_id;
            cache.eop_table = eop_table;
            cache.utc_scales = scales;
            cache.utc_diagnostic = time_diagnostic;
            cache.has_celestial_pole_offset = time_diagnostic.used_eop;
            if (cache.has_celestial_pole_offset) {
                cache.celestial_pole_offset_dx_rad =
                    scratch.apparent_options.celestial_pole_offset_dx_rad;
                cache.celestial_pole_offset_dy_rad =
                    scratch.apparent_options.celestial_pole_offset_dy_rad;
                cache.celestial_pole_offset_dx_rate_rad_per_day =
                    scratch.apparent_options.celestial_pole_offset_dx_rate_rad_per_day;
                cache.celestial_pole_offset_dy_rate_rad_per_day =
                    scratch.apparent_options.celestial_pole_offset_dy_rate_rad_per_day;
            }
        }
    }
    if (cpo_status != TAIYIN_STATUS_OK) {
        set_diagnostics(
            diagnostics,
            body_count,
            body_ids,
            cpo_status,
            context->observer_id,
            scales.jd_tdb);
        return cpo_status;
    }

    const Status status = calc_observed_resolved_scales(
        &scratch,
        scales.jd_utc,
        scales.jd_ut1,
        scales.jd_tt,
        scales.jd_tdb,
        time_diagnostic.used_eop,
        scales.jd_utc,
        body_ids,
        body_count,
        flags,
        out,
        diagnostics);
    for (size_t i = 0; diagnostics && i < body_count; ++i) {
        copy_time_scale_diagnostic(diagnostics + i, time_diagnostic);
    }
    return status;
}

}  // namespace runtime
}  // namespace taiyin
