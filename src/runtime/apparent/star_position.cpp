#include "taiyin/runtime/star_position.h"

#include "runtime/apparent/builtin_star_position.h"

#include "runtime/core/native_context_checks.h"
#include "runtime/core/runtime_state_block_adapter.h"

#include "taiyin/angle.h"
#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/corrections.h"
#include "taiyin/dispatch.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/geometry.h"
#include "taiyin/observer.h"
#include "taiyin/physical_constants.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/star_catalog_tsc1.h"
#include "taiyin/star_provider_tsf1.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

namespace taiyin {
namespace runtime {
namespace {

const uint64_t SUPPORTED_STAR_POSITION_FLAGS =
    static_cast<uint64_t>(TAIYIN_NATIVE_POSITION_SPEED)
    | TAIYIN_NATIVE_POSITION_XYZ
    | TAIYIN_NATIVE_POSITION_EQUATORIAL
    | TAIYIN_NATIVE_POSITION_RADIANS
    | TAIYIN_NATIVE_POSITION_TRUEPOS
    | TAIYIN_NATIVE_POSITION_ASTROMETRIC
    | TAIYIN_NATIVE_POSITION_NO_ABERR
    | TAIYIN_NATIVE_POSITION_NO_GDEFL
    | TAIYIN_NATIVE_POSITION_NONUT
    | TAIYIN_NATIVE_POSITION_TOPOCENTRIC;

const uint64_t SUPPORTED_STAR_OBSERVED_FLAGS =
    TAIYIN_OBSERVED_CALCULATION_FLAGS_MASK
    | TAIYIN_OBSERVED_HORIZONTAL
    | TAIYIN_OBSERVED_REFRACTION
    | TAIYIN_OBSERVED_STRICT_METEOROLOGY;

struct GlobalStarCatalogStore {
    std::mutex mutex;
    std::vector<std::unique_ptr<uint8_t[]> > tsc1_owned_memory;
    std::vector<std::unique_ptr<Tsc1StarProvider> > tsc1_providers;
    std::vector<std::unique_ptr<Tsf1StarProvider> > tsf1_providers;

    GlobalStarCatalogStore() noexcept
        : mutex(), tsc1_owned_memory(), tsc1_providers(), tsf1_providers() {}
};

GlobalStarCatalogStore& global_star_store() noexcept {
    static GlobalStarCatalogStore store;
    return store;
}

void clear_out(double out[6]) noexcept {
    if (!out) {
        return;
    }
    for (int i = 0; i < 6; ++i) {
        out[i] = 0.0;
    }
}

void clear_observed_out(ObservedPosition* out, size_t count) noexcept {
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
    const SplitJulianDate& jd_tdb
) noexcept {
    if (!diagnostic) {
        return;
    }
    *diagnostic = EphemerisEvalDiagnostic();
    diagnostic->status = status;
    diagnostic->target_id = target_id;
    diagnostic->center_id = TSC1_DEFAULT_CENTER_ID;
    diagnostic->frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
    diagnostic->jd_tdb = jd_tdb;
}

void set_observed_diagnostic(
    EphemerisEvalDiagnostic* diagnostic,
    Status status,
    int target_id,
    const SplitJulianDate& jd_tdb
) noexcept {
    set_diagnostic(diagnostic, status, target_id, jd_tdb);
}

void set_observed_diagnostics(
    EphemerisEvalDiagnostic* diagnostics,
    size_t count,
    Status status,
    const SplitJulianDate& jd_tdb
) noexcept {
    if (!diagnostics) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        set_observed_diagnostic(diagnostics + i, status, 0, jd_tdb);
    }
}

Status fail_position(
    double out[6],
    EphemerisEvalDiagnostic* diagnostic,
    Status status,
    int target_id,
    const SplitJulianDate& jd_tdb
) noexcept {
    clear_out(out);
    set_diagnostic(diagnostic, status, target_id, jd_tdb);
    return status;
}

Status fail_observed(
    ObservedPosition* out,
    size_t count,
    EphemerisEvalDiagnostic* diagnostics,
    Status status,
    int target_id,
    const SplitJulianDate& jd_tdb
) noexcept {
    clear_observed_out(out, count);
    if (count == 1 && out) {
        out[0].status = status;
        out[0].body_id = target_id;
        set_observed_diagnostic(&out[0].diagnostic, status, target_id, jd_tdb);
    }
    if (diagnostics && count == 1) {
        set_observed_diagnostic(diagnostics, status, target_id, jd_tdb);
    } else {
        set_observed_diagnostics(diagnostics, count, status, jd_tdb);
    }
    return status;
}

bool valid_deflection_model_id(int model_id) noexcept {
    return model_id == TAIYIN_DEFLECTION_MODEL_ERFA
        || model_id == TAIYIN_DEFLECTION_MODEL_SOLAR_DISK;
}

Vector3 zero_vector() noexcept {
    Vector3 out = { 0.0, 0.0, 0.0 };
    return out;
}

void set_zero_state(CartesianState* out) noexcept {
    if (!out) {
        return;
    }
    out->position_au = zero_vector();
    out->velocity_au_per_day = zero_vector();
    out->acceleration_au_per_day2 = zero_vector();
}

Vector3 transform_vector(const Vector3& value, const double matrix[9]) noexcept {
    const Vector3 out = {
        matrix[0] * value.x + matrix[1] * value.y + matrix[2] * value.z,
        matrix[3] * value.x + matrix[4] * value.y + matrix[5] * value.z,
        matrix[6] * value.x + matrix[7] * value.y + matrix[8] * value.z,
    };
    return out;
}

Vector3 transform_velocity_array(
    const Vector3& position,
    const Vector3& velocity,
    const double matrix[9],
    const double matrix_dot[9]
) noexcept {
    return vector3_add(transform_vector(velocity, matrix), transform_vector(position, matrix_dot));
}

Vector3 transform_acceleration_array(
    const Vector3& position,
    const Vector3& velocity,
    const Vector3& acceleration,
    const double matrix[9],
    const double matrix_dot[9],
    const double matrix_ddot[9]
) noexcept {
    return vector3_add(
        transform_vector(acceleration, matrix),
        vector3_add(
            vector3_scale(transform_vector(velocity, matrix_dot), 2.0),
            transform_vector(position, matrix_ddot)));
}

int without_nutation_output_frame(int output_frame_id) noexcept {
    switch (output_frame_id) {
    case TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE:
        return TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE;
    case TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE:
        return TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE;
    case TAIYIN_APPARENT_FRAME_CIRS:
        return -1;
    default:
        return output_frame_id;
    }
}

int star_output_frame(const NativeCalcContext& context, uint64_t flags) noexcept {
    if ((flags & TAIYIN_NATIVE_POSITION_EQUATORIAL) != 0u) {
        return (flags & TAIYIN_NATIVE_POSITION_NONUT) != 0u
            ? TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE
            : TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE;
    }
    return (flags & TAIYIN_NATIVE_POSITION_NONUT) != 0u
        ? without_nutation_output_frame(context.apparent_options.output_frame_id)
        : context.apparent_options.output_frame_id;
}

uint32_t observed_flags_from_native_star_flags(uint64_t flags) noexcept {
    uint32_t observed_flags = 0u;
    if ((flags & TAIYIN_NATIVE_POSITION_TOPOCENTRIC) != 0u) {
        observed_flags |= TAIYIN_OBSERVED_TOPOCENTRIC;
    }
    if ((flags & TAIYIN_NATIVE_POSITION_SPEED) != 0u) {
        observed_flags |= TAIYIN_OBSERVED_SPEED;
    }
    if ((flags & TAIYIN_NATIVE_POSITION_TRUEPOS) != 0u) {
        observed_flags |= TAIYIN_OBSERVED_TRUEPOS;
    }
    if ((flags & TAIYIN_NATIVE_POSITION_ASTROMETRIC) != 0u) {
        observed_flags |= TAIYIN_OBSERVED_ASTROMETRIC;
    }
    if ((flags & TAIYIN_NATIVE_POSITION_NO_ABERR) != 0u) {
        observed_flags |= TAIYIN_OBSERVED_NO_ABERR;
    }
    if ((flags & TAIYIN_NATIVE_POSITION_NO_GDEFL) != 0u) {
        observed_flags |= TAIYIN_OBSERVED_NO_GDEFL;
    }
    return observed_flags;
}

bool eval_global_star_state(
    const char* star_key,
    const SplitJulianDate& jd_tdb,
    CartesianState* out,
    int* out_runtime_id
) noexcept {
    if (!star_key || !out) {
        return false;
    }
    GlobalStarCatalogStore& store = global_star_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    Tsc1ResolvedStar resolved;
    for (size_t i = 0; i < store.tsc1_providers.size(); ++i) {
        Tsc1StarProvider* provider = store.tsc1_providers[i].get();
        if (provider && provider->resolve(star_key, &resolved)
            && provider->eval_state(star_key, jd_tdb, out)) {
            if (out_runtime_id) {
                *out_runtime_id = resolved.runtime_id;
            }
            return true;
        }
    }
    for (size_t i = 0; i < store.tsf1_providers.size(); ++i) {
        Tsf1StarProvider* provider = store.tsf1_providers[i].get();
        if (provider && provider->resolve(star_key, &resolved)
            && provider->eval_state(star_key, jd_tdb, out)) {
            if (out_runtime_id) {
                *out_runtime_id = resolved.runtime_id;
            }
            return true;
        }
    }
    return false;
}

Status resolve_tt_to_tdb(
    const NativeCalcContext& context,
    const SplitJulianDate& jd_tt,
    SplitJulianDate* out_jd_tdb
) noexcept {
    if (!out_jd_tdb || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double tdb_minus_tt_seconds = dispatch::eval_tdb(context.model_context.tdb_model_id, jd_tt, 0);
    SplitJulianDate jd_tdb;
    if (!add_seconds_to_split_jd(jd_tt, tdb_minus_tt_seconds, &jd_tdb)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    *out_jd_tdb = jd_tdb;
    return TAIYIN_STATUS_OK;
}

Status resolve_ut_to_tdb_tt(
    const NativeCalcContext& context,
    const SplitJulianDate& jd_ut,
    SplitJulianDate* out_jd_tdb,
    SplitJulianDate* out_jd_tt
) noexcept {
    if (!out_jd_tdb || !out_jd_tt || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
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
    SplitJulianDate jd_tdb;
    const Status tdb_status = resolve_tt_to_tdb(context, jd_tt, &jd_tdb);
    if (tdb_status != TAIYIN_STATUS_OK) {
        return tdb_status;
    }
    *out_jd_tdb = jd_tdb;
    *out_jd_tt = jd_tt;
    return TAIYIN_STATUS_OK;
}

bool star_origin_route_unavailable(Status status) noexcept {
    return status == TAIYIN_EPHEMERIS_ERROR_NO_ROUTE
        || status == TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP
        || status == TAIYIN_EPHEMERIS_ERROR_COMPOSITE_MISSING_COMPONENT
        || status == TAIYIN_EPHEMERIS_ERROR_COMPOSITE_COVERAGE_GAP;
}

Status eval_context_relative_state(
    const NativeCalcContext& context,
    int target_id,
    int center_id,
    const SplitJulianDate& jd_tdb,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out || !split_julian_date_is_finite(jd_tdb)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    RuntimeStateEvalContext eval_context;
    eval_context.route_rule_id = context.route_rule_id;
    eval_context.route_rules = context.route_rules;
    EphemerisResult result;
    const Status status = eval_runtime_body_state(
        eval_context,
        target_id,
        center_id,
        jd_tdb,
        internal::EPHEMERIS_BLOCK_COMPONENT_STATE,
        context.route_rule_id,
        context.route_rules,
        &result,
        diagnostic);
    if (status != TAIYIN_STATUS_OK || !native_cartesian_state_is_finite(result.state)) {
        set_zero_state(out);
        return status == TAIYIN_STATUS_OK ? TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED : status;
    }
    *out = result.state;
    return TAIYIN_STATUS_OK;
}

struct StarObserverFrame {
    CartesianState catalog_origin_from_ssb;
    CartesianState observer_from_origin;
    CartesianState sun_from_origin;

    StarObserverFrame() noexcept
        : catalog_origin_from_ssb(),
          observer_from_origin(),
          sun_from_origin() {}
};

Status resolve_exact_star_observer_frame(
    const NativeCalcContext& context,
    int origin_id,
    const SplitJulianDate& jd_tdb,
    bool needs_sun,
    StarObserverFrame* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = StarObserverFrame();
    Status status = eval_context_relative_state(
        context,
        context.observer_id,
        origin_id,
        jd_tdb,
        &out->observer_from_origin,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }
    status = eval_context_relative_state(
        context,
        origin_id,
        TAIYIN_BODY_SSB,
        jd_tdb,
        &out->catalog_origin_from_ssb,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }
    if (needs_sun) {
        status = eval_context_relative_state(
            context,
            TAIYIN_BODY_SUN,
            origin_id,
            jd_tdb,
            &out->sun_from_origin,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            return status;
        }
    }
    return TAIYIN_STATUS_OK;
}

Status resolve_star_observer_frame(
    const NativeCalcContext& context,
    const SplitJulianDate& jd_tdb,
    bool needs_sun,
    StarObserverFrame* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out || !split_julian_date_is_finite(jd_tdb)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    Status preferred_status = resolve_exact_star_observer_frame(
        context, context.center_id, jd_tdb, needs_sun, out, diagnostic);
    if (preferred_status == TAIYIN_STATUS_OK) {
        return TAIYIN_STATUS_OK;
    }
    if (!star_origin_route_unavailable(preferred_status)) {
        return preferred_status;
    }

    if (context.center_id != TAIYIN_BODY_SSB) {
        const Status ssb_status = resolve_exact_star_observer_frame(
            context, TAIYIN_BODY_SSB, jd_tdb, needs_sun, out, diagnostic);
        if (ssb_status == TAIYIN_STATUS_OK) {
            return TAIYIN_STATUS_OK;
        }
        if (!star_origin_route_unavailable(ssb_status)) {
            return ssb_status;
        }
        preferred_status = ssb_status;
    }

    return preferred_status;
}

bool calc_star_output_matrices(
    const NativeCalcContext& context,
    const SplitJulianDate& jd_tt,
    uint32_t matrix_flags,
    double output_matrix[9],
    double output_matrix_dot[9],
    double output_matrix_ddot[9]
) noexcept {
    double precession_matrix[9];
    double nutation_matrix_values[9];
    return calc_apparent_matrices(
        jd_tt,
        matrix_flags,
        TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE,
        context.model_context.precession_model_id,
        context.model_context.nutation_model_id,
        context.model_context.obliquity_model_id,
        context.model_context.frame_route_id,
        context.apparent_options.celestial_pole_offset_dx_rad,
        context.apparent_options.celestial_pole_offset_dy_rad,
        context.apparent_options.celestial_pole_offset_dx_rate_rad_per_day,
        context.apparent_options.celestial_pole_offset_dy_rate_rad_per_day,
        context.apparent_options.matrix_derivative_step_days,
        precession_matrix,
        nutation_matrix_values,
        output_matrix,
        output_matrix_dot,
        output_matrix_ddot,
        0,
        0,
        0,
        0);
}

bool calc_star_local_sidereal(
    const NativeCalcContext& context,
    const SplitJulianDate& jd_ut,
    const SplitJulianDate& jd_tt,
    double* out_local_sidereal,
    double* out_local_sidereal_rate
) noexcept {
    if (!out_local_sidereal || !out_local_sidereal_rate) {
        return false;
    }
    double sidereal = 0.0;
    if (!gast_model_rad(
            context.model_context.precession_model_id,
            context.model_context.nutation_model_id,
            jd_ut,
            jd_tt,
            &sidereal)
        || !gast_rate_model_rad_per_day(
            context.model_context.precession_model_id,
            context.model_context.nutation_model_id,
            jd_tt,
            0.0,
            0.0,
            context.apparent_options.matrix_derivative_step_days,
            out_local_sidereal_rate)) {
        return false;
    }
    *out_local_sidereal = normalize_radians(sidereal + context.observer_location.longitude_rad);
    return true;
}

Status build_observed_star_state_from_barycentric(
    const NativeCalcContext& context,
    const CartesianState& star_bary,
    int runtime_id,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    bool refresh_topocentric_observer,
    CartesianState* out_icrf,
    int* out_runtime_id,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_icrf || !out_runtime_id || !native_cartesian_state_is_finite(star_bary)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (context.observer_id != TAIYIN_BODY_EARTH) {
        set_zero_state(out_icrf);
        set_diagnostic(diagnostic, TAIYIN_ERROR_UNSUPPORTED, runtime_id, jd_tdb);
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    if (!valid_deflection_model_id(context.apparent_options.deflection_model_id)) {
        set_zero_state(out_icrf);
        set_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, 0, jd_tdb);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const bool truepos = (flags & TAIYIN_OBSERVED_TRUEPOS) != 0u;
    const bool astrometric = (flags & TAIYIN_OBSERVED_ASTROMETRIC) != 0u;
    const bool needs_deflection = !truepos
        && !astrometric
        && (flags & TAIYIN_OBSERVED_NO_GDEFL) == 0u;
    const bool needs_aberration = !truepos
        && !astrometric
        && (flags & TAIYIN_OBSERVED_NO_ABERR) == 0u;
    StarObserverFrame frame;
    Status status = resolve_star_observer_frame(
        context,
        jd_tdb,
        needs_deflection || needs_aberration,
        &frame,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        set_zero_state(out_icrf);
        set_diagnostic(diagnostic, status, runtime_id, jd_tdb);
        return status;
    }

    CartesianState observer_in_catalog_frame;
    observer_in_catalog_frame.position_au = vector3_add(
        frame.catalog_origin_from_ssb.position_au,
        frame.observer_from_origin.position_au);
    observer_in_catalog_frame.velocity_au_per_day = vector3_add(
        frame.catalog_origin_from_ssb.velocity_au_per_day,
        frame.observer_from_origin.velocity_au_per_day);
    observer_in_catalog_frame.acceleration_au_per_day2 = vector3_add(
        frame.catalog_origin_from_ssb.acceleration_au_per_day2,
        frame.observer_from_origin.acceleration_au_per_day2);
    const bool want_topocentric = (flags & TAIYIN_OBSERVED_TOPOCENTRIC) != 0u;
    if (want_topocentric) {
        NativeCalcContext topo_context = context;
        if (refresh_topocentric_observer
            && topo_context.fields.has(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION)
            && native_observer_location_is_finite(topo_context.observer_location)) {
            status = native_context_set_simple_topocentric_observer(
                &topo_context,
                topo_context.observer_location,
                jd_ut,
                jd_tt);
            if (status != TAIYIN_STATUS_OK) {
                set_zero_state(out_icrf);
                set_diagnostic(diagnostic, status, runtime_id, jd_tdb);
                return status;
            }
        }
        if (!topo_context.fields.has(TAIYIN_NATIVE_FIELD_TOPOCENTRIC_OFFSET)
            || !native_cartesian_state_is_finite(topo_context.apparent_options.observer_offset)) {
            set_zero_state(out_icrf);
            set_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, runtime_id, jd_tdb);
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        observer_in_catalog_frame.position_au = vector3_add(
            observer_in_catalog_frame.position_au,
            topo_context.apparent_options.observer_offset.position_au);
        observer_in_catalog_frame.velocity_au_per_day = vector3_add(
            observer_in_catalog_frame.velocity_au_per_day,
            topo_context.apparent_options.observer_offset.velocity_au_per_day);
        observer_in_catalog_frame.acceleration_au_per_day2 = vector3_add(
            observer_in_catalog_frame.acceleration_au_per_day2,
            topo_context.apparent_options.observer_offset.acceleration_au_per_day2);
    }

    CartesianState observed;
    observed.position_au = vector3_subtract(
        star_bary.position_au, observer_in_catalog_frame.position_au);
    observed.velocity_au_per_day = vector3_subtract(
        star_bary.velocity_au_per_day,
        observer_in_catalog_frame.velocity_au_per_day);
    observed.acceleration_au_per_day2 = vector3_subtract(
        star_bary.acceleration_au_per_day2,
        observer_in_catalog_frame.acceleration_au_per_day2);

    CartesianState sun_in_catalog_frame;
    sun_in_catalog_frame.position_au = vector3_add(
        frame.catalog_origin_from_ssb.position_au,
        frame.sun_from_origin.position_au);
    sun_in_catalog_frame.velocity_au_per_day = vector3_add(
        frame.catalog_origin_from_ssb.velocity_au_per_day,
        frame.sun_from_origin.velocity_au_per_day);
    sun_in_catalog_frame.acceleration_au_per_day2 = vector3_add(
        frame.catalog_origin_from_ssb.acceleration_au_per_day2,
        frame.sun_from_origin.acceleration_au_per_day2);

    if (needs_deflection) {
        CartesianState deflected = observed;
        const Vector3 observer_helio_pos = vector3_subtract(
            observer_in_catalog_frame.position_au,
            sun_in_catalog_frame.position_au);
        const Vector3 observer_helio_vel = vector3_subtract(
            observer_in_catalog_frame.velocity_au_per_day,
            sun_in_catalog_frame.velocity_au_per_day);
        const Vector3 zero = zero_vector();
        if (!apply_gravitational_deflection_from_body_with_model(
                observed.position_au,
                observed.velocity_au_per_day,
                observer_helio_pos,
                observer_helio_vel,
                zero,
                zero,
                observed.position_au,
                observed.velocity_au_per_day,
                TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
                TAIYIN_SOLAR_DEFLECTION_LIMIT,
                context.apparent_options.deflection_model_id,
                &deflected.position_au,
                &deflected.velocity_au_per_day)) {
            set_zero_state(out_icrf);
            set_diagnostic(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, runtime_id, jd_tdb);
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }
        observed.position_au = deflected.position_au;
        observed.velocity_au_per_day = deflected.velocity_au_per_day;
    }

    if (needs_aberration) {
        CartesianState aberrated = observed;
        const Vector3 observer_helio_pos = vector3_subtract(
            observer_in_catalog_frame.position_au,
            sun_in_catalog_frame.position_au);
        const Vector3 observer_helio_vel = vector3_subtract(
            observer_in_catalog_frame.velocity_au_per_day,
            sun_in_catalog_frame.velocity_au_per_day);
        if (!apply_annual_aberration(
                observed.position_au,
                observed.velocity_au_per_day,
                observer_helio_pos,
                observer_helio_vel,
                observer_in_catalog_frame.velocity_au_per_day,
                observer_in_catalog_frame.acceleration_au_per_day2,
                TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
                &aberrated.position_au,
                &aberrated.velocity_au_per_day)) {
            set_zero_state(out_icrf);
            set_diagnostic(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, runtime_id, jd_tdb);
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }
        observed.position_au = aberrated.position_au;
        observed.velocity_au_per_day = aberrated.velocity_au_per_day;
    }

    if (!native_cartesian_state_is_finite(observed)) {
        set_zero_state(out_icrf);
        set_diagnostic(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, runtime_id, jd_tdb);
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out_icrf = observed;
    *out_runtime_id = runtime_id;
    set_diagnostic(diagnostic, TAIYIN_STATUS_OK, runtime_id, jd_tdb);
    return TAIYIN_STATUS_OK;
}

Status build_observed_star_state(
    const NativeCalcContext& context,
    const char* star_key,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    CartesianState* out_icrf,
    int* out_runtime_id,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!star_key || !out_icrf || !out_runtime_id) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    CartesianState star_bary;
    int runtime_id = 0;
    if (!eval_global_star_state(star_key, jd_tdb, &star_bary, &runtime_id)
        || !native_cartesian_state_is_finite(star_bary)) {
        set_zero_state(out_icrf);
        set_diagnostic(diagnostic, TAIYIN_FILE_ERROR_NOT_FOUND, runtime_id, jd_tdb);
        return TAIYIN_FILE_ERROR_NOT_FOUND;
    }
    return build_observed_star_state_from_barycentric(
        context, star_bary, runtime_id, jd_tdb, jd_tt, jd_ut, flags, true,
        out_icrf, out_runtime_id, diagnostic);
}

Status calc_star_position_from_barycentric_tdb(
    const NativeCalcContext* context,
    const CartesianState& star_bary,
    int runtime_id,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint64_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_out(out);
    if (!context || !out || !split_julian_date_is_finite(jd_tdb)
        || !split_julian_date_is_finite(jd_tt)
        || !native_cartesian_state_is_finite(star_bary)) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, runtime_id, jd_tdb);
    }
    if (context->observer_id != TAIYIN_BODY_EARTH) {
        return fail_position(
            out, diagnostic, TAIYIN_ERROR_UNSUPPORTED, runtime_id, jd_tdb);
    }
    if ((flags & ~SUPPORTED_STAR_POSITION_FLAGS) != 0u) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_UNSUPPORTED, runtime_id, jd_tdb);
    }

    CartesianState state;
    int resolved_runtime_id = runtime_id;
    const uint32_t observed_flags = observed_flags_from_native_star_flags(flags);
    const Status state_status = build_observed_star_state_from_barycentric(
        *context, star_bary, runtime_id, jd_tdb, jd_tt, jd_tt, observed_flags,
        false,
        &state, &resolved_runtime_id, diagnostic);
    if (state_status != TAIYIN_STATUS_OK) {
        return fail_position(out, diagnostic, state_status, resolved_runtime_id, jd_tdb);
    }

    const uint32_t matrix_flags = (flags & TAIYIN_NATIVE_POSITION_SPEED) != 0u
        ? TAIYIN_APPARENT_SPHERICAL | TAIYIN_APPARENT_VELOCITY
        : TAIYIN_APPARENT_SPHERICAL;
    double precession_matrix[9];
    double nutation_matrix_values[9];
    double output_matrix[9];
    double output_matrix_dot[9];
    double output_matrix_ddot[9];
    const int output_frame_id = star_output_frame(*context, flags);
    if (output_frame_id < 0) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_UNSUPPORTED, resolved_runtime_id, jd_tdb);
    }
    if (!calc_apparent_matrices(
            jd_tt,
            matrix_flags,
            output_frame_id,
            context->model_context.precession_model_id,
            context->model_context.nutation_model_id,
            context->model_context.obliquity_model_id,
            context->model_context.frame_route_id,
            context->apparent_options.celestial_pole_offset_dx_rad,
            context->apparent_options.celestial_pole_offset_dy_rad,
            context->apparent_options.celestial_pole_offset_dx_rate_rad_per_day,
            context->apparent_options.celestial_pole_offset_dy_rate_rad_per_day,
            context->apparent_options.matrix_derivative_step_days,
            precession_matrix,
            nutation_matrix_values,
            output_matrix,
            output_matrix_dot,
            output_matrix_ddot,
            0,
            0,
            0,
            0,
            context->apparent_options.custom_output_frame_evaluator,
            context->apparent_options.custom_output_frame_data)) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_UNSUPPORTED, resolved_runtime_id, jd_tdb);
    }

    const Vector3 position = transform_vector(state.position_au, output_matrix);
    const Vector3 velocity = transform_velocity_array(
        state.position_au, state.velocity_au_per_day, output_matrix, output_matrix_dot);
    if ((flags & TAIYIN_NATIVE_POSITION_XYZ) != 0u) {
        out[0] = position.x;
        out[1] = position.y;
        out[2] = position.z;
        if ((flags & TAIYIN_NATIVE_POSITION_SPEED) != 0u) {
            out[3] = velocity.x;
            out[4] = velocity.y;
            out[5] = velocity.z;
        }
    } else {
        EclipticPositionVelocity spherical;
        if (!cartesian_position_velocity_to_ecliptic(position, velocity, &spherical)) {
            return fail_position(out, diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED,
                                 resolved_runtime_id, jd_tdb);
        }
        const double angle_scale = (flags & TAIYIN_NATIVE_POSITION_RADIANS) != 0u
            ? 1.0 : TAIYIN_RAD_TO_DEG;
        out[0] = spherical.longitude_rad * angle_scale;
        out[1] = spherical.latitude_rad * angle_scale;
        out[2] = spherical.radius_au;
        if ((flags & TAIYIN_NATIVE_POSITION_SPEED) != 0u) {
            out[3] = spherical.longitude_rate_rad_per_day * angle_scale;
            out[4] = spherical.latitude_rate_rad_per_day * angle_scale;
            out[5] = spherical.radius_rate_au_per_day;
        }
    }
    if (!std::isfinite(out[0]) || !std::isfinite(out[1]) || !std::isfinite(out[2])) {
        return fail_position(out, diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED,
                             resolved_runtime_id, jd_tdb);
    }
    set_diagnostic(diagnostic, TAIYIN_STATUS_OK, resolved_runtime_id, jd_tdb);
    return TAIYIN_STATUS_OK;
}

}  // namespace

Status add_global_tsc1_star_catalog(const char* path) noexcept {
    if (!path || path[0] == '\0') {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    std::unique_ptr<Tsc1StarProvider> provider(new (std::nothrow) Tsc1StarProvider());
    if (!provider) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    if (!provider->load_from_file(path)) {
        return TAIYIN_FILE_ERROR_NOT_FOUND;
    }
    GlobalStarCatalogStore& store = global_star_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    try {
        store.tsc1_providers.push_back(std::move(provider));
    } catch (...) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    return TAIYIN_STATUS_OK;
}

Status add_global_tsc1_star_catalog_from_memory(const uint8_t* data, size_t size) noexcept {
    if (!data || size == 0) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    std::unique_ptr<uint8_t[]> owned_data(new (std::nothrow) uint8_t[size]);
    if (!owned_data) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    std::memcpy(owned_data.get(), data, size);
    std::unique_ptr<Tsc1StarProvider> provider(new (std::nothrow) Tsc1StarProvider());
    if (!provider) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    if (!provider->load_from_memory(owned_data.get(), size)) {
        return TAIYIN_FILE_ERROR_BAD_FORMAT;
    }
    GlobalStarCatalogStore& store = global_star_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    try {
        store.tsc1_owned_memory.reserve(store.tsc1_owned_memory.size() + 1);
        store.tsc1_providers.reserve(store.tsc1_providers.size() + 1);
        store.tsc1_owned_memory.push_back(std::move(owned_data));
        store.tsc1_providers.push_back(std::move(provider));
    } catch (...) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    return TAIYIN_STATUS_OK;
}

Status add_global_tsf1_star_catalog(const char* path) noexcept {
    if (!path || path[0] == '\0') {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    std::unique_ptr<Tsf1StarProvider> provider(new (std::nothrow) Tsf1StarProvider());
    if (!provider) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    if (!provider->load_from_file(path)) {
        return TAIYIN_FILE_ERROR_NOT_FOUND;
    }
    GlobalStarCatalogStore& store = global_star_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    try {
        store.tsf1_providers.push_back(std::move(provider));
    } catch (...) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    return TAIYIN_STATUS_OK;
}

void clear_global_star_catalogs() noexcept {
    GlobalStarCatalogStore& store = global_star_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    store.tsc1_providers.clear();
    store.tsc1_owned_memory.clear();
    store.tsf1_providers.clear();
}

size_t global_star_catalog_count() noexcept {
    GlobalStarCatalogStore& store = global_star_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    return store.tsc1_providers.size() + store.tsf1_providers.size();
}

Status find_global_star_magnitude(const char* star_key, double* out_magnitude) noexcept {
    if (out_magnitude) {
        *out_magnitude = NAN;
    }
    if (!star_key || star_key[0] == '\0' || !out_magnitude) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    GlobalStarCatalogStore& store = global_star_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    for (size_t i = 0; i < store.tsc1_providers.size(); ++i) {
        Tsc1StarProvider* provider = store.tsc1_providers[i].get();
        Tsc1ResolvedStar resolved;
        if (provider && provider->resolve(star_key, &resolved) && resolved.record
            && std::isfinite(resolved.record->magnitude)) {
            *out_magnitude = resolved.record->magnitude;
            return TAIYIN_STATUS_OK;
        }
    }
    for (size_t i = 0; i < store.tsf1_providers.size(); ++i) {
        Tsf1StarProvider* provider = store.tsf1_providers[i].get();
        Tsc1ResolvedStar resolved;
        if (provider && provider->resolve(star_key, &resolved) && resolved.record
            && std::isfinite(resolved.record->magnitude)) {
            *out_magnitude = resolved.record->magnitude;
            return TAIYIN_STATUS_OK;
        }
    }
    return TAIYIN_EPHEMERIS_ERROR_NO_ROUTE;
}

Status calc_star_position_tdb(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint64_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_out(out);
    if (!context) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, 0, jd_tdb);
    }
    if (context->observer_id != TAIYIN_BODY_EARTH) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_UNSUPPORTED, 0, jd_tdb);
    }
    const SplitJulianDate resolved_jd_tt = split_julian_date_is_finite(jd_tt)
        && jd_tt != SplitJulianDate() ? jd_tt : jd_tdb;
    if (!star_key || star_key[0] == '\0' || !out
        || !split_julian_date_is_finite(jd_tdb)
        || !split_julian_date_is_finite(resolved_jd_tt)) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, 0, jd_tdb);
    }
    if ((flags & ~SUPPORTED_STAR_POSITION_FLAGS) != 0u) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_UNSUPPORTED, 0, jd_tdb);
    }

    CartesianState star_bary;
    int runtime_id = 0;
    if (!eval_global_star_state(star_key, jd_tdb, &star_bary, &runtime_id)
        || !native_cartesian_state_is_finite(star_bary)) {
        return fail_position(out, diagnostic, TAIYIN_FILE_ERROR_NOT_FOUND, runtime_id, jd_tdb);
    }
    return calc_star_position_from_barycentric_tdb(
        context, star_bary, runtime_id, jd_tdb, resolved_jd_tt, flags, out, diagnostic);
}

Status calc_builtin_star_position_tt(
    const NativeCalcContext* context,
    const BuiltinStarAstrometry& astrometry,
    const SplitJulianDate& jd_tt,
    uint64_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_out(out);
    if (!context || !out || !split_julian_date_is_finite(jd_tt)
        || !std::isfinite(astrometry.ra_j2000_rad)
        || !std::isfinite(astrometry.dec_j2000_rad)
        || !std::isfinite(astrometry.pm_ra_mas_per_year)
        || !std::isfinite(astrometry.pm_dec_mas_per_year)
        || !std::isfinite(astrometry.parallax_mas)
        || !std::isfinite(astrometry.radial_velocity_km_per_second)
        || !std::isfinite(astrometry.reference_jd_tdb)
        || astrometry.parallax_mas < 0.0) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, 0, jd_tt);
    }

    SplitJulianDate jd_tdb;
    const Status time_status = resolve_tt_to_tdb(*context, jd_tt, &jd_tdb);
    if (time_status != TAIYIN_STATUS_OK) {
        return fail_position(out, diagnostic, time_status, 0, jd_tt);
    }

    const bool has_parallax = astrometry.parallax_mas > 0.0;
    const double distance_au = has_parallax
        ? TAIYIN_AU_PER_PARALLAX_MAS / astrometry.parallax_mas
        : 1.0e9;
    const Vector3 direction = spherical_to_cartesian(
        astrometry.ra_j2000_rad, astrometry.dec_j2000_rad, 1.0);
    const Vector3 e_alpha = {
        -std::sin(astrometry.ra_j2000_rad), std::cos(astrometry.ra_j2000_rad), 0.0
    };
    const Vector3 e_delta = {
        -std::sin(astrometry.dec_j2000_rad) * std::cos(astrometry.ra_j2000_rad),
        -std::sin(astrometry.dec_j2000_rad) * std::sin(astrometry.ra_j2000_rad),
        std::cos(astrometry.dec_j2000_rad)
    };
    const double radial_velocity_au_per_day = astrometry.radial_velocity_km_per_second
        * TAIYIN_KM_PER_S_TO_AU_PER_DAY;
    const double pm_ra_au_per_day = has_parallax
        ? astrometry.pm_ra_mas_per_year / (astrometry.parallax_mas * DAYS_PER_JULIAN_YEAR)
        : astrometry.pm_ra_mas_per_year * TAIYIN_MAS_PER_YEAR_TO_RAD_PER_DAY * distance_au;
    const double pm_dec_au_per_day = has_parallax
        ? astrometry.pm_dec_mas_per_year / (astrometry.parallax_mas * DAYS_PER_JULIAN_YEAR)
        : astrometry.pm_dec_mas_per_year * TAIYIN_MAS_PER_YEAR_TO_RAD_PER_DAY * distance_au;

    CartesianState star_bary;
    star_bary.position_au = vector3_scale(direction, distance_au);
    star_bary.velocity_au_per_day = vector3_add(
        vector3_scale(direction, radial_velocity_au_per_day),
        vector3_add(vector3_scale(e_alpha, pm_ra_au_per_day), vector3_scale(e_delta, pm_dec_au_per_day)));
    star_bary.acceleration_au_per_day2 = zero_vector();
    SplitJulianDate reference_jd_tdb;
    if (!split_julian_date_from_double(astrometry.reference_jd_tdb, &reference_jd_tdb)) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, 0, jd_tdb);
    }
    const double elapsed_days = days_between_split_jd(reference_jd_tdb, jd_tdb);
    star_bary.position_au = vector3_add(
        star_bary.position_au, vector3_scale(star_bary.velocity_au_per_day, elapsed_days));
    if (!native_cartesian_state_is_finite(star_bary)) {
        return fail_position(out, diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, 0, jd_tdb);
    }
    return calc_star_position_from_barycentric_tdb(
        context, star_bary, 0, jd_tdb, jd_tt, flags, out, diagnostic);
}

Status calc_star_position_tt(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& jd_tt,
    uint64_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_out(out);
    if (!context || !split_julian_date_is_finite(jd_tt)) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, 0, jd_tt);
    }
    SplitJulianDate jd_tdb;
    const Status time_status = resolve_tt_to_tdb(*context, jd_tt, &jd_tdb);
    if (time_status != TAIYIN_STATUS_OK) {
        return fail_position(out, diagnostic, time_status, 0, jd_tt);
    }
    return calc_star_position_tdb(context, star_key, jd_tdb, jd_tt, flags, out, diagnostic);
}

Status calc_star_position_ut_delta_t(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& jd_ut1,
    double delta_t_seconds,
    uint64_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_out(out);
    if (!context || !split_julian_date_is_finite(jd_ut1) || !std::isfinite(delta_t_seconds)) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, 0, jd_ut1);
    }
    const NativeCalcContext& ctx = *context;
    SplitJulianDate jd_tt;
    if (!ut1_to_tt_split_jd(jd_ut1, delta_t_seconds, &jd_tt)) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_UNSUPPORTED, 0, jd_ut1);
    }
    SplitJulianDate jd_tdb;
    const Status time_status = resolve_tt_to_tdb(ctx, jd_tt, &jd_tdb);
    if (time_status != TAIYIN_STATUS_OK) {
        return fail_position(out, diagnostic, time_status, 0, jd_ut1);
    }
    return calc_star_position_tdb(context, star_key, jd_tdb, jd_tt, flags, out, diagnostic);
}

Status calc_star_position_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_out(out);
    if (!context) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, 0, jd_ut);
    }
    SplitJulianDate jd_tdb;
    SplitJulianDate jd_tt;
    const Status time_status = resolve_ut_to_tdb_tt(*context, jd_ut, &jd_tdb, &jd_tt);
    if (time_status != TAIYIN_STATUS_OK) {
        return fail_position(out, diagnostic, time_status, 0, jd_ut);
    }
    return calc_star_position_tdb(context, star_key, jd_tdb, jd_tt, flags, out, diagnostic);
}

Status calc_star_positions_tdb(
    const NativeCalcContext* context,
    const char* const* star_keys,
    size_t star_count,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint64_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept {
    if (star_count == 0) {
        return TAIYIN_STATUS_OK;
    }
    if (!star_keys || !out) {
        for (size_t i = 0; out && i < star_count; ++i) {
            clear_out(out + i * 6);
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    Status first_status = TAIYIN_STATUS_OK;
    for (size_t i = 0; i < star_count; ++i) {
        const Status status = calc_star_position_tdb(
            context,
            star_keys[i],
            jd_tdb,
            jd_tt,
            flags,
            out + i * 6,
            diagnostics ? diagnostics + i : 0);
        if (first_status == TAIYIN_STATUS_OK && status != TAIYIN_STATUS_OK) {
            first_status = status;
        }
    }
    return first_status;
}

Status calc_star_positions_tt(
    const NativeCalcContext* context,
    const char* const* star_keys,
    size_t star_count,
    const SplitJulianDate& jd_tt,
    uint64_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept {
    if (star_count == 0) {
        return TAIYIN_STATUS_OK;
    }
    if (!context || !star_keys || !out || !split_julian_date_is_finite(jd_tt)) {
        for (size_t i = 0; out && i < star_count; ++i) {
            clear_out(out + i * 6);
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate jd_tdb;
    const Status time_status = resolve_tt_to_tdb(*context, jd_tt, &jd_tdb);
    if (time_status != TAIYIN_STATUS_OK) {
        for (size_t i = 0; out && i < star_count; ++i) {
            clear_out(out + i * 6);
        }
        return time_status;
    }
    return calc_star_positions_tdb(context, star_keys, star_count, jd_tdb, jd_tt, flags, out, diagnostics);
}

Status calc_star_positions_ut_delta_t(
    const NativeCalcContext* context,
    const char* const* star_keys,
    size_t star_count,
    const SplitJulianDate& jd_ut1,
    double delta_t_seconds,
    uint64_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept {
    if (star_count == 0) {
        return TAIYIN_STATUS_OK;
    }
    if (!context || !star_keys || !out
        || !split_julian_date_is_finite(jd_ut1) || !std::isfinite(delta_t_seconds)) {
        for (size_t i = 0; out && i < star_count; ++i) {
            clear_out(out + i * 6);
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate jd_tt;
    if (!ut1_to_tt_split_jd(jd_ut1, delta_t_seconds, &jd_tt)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    SplitJulianDate jd_tdb;
    const Status time_status = resolve_tt_to_tdb(*context, jd_tt, &jd_tdb);
    if (time_status != TAIYIN_STATUS_OK) {
        for (size_t i = 0; out && i < star_count; ++i) {
            clear_out(out + i * 6);
        }
        return time_status;
    }
    return calc_star_positions_tdb(context, star_keys, star_count, jd_tdb, jd_tt, flags, out, diagnostics);
}

Status calc_star_positions_ut(
    const NativeCalcContext* context,
    const char* const* star_keys,
    size_t star_count,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept {
    if (star_count == 0) {
        return TAIYIN_STATUS_OK;
    }
    if (!context || !star_keys || !out) {
        for (size_t i = 0; out && i < star_count; ++i) {
            clear_out(out + i * 6);
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate jd_tdb;
    SplitJulianDate jd_tt;
    const Status time_status = resolve_ut_to_tdb_tt(*context, jd_ut, &jd_tdb, &jd_tt);
    if (time_status != TAIYIN_STATUS_OK) {
        for (size_t i = 0; out && i < star_count; ++i) {
            clear_out(out + i * 6);
        }
        return time_status;
    }
    return calc_star_positions_tdb(context, star_keys, star_count, jd_tdb, jd_tt, flags, out, diagnostics);
}

Status calc_observed_star_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    ObservedPosition* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_observed_stars_ut(context, &star_key, 1, jd_ut, flags, out, diagnostic);
}

Status calc_observed_stars_ut(
    const NativeCalcContext* context,
    const char* const* star_keys,
    size_t star_count,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    ObservedPosition* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept {
    clear_observed_out(out, star_count);
    if (star_count == 0) {
        return TAIYIN_STATUS_OK;
    }
    if (!context || !star_keys || !out || !split_julian_date_is_finite(jd_ut)) {
        return fail_observed(out, star_count, diagnostics, TAIYIN_ERROR_INVALID_ARGUMENT, 0, jd_ut);
    }
    if ((flags & ~SUPPORTED_STAR_OBSERVED_FLAGS) != 0u) {
        return fail_observed(out, star_count, diagnostics, TAIYIN_ERROR_UNSUPPORTED, 0, jd_ut);
    }
    if (context->observer_id != TAIYIN_BODY_EARTH) {
        return fail_observed(
            out,
            star_count,
            diagnostics,
            TAIYIN_ERROR_UNSUPPORTED,
            0,
            jd_ut);
    }

    const bool want_topocentric = (flags & TAIYIN_OBSERVED_TOPOCENTRIC) != 0u;
    const bool want_horizontal = (flags & (TAIYIN_OBSERVED_HORIZONTAL | TAIYIN_OBSERVED_REFRACTION)) != 0u;
    const bool want_refraction = (flags & TAIYIN_OBSERVED_REFRACTION) != 0u;
    if (want_horizontal && !want_topocentric) {
        return fail_observed(out, star_count, diagnostics, TAIYIN_ERROR_INVALID_ARGUMENT, 0, jd_ut);
    }
    if (want_horizontal
        && (!context->fields.has(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION)
            || !native_observer_location_is_finite(context->observer_location))) {
        return fail_observed(out, star_count, diagnostics, TAIYIN_ERROR_INVALID_ARGUMENT, 0, jd_ut);
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
            return fail_observed(out, star_count, diagnostics, TAIYIN_ERROR_INVALID_ARGUMENT, 0, jd_ut);
        }
    }

    SplitJulianDate jd_tdb;
    SplitJulianDate jd_tt;
    const Status time_status = resolve_ut_to_tdb_tt(*context, jd_ut, &jd_tdb, &jd_tt);
    if (time_status != TAIYIN_STATUS_OK) {
        return fail_observed(out, star_count, diagnostics, time_status, 0, jd_ut);
    }

    const uint32_t matrix_flags = (flags & TAIYIN_OBSERVED_SPEED) != 0u
        ? TAIYIN_APPARENT_SPHERICAL | TAIYIN_APPARENT_VELOCITY
        : TAIYIN_APPARENT_SPHERICAL;
    double output_matrix[9];
    double output_matrix_dot[9];
    double output_matrix_ddot[9];
    if (!calc_star_output_matrices(*context, jd_tt, matrix_flags, output_matrix, output_matrix_dot, output_matrix_ddot)) {
        return fail_observed(out, star_count, diagnostics, TAIYIN_ERROR_UNSUPPORTED, 0, jd_tdb);
    }

    double local_sidereal = 0.0;
    double local_sidereal_rate = 0.0;
    if (want_horizontal
        && !calc_star_local_sidereal(*context, jd_ut, jd_tt, &local_sidereal, &local_sidereal_rate)) {
        return fail_observed(out, star_count, diagnostics, TAIYIN_ERROR_UNSUPPORTED, 0, jd_tdb);
    }

    Status first_status = TAIYIN_STATUS_OK;
    for (size_t i = 0; i < star_count; ++i) {
        out[i] = ObservedPosition();
        if (!star_keys[i] || star_keys[i][0] == '\0') {
            out[i].status = TAIYIN_ERROR_INVALID_ARGUMENT;
            set_diagnostic(&out[i].diagnostic, out[i].status, 0, jd_tdb);
            if (diagnostics) {
                diagnostics[i] = out[i].diagnostic;
            }
            if (first_status == TAIYIN_STATUS_OK) {
                first_status = out[i].status;
            }
            continue;
        }

        CartesianState icrf_state;
        int runtime_id = 0;
        EphemerisEvalDiagnostic diagnostic;
        const Status status = build_observed_star_state(
            *context,
            star_keys[i],
            jd_tdb,
            jd_tt,
            jd_ut,
            flags,
            &icrf_state,
            &runtime_id,
            &diagnostic);
        out[i].body_id = runtime_id;
        out[i].status = status;
        out[i].diagnostic = diagnostic;
        if (diagnostics) {
            diagnostics[i] = diagnostic;
        }
        if (status != TAIYIN_STATUS_OK) {
            if (first_status == TAIYIN_STATUS_OK) {
                first_status = status;
            }
            continue;
        }

        CartesianState true_equator;
        true_equator.position_au = transform_vector(icrf_state.position_au, output_matrix);
        true_equator.velocity_au_per_day = transform_velocity_array(
            icrf_state.position_au,
            icrf_state.velocity_au_per_day,
            output_matrix,
            output_matrix_dot);
        true_equator.acceleration_au_per_day2 = transform_acceleration_array(
            icrf_state.position_au,
            icrf_state.velocity_au_per_day,
            icrf_state.acceleration_au_per_day2,
            output_matrix,
            output_matrix_dot,
            output_matrix_ddot);
        if (!native_cartesian_state_is_finite(true_equator)) {
            out[i].status = TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
            out[i].diagnostic.status = out[i].status;
            if (diagnostics) {
                diagnostics[i] = out[i].diagnostic;
            }
            if (first_status == TAIYIN_STATUS_OK) {
                first_status = out[i].status;
            }
            continue;
        }

        EclipticPositionVelocity spherical;
        if (!cartesian_position_velocity_to_ecliptic(true_equator.position_au, true_equator.velocity_au_per_day, &spherical)) {
            out[i].status = TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
            out[i].diagnostic.status = out[i].status;
            if (diagnostics) {
                diagnostics[i] = out[i].diagnostic;
            }
            if (first_status == TAIYIN_STATUS_OK) {
                first_status = out[i].status;
            }
            continue;
        }

        out[i].apparent.body_id = runtime_id;
        out[i].apparent.status = TAIYIN_STATUS_OK;
        out[i].apparent.diagnostic = diagnostic;
        out[i].apparent.geometric_state = true_equator;
        out[i].apparent.apparent_state = true_equator;
        out[i].apparent.longitude_rad = spherical.longitude_rad;
        out[i].apparent.latitude_rad = spherical.latitude_rad;
        out[i].apparent.distance_au = spherical.radius_au;
        out[i].apparent.light_time_days = 0.0;
        out[i].apparent.cache_hit = true;

        if (want_horizontal) {
            out[i].horizontal = topocentric_position_to_horizontal(
                true_equator.position_au,
                local_sidereal,
                context->observer_location.latitude_rad);
            out[i].refracted_horizontal = out[i].horizontal;
            if ((flags & TAIYIN_OBSERVED_SPEED) != 0u) {
                topocentric_velocity_to_horizontal_rates(
                    true_equator.position_au,
                    true_equator.velocity_au_per_day,
                    local_sidereal,
                    local_sidereal_rate,
                    context->observer_location.latitude_rad,
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

    return first_status;
}

}  // namespace runtime
}  // namespace taiyin
