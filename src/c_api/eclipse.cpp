#include "taiyin/c/eclipse.h"

#include "c_api_internal.h"
#include "taiyin/runtime/eclipse_search.h"

#include <cstring>
#include <new>
#include <vector>

namespace {

bool valid_diagnostic(taiyin_ephemeris_diagnostic* diagnostic) noexcept {
    return !diagnostic || taiyin_c_internal::valid_struct(diagnostic);
}

template <typename T>
void init_struct(T* value) noexcept {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void copy_lunar_tt(
    const taiyin::runtime::LunarEclipseResult& source,
    taiyin_lunar_eclipse_result_tt* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->kind = source.kind;
    taiyin_c_internal::from_cpp_split_jd(source.maximum_jd_tt, &out->maximum_jd_tt);
    out->umbral_magnitude = source.umbral_magnitude;
    out->penumbral_magnitude = source.penumbral_magnitude;
    out->axis_distance_rad = source.axis_distance_rad;
    out->umbra_radius_rad = source.umbra_radius_rad;
    out->penumbra_radius_rad = source.penumbra_radius_rad;
    out->moon_radius_rad = source.moon_radius_rad;
    for (size_t i = 0; i < TAIYIN_C_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
        taiyin_c_internal::from_cpp_split_jd(
            source.contact_jd_tt[i], &out->contact_jd_tt[i]);
    }
}

void copy_lunar_ut(
    const taiyin::runtime::LunarEclipseResultUt& source,
    taiyin_lunar_eclipse_result_ut* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->kind = source.kind;
    taiyin_c_internal::from_cpp_split_jd(source.maximum_jd_ut, &out->maximum_jd_ut);
    out->delta_t_seconds = source.delta_t_seconds;
    out->umbral_magnitude = source.umbral_magnitude;
    out->penumbral_magnitude = source.penumbral_magnitude;
    out->axis_distance_rad = source.axis_distance_rad;
    out->umbra_radius_rad = source.umbra_radius_rad;
    out->penumbra_radius_rad = source.penumbra_radius_rad;
    out->moon_radius_rad = source.moon_radius_rad;
    for (size_t i = 0; i < TAIYIN_C_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
        taiyin_c_internal::from_cpp_split_jd(
            source.contact_jd_ut[i], &out->contact_jd_ut[i]);
    }
}

taiyin::runtime::LunarEclipseResult to_cpp_lunar_tt(
    const taiyin_lunar_eclipse_result_tt& source
) noexcept {
    taiyin::runtime::LunarEclipseResult out;
    out.kind = source.kind;
    out.maximum_jd_tt = taiyin_c_internal::to_cpp_split_jd(source.maximum_jd_tt);
    out.umbral_magnitude = source.umbral_magnitude;
    out.penumbral_magnitude = source.penumbral_magnitude;
    out.axis_distance_rad = source.axis_distance_rad;
    out.umbra_radius_rad = source.umbra_radius_rad;
    out.penumbra_radius_rad = source.penumbra_radius_rad;
    out.moon_radius_rad = source.moon_radius_rad;
    for (size_t i = 0; i < TAIYIN_C_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
        out.contact_jd_tt[i] =
            taiyin_c_internal::to_cpp_split_jd(source.contact_jd_tt[i]);
    }
    return out;
}

taiyin::runtime::LunarEclipseResultUt to_cpp_lunar_ut(
    const taiyin_lunar_eclipse_result_ut& source
) noexcept {
    taiyin::runtime::LunarEclipseResultUt out;
    out.kind = source.kind;
    out.maximum_jd_ut = taiyin_c_internal::to_cpp_split_jd(source.maximum_jd_ut);
    out.delta_t_seconds = source.delta_t_seconds;
    out.umbral_magnitude = source.umbral_magnitude;
    out.penumbral_magnitude = source.penumbral_magnitude;
    out.axis_distance_rad = source.axis_distance_rad;
    out.umbra_radius_rad = source.umbra_radius_rad;
    out.penumbra_radius_rad = source.penumbra_radius_rad;
    out.moon_radius_rad = source.moon_radius_rad;
    for (size_t i = 0; i < TAIYIN_C_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
        out.contact_jd_ut[i] =
            taiyin_c_internal::to_cpp_split_jd(source.contact_jd_ut[i]);
    }
    return out;
}

void copy_local_lunar_tt(
    const taiyin::runtime::LocalLunarEclipseResult& source,
    taiyin_local_lunar_eclipse_result_tt* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->eclipse_kind = source.eclipse_kind;
    out->visibility_flags = source.visibility_flags;
    taiyin_c_internal::from_cpp_split_jd(source.maximum_jd_tt, &out->maximum_jd_tt);
    out->umbral_magnitude = source.umbral_magnitude;
    out->penumbral_magnitude = source.penumbral_magnitude;
    for (size_t i = 0; i < TAIYIN_C_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
        taiyin_c_internal::from_cpp_split_jd(
            source.contact_jd_tt[i], &out->contact_jd_tt[i]);
        out->contact_moon_altitude_deg[i] =
            source.contact_moon_altitude_deg[i];
        out->contact_moon_azimuth_deg[i] =
            source.contact_moon_azimuth_deg[i];
    }
    taiyin_c_internal::from_cpp_split_jd(source.moonrise_jd_tt, &out->moonrise_jd_tt);
    taiyin_c_internal::from_cpp_split_jd(source.moonset_jd_tt, &out->moonset_jd_tt);
}

void copy_local_lunar_ut(
    const taiyin::runtime::LocalLunarEclipseResultUt& source,
    taiyin_local_lunar_eclipse_result_ut* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->eclipse_kind = source.eclipse_kind;
    out->visibility_flags = source.visibility_flags;
    taiyin_c_internal::from_cpp_split_jd(source.maximum_jd_ut, &out->maximum_jd_ut);
    out->delta_t_seconds = source.delta_t_seconds;
    out->umbral_magnitude = source.umbral_magnitude;
    out->penumbral_magnitude = source.penumbral_magnitude;
    for (size_t i = 0; i < TAIYIN_C_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
        taiyin_c_internal::from_cpp_split_jd(
            source.contact_jd_ut[i], &out->contact_jd_ut[i]);
        out->contact_moon_altitude_deg[i] =
            source.contact_moon_altitude_deg[i];
        out->contact_moon_azimuth_deg[i] =
            source.contact_moon_azimuth_deg[i];
    }
    taiyin_c_internal::from_cpp_split_jd(source.moonrise_jd_ut, &out->moonrise_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.moonset_jd_ut, &out->moonset_jd_ut);
}

void copy_solar_tt(
    const taiyin::runtime::SolarEclipseResult& source,
    taiyin_solar_eclipse_result_tt* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->kind = source.kind;
    taiyin_c_internal::from_cpp_split_jd(source.maximum_jd_tt, &out->maximum_jd_tt);
    out->axis_distance_km = source.axis_distance_km;
    out->penumbra_radius_km = source.penumbra_radius_km;
    out->core_radius_km = source.core_radius_km;
    out->penumbral_margin_km = source.penumbral_margin_km;
    out->central_margin_km = source.central_margin_km;
    out->maximum_latitude_deg = source.maximum_latitude_deg;
    out->maximum_longitude_deg = source.maximum_longitude_deg;
    for (size_t i = 0; i < TAIYIN_C_SOLAR_ECLIPSE_CONTACT_COUNT; ++i) {
        taiyin_c_internal::from_cpp_split_jd(
            source.contact_jd_tt[i], &out->contact_jd_tt[i]);
    }
}

void copy_solar_ut(
    const taiyin::runtime::SolarEclipseResultUt& source,
    taiyin_solar_eclipse_result_ut* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->kind = source.kind;
    taiyin_c_internal::from_cpp_split_jd(source.maximum_jd_ut, &out->maximum_jd_ut);
    out->delta_t_seconds = source.delta_t_seconds;
    out->axis_distance_km = source.axis_distance_km;
    out->penumbra_radius_km = source.penumbra_radius_km;
    out->core_radius_km = source.core_radius_km;
    out->penumbral_margin_km = source.penumbral_margin_km;
    out->central_margin_km = source.central_margin_km;
    out->maximum_latitude_deg = source.maximum_latitude_deg;
    out->maximum_longitude_deg = source.maximum_longitude_deg;
    for (size_t i = 0; i < TAIYIN_C_SOLAR_ECLIPSE_CONTACT_COUNT; ++i) {
        taiyin_c_internal::from_cpp_split_jd(
            source.contact_jd_ut[i], &out->contact_jd_ut[i]);
    }
}

void copy_local_solar_tt(
    const taiyin::runtime::LocalSolarEclipseResult& source,
    taiyin_local_solar_eclipse_result_tt* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->kind = source.kind;
    taiyin_c_internal::from_cpp_split_jd(source.maximum_jd_tt, &out->maximum_jd_tt);
    out->magnitude = source.magnitude;
    out->obscuration = source.obscuration;
    out->sun_altitude_deg = source.sun_altitude_deg;
    out->sun_azimuth_deg = source.sun_azimuth_deg;
    for (size_t i = 0; i < TAIYIN_C_LOCAL_SOLAR_CONTACT_COUNT; ++i) {
        taiyin_c_internal::from_cpp_split_jd(
            source.contact_jd_tt[i], &out->contact_jd_tt[i]);
    }
    out->position_angle_c1_deg = source.position_angle_c1_deg;
    out->position_angle_c4_deg = source.position_angle_c4_deg;
    out->vertex_angle_c1_deg = source.vertex_angle_c1_deg;
    out->vertex_angle_c4_deg = source.vertex_angle_c4_deg;
    out->sunrise_magnitude = source.sunrise_magnitude;
    out->sunset_magnitude = source.sunset_magnitude;
    out->duration_seconds = source.duration_seconds;
    out->moon_sun_radius_ratio = source.moon_sun_radius_ratio;
}

void copy_local_solar_ut(
    const taiyin::runtime::LocalSolarEclipseResultUt& source,
    taiyin_local_solar_eclipse_result_ut* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->kind = source.kind;
    taiyin_c_internal::from_cpp_split_jd(source.maximum_jd_ut, &out->maximum_jd_ut);
    out->delta_t_seconds = source.delta_t_seconds;
    out->magnitude = source.magnitude;
    out->obscuration = source.obscuration;
    out->sun_altitude_deg = source.sun_altitude_deg;
    out->sun_azimuth_deg = source.sun_azimuth_deg;
    for (size_t i = 0; i < TAIYIN_C_LOCAL_SOLAR_CONTACT_COUNT; ++i) {
        taiyin_c_internal::from_cpp_split_jd(
            source.contact_jd_ut[i], &out->contact_jd_ut[i]);
    }
    out->position_angle_c1_deg = source.position_angle_c1_deg;
    out->position_angle_c4_deg = source.position_angle_c4_deg;
    out->vertex_angle_c1_deg = source.vertex_angle_c1_deg;
    out->vertex_angle_c4_deg = source.vertex_angle_c4_deg;
    out->sunrise_magnitude = source.sunrise_magnitude;
    out->sunset_magnitude = source.sunset_magnitude;
    out->duration_seconds = source.duration_seconds;
    out->moon_sun_radius_ratio = source.moon_sun_radius_ratio;
}

void copy_circumstances_tt(
    const taiyin::runtime::LocalSolarEclipseCircumstances& source,
    taiyin_local_solar_eclipse_circumstances_tt* out
) noexcept {
    out->struct_size = sizeof(*out);
    taiyin_c_internal::from_cpp_split_jd(source.jd_tt, &out->jd_tt);
    out->magnitude = source.magnitude;
    out->obscuration = source.obscuration;
    out->center_separation_deg = source.center_separation_deg;
    out->sun_angular_radius_deg = source.sun_angular_radius_deg;
    out->moon_angular_radius_deg = source.moon_angular_radius_deg;
    out->sun_altitude_deg = source.sun_altitude_deg;
    out->sun_azimuth_deg = source.sun_azimuth_deg;
}

void copy_circumstances_ut(
    const taiyin::runtime::LocalSolarEclipseCircumstancesUt& source,
    taiyin_local_solar_eclipse_circumstances_ut* out
) noexcept {
    out->struct_size = sizeof(*out);
    taiyin_c_internal::from_cpp_split_jd(source.jd_ut, &out->jd_ut);
    out->delta_t_seconds = source.delta_t_seconds;
    out->magnitude = source.magnitude;
    out->obscuration = source.obscuration;
    out->center_separation_deg = source.center_separation_deg;
    out->sun_angular_radius_deg = source.sun_angular_radius_deg;
    out->moon_angular_radius_deg = source.moon_angular_radius_deg;
    out->sun_altitude_deg = source.sun_altitude_deg;
    out->sun_azimuth_deg = source.sun_azimuth_deg;
}

void copy_boundary(
    const taiyin::runtime::LocalSolarEclipseBoundary& source,
    taiyin_local_solar_eclipse_boundary* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->center_longitude_deg = source.center_longitude_deg;
    out->center_latitude_deg = source.center_latitude_deg;
    out->center_kind = source.center_kind;
    out->umbra_north_longitude_deg = source.umbra_north_longitude_deg;
    out->umbra_north_latitude_deg = source.umbra_north_latitude_deg;
    out->umbra_south_longitude_deg = source.umbra_south_longitude_deg;
    out->umbra_south_latitude_deg = source.umbra_south_latitude_deg;
    out->penumbra_north_longitude_deg = source.penumbra_north_longitude_deg;
    out->penumbra_north_latitude_deg = source.penumbra_north_latitude_deg;
    out->penumbra_south_longitude_deg = source.penumbra_south_longitude_deg;
    out->penumbra_south_latitude_deg = source.penumbra_south_latitude_deg;
    out->umbra_width_km = source.umbra_width_km;
}

void copy_besselian(
    const taiyin::runtime::SolarBesselianElements& source,
    taiyin_solar_besselian_elements* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->t_hours = source.t_hours;
    out->x = source.x;
    out->y = source.y;
    out->zeta = source.zeta;
    out->d_deg = source.d_deg;
    out->mu_deg = source.mu_deg;
    out->l1 = source.l1;
    out->l2 = source.l2;
    out->f1_deg = source.f1_deg;
    out->f2_deg = source.f2_deg;
    out->tan_f1 = source.tan_f1;
    out->tan_f2 = source.tan_f2;
    out->gamma = source.gamma;
}

void copy_polynomial(
    const taiyin::runtime::SolarBesselianPolynomial& source,
    taiyin_solar_besselian_polynomial* out
) noexcept {
    out->struct_size = sizeof(*out);
    taiyin_c_internal::from_cpp_split_jd(source.t0_jd_tt, &out->t0_jd_tt);
    out->span_hours = source.span_hours;
    out->sample_step_hours = source.sample_step_hours;
    out->degree = source.degree;
    for (size_t i = 0; i < TAIYIN_C_SOLAR_BESSELIAN_COEFF_COUNT; ++i) {
        out->x[i] = source.x[i];
        out->y[i] = source.y[i];
        out->zeta[i] = source.zeta[i];
        out->d_deg[i] = source.d_deg[i];
        out->mu_deg[i] = source.mu_deg[i];
        out->l1[i] = source.l1[i];
        out->l2[i] = source.l2[i];
    }
    out->f1_deg = source.f1_deg;
    out->f2_deg = source.f2_deg;
    out->tan_f1 = source.tan_f1;
    out->tan_f2 = source.tan_f2;
    copy_besselian(source.center, &out->center);
    copy_besselian(source.max_residual, &out->max_residual);
}

taiyin::runtime::SolarBesselianPolynomial to_cpp_polynomial(
    const taiyin_solar_besselian_polynomial& source
) noexcept {
    taiyin::runtime::SolarBesselianPolynomial out{};
    out.t0_jd_tt = taiyin_c_internal::to_cpp_split_jd(source.t0_jd_tt);
    out.span_hours = source.span_hours;
    out.sample_step_hours = source.sample_step_hours;
    out.degree = source.degree;
    for (size_t i = 0; i < TAIYIN_C_SOLAR_BESSELIAN_COEFF_COUNT; ++i) {
        out.x[i] = source.x[i];
        out.y[i] = source.y[i];
        out.zeta[i] = source.zeta[i];
        out.d_deg[i] = source.d_deg[i];
        out.mu_deg[i] = source.mu_deg[i];
        out.l1[i] = source.l1[i];
        out.l2[i] = source.l2[i];
    }
    out.f1_deg = source.f1_deg;
    out.f2_deg = source.f2_deg;
    out.tan_f1 = source.tan_f1;
    out.tan_f2 = source.tan_f2;
    return out;
}

void copy_path(
    const taiyin::runtime::SolarEclipsePathPoint& source,
    taiyin_solar_eclipse_path_point* out
) noexcept {
    out->struct_size = sizeof(*out);
    taiyin_c_internal::from_cpp_split_jd(source.jd_tt, &out->jd_tt);
    taiyin_c_internal::from_cpp_split_jd(source.jd_ut, &out->jd_ut);
    out->latitude_deg = source.latitude_deg;
    out->longitude_deg = source.longitude_deg;
    out->elevation_m = source.elevation_m;
    out->sun_altitude_deg = source.sun_altitude_deg;
    out->sun_azimuth_deg = source.sun_azimuth_deg;
}

void copy_route_row(
    const taiyin::runtime::SolarEclipseRouteRow& source,
    taiyin_solar_eclipse_route_row* out
) noexcept {
    out->struct_size = sizeof(*out);
    taiyin_c_internal::from_cpp_split_jd(source.jd_tt, &out->jd_tt);
    taiyin_c_internal::from_cpp_split_jd(source.jd_ut, &out->jd_ut);
    copy_path(source.center_line, &out->center_line);
    copy_path(source.penumbral_north_limit, &out->penumbral_north_limit);
    copy_path(source.penumbral_south_limit, &out->penumbral_south_limit);
    copy_path(source.north_limit, &out->north_limit);
    copy_path(source.south_limit, &out->south_limit);
    copy_path(
        source.half_magnitude_north_limit, &out->half_magnitude_north_limit);
    copy_path(
        source.half_magnitude_south_limit, &out->half_magnitude_south_limit);
    out->path_width_km = source.path_width_km;
    out->duration_seconds = source.duration_seconds;
    out->sun_altitude_deg = source.sun_altitude_deg;
    out->sun_azimuth_deg = source.sun_azimuth_deg;
}

void copy_curve_point(
    const taiyin::runtime::SolarEclipseRouteCurvePoint& source,
    taiyin_solar_eclipse_route_curve_point* out
) noexcept {
    out->struct_size = sizeof(*out);
    taiyin_c_internal::from_cpp_split_jd(source.jd_tt, &out->jd_tt);
    taiyin_c_internal::from_cpp_split_jd(source.jd_ut, &out->jd_ut);
    out->curve_kind = source.curve_kind;
    out->latitude_deg = source.latitude_deg;
    out->longitude_deg = source.longitude_deg;
}

void copy_product_point(
    const taiyin::runtime::SolarEclipseRouteProductPoint& source,
    taiyin_solar_eclipse_route_product_point* out
) noexcept {
    out->struct_size = sizeof(*out);
    taiyin_c_internal::from_cpp_split_jd(source.jd_tt, &out->jd_tt);
    taiyin_c_internal::from_cpp_split_jd(source.jd_ut, &out->jd_ut);
    out->point_kind = source.point_kind;
    out->source_curve_kind = source.source_curve_kind;
    out->latitude_deg = source.latitude_deg;
    out->longitude_deg = source.longitude_deg;
    out->unwrapped_longitude_deg = source.unwrapped_longitude_deg;
}

void copy_summary(
    const taiyin::runtime::SolarEclipseRouteProductSummary& source,
    taiyin_solar_eclipse_route_product_summary* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->flags = source.flags;
    out->curve_point_count = source.curve_point_count;
    out->center_line_count = source.center_line_count;
    out->core_north_count = source.core_north_count;
    out->core_south_count = source.core_south_count;
    out->core_begin_horizon_count = source.core_begin_horizon_count;
    out->core_end_horizon_count = source.core_end_horizon_count;
    out->penumbral_north_count = source.penumbral_north_count;
    out->penumbral_south_count = source.penumbral_south_count;
    out->half_magnitude_north_count = source.half_magnitude_north_count;
    out->half_magnitude_south_count = source.half_magnitude_south_count;
    out->core_polygon_point_count = source.core_polygon_point_count;
    out->penumbral_polygon_point_count = source.penumbral_polygon_point_count;
    out->half_magnitude_polygon_point_count =
        source.half_magnitude_polygon_point_count;
    out->polygon_point_count = source.polygon_point_count;
    out->min_latitude_deg = source.min_latitude_deg;
    out->max_latitude_deg = source.max_latitude_deg;
    out->min_unwrapped_longitude_deg = source.min_unwrapped_longitude_deg;
    out->max_unwrapped_longitude_deg = source.max_unwrapped_longitude_deg;
}

template <typename CppOut, typename COut, typename Eval, typename Copy>
taiyin_status run_result(
    const taiyin_context* context,
    COut* out,
    taiyin_ephemeris_diagnostic* diagnostic,
    const Eval& eval,
    const Copy& copy
) {
    if (!context || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    CppOut cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        eval(&cpp_out, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

template <typename CppOut, typename COut, typename Eval, typename Copy>
taiyin_status run_array(
    const taiyin_context* context,
    COut* out,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic,
    const Eval& eval,
    const Copy& copy
) {
    if (!context || !out_count || (capacity != 0 && !out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    try {
        std::vector<CppOut> cpp_results(capacity);
        taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
        const taiyin::Status status = eval(
            capacity ? cpp_results.data() : nullptr, capacity, out_count,
            diagnostic ? &cpp_diagnostic : 0);
        if (status == taiyin::TAIYIN_STATUS_OK) {
            const size_t copied = *out_count < capacity ? *out_count : capacity;
            for (size_t i = 0; i < copied; ++i) {
                copy(cpp_results[i], &out[i]);
            }
        }
        taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
        return status;
    } catch (const std::bad_alloc&) {
        *out_count = 0;
        return taiyin_c_internal::out_of_memory();
    } catch (...) {
        *out_count = 0;
        return TAIYIN_ERROR_INTERNAL;
    }
}

}  // namespace

extern "C" {

#define TAIYIN_DEFINE_INIT(function_name, type_name) \
    void TAIYIN_C_CALL function_name(type_name* value) { init_struct(value); }

TAIYIN_DEFINE_INIT(taiyin_lunar_eclipse_result_tt_init, taiyin_lunar_eclipse_result_tt)
TAIYIN_DEFINE_INIT(taiyin_lunar_eclipse_result_ut_init, taiyin_lunar_eclipse_result_ut)
TAIYIN_DEFINE_INIT(taiyin_local_lunar_eclipse_result_tt_init, taiyin_local_lunar_eclipse_result_tt)
TAIYIN_DEFINE_INIT(taiyin_local_lunar_eclipse_result_ut_init, taiyin_local_lunar_eclipse_result_ut)
TAIYIN_DEFINE_INIT(taiyin_solar_eclipse_result_tt_init, taiyin_solar_eclipse_result_tt)
TAIYIN_DEFINE_INIT(taiyin_solar_eclipse_result_ut_init, taiyin_solar_eclipse_result_ut)
TAIYIN_DEFINE_INIT(taiyin_local_solar_eclipse_result_tt_init, taiyin_local_solar_eclipse_result_tt)
TAIYIN_DEFINE_INIT(taiyin_local_solar_eclipse_result_ut_init, taiyin_local_solar_eclipse_result_ut)
TAIYIN_DEFINE_INIT(taiyin_local_solar_eclipse_circumstances_tt_init, taiyin_local_solar_eclipse_circumstances_tt)
TAIYIN_DEFINE_INIT(taiyin_local_solar_eclipse_circumstances_ut_init, taiyin_local_solar_eclipse_circumstances_ut)
TAIYIN_DEFINE_INIT(taiyin_local_solar_eclipse_boundary_init, taiyin_local_solar_eclipse_boundary)
TAIYIN_DEFINE_INIT(taiyin_solar_eclipse_route_product_summary_init, taiyin_solar_eclipse_route_product_summary)
TAIYIN_DEFINE_INIT(taiyin_solar_besselian_elements_init, taiyin_solar_besselian_elements)

#undef TAIYIN_DEFINE_INIT

void TAIYIN_C_CALL taiyin_solar_eclipse_route_row_init(
    taiyin_solar_eclipse_route_row* value
) {
    init_struct(value);
    if (!value) return;
    value->center_line.struct_size = sizeof(value->center_line);
    value->penumbral_north_limit.struct_size =
        sizeof(value->penumbral_north_limit);
    value->penumbral_south_limit.struct_size =
        sizeof(value->penumbral_south_limit);
    value->north_limit.struct_size = sizeof(value->north_limit);
    value->south_limit.struct_size = sizeof(value->south_limit);
    value->half_magnitude_north_limit.struct_size =
        sizeof(value->half_magnitude_north_limit);
    value->half_magnitude_south_limit.struct_size =
        sizeof(value->half_magnitude_south_limit);
}

void TAIYIN_C_CALL taiyin_solar_besselian_polynomial_init(
    taiyin_solar_besselian_polynomial* value
) {
    init_struct(value);
    if (!value) return;
    value->center.struct_size = sizeof(value->center);
    value->max_residual.struct_size = sizeof(value->max_residual);
}

taiyin_status TAIYIN_C_CALL taiyin_solve_lunar_eclipse_at_tt(
    const taiyin_context* context, const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    taiyin_lunar_eclipse_result_tt* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_tt)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::LunarEclipseResult>(
        context, out, diagnostic,
        [&](taiyin::runtime::LunarEclipseResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::solve_lunar_eclipse_at(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_tt),
                flags, cpp_out, cpp_diagnostic);
        }, copy_lunar_tt);
}

taiyin_status TAIYIN_C_CALL taiyin_solve_lunar_eclipse_at_ut(
    const taiyin_context* context, const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    taiyin_lunar_eclipse_result_ut* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_ut)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::LunarEclipseResultUt>(
        context, out, diagnostic,
        [&](taiyin::runtime::LunarEclipseResultUt* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::solve_lunar_eclipse_at_ut(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_ut),
                flags, cpp_out, cpp_diagnostic);
        }, copy_lunar_ut);
}

taiyin_status TAIYIN_C_CALL taiyin_search_next_lunar_eclipse_tt(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_start_tt, uint32_t kind_filter,
    uint64_t flags, taiyin_lunar_eclipse_result_tt* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_start_tt)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::LunarEclipseResult>(
        context, out, diagnostic,
        [&](taiyin::runtime::LunarEclipseResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_lunar_eclipse_tt(
                &context->value,
                taiyin_c_internal::to_cpp_split_jd(*jd_start_tt),
                kind_filter, flags, cpp_out,
                cpp_diagnostic);
        }, copy_lunar_tt);
}

taiyin_status TAIYIN_C_CALL taiyin_search_next_lunar_eclipse_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_start_ut, uint32_t kind_filter,
    uint64_t flags, taiyin_lunar_eclipse_result_ut* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_start_ut)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::LunarEclipseResultUt>(
        context, out, diagnostic,
        [&](taiyin::runtime::LunarEclipseResultUt* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_lunar_eclipse_ut(
                &context->value,
                taiyin_c_internal::to_cpp_split_jd(*jd_start_ut),
                kind_filter, flags, cpp_out,
                cpp_diagnostic);
        }, copy_lunar_ut);
}

taiyin_status TAIYIN_C_CALL taiyin_search_lunar_eclipses_tt(
    const taiyin_context* context, const taiyin_split_julian_date* start,
    const taiyin_split_julian_date* end,
    uint32_t kind_filter, uint64_t flags,
    taiyin_lunar_eclipse_result_tt* out, size_t capacity, size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(start) || !taiyin_c_internal::valid_split_jd(end)) return taiyin_c_internal::invalid_argument();
    return run_array<taiyin::runtime::LunarEclipseResult>(
        context, out, capacity, out_count, diagnostic,
        [&](taiyin::runtime::LunarEclipseResult* cpp_out, size_t cap,
            size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_lunar_eclipses_tt(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*start),
                taiyin_c_internal::to_cpp_split_jd(*end), kind_filter, flags,
                cpp_out, cap, count, cpp_diagnostic);
        }, copy_lunar_tt);
}

taiyin_status TAIYIN_C_CALL taiyin_search_lunar_eclipses_ut(
    const taiyin_context* context, const taiyin_split_julian_date* start,
    const taiyin_split_julian_date* end,
    uint32_t kind_filter, uint64_t flags,
    taiyin_lunar_eclipse_result_ut* out, size_t capacity, size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(start) || !taiyin_c_internal::valid_split_jd(end)) return taiyin_c_internal::invalid_argument();
    return run_array<taiyin::runtime::LunarEclipseResultUt>(
        context, out, capacity, out_count, diagnostic,
        [&](taiyin::runtime::LunarEclipseResultUt* cpp_out, size_t cap,
            size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_lunar_eclipses_ut(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*start),
                taiyin_c_internal::to_cpp_split_jd(*end), kind_filter, flags,
                cpp_out, cap, count, cpp_diagnostic);
        }, copy_lunar_ut);
}

taiyin_status TAIYIN_C_CALL taiyin_compute_local_lunar_eclipse_visibility_tt(
    const taiyin_context* context,
    const taiyin_lunar_eclipse_result_tt* eclipse, uint64_t flags,
    taiyin_local_lunar_eclipse_result_tt* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_struct(eclipse)) {
        return taiyin_c_internal::invalid_argument();
    }
    const taiyin::runtime::LunarEclipseResult cpp_eclipse =
        to_cpp_lunar_tt(*eclipse);
    return run_result<taiyin::runtime::LocalLunarEclipseResult>(
        context, out, diagnostic,
        [&](taiyin::runtime::LocalLunarEclipseResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::compute_local_lunar_eclipse_visibility_tt(
                &context->value, &cpp_eclipse, flags, cpp_out, cpp_diagnostic);
        }, copy_local_lunar_tt);
}

taiyin_status TAIYIN_C_CALL taiyin_compute_local_lunar_eclipse_visibility_ut(
    const taiyin_context* context,
    const taiyin_lunar_eclipse_result_ut* eclipse, uint64_t flags,
    taiyin_local_lunar_eclipse_result_ut* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_struct(eclipse)) {
        return taiyin_c_internal::invalid_argument();
    }
    const taiyin::runtime::LunarEclipseResultUt cpp_eclipse =
        to_cpp_lunar_ut(*eclipse);
    return run_result<taiyin::runtime::LocalLunarEclipseResultUt>(
        context, out, diagnostic,
        [&](taiyin::runtime::LocalLunarEclipseResultUt* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::compute_local_lunar_eclipse_visibility_ut(
                &context->value, &cpp_eclipse, flags, cpp_out, cpp_diagnostic);
        }, copy_local_lunar_ut);
}

taiyin_status TAIYIN_C_CALL taiyin_search_next_local_lunar_eclipse_tt(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_start_tt, uint32_t kind_filter,
    uint64_t flags, taiyin_local_lunar_eclipse_result_tt* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_start_tt)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::LocalLunarEclipseResult>(
        context, out, diagnostic,
        [&](taiyin::runtime::LocalLunarEclipseResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_local_lunar_eclipse_tt(
                &context->value,
                taiyin_c_internal::to_cpp_split_jd(*jd_start_tt), kind_filter,
                flags, cpp_out,
                cpp_diagnostic);
        }, copy_local_lunar_tt);
}

taiyin_status TAIYIN_C_CALL taiyin_search_next_local_lunar_eclipse_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_start_ut, uint32_t kind_filter,
    uint64_t flags, taiyin_local_lunar_eclipse_result_ut* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_start_ut)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::LocalLunarEclipseResultUt>(
        context, out, diagnostic,
        [&](taiyin::runtime::LocalLunarEclipseResultUt* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_local_lunar_eclipse_ut(
                &context->value,
                taiyin_c_internal::to_cpp_split_jd(*jd_start_ut), kind_filter,
                flags, cpp_out,
                cpp_diagnostic);
        }, copy_local_lunar_ut);
}

taiyin_status TAIYIN_C_CALL taiyin_solve_solar_eclipse_at_tt(
    const taiyin_context* context, const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    taiyin_solar_eclipse_result_tt* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_tt)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::SolarEclipseResult>(
        context, out, diagnostic,
        [&](taiyin::runtime::SolarEclipseResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::solve_solar_eclipse_at(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_tt),
                flags, cpp_out, cpp_diagnostic);
        }, copy_solar_tt);
}

taiyin_status TAIYIN_C_CALL taiyin_solve_solar_eclipse_at_ut(
    const taiyin_context* context, const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    taiyin_solar_eclipse_result_ut* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_ut)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::SolarEclipseResultUt>(
        context, out, diagnostic,
        [&](taiyin::runtime::SolarEclipseResultUt* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::solve_solar_eclipse_at_ut(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_ut),
                flags, cpp_out, cpp_diagnostic);
        }, copy_solar_ut);
}

taiyin_status TAIYIN_C_CALL taiyin_search_next_solar_eclipse_tt(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_start_tt, uint32_t kind_filter,
    uint64_t flags, taiyin_solar_eclipse_result_tt* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_start_tt)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::SolarEclipseResult>(
        context, out, diagnostic,
        [&](taiyin::runtime::SolarEclipseResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_solar_eclipse_tt(
                &context->value,
                taiyin_c_internal::to_cpp_split_jd(*jd_start_tt), kind_filter,
                flags, cpp_out,
                cpp_diagnostic);
        }, copy_solar_tt);
}

taiyin_status TAIYIN_C_CALL taiyin_search_next_solar_eclipse_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_start_ut, uint32_t kind_filter,
    uint64_t flags, taiyin_solar_eclipse_result_ut* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_start_ut)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::SolarEclipseResultUt>(
        context, out, diagnostic,
        [&](taiyin::runtime::SolarEclipseResultUt* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_solar_eclipse_ut(
                &context->value,
                taiyin_c_internal::to_cpp_split_jd(*jd_start_ut), kind_filter,
                flags, cpp_out,
                cpp_diagnostic);
        }, copy_solar_ut);
}

taiyin_status TAIYIN_C_CALL taiyin_search_solar_eclipses_tt(
    const taiyin_context* context, const taiyin_split_julian_date* start,
    const taiyin_split_julian_date* end,
    uint32_t kind_filter, uint64_t flags,
    taiyin_solar_eclipse_result_tt* out, size_t capacity, size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(start) || !taiyin_c_internal::valid_split_jd(end)) return taiyin_c_internal::invalid_argument();
    return run_array<taiyin::runtime::SolarEclipseResult>(
        context, out, capacity, out_count, diagnostic,
        [&](taiyin::runtime::SolarEclipseResult* cpp_out, size_t cap,
            size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_solar_eclipses_tt(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*start),
                taiyin_c_internal::to_cpp_split_jd(*end), kind_filter, flags,
                cpp_out, cap, count, cpp_diagnostic);
        }, copy_solar_tt);
}

taiyin_status TAIYIN_C_CALL taiyin_search_solar_eclipses_ut(
    const taiyin_context* context, const taiyin_split_julian_date* start,
    const taiyin_split_julian_date* end,
    uint32_t kind_filter, uint64_t flags,
    taiyin_solar_eclipse_result_ut* out, size_t capacity, size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(start) || !taiyin_c_internal::valid_split_jd(end)) return taiyin_c_internal::invalid_argument();
    return run_array<taiyin::runtime::SolarEclipseResultUt>(
        context, out, capacity, out_count, diagnostic,
        [&](taiyin::runtime::SolarEclipseResultUt* cpp_out, size_t cap,
            size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_solar_eclipses_ut(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*start),
                taiyin_c_internal::to_cpp_split_jd(*end), kind_filter, flags,
                cpp_out, cap, count, cpp_diagnostic);
        }, copy_solar_ut);
}

taiyin_status TAIYIN_C_CALL taiyin_solve_local_solar_eclipse_at_tt(
    const taiyin_context* context, const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    taiyin_local_solar_eclipse_result_tt* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_tt)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::LocalSolarEclipseResult>(
        context, out, diagnostic,
        [&](taiyin::runtime::LocalSolarEclipseResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::solve_local_solar_eclipse_at_tt(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_tt),
                flags, cpp_out, cpp_diagnostic);
        }, copy_local_solar_tt);
}

taiyin_status TAIYIN_C_CALL taiyin_solve_local_solar_eclipse_at_ut(
    const taiyin_context* context, const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    taiyin_local_solar_eclipse_result_ut* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_ut)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::LocalSolarEclipseResultUt>(
        context, out, diagnostic,
        [&](taiyin::runtime::LocalSolarEclipseResultUt* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::solve_local_solar_eclipse_at_ut(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_ut),
                flags, cpp_out, cpp_diagnostic);
        }, copy_local_solar_ut);
}

taiyin_status TAIYIN_C_CALL taiyin_search_next_local_solar_eclipse_tt(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_start_tt, uint32_t kind_filter,
    uint64_t flags, taiyin_local_solar_eclipse_result_tt* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_start_tt)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::LocalSolarEclipseResult>(
        context, out, diagnostic,
        [&](taiyin::runtime::LocalSolarEclipseResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_local_solar_eclipse_tt(
                &context->value,
                taiyin_c_internal::to_cpp_split_jd(*jd_start_tt), kind_filter,
                flags, cpp_out,
                cpp_diagnostic);
        }, copy_local_solar_tt);
}

taiyin_status TAIYIN_C_CALL taiyin_search_next_local_solar_eclipse_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_start_ut, uint32_t kind_filter,
    uint64_t flags, taiyin_local_solar_eclipse_result_ut* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_start_ut)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::LocalSolarEclipseResultUt>(
        context, out, diagnostic,
        [&](taiyin::runtime::LocalSolarEclipseResultUt* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_local_solar_eclipse_ut(
                &context->value,
                taiyin_c_internal::to_cpp_split_jd(*jd_start_ut), kind_filter,
                flags, cpp_out,
                cpp_diagnostic);
        }, copy_local_solar_ut);
}

taiyin_status TAIYIN_C_CALL taiyin_compute_local_solar_circumstances_tt(
    const taiyin_context* context, const taiyin_split_julian_date* jd_tt,
    taiyin_local_solar_eclipse_circumstances_tt* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_tt)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::LocalSolarEclipseCircumstances>(
        context, out, diagnostic,
        [&](taiyin::runtime::LocalSolarEclipseCircumstances* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::compute_local_solar_circumstances_tt(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_tt),
                cpp_out, cpp_diagnostic);
        }, copy_circumstances_tt);
}

taiyin_status TAIYIN_C_CALL taiyin_compute_local_solar_circumstances_ut(
    const taiyin_context* context, const taiyin_split_julian_date* jd_ut,
    taiyin_local_solar_eclipse_circumstances_ut* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_ut)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::LocalSolarEclipseCircumstancesUt>(
        context, out, diagnostic,
        [&](taiyin::runtime::LocalSolarEclipseCircumstancesUt* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::compute_local_solar_circumstances_ut(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_ut),
                cpp_out, cpp_diagnostic);
        }, copy_circumstances_ut);
}

taiyin_status TAIYIN_C_CALL taiyin_compute_solar_besselian_elements_tt(
    const taiyin_context* context, const taiyin_split_julian_date* jd_tt,
    double t_hours,
    taiyin_solar_besselian_elements* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_tt)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::SolarBesselianElements>(
        context, out, diagnostic,
        [&](taiyin::runtime::SolarBesselianElements* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::compute_solar_besselian_elements_tt(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_tt),
                t_hours, cpp_out, cpp_diagnostic);
        }, copy_besselian);
}

taiyin_status TAIYIN_C_CALL taiyin_compute_solar_besselian_polynomial_tt(
    const taiyin_context* context,
    const taiyin_split_julian_date* center_jd_tt, double span_hours,
    double sample_step_hours, int32_t degree,
    taiyin_solar_besselian_polynomial* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(center_jd_tt)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::SolarBesselianPolynomial>(
        context, out, diagnostic,
        [&](taiyin::runtime::SolarBesselianPolynomial* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::compute_solar_besselian_polynomial_tt(
                &context->value,
                taiyin_c_internal::to_cpp_split_jd(*center_jd_tt), span_hours,
                sample_step_hours, degree, cpp_out, cpp_diagnostic);
        }, copy_polynomial);
}

taiyin_status TAIYIN_C_CALL taiyin_evaluate_solar_besselian_polynomial(
    const taiyin_solar_besselian_polynomial* polynomial,
    double t_hours,
    taiyin_solar_besselian_elements* out
) {
    if (!taiyin_c_internal::valid_struct(polynomial)
        || !taiyin_c_internal::valid_struct(out)) {
        return taiyin_c_internal::invalid_argument();
    }
    const taiyin::runtime::SolarBesselianPolynomial cpp_polynomial =
        to_cpp_polynomial(*polynomial);
    taiyin::runtime::SolarBesselianElements cpp_out;
    const taiyin::Status status =
        taiyin::runtime::evaluate_solar_besselian_polynomial(
            &cpp_polynomial, t_hours, &cpp_out);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_besselian(cpp_out, out);
    return status;
}

taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_row_tt(
    const taiyin_context* context, const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    taiyin_solar_eclipse_route_row* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_tt)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::SolarEclipseRouteRow>(
        context, out, diagnostic,
        [&](taiyin::runtime::SolarEclipseRouteRow* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::compute_solar_eclipse_route_row_tt(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_tt),
                flags, cpp_out, cpp_diagnostic);
        }, copy_route_row);
}

taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_row_ut(
    const taiyin_context* context, const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    taiyin_solar_eclipse_route_row* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_ut)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::SolarEclipseRouteRow>(
        context, out, diagnostic,
        [&](taiyin::runtime::SolarEclipseRouteRow* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::compute_solar_eclipse_route_row_ut(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_ut),
                flags, cpp_out, cpp_diagnostic);
        }, copy_route_row);
}

taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_tt(
    const taiyin_context* context, const taiyin_split_julian_date* start,
    const taiyin_split_julian_date* end,
    double step_minutes, uint64_t flags,
    taiyin_solar_eclipse_route_row* out, size_t capacity, size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(start) || !taiyin_c_internal::valid_split_jd(end)) return taiyin_c_internal::invalid_argument();
    return run_array<taiyin::runtime::SolarEclipseRouteRow>(
        context, out, capacity, out_count, diagnostic,
        [&](taiyin::runtime::SolarEclipseRouteRow* cpp_out, size_t cap,
            size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::compute_solar_eclipse_route_tt(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*start),
                taiyin_c_internal::to_cpp_split_jd(*end), step_minutes, flags,
                cpp_out, cap, count, cpp_diagnostic);
        }, copy_route_row);
}

taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_ut(
    const taiyin_context* context, const taiyin_split_julian_date* start,
    const taiyin_split_julian_date* end,
    double step_minutes, uint64_t flags,
    taiyin_solar_eclipse_route_row* out, size_t capacity, size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(start) || !taiyin_c_internal::valid_split_jd(end)) return taiyin_c_internal::invalid_argument();
    return run_array<taiyin::runtime::SolarEclipseRouteRow>(
        context, out, capacity, out_count, diagnostic,
        [&](taiyin::runtime::SolarEclipseRouteRow* cpp_out, size_t cap,
            size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::compute_solar_eclipse_route_ut(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*start),
                taiyin_c_internal::to_cpp_split_jd(*end), step_minutes, flags,
                cpp_out, cap, count, cpp_diagnostic);
        }, copy_route_row);
}

taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_curves_tt(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_near_tt, uint64_t flags,
    size_t route_sample_count, taiyin_solar_eclipse_route_curve_point* out,
    size_t capacity, size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_near_tt)) return taiyin_c_internal::invalid_argument();
    return run_array<taiyin::runtime::SolarEclipseRouteCurvePoint>(
        context, out, capacity, out_count, diagnostic,
        [&](taiyin::runtime::SolarEclipseRouteCurvePoint* cpp_out, size_t cap,
            size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::
                compute_solar_eclipse_route_curves_tt_with_options(
                    &context->value,
                    taiyin_c_internal::to_cpp_split_jd(*jd_near_tt), flags,
                    route_sample_count,
                    cpp_out, cap, count, cpp_diagnostic);
        }, copy_curve_point);
}

taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_curves_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_near_ut, uint64_t flags,
    size_t route_sample_count, taiyin_solar_eclipse_route_curve_point* out,
    size_t capacity, size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_near_ut)) return taiyin_c_internal::invalid_argument();
    return run_array<taiyin::runtime::SolarEclipseRouteCurvePoint>(
        context, out, capacity, out_count, diagnostic,
        [&](taiyin::runtime::SolarEclipseRouteCurvePoint* cpp_out, size_t cap,
            size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::
                compute_solar_eclipse_route_curves_ut_with_options(
                    &context->value,
                    taiyin_c_internal::to_cpp_split_jd(*jd_near_ut), flags,
                    route_sample_count,
                    cpp_out, cap, count, cpp_diagnostic);
        }, copy_curve_point);
}

}  // extern "C"

template <typename Eval>
taiyin_status run_product(
    const taiyin_context* context,
    taiyin_solar_eclipse_route_product_point* out,
    size_t capacity,
    size_t* out_count,
    taiyin_solar_eclipse_route_product_summary* summary,
    taiyin_ephemeris_diagnostic* diagnostic,
    const Eval& eval
) {
    if (!context || !out_count || !taiyin_c_internal::valid_struct(summary)
        || (capacity != 0 && !out) || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    try {
        std::vector<taiyin::runtime::SolarEclipseRouteProductPoint> cpp_points(
            capacity);
        taiyin::runtime::SolarEclipseRouteProductSummary cpp_summary;
        taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
        const taiyin::Status status = eval(
            capacity ? cpp_points.data() : nullptr, capacity, out_count,
            &cpp_summary, diagnostic ? &cpp_diagnostic : 0);
        if (status == taiyin::TAIYIN_STATUS_OK) {
            const size_t copied = *out_count < capacity ? *out_count : capacity;
            for (size_t i = 0; i < copied; ++i) {
                copy_product_point(cpp_points[i], &out[i]);
            }
        }
        if (status == taiyin::TAIYIN_STATUS_OK
            || status == taiyin::TAIYIN_ERROR_OUT_OF_MEMORY) {
            copy_summary(cpp_summary, summary);
        }
        taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
        return status;
    } catch (const std::bad_alloc&) {
        *out_count = 0;
        return taiyin_c_internal::out_of_memory();
    } catch (...) {
        *out_count = 0;
        return TAIYIN_ERROR_INTERNAL;
    }
}

extern "C" {

taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_product_tt(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_near_tt, uint64_t flags,
    size_t route_sample_count, taiyin_solar_eclipse_route_product_point* out,
    size_t capacity, size_t* out_count,
    taiyin_solar_eclipse_route_product_summary* summary,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_near_tt)) return taiyin_c_internal::invalid_argument();
    return run_product(
        context, out, capacity, out_count, summary, diagnostic,
        [&](taiyin::runtime::SolarEclipseRouteProductPoint* cpp_out,
            size_t cap, size_t* count,
            taiyin::runtime::SolarEclipseRouteProductSummary* cpp_summary,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::
                compute_solar_eclipse_route_product_tt_with_options(
                    &context->value,
                    taiyin_c_internal::to_cpp_split_jd(*jd_near_tt), flags,
                    route_sample_count,
                    cpp_out, cap, count, cpp_summary, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_product_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_near_ut, uint64_t flags,
    size_t route_sample_count, taiyin_solar_eclipse_route_product_point* out,
    size_t capacity, size_t* out_count,
    taiyin_solar_eclipse_route_product_summary* summary,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_near_ut)) return taiyin_c_internal::invalid_argument();
    return run_product(
        context, out, capacity, out_count, summary, diagnostic,
        [&](taiyin::runtime::SolarEclipseRouteProductPoint* cpp_out,
            size_t cap, size_t* count,
            taiyin::runtime::SolarEclipseRouteProductSummary* cpp_summary,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::
                compute_solar_eclipse_route_product_ut_with_options(
                    &context->value,
                    taiyin_c_internal::to_cpp_split_jd(*jd_near_ut), flags,
                    route_sample_count,
                    cpp_out, cap, count, cpp_summary, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_map_product_tt(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_near_tt, uint64_t flags,
    size_t route_sample_count, taiyin_solar_eclipse_route_product_point* out,
    size_t capacity, size_t* out_count,
    taiyin_solar_eclipse_route_product_summary* summary,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_near_tt)) return taiyin_c_internal::invalid_argument();
    return run_product(
        context, out, capacity, out_count, summary, diagnostic,
        [&](taiyin::runtime::SolarEclipseRouteProductPoint* cpp_out,
            size_t cap, size_t* count,
            taiyin::runtime::SolarEclipseRouteProductSummary* cpp_summary,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::
                compute_solar_eclipse_route_map_product_tt_with_options(
                    &context->value,
                    taiyin_c_internal::to_cpp_split_jd(*jd_near_tt), flags,
                    route_sample_count,
                    cpp_out, cap, count, cpp_summary, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_map_product_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_near_ut, uint64_t flags,
    size_t route_sample_count, taiyin_solar_eclipse_route_product_point* out,
    size_t capacity, size_t* out_count,
    taiyin_solar_eclipse_route_product_summary* summary,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_near_ut)) return taiyin_c_internal::invalid_argument();
    return run_product(
        context, out, capacity, out_count, summary, diagnostic,
        [&](taiyin::runtime::SolarEclipseRouteProductPoint* cpp_out,
            size_t cap, size_t* count,
            taiyin::runtime::SolarEclipseRouteProductSummary* cpp_summary,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::
                compute_solar_eclipse_route_map_product_ut_with_options(
                    &context->value,
                    taiyin_c_internal::to_cpp_split_jd(*jd_near_ut), flags,
                    route_sample_count,
                    cpp_out, cap, count, cpp_summary, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_compute_local_solar_eclipse_boundary_tt(
    const taiyin_context* context, const taiyin_split_julian_date* jd_tt,
    double longitude_deg,
    double latitude_deg, taiyin_local_solar_eclipse_boundary* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_tt)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::LocalSolarEclipseBoundary>(
        context, out, diagnostic,
        [&](taiyin::runtime::LocalSolarEclipseBoundary* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::compute_local_solar_eclipse_boundary_tt(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_tt),
                longitude_deg, latitude_deg, cpp_out, cpp_diagnostic);
        }, copy_boundary);
}

taiyin_status TAIYIN_C_CALL taiyin_compute_local_solar_eclipse_boundary_ut(
    const taiyin_context* context, const taiyin_split_julian_date* jd_ut,
    double longitude_deg,
    double latitude_deg, taiyin_local_solar_eclipse_boundary* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_ut)) return taiyin_c_internal::invalid_argument();
    return run_result<taiyin::runtime::LocalSolarEclipseBoundary>(
        context, out, diagnostic,
        [&](taiyin::runtime::LocalSolarEclipseBoundary* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::compute_local_solar_eclipse_boundary_ut(
                &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_ut),
                longitude_deg, latitude_deg, cpp_out, cpp_diagnostic);
        }, copy_boundary);
}

}  // extern "C"
