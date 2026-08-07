#include "taiyin/astrology/targets.h"

#include "taiyin/astrology/lunar_points.h"
#include "taiyin/angle.h"
#include "taiyin/runtime/native_position.h"

#include <cmath>

namespace taiyin {
namespace astrology {
namespace {

void clear_position(double out[6]) noexcept {
    if (!out) return;
    for (int i = 0; i < 6; ++i) out[i] = 0.0;
}

void set_diagnostic(
    runtime::EphemerisEvalDiagnostic* diagnostic,
    Status status,
    int target_id,
    const runtime::NativeCalcContext* context,
    const SplitJulianDate& jd_tdb
) noexcept {
    if (!diagnostic) return;
    if (diagnostic->target_id != 0 && diagnostic->target_id != target_id) {
        diagnostic->component_target_id = diagnostic->target_id;
        diagnostic->component_center_id = diagnostic->center_id;
    }
    diagnostic->status = status;
    diagnostic->target_id = target_id;
    diagnostic->center_id = context ? context->observer_id : 0;
    diagnostic->jd_tdb = jd_tdb;
}

uint32_t point_definition_flags(uint32_t flags) noexcept {
    // These flags affect how this adapter presents its result, rather than
    // the geocentric lunar-point definition consumed by the typed APIs.
    return flags & ~(runtime::TAIYIN_NATIVE_POSITION_SPEED
        | runtime::TAIYIN_NATIVE_POSITION_XYZ
        | runtime::TAIYIN_NATIVE_POSITION_RADIANS
        | runtime::TAIYIN_NATIVE_POSITION_TOPOCENTRIC
        | runtime::TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX);
}

Status write_spherical_position(
    const runtime::NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_tdb,
    uint32_t flags,
    double longitude_rad,
    double latitude_rad,
    double distance_au,
    double longitude_rate_rad_per_day,
    double latitude_rate_rad_per_day,
    double distance_rate_au_per_day,
    double out[6],
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_position(out);
    if (!context || !out || !std::isfinite(longitude_rad) || !std::isfinite(latitude_rad)) {
        set_diagnostic(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, target_id, context, jd_tdb);
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    if ((flags & runtime::TAIYIN_NATIVE_POSITION_XYZ) != 0u) {
        const double cos_longitude = std::cos(longitude_rad);
        const double sin_longitude = std::sin(longitude_rad);
        const double cos_latitude = std::cos(latitude_rad);
        const double sin_latitude = std::sin(latitude_rad);
        out[0] = distance_au * cos_latitude * cos_longitude;
        out[1] = distance_au * cos_latitude * sin_longitude;
        out[2] = distance_au * sin_latitude;
        if ((flags & runtime::TAIYIN_NATIVE_POSITION_SPEED) != 0u) {
            out[3] = distance_rate_au_per_day * cos_latitude * cos_longitude
                - distance_au * latitude_rate_rad_per_day * sin_latitude * cos_longitude
                - distance_au * longitude_rate_rad_per_day * cos_latitude * sin_longitude;
            out[4] = distance_rate_au_per_day * cos_latitude * sin_longitude
                - distance_au * latitude_rate_rad_per_day * sin_latitude * sin_longitude
                + distance_au * longitude_rate_rad_per_day * cos_latitude * cos_longitude;
            out[5] = distance_rate_au_per_day * sin_latitude
                + distance_au * latitude_rate_rad_per_day * cos_latitude;
        }
        set_diagnostic(diagnostic, TAIYIN_STATUS_OK, target_id, context, jd_tdb);
        return TAIYIN_STATUS_OK;
    }
    const double angle_scale = (flags & runtime::TAIYIN_NATIVE_POSITION_RADIANS) != 0u
        ? 1.0 : TAIYIN_RAD_TO_DEG;
    out[0] = longitude_rad * angle_scale;
    out[1] = latitude_rad * angle_scale;
    out[2] = distance_au;
    if ((flags & runtime::TAIYIN_NATIVE_POSITION_SPEED) != 0u) {
        out[3] = longitude_rate_rad_per_day * angle_scale;
        out[4] = latitude_rate_rad_per_day * angle_scale;
        out[5] = distance_rate_au_per_day;
    }
    set_diagnostic(diagnostic, TAIYIN_STATUS_OK, target_id, context, jd_tdb);
    return TAIYIN_STATUS_OK;
}

Status eval_lunar_point_tdb(
    const runtime::NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    double out[6],
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_position(out);
    if (diagnostic) *diagnostic = runtime::EphemerisEvalDiagnostic();
    if (!context || !out || !split_julian_date_is_finite(jd_tdb)) {
        set_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, context, jd_tdb);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const SplitJulianDate resolved_jd_tt = split_julian_date_is_finite(jd_tt)
        ? jd_tt
        : jd_tdb;
    const uint32_t definition_flags = point_definition_flags(flags);
    if (target_id == TAIYIN_ASTROLOGY_TARGET_TRUE_NODE
        || target_id == TAIYIN_ASTROLOGY_TARGET_TRUE_DESCENDING_NODE
        || target_id == TAIYIN_ASTROLOGY_TARGET_MEAN_NODE
        || target_id == TAIYIN_ASTROLOGY_TARGET_MEAN_DESCENDING_NODE) {
        const LunarNodeKind kind = (target_id == TAIYIN_ASTROLOGY_TARGET_TRUE_DESCENDING_NODE
                || target_id == TAIYIN_ASTROLOGY_TARGET_MEAN_DESCENDING_NODE)
            ? TAIYIN_LUNAR_NODE_DESCENDING : TAIYIN_LUNAR_NODE_ASCENDING;
        LunarNodePosition node;
        const Status status = (target_id == TAIYIN_ASTROLOGY_TARGET_TRUE_NODE
                || target_id == TAIYIN_ASTROLOGY_TARGET_TRUE_DESCENDING_NODE)
            ? calc_lunar_true_node_tt(context, resolved_jd_tt, kind, definition_flags, &node, diagnostic)
            : calc_lunar_mean_node_tt(context, resolved_jd_tt, kind, definition_flags, &node, diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            set_diagnostic(diagnostic, status, target_id, context, jd_tdb);
            return status;
        }
        return write_spherical_position(
            context, target_id, jd_tdb, flags, node.longitude_rad, 0.0, NAN,
            node.longitude_rate_rad_per_day, 0.0, NAN, out, diagnostic);
    }

    LunarApsisPosition apogee;
    Status status = TAIYIN_ERROR_INVALID_ARGUMENT;
    if (target_id == TAIYIN_ASTROLOGY_TARGET_MEAN_LILITH) {
        status = calc_lunar_mean_apogee_tt(
            context, resolved_jd_tt, definition_flags, &apogee, diagnostic);
    } else if (target_id == TAIYIN_ASTROLOGY_TARGET_OSCULATING_LILITH) {
        status = calc_lunar_osculating_apogee_tt(
            context, resolved_jd_tt, definition_flags, &apogee, diagnostic);
    } else if (target_id == TAIYIN_ASTROLOGY_TARGET_FITTED_LILITH) {
        status = calc_lunar_fitted_apogee_tt(
            context, resolved_jd_tt, definition_flags, &apogee, diagnostic);
    }
    if (status != TAIYIN_STATUS_OK) {
        set_diagnostic(diagnostic, status, target_id, context, jd_tdb);
        return status;
    }
    return write_spherical_position(
        context, target_id, jd_tdb, flags,
        apogee.longitude_rad, apogee.latitude_rad, apogee.distance_au,
        apogee.longitude_rate_rad_per_day, apogee.latitude_rate_rad_per_day,
        apogee.distance_rate_au_per_day,
        out, diagnostic);
}

}  // namespace

Status register_builtin_astrology_targets() noexcept {
    const int target_ids[] = {
        TAIYIN_ASTROLOGY_TARGET_TRUE_NODE,
        TAIYIN_ASTROLOGY_TARGET_TRUE_DESCENDING_NODE,
        TAIYIN_ASTROLOGY_TARGET_MEAN_NODE,
        TAIYIN_ASTROLOGY_TARGET_MEAN_DESCENDING_NODE,
        TAIYIN_ASTROLOGY_TARGET_MEAN_LILITH,
        TAIYIN_ASTROLOGY_TARGET_OSCULATING_LILITH,
        TAIYIN_ASTROLOGY_TARGET_FITTED_LILITH,
    };
    for (size_t i = 0; i < sizeof(target_ids) / sizeof(target_ids[0]); ++i) {
        if (!runtime::register_global_native_position_evaluator(target_ids[i], eval_lunar_point_tdb)) {
            return TAIYIN_RUNTIME_ERROR_REGISTRY_FAILED;
        }
    }
    return TAIYIN_STATUS_OK;
}

}  // namespace astrology
}  // namespace taiyin
