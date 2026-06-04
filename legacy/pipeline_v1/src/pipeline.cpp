#include "taiyin/pipeline.h"
#include "taiyin/corrections.h"
#include "taiyin/dispatch.h"
#include "taiyin/math_solvers.h"
#include "taiyin/geometry.h"

#include <unordered_map>

namespace taiyin {
namespace pipeline {

// --- Step registry ---

static std::unordered_map<int, PipelineStepFn>& pipeline_steps() {
    static std::unordered_map<int, PipelineStepFn> steps;
    return steps;
}

void register_step(int id, PipelineStepFn fn) {
    if (!fn) return;
    pipeline_steps()[id] = fn;
}

PipelineStepFn lookup_step(int id) {
    auto& steps = pipeline_steps();
    auto it = steps.find(id);
    return it != steps.end() ? it->second : nullptr;
}

// --- Step implementations ---

int step_light_time(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const LightTimeStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<LightTimeStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;
    bool ok = solve_light_time_velocity(
        in->jd_tdb,
        in->observer_position_au,
        in->observer_velocity_au_per_day,
        in->target_position_fn,
        in->target_velocity_fn,
        in->target_block,
        TAIYIN_LIGHT_TIME_DAYS_PER_AU,
        in->max_iterations,
        in->tolerance_days,
        &out->observed_position_au,
        &out->observed_velocity_au_per_day,
        &out->tau_days,
        &out->tau_rate,
        nullptr, nullptr);
    return ok ? OK : ERR_STEP_EXECUTION_FAILED;
}

int step_deflection(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const DeflectionStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<DeflectionStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;
    bool ok = apply_gravitational_deflection_from_body(
        in->apparent_position_au,
        in->apparent_velocity_au_per_day,
        in->observer_heliocentric_position_au,
        in->observer_heliocentric_velocity_au_per_day,
        in->sun_heliocentric_position_au,
        in->sun_heliocentric_velocity_au_per_day,
        in->apparent_position_au,
        in->apparent_velocity_au_per_day,
        in->schwarzschild_radius_au,
        in->deflection_limit,
        &out->apparent_position_au,
        &out->apparent_velocity_au_per_day);
    return ok ? OK : ERR_STEP_EXECUTION_FAILED;
}

int step_aberration(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const AberrationStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<AberrationStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;
    bool ok = apply_annual_aberration(
        in->apparent_position_au,
        in->apparent_velocity_au_per_day,
        in->observer_heliocentric_position_au,
        in->observer_heliocentric_velocity_au_per_day,
        in->observer_barycentric_velocity_au_per_day,
        in->observer_barycentric_acceleration_au_per_day2,
        in->light_time_days_per_au,
        in->solar_schwarzschild_radius_au,
        &out->apparent_position_au,
        &out->apparent_velocity_au_per_day);
    return ok ? OK : ERR_STEP_EXECUTION_FAILED;
}

int step_frame_transform(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const FrameTransformStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<FrameTransformStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;

    dispatch::FrameRouteDispatchData route_data;
    route_data.xp_rad = in->xp_rad;
    route_data.yp_rad = in->yp_rad;
    route_data.sp_rad = in->sp_rad;
    route_data.dx_rad = in->dx_rad;
    route_data.dy_rad = in->dy_rad;
    route_data.precession_model = in->precession_model;
    route_data.nutation_model = in->nutation_model;

    Matrix3x3 frame;
    if (!dispatch::eval_frame_route(in->frame_route, in->jd_ut1, in->jd_tt, &route_data, &frame)) {
        return ERR_STEP_EXECUTION_FAILED;
    }

    out->position_au = matrix3x3_multiply_vector(frame, in->position_au);
    out->velocity_au_per_day = matrix3x3_multiply_vector(frame, in->velocity_au_per_day);
    return OK;
}

int step_observer_geocentric(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const ObserverGeocentricStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<ObserverGeocentricStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;

    if (in->frame_route == dispatch::FRAME_ROUTE_EQUINOX) {
        if (!observer_geocentric_true_equator_of_date_position_au(
                in->longitude_rad, in->latitude_rad, in->height_m,
                in->jd_ut1, in->jd_tt,
                in->xp_rad, in->yp_rad, in->sp_rad,
                &out->observer_position_au)) {
            return ERR_STEP_EXECUTION_FAILED;
        }
        if (!observer_geocentric_true_equator_of_date_velocity_au_per_day(
                in->longitude_rad, in->latitude_rad, in->height_m,
                in->jd_ut1, in->jd_tt,
                in->xp_rad, in->yp_rad, in->sp_rad,
                in->xp_rate_rad_per_day, in->yp_rate_rad_per_day,
                in->sp_rate_rad_per_day,
                in->dut1_rate_seconds_per_day, in->lod_seconds,
                in->derivative_step_days,
                &out->observer_velocity_au_per_day)) {
            return ERR_STEP_EXECUTION_FAILED;
        }
    } else if (in->frame_route == dispatch::FRAME_ROUTE_CIRS) {
        if (!observer_geocentric_cirs_position_au(
                in->longitude_rad, in->latitude_rad, in->height_m,
                in->jd_ut1,
                in->xp_rad, in->yp_rad, in->sp_rad,
                &out->observer_position_au)) {
            return ERR_STEP_EXECUTION_FAILED;
        }
        if (!observer_geocentric_cirs_velocity_au_per_day(
                in->longitude_rad, in->latitude_rad, in->height_m,
                in->jd_ut1,
                in->xp_rad, in->yp_rad, in->sp_rad,
                in->xp_rate_rad_per_day, in->yp_rate_rad_per_day,
                in->sp_rate_rad_per_day,
                in->dut1_rate_seconds_per_day, in->lod_seconds,
                in->derivative_step_days,
                &out->observer_velocity_au_per_day)) {
            return ERR_STEP_EXECUTION_FAILED;
        }
    } else {
        return ERR_STEP_EXECUTION_FAILED;
    }
    return OK;
}

int step_topocentric(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const TopocentricStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<TopocentricStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;
    out->topocentric_position_au = topocentric_position_au(
        in->target_position_au, in->observer_position_au);
    out->topocentric_velocity_au_per_day = topocentric_velocity_au_per_day(
        in->target_velocity_au_per_day, in->observer_velocity_au_per_day);
    return OK;
}

int step_diurnal_aberration(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const DiurnalAberrationStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<DiurnalAberrationStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;
    bool ok = apply_observer_velocity_aberration(
        in->topocentric_position_au,
        in->topocentric_velocity_au_per_day,
        in->observer_velocity_au_per_day,
        in->observer_acceleration_au_per_day2,
        TAIYIN_LIGHT_TIME_DAYS_PER_AU,
        &out->topocentric_position_au,
        &out->topocentric_velocity_au_per_day);
    return ok ? OK : ERR_STEP_EXECUTION_FAILED;
}

int step_horizontal(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const HorizontalStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<HorizontalStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;
    out->horizontal = topocentric_position_to_horizontal(
        in->topocentric_position_au,
        in->local_sidereal_rad,
        in->latitude_rad);
    return OK;
}

int step_refraction(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const RefractionStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<RefractionStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;

    dispatch::RefractionDispatchData ref_data;
    ref_data.altitude_rad = in->horizontal.altitude_rad;
    ref_data.pressure_mbar = in->pressure_mbar;
    ref_data.temperature_c = in->temperature_c;
    ref_data.relative_humidity = in->relative_humidity;
    ref_data.wavelength_micrometer = in->wavelength_micrometer;
    ref_data.max_iterations = in->max_iterations;
    ref_data.tolerance = in->tolerance;

    double correction = dispatch::eval_refraction(in->refraction_model, &ref_data);
    out->horizontal = in->horizontal;
    out->horizontal.altitude_rad += correction;
    return OK;
}

int step_earth_shadow_radii(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const EarthShadowRadiiStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<EarthShadowRadiiStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;

    bool ok = calculate_earth_shadow_radii(
        in->moon_distance_equatorial_radii,
        in->sun_distance_au,
        in->earth_scale,
        in->sun_scale,
        in->parallax_scale,
        &out->umbra_rad,
        &out->penumbra_rad
    );
    return ok ? OK : ERR_STEP_EXECUTION_FAILED;
}

int step_ecliptic_to_fundamental(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const EclipticToFundamentalStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<EclipticToFundamentalStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;

    bool ok = project_ecliptic_to_fundamental(
        in->moon_longitude_rad,
        in->moon_latitude_rad,
        in->sun_longitude_rad,
        in->sun_latitude_rad,
        &out->fundamental_x,
        &out->fundamental_y
    );
    return ok ? OK : ERR_STEP_EXECUTION_FAILED;
}

int step_bessel_to_geodetic(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const BesselToGeodeticStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<BesselToGeodeticStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;

    bool ok = project_bessel_to_geodetic(
        in->intersection,
        in->axis_ratio,
        in->bessel_ra,
        in->bessel_dec,
        in->gst,
        &out->longitude_rad,
        &out->latitude_rad
    );
    return ok ? OK : ERR_STEP_EXECUTION_FAILED;
}

int step_line_spheroid_intersect(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const LineSpheroidIntersectStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<LineSpheroidIntersectStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;

    bool ok = intersect_line_spheroid(
        in->p1,
        in->p2,
        in->axis_ratio,
        in->equator_radius,
        &out->intersection,
        &out->dist1,
        &out->dist2
    );
    return ok ? OK : ERR_STEP_EXECUTION_FAILED;
}

int step_quadratic_contact(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const QuadraticContactStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<QuadraticContactStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;

    bool ok = solve_quadratic_contact(
        in->t0,
        in->x0,
        in->y0,
        in->vx,
        in->vy,
        in->contact_radius,
        &out->t1,
        &out->t2
    );
    return ok ? OK : ERR_STEP_EXECUTION_FAILED;
}

int step_ecliptic_projection(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const EclipticProjectionStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<EclipticProjectionStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;

    EclipticPositionVelocity ecliptic;
    bool ok = cartesian_position_velocity_to_ecliptic(in->position_au, in->velocity_au_per_day, &ecliptic);
    if (!ok) return ERR_STEP_EXECUTION_FAILED;

    out->longitude_rad = ecliptic.longitude_rad;
    out->latitude_rad = ecliptic.latitude_rad;
    out->radius_au = ecliptic.radius_au;
    out->longitude_rate_rad_per_day = ecliptic.longitude_rate_rad_per_day;
    out->latitude_rate_rad_per_day = ecliptic.latitude_rate_rad_per_day;
    out->radius_rate_au_per_day = ecliptic.radius_rate_au_per_day;
    return OK;
}

int step_eval_body(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const EvalBodyStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<EvalBodyStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;

    if (in->target_position_fn) {
        if (!in->target_position_fn(in->jd_tdb, in->target_block, &out->position_au)) {
            return ERR_STEP_EXECUTION_FAILED;
        }
        if (in->target_velocity_fn) {
            if (!in->target_velocity_fn(in->jd_tdb, in->target_block, &out->velocity_au_per_day)) {
                return ERR_STEP_EXECUTION_FAILED;
            }
        } else {
            out->velocity_au_per_day = {0.0, 0.0, 0.0};
        }
        if (in->target_acceleration_fn) {
            if (!in->target_acceleration_fn(in->jd_tdb, in->target_block, &out->acceleration_au_per_day2)) {
                return ERR_STEP_EXECUTION_FAILED;
            }
        } else {
            out->acceleration_au_per_day2 = {0.0, 0.0, 0.0};
        }
    } else if (in->target_block) {
        const auto* block = static_cast<const internal::CompiledEphemerisBlock*>(in->target_block);
        CartesianState state;
        if (!internal::eval_compiled_ephemeris_block(in->jd_tdb, block, &state)) {
            return ERR_STEP_EXECUTION_FAILED;
        }
        out->position_au = state.position_au;
        out->velocity_au_per_day = state.velocity_au_per_day;
        
        Vector3 accel = {0.0, 0.0, 0.0};
        internal::eval_compiled_ephemeris_block_acceleration(in->jd_tdb, block, &accel);
        out->acceleration_au_per_day2 = accel;
    } else {
        out->position_au = {0.0, 0.0, 0.0};
        out->velocity_au_per_day = {0.0, 0.0, 0.0};
        out->acceleration_au_per_day2 = {0.0, 0.0, 0.0};
    }
    return OK;
}

int step_eval_body_position(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const EvalBodyPositionStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<EvalBodyPositionStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;

    if (in->target_position_fn) {
        if (!in->target_position_fn(in->jd_tdb, in->target_block, &out->position_au)) {
            return ERR_STEP_EXECUTION_FAILED;
        }
    } else if (in->target_block) {
        const auto* block = static_cast<const internal::CompiledEphemerisBlock*>(in->target_block);
        Vector3 pos = {0.0, 0.0, 0.0};
        if (!internal::eval_compiled_ephemeris_block_position(in->jd_tdb, block, &pos)) {
            return ERR_STEP_EXECUTION_FAILED;
        }
        out->position_au = pos;
    } else {
        out->position_au = {0.0, 0.0, 0.0};
    }
    return OK;
}

int step_eval_body_velocity(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const EvalBodyVelocityStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<EvalBodyVelocityStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;

    if (in->target_velocity_fn) {
        if (!in->target_velocity_fn(in->jd_tdb, in->target_block, &out->velocity_au_per_day)) {
            return ERR_STEP_EXECUTION_FAILED;
        }
    } else if (in->target_block) {
        const auto* block = static_cast<const internal::CompiledEphemerisBlock*>(in->target_block);
        Vector3 vel = {0.0, 0.0, 0.0};
        if (!internal::eval_compiled_ephemeris_block_velocity(in->jd_tdb, block, &vel)) {
            return ERR_STEP_EXECUTION_FAILED;
        }
        out->velocity_au_per_day = vel;
    } else {
        out->velocity_au_per_day = {0.0, 0.0, 0.0};
    }
    return OK;
}

int step_eval_body_acceleration(PipelineContext* ctx, int step_index) {
    auto* in = static_cast<const EvalBodyAccelerationStepData*>(ctx->step_data_in[step_index]);
    auto* out = static_cast<EvalBodyAccelerationStepData*>(ctx->step_data_out[step_index]);
    if (!in || !out) return ERR_NULL_CONTEXT;

    if (in->target_acceleration_fn) {
        if (!in->target_acceleration_fn(in->jd_tdb, in->target_block, &out->acceleration_au_per_day2)) {
            return ERR_STEP_EXECUTION_FAILED;
        }
    } else if (in->target_block) {
        const auto* block = static_cast<const internal::CompiledEphemerisBlock*>(in->target_block);
        Vector3 accel = {0.0, 0.0, 0.0};
        // Note: some blocks may not support acceleration, we default to {0,0,0} if not available
        internal::eval_compiled_ephemeris_block_acceleration(in->jd_tdb, block, &accel);
        out->acceleration_au_per_day2 = accel;
    } else {
        out->acceleration_au_per_day2 = {0.0, 0.0, 0.0};
    }
    return OK;
}

// --- Pipeline execution ---

static bool latest_apparent_state(
    const int* step_ids,
    PipelineContext* ctx,
    int before_index,
    Vector3* position_au,
    Vector3* velocity_au_per_day
) {
    for (int i = before_index - 1; i >= 0; --i) {
        if (step_ids[i] == STEP_ABERRATION) {
            auto* d = static_cast<AberrationStepData*>(ctx->step_data_out[i]);
            if (!d) return false;
            *position_au = d->apparent_position_au;
            *velocity_au_per_day = d->apparent_velocity_au_per_day;
            return true;
        }
        if (step_ids[i] == STEP_DEFLECTION) {
            auto* d = static_cast<DeflectionStepData*>(ctx->step_data_out[i]);
            if (!d) return false;
            *position_au = d->apparent_position_au;
            *velocity_au_per_day = d->apparent_velocity_au_per_day;
            return true;
        }
        if (step_ids[i] == STEP_LIGHT_TIME) {
            auto* d = static_cast<LightTimeStepData*>(ctx->step_data_out[i]);
            if (!d) return false;
            *position_au = d->observed_position_au;
            *velocity_au_per_day = d->observed_velocity_au_per_day;
            return true;
        }
    }
    return false;
}

static bool latest_framed_state(
    const int* step_ids,
    PipelineContext* ctx,
    int before_index,
    Vector3* position_au,
    Vector3* velocity_au_per_day
) {
    for (int i = before_index - 1; i >= 0; --i) {
        if (step_ids[i] == STEP_FRAME_TRANSFORM) {
            auto* d = static_cast<FrameTransformStepData*>(ctx->step_data_out[i]);
            if (!d) return false;
            *position_au = d->position_au;
            *velocity_au_per_day = d->velocity_au_per_day;
            return true;
        }
    }
    return false;
}

static bool latest_observer_state(
    const int* step_ids,
    PipelineContext* ctx,
    int before_index,
    Vector3* position_au,
    Vector3* velocity_au_per_day
) {
    for (int i = before_index - 1; i >= 0; --i) {
        if (step_ids[i] == STEP_OBSERVER_GEOCENTRIC) {
            auto* d = static_cast<ObserverGeocentricStepData*>(ctx->step_data_out[i]);
            if (!d) return false;
            *position_au = d->observer_position_au;
            *velocity_au_per_day = d->observer_velocity_au_per_day;
            return true;
        }
    }
    return false;
}

static bool wire_step_inputs(const int* step_ids, PipelineContext* ctx, int step_index) {
    switch (step_ids[step_index]) {
        case STEP_LIGHT_TIME: {
            auto* d = static_cast<LightTimeStepData*>(ctx->step_data_in[step_index]);
            if (!d) return false;
            // Search backwards for STEP_EVAL_BODY representing Earth (body_id = 399)
            for (int i = step_index - 1; i >= 0; --i) {
                if (step_ids[i] == STEP_EVAL_BODY && 
                    static_cast<EvalBodyStepData*>(ctx->step_data_out[i])->body_id == 399) {
                    auto* earth = static_cast<EvalBodyStepData*>(ctx->step_data_out[i]);
                    d->observer_position_au = earth->position_au;
                    d->observer_velocity_au_per_day = earth->velocity_au_per_day;
                    break;
                }
            }
            return true;
        }
        case STEP_DEFLECTION: {
            auto* d = static_cast<DeflectionStepData*>(ctx->step_data_in[step_index]);
            if (!d) return false;
            if (!latest_apparent_state(step_ids, ctx, step_index, &d->apparent_position_au, &d->apparent_velocity_au_per_day)) {
                return false;
            }
            // Wire Earth heliocentric state from STEP_EVAL_BODY (body_id = 399)
            for (int i = step_index - 1; i >= 0; --i) {
                if (step_ids[i] == STEP_EVAL_BODY && 
                    static_cast<EvalBodyStepData*>(ctx->step_data_out[i])->body_id == 399) {
                    auto* earth = static_cast<EvalBodyStepData*>(ctx->step_data_out[i]);
                    d->observer_heliocentric_position_au = earth->position_au;
                    d->observer_heliocentric_velocity_au_per_day = earth->velocity_au_per_day;
                    break;
                }
            }
            // If Jupiter deflection, wire Jupiter state from STEP_EVAL_BODY (body_id = 5)
            if (d->schwarzschild_radius_au < 1e-9) {
                for (int i = step_index - 1; i >= 0; --i) {
                    if (step_ids[i] == STEP_EVAL_BODY && 
                        static_cast<EvalBodyStepData*>(ctx->step_data_out[i])->body_id == 5) {
                        auto* jup = static_cast<EvalBodyStepData*>(ctx->step_data_out[i]);
                        d->sun_heliocentric_position_au = jup->position_au;
                        d->sun_heliocentric_velocity_au_per_day = jup->velocity_au_per_day;
                        break;
                    }
                }
            }
            return true;
        }
        case STEP_ABERRATION: {
            auto* d = static_cast<AberrationStepData*>(ctx->step_data_in[step_index]);
            if (!d) return false;
            if (!latest_apparent_state(step_ids, ctx, step_index, &d->apparent_position_au, &d->apparent_velocity_au_per_day)) {
                return false;
            }
            // Wire Earth Heliocentric (body_id = 399)
            for (int i = step_index - 1; i >= 0; --i) {
                if (step_ids[i] == STEP_EVAL_BODY && 
                    static_cast<EvalBodyStepData*>(ctx->step_data_out[i])->body_id == 399) {
                    auto* earth = static_cast<EvalBodyStepData*>(ctx->step_data_out[i]);
                    d->observer_heliocentric_position_au = earth->position_au;
                    d->observer_heliocentric_velocity_au_per_day = earth->velocity_au_per_day;
                    break;
                }
            }
            // Wire Earth Barycentric (body_id = 3990)
            for (int i = step_index - 1; i >= 0; --i) {
                if (step_ids[i] == STEP_EVAL_BODY && 
                    static_cast<EvalBodyStepData*>(ctx->step_data_out[i])->body_id == 3990) {
                    auto* earth = static_cast<EvalBodyStepData*>(ctx->step_data_out[i]);
                    d->observer_barycentric_velocity_au_per_day = earth->velocity_au_per_day;
                    d->observer_barycentric_acceleration_au_per_day2 = earth->acceleration_au_per_day2;
                    break;
                }
            }
            return true;
        }
        case STEP_FRAME_TRANSFORM: {
            auto* d = static_cast<FrameTransformStepData*>(ctx->step_data_in[step_index]);
            if (!d) return false;
            return latest_apparent_state(step_ids, ctx, step_index, &d->position_au, &d->velocity_au_per_day);
        }
        case STEP_TOPOCENTRIC: {
            auto* d = static_cast<TopocentricStepData*>(ctx->step_data_in[step_index]);
            if (!d) return false;
            if (!latest_framed_state(step_ids, ctx, step_index, &d->target_position_au, &d->target_velocity_au_per_day)) return false;
            return latest_observer_state(step_ids, ctx, step_index, &d->observer_position_au, &d->observer_velocity_au_per_day);
        }
        case STEP_DIURNAL_ABERRATION: {
            auto* d = static_cast<DiurnalAberrationStepData*>(ctx->step_data_in[step_index]);
            if (!d) return false;
            bool found_topo = false;
            for (int i = step_index - 1; i >= 0; --i) {
                if (step_ids[i] == STEP_TOPOCENTRIC) {
                    auto* topo = static_cast<TopocentricStepData*>(ctx->step_data_out[i]);
                    if (!topo) return false;
                    d->topocentric_position_au = topo->topocentric_position_au;
                    d->topocentric_velocity_au_per_day = topo->topocentric_velocity_au_per_day;
                    found_topo = true;
                    break;
                }
            }
            if (!found_topo) return false;
            bool found_obs = false;
            for (int i = step_index - 1; i >= 0; --i) {
                if (step_ids[i] == STEP_OBSERVER_GEOCENTRIC) {
                    auto* obs = static_cast<ObserverGeocentricStepData*>(ctx->step_data_out[i]);
                    if (!obs) return false;
                    d->observer_velocity_au_per_day = obs->observer_velocity_au_per_day;
                    d->observer_acceleration_au_per_day2 = {0.0, 0.0, 0.0};
                    found_obs = true;
                    break;
                }
            }
            return found_obs;
        }
        case STEP_HORIZONTAL: {
            auto* d = static_cast<HorizontalStepData*>(ctx->step_data_in[step_index]);
            if (!d) return false;
            for (int i = step_index - 1; i >= 0; --i) {
                if (step_ids[i] == STEP_DIURNAL_ABERRATION) {
                    auto* da = static_cast<DiurnalAberrationStepData*>(ctx->step_data_out[i]);
                    if (!da) return false;
                    d->topocentric_position_au = da->topocentric_position_au;
                    return true;
                }
                if (step_ids[i] == STEP_TOPOCENTRIC) {
                    auto* topo = static_cast<TopocentricStepData*>(ctx->step_data_out[i]);
                    if (!topo) return false;
                    d->topocentric_position_au = topo->topocentric_position_au;
                    return true;
                }
            }
            return false;
        }
        case STEP_REFRACTION: {
            auto* d = static_cast<RefractionStepData*>(ctx->step_data_in[step_index]);
            if (!d) return false;
            for (int i = step_index - 1; i >= 0; --i) {
                if (step_ids[i] == STEP_HORIZONTAL) {
                    auto* horiz = static_cast<HorizontalStepData*>(ctx->step_data_out[i]);
                    if (!horiz) return false;
                    d->horizontal = horiz->horizontal;
                    return true;
                }
            }
            return false;
        }
        case STEP_ECLIPTIC_PROJECTION: {
            auto* d = static_cast<EclipticProjectionStepData*>(ctx->step_data_in[step_index]);
            if (!d) return false;
            return latest_framed_state(step_ids, ctx, step_index, &d->position_au, &d->velocity_au_per_day);
        }
        default:
            return true;
    }
}

int run_pipeline(const int* step_ids, int step_count, PipelineContext* ctx) {
    if (!step_ids || !ctx || step_count <= 0) return ERR_NULL_CONTEXT;
    if (static_cast<int>(ctx->step_data_in.size()) < step_count ||
        static_cast<int>(ctx->step_data_out.size()) < step_count) return ERR_STEP_DATA_SIZE;
    for (int i = 0; i < step_count; ++i) {
        PipelineStepFn fn = lookup_step(step_ids[i]);
        if (!fn) return ERR_STEP_NOT_FOUND;
        if (!wire_step_inputs(step_ids, ctx, i)) return ERR_WIRING_FAILED;
        int status = fn(ctx, i);
        if (status != OK) return status;
    }
    return OK;
}

// --- Preset pipelines ---

const int kEquinoxPipeline[] = {
    STEP_LIGHT_TIME,
    STEP_DEFLECTION,
    STEP_DEFLECTION,
    STEP_ABERRATION,
    STEP_FRAME_TRANSFORM,
    STEP_OBSERVER_GEOCENTRIC,
    STEP_TOPOCENTRIC,
    STEP_DIURNAL_ABERRATION,
    STEP_HORIZONTAL,
    STEP_REFRACTION,
};

const int kEquinoxPipelineStepCount = 10;

const int kCirsPipeline[] = {
    STEP_LIGHT_TIME,
    STEP_DEFLECTION,
    STEP_DEFLECTION,
    STEP_ABERRATION,
    STEP_FRAME_TRANSFORM,
    STEP_OBSERVER_GEOCENTRIC,
    STEP_TOPOCENTRIC,
    STEP_DIURNAL_ABERRATION,
    STEP_HORIZONTAL,
    STEP_REFRACTION,
};

const int kCirsPipelineStepCount = 10;

// --- Auto-registration ---

static bool g_steps_registered = (
    register_step(STEP_LIGHT_TIME, step_light_time),
    register_step(STEP_DEFLECTION, step_deflection),
    register_step(STEP_ABERRATION, step_aberration),
    register_step(STEP_FRAME_TRANSFORM, step_frame_transform),
    register_step(STEP_OBSERVER_GEOCENTRIC, step_observer_geocentric),
    register_step(STEP_TOPOCENTRIC, step_topocentric),
    register_step(STEP_DIURNAL_ABERRATION, step_diurnal_aberration),
    register_step(STEP_HORIZONTAL, step_horizontal),
    register_step(STEP_REFRACTION, step_refraction),
    register_step(STEP_EARTH_SHADOW_RADII, step_earth_shadow_radii),
    register_step(STEP_ECLIPTIC_TO_FUNDAMENTAL, step_ecliptic_to_fundamental),
    register_step(STEP_BESSEL_TO_GEODETIC, step_bessel_to_geodetic),
    register_step(STEP_LINE_SPHEROID_INTERSECT, step_line_spheroid_intersect),
    register_step(STEP_QUADRATIC_CONTACT, step_quadratic_contact),
    register_step(STEP_ECLIPTIC_PROJECTION, step_ecliptic_projection),
    register_step(STEP_EVAL_BODY, step_eval_body),
    register_step(STEP_EVAL_BODY_POSITION, step_eval_body_position),
    register_step(STEP_EVAL_BODY_VELOCITY, step_eval_body_velocity),
    register_step(STEP_EVAL_BODY_ACCELERATION, step_eval_body_acceleration),
    true);

}  // namespace pipeline
}  // namespace taiyin
