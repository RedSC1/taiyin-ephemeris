#include "runtime/apparent/fast_apparent.h"

#include "runtime/core/runtime_state_block_adapter.h"

#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/coordinates.h"
#include "taiyin/corrections.h"
#include "taiyin/dispatch.h"
#include "taiyin/vector3.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace taiyin {
namespace runtime {

namespace {

constexpr int kMaxFastCorrectionSampleRadius = 4096;

int output_frame_id(FastApparentFrame frame) noexcept {
    switch (frame) {
    case FAST_APPARENT_TRUE_EQUATOR_OF_DATE:
        return TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE;
    case FAST_APPARENT_TRUE_ECLIPTIC_OF_DATE:
        return TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    default:
        return -1;
    }
}

uint64_t mix_u64(uint64_t seed, uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    value ^= value >> 31;
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
}

uint64_t hash_double_bits(double value) noexcept {
    uint64_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

uint64_t fast_correction_series_identity(
    const NativeCalcContext& context,
    int body_0_id,
    int body_1_id,
    const FastApparentOptions& options
) noexcept {
    const ApparentOptions& apparent = context.apparent_options;
    uint64_t hash = 0xcbf29ce484222325ull;
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(body_0_id)));
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(body_1_id)));
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<int>(options.frame)));
    hash = mix_u64(hash, options.with_velocity ? 1u : 0u);
    hash = mix_u64(hash, options.true_position ? 1u : 0u);
    hash = mix_u64(hash, static_cast<uint64_t>(apparent.flags));
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(apparent.output_frame_id)));
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(apparent.light_time_method_id)));
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(apparent.shapiro_delay_model_id)));
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(apparent.aberration_model_id)));
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(apparent.deflection_model_id)));
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(apparent.max_light_time_iterations)));
    hash = mix_u64(hash, hash_double_bits(apparent.light_time_tolerance_days));
    hash = mix_u64(hash, hash_double_bits(apparent.matrix_derivative_step_days));
    hash = mix_u64(hash, hash_double_bits(apparent.celestial_pole_offset_dx_rad));
    hash = mix_u64(hash, hash_double_bits(apparent.celestial_pole_offset_dy_rad));
    hash = mix_u64(hash, hash_double_bits(apparent.celestial_pole_offset_dx_rate_rad_per_day));
    hash = mix_u64(hash, hash_double_bits(apparent.celestial_pole_offset_dy_rate_rad_per_day));
    hash = mix_u64(hash, static_cast<uint64_t>(apparent.deflector_count));
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(apparent.solar_deflector_index)));
    const size_t deflector_count = apparent.deflectors ? std::min<size_t>(apparent.deflector_count, 8u) : 0u;
    for (size_t i = 0; i < deflector_count; ++i) {
        hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(apparent.deflectors[i].body_id)));
        hash = mix_u64(hash, hash_double_bits(apparent.deflectors[i].schwarzschild_radius_au));
        hash = mix_u64(hash, hash_double_bits(apparent.deflectors[i].limit));
    }
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(context.model_context.tdb_model_id)));
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(context.model_context.precession_model_id)));
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(context.model_context.nutation_model_id)));
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(context.model_context.obliquity_model_id)));
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(context.model_context.frame_route_id)));
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(context.observer_id)));
    hash = mix_u64(hash, static_cast<uint64_t>(static_cast<uint32_t>(context.center_id)));
    hash = mix_u64(hash, context.route_rule_id);
    return hash == 0u ? 1u : hash;
}

uint32_t fast_apparent_flags(const NativeCalcContext& context, const FastApparentOptions& options) noexcept {
    uint32_t flags = context.apparent_options.flags | TAIYIN_APPARENT_SPHERICAL;
    flags &= ~TAIYIN_APPARENT_ACCELERATION;
    if (options.with_velocity) {
        flags |= TAIYIN_APPARENT_VELOCITY;
    } else {
        flags &= ~TAIYIN_APPARENT_VELOCITY;
    }
    if (options.true_position) {
        flags &= ~(TAIYIN_APPARENT_LIGHT_TIME
            | TAIYIN_APPARENT_ABERRATION
            | TAIYIN_APPARENT_DEFLECTION
            | TAIYIN_APPARENT_SHAPIRO_DELAY
            | TAIYIN_APPARENT_TOPOCENTRIC);
    }
    return flags;
}

uint32_t runtime_components_for_fast_apparent_flags(uint32_t flags) noexcept {
    uint32_t components = internal::EPHEMERIS_BLOCK_COMPONENT_POSITION;
    if ((flags & TAIYIN_APPARENT_VELOCITY) != 0u) {
        components |= internal::EPHEMERIS_BLOCK_COMPONENT_VELOCITY;
    }
    return components;
}

Vector3 zero_vector() noexcept {
    return Vector3{0.0, 0.0, 0.0};
}

bool finite_vector(const Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Matrix3x3 matrix_from_array9(const double values[9]) noexcept {
    Matrix3x3 matrix = matrix3x3_identity();
    if (!values) return matrix;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            matrix.m[row][col] = values[row * 3 + col];
        }
    }
    return matrix;
}

bool need_velocity_for_fast_apparent(uint32_t flags) noexcept {
    return (flags & (TAIYIN_APPARENT_VELOCITY | TAIYIN_APPARENT_ACCELERATION)) != 0u;
}

bool need_acceleration_for_fast_apparent(uint32_t flags) noexcept {
    return (flags & TAIYIN_APPARENT_ACCELERATION) != 0u;
}

bool valid_deflection_model_id_for_fast_apparent(int model_id) noexcept {
    return model_id == TAIYIN_DEFLECTION_MODEL_ERFA
        || model_id == TAIYIN_DEFLECTION_MODEL_SOLAR_DISK;
}

bool valid_solar_deflector_index_for_fast_apparent(size_t deflector_count, int solar_deflector_index) noexcept {
    return solar_deflector_index < 0
        || static_cast<size_t>(solar_deflector_index) < deflector_count;
}

bool eval_block_state_for_fast_apparent(
    const SplitJulianDate& jd_tdb,
    const internal::CompiledEphemerisBlock* block,
    uint32_t flags,
    CartesianState* out
) noexcept {
    if (!out || !block || !block->data || !block->position) {
        return false;
    }
    uint32_t components = internal::EPHEMERIS_BLOCK_COMPONENT_POSITION;
    if (need_velocity_for_fast_apparent(flags)) {
        components |= internal::EPHEMERIS_BLOCK_COMPONENT_VELOCITY;
    }
    if (need_acceleration_for_fast_apparent(flags)) {
        components |= internal::EPHEMERIS_BLOCK_COMPONENT_ACCELERATION;
    }
    if (!internal::eval_compiled_ephemeris_block_components(jd_tdb, block, components, out)) {
        return false;
    }
    return finite_vector(out->position_au)
        && (!need_velocity_for_fast_apparent(flags) || finite_vector(out->velocity_au_per_day))
        && (!need_acceleration_for_fast_apparent(flags) || finite_vector(out->acceleration_au_per_day2));
}

bool eval_fast_apparent_target_position_direct(
    const SplitJulianDate& jd_tdb,
    int target_id,
    const internal::CompiledEphemerisBlock* target_block,
    const CartesianState& observer,
    const CartesianState& solar,
    size_t deflector_count,
    int solar_deflector_index,
    const int* deflector_ids,
    const double* deflector_schwarzschild_radius_au,
    const double* deflector_limit,
    uint32_t flags,
    int light_time_method_id,
    int shapiro_delay_model_id,
    int aberration_model_id,
    int deflection_model_id,
    int max_light_time_iterations,
    double light_time_tolerance_days,
    const Matrix3x3& output_matrix,
    Vector3* out_position
) noexcept {
    if (!out_position || !target_block || !target_block->data || !target_block->position) {
        return false;
    }
    if (light_time_method_id != 0 || shapiro_delay_model_id != 0
        || !valid_deflection_model_id_for_fast_apparent(deflection_model_id)
        || need_velocity_for_fast_apparent(flags) || need_acceleration_for_fast_apparent(flags)) {
        return false;
    }

    CartesianState target;
    if (target_id == TAIYIN_BODY_SUN) {
        target = solar;
    } else if (!eval_block_state_for_fast_apparent(jd_tdb, target_block, flags, &target)) {
        return false;
    }

    const Vector3 geometric = vector3_subtract(target.position_au, observer.position_au);
    Vector3 position = geometric;
    if (!finite_vector(geometric) || vector3_norm(geometric) == 0.0) {
        return false;
    }

    if ((flags & TAIYIN_APPARENT_LIGHT_TIME) != 0u) {
        Vector3 retarded_position;
        double light_time_days = 0.0;
        if (!solve_light_time_position(
                jd_tdb,
                observer.position_au,
                target_block->position,
                target_block->data,
                TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                max_light_time_iterations,
                light_time_tolerance_days,
                &position,
                &light_time_days,
                &retarded_position)) {
            return false;
        }
    }

    if ((flags & TAIYIN_APPARENT_DEFLECTION) != 0u) {
        if (!deflector_ids || !deflector_schwarzschild_radius_au || !deflector_limit
            || solar_deflector_index < 0
            || static_cast<size_t>(solar_deflector_index) >= deflector_count) {
            return false;
        }
        if (deflector_ids[solar_deflector_index] != target_id) {
            Vector3 next_position;
            Vector3 next_velocity;
            if (!apply_gravitational_deflection_from_body_with_model(
                    position,
                    zero_vector(),
                    observer.position_au,
                    zero_vector(),
                    solar.position_au,
                    zero_vector(),
                    position,
                    zero_vector(),
                    deflector_schwarzschild_radius_au[solar_deflector_index],
                    deflector_limit[solar_deflector_index],
                    deflection_model_id,
                    &next_position,
                    &next_velocity)) {
                return false;
            }
            position = next_position;
        }
    }

    if ((flags & TAIYIN_APPARENT_ABERRATION) != 0u) {
        if (!deflector_schwarzschild_radius_au
            || solar_deflector_index < 0
            || static_cast<size_t>(solar_deflector_index) >= deflector_count) {
            return false;
        }
        const dispatch::AberrationDispatchData aberration_data = {
            position,
            zero_vector(),
            zero_vector(),
            vector3_subtract(observer.position_au, solar.position_au),
            zero_vector(),
            zero_vector(),
            observer.velocity_au_per_day,
            zero_vector(),
            TAIYIN_LIGHT_TIME_DAYS_PER_AU,
            deflector_schwarzschild_radius_au[solar_deflector_index],
            false,
        };
        Vector3 aberrated_velocity;
        Vector3 aberrated_acceleration;
        if (!dispatch::eval_selected_aberration(
                aberration_model_id,
                &aberration_data,
                &position,
                &aberrated_velocity,
                &aberrated_acceleration)) {
            return false;
        }
    }

    if ((flags & TAIYIN_APPARENT_USE_MATRIX) != 0u) {
        position = transform_position_with_matrix(position, output_matrix);
    }
    if (!finite_vector(position)) {
        return false;
    }
    *out_position = position;
    return true;
}

CartesianState zero_state() noexcept {
    CartesianState state;
    state.position_au = zero_vector();
    state.velocity_au_per_day = zero_vector();
    state.acceleration_au_per_day2 = zero_vector();
    return state;
}

CartesianState state_add(const CartesianState& a, const CartesianState& b) noexcept {
    CartesianState out;
    out.position_au = vector3_add(a.position_au, b.position_au);
    out.velocity_au_per_day = vector3_add(a.velocity_au_per_day, b.velocity_au_per_day);
    out.acceleration_au_per_day2 = vector3_add(a.acceleration_au_per_day2, b.acceleration_au_per_day2);
    return out;
}

CartesianState state_subtract(const CartesianState& a, const CartesianState& b) noexcept {
    CartesianState out;
    out.position_au = vector3_subtract(a.position_au, b.position_au);
    out.velocity_au_per_day = vector3_subtract(a.velocity_au_per_day, b.velocity_au_per_day);
    out.acceleration_au_per_day2 = vector3_subtract(a.acceleration_au_per_day2, b.acceleration_au_per_day2);
    return out;
}

double lagrange_scalar(
    const double* xs,
    const double* ys,
    size_t count,
    double x
) noexcept {
    double out = 0.0;
    for (size_t i = 0; i < count; ++i) {
        double weight = 1.0;
        for (size_t j = 0; j < count; ++j) {
            if (i == j) continue;
            const double denominator = xs[i] - xs[j];
            if (denominator == 0.0) return std::nan("");
            weight *= (x - xs[j]) / denominator;
        }
        out += ys[i] * weight;
    }
    return out;
}

double catmull_rom_scalar(
    double y0,
    double y1,
    double y2,
    double y3,
    double t
) noexcept {
    const double t2 = t * t;
    const double t3 = t2 * t;
    return 0.5 * ((2.0 * y1)
        + (-y0 + y2) * t
        + (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3) * t2
        + (-y0 + 3.0 * y1 - 3.0 * y2 + y3) * t3);
}

double interpolate_scalar_samples(
    const double* xs,
    const double* ys,
    size_t count,
    double x,
    FastApparentCorrectionInterpolationKind kind
) noexcept {
    if (count == 0) return std::nan("");
    if (count == 1) return ys[0];
    if (kind == FAST_APPARENT_CORRECTION_INTERPOLATION_CATMULL_ROM && count == 4) {
        const double h0 = xs[1] - xs[0];
        const double h1 = xs[2] - xs[1];
        const double h2 = xs[3] - xs[2];
        if (h0 > 0.0 && h1 > 0.0 && h2 > 0.0
            && std::fabs(h0 - h1) <= std::max(1.0e-12, std::fabs(h1) * 1.0e-9)
            && std::fabs(h2 - h1) <= std::max(1.0e-12, std::fabs(h1) * 1.0e-9)) {
            const double t = (x - xs[1]) / h1;
            return catmull_rom_scalar(ys[0], ys[1], ys[2], ys[3], t);
        }
    }
    return lagrange_scalar(xs, ys, count, x);
}

void interpolate_body_sample(
    const FastApparentCorrectionEpochSample* const* samples,
    const double* xs,
    size_t count,
    double x,
    FastApparentCorrectionInterpolationKind kind,
    bool body_1,
    FastApparentCorrectionBodySample* out
) noexcept {
    if (!out || !samples || !xs || count == 0 || count > 4) return;
    const FastApparentCorrectionBodySample FastApparentCorrectionEpochSample::*body_member =
        body_1 ? &FastApparentCorrectionEpochSample::body_1 : &FastApparentCorrectionEpochSample::body_0;
    double values[4] = {};
    const CartesianState FastApparentCorrectionBodySample::*state_members[] = {
        &FastApparentCorrectionBodySample::geometric,
        &FastApparentCorrectionBodySample::light_time_delta,
        &FastApparentCorrectionBodySample::deflection_delta,
        &FastApparentCorrectionBodySample::aberration_delta
    };
    CartesianState FastApparentCorrectionBodySample::*out_members[] = {
        &FastApparentCorrectionBodySample::geometric,
        &FastApparentCorrectionBodySample::light_time_delta,
        &FastApparentCorrectionBodySample::deflection_delta,
        &FastApparentCorrectionBodySample::aberration_delta
    };
    for (size_t m = 0; m < 4; ++m) {
        CartesianState* dst = &(out->*out_members[m]);
        const CartesianState FastApparentCorrectionBodySample::*src_member = state_members[m];
        for (size_t i = 0; i < count; ++i) values[i] = ((samples[i]->*body_member).*src_member).position_au.x;
        dst->position_au.x = interpolate_scalar_samples(xs, values, count, x, kind);
        for (size_t i = 0; i < count; ++i) values[i] = ((samples[i]->*body_member).*src_member).position_au.y;
        dst->position_au.y = interpolate_scalar_samples(xs, values, count, x, kind);
        for (size_t i = 0; i < count; ++i) values[i] = ((samples[i]->*body_member).*src_member).position_au.z;
        dst->position_au.z = interpolate_scalar_samples(xs, values, count, x, kind);
        for (size_t i = 0; i < count; ++i) values[i] = ((samples[i]->*body_member).*src_member).velocity_au_per_day.x;
        dst->velocity_au_per_day.x = interpolate_scalar_samples(xs, values, count, x, kind);
        for (size_t i = 0; i < count; ++i) values[i] = ((samples[i]->*body_member).*src_member).velocity_au_per_day.y;
        dst->velocity_au_per_day.y = interpolate_scalar_samples(xs, values, count, x, kind);
        for (size_t i = 0; i < count; ++i) values[i] = ((samples[i]->*body_member).*src_member).velocity_au_per_day.z;
        dst->velocity_au_per_day.z = interpolate_scalar_samples(xs, values, count, x, kind);
        for (size_t i = 0; i < count; ++i) values[i] = ((samples[i]->*body_member).*src_member).acceleration_au_per_day2.x;
        dst->acceleration_au_per_day2.x = interpolate_scalar_samples(xs, values, count, x, kind);
        for (size_t i = 0; i < count; ++i) values[i] = ((samples[i]->*body_member).*src_member).acceleration_au_per_day2.y;
        dst->acceleration_au_per_day2.y = interpolate_scalar_samples(xs, values, count, x, kind);
        for (size_t i = 0; i < count; ++i) values[i] = ((samples[i]->*body_member).*src_member).acceleration_au_per_day2.z;
        dst->acceleration_au_per_day2.z = interpolate_scalar_samples(xs, values, count, x, kind);
    }
}

void interpolate_epoch_sample_values(
    const FastApparentCorrectionEpochSample* const* samples,
    const double* xs,
    size_t count,
    double x,
    const SplitJulianDate& jd_tt,
    FastApparentCorrectionInterpolationKind kind,
    FastApparentCorrectionEpochSample* out
) noexcept {
    if (!out || !samples || !xs || count == 0 || count > 4) return;
    *out = FastApparentCorrectionEpochSample();
    out->jd_tt = jd_tt;
    double values[4] = {};
    for (int k = 0; k < 9; ++k) {
        for (size_t i = 0; i < count; ++i) values[i] = samples[i]->matrix[k];
        out->matrix[k] = interpolate_scalar_samples(xs, values, count, x, kind);
        for (size_t i = 0; i < count; ++i) values[i] = samples[i]->matrix_dot[k];
        out->matrix_dot[k] = interpolate_scalar_samples(xs, values, count, x, kind);
    }
    interpolate_body_sample(samples, xs, count, x, kind, false, &out->body_0);
    interpolate_body_sample(samples, xs, count, x, kind, true, &out->body_1);
}

bool eval_fast_apparent_target_position_from_correction(
    const FastApparentCorrectionBodySample& current,
    const FastApparentCorrectionBodySample& correction,
    const Matrix3x3& matrix,
    const Matrix3x3& matrix_dot,
    bool with_velocity,
    CartesianState* out_state
) noexcept {
    if (!out_state || !finite_vector(current.geometric.position_au)) {
        return false;
    }
    CartesianState corrected = current.geometric;
    corrected = state_add(corrected, correction.light_time_delta);
    corrected = state_add(corrected, correction.deflection_delta);
    corrected = state_add(corrected, correction.aberration_delta);
    const Vector3 unrotated_position = corrected.position_au;
    corrected.position_au = transform_position_with_matrix(corrected.position_au, matrix);
    if (with_velocity) {
        corrected.velocity_au_per_day = transform_velocity_with_matrix(
            unrotated_position,
            corrected.velocity_au_per_day,
            matrix,
            matrix_dot);
    } else {
        corrected.velocity_au_per_day = zero_vector();
    }
    corrected.acceleration_au_per_day2 = zero_vector();
    if (!finite_vector(corrected.position_au)
        || (with_velocity && !finite_vector(corrected.velocity_au_per_day))) {
        return false;
    }
    *out_state = corrected;
    return true;
}

bool eval_fast_apparent_target_geometric_sample(
    const SplitJulianDate& jd_tdb,
    int target_id,
    const internal::CompiledEphemerisBlock* target_block,
    const CartesianState& observer,
    const CartesianState& solar,
    bool with_velocity,
    FastApparentCorrectionBodySample* out
) noexcept {
    if (!out || !target_block || !target_block->data || !target_block->position) {
        return false;
    }
    CartesianState target;
    if (target_id == TAIYIN_BODY_SUN) {
        target = solar;
    } else if (!eval_block_state_for_fast_apparent(
            jd_tdb,
            target_block,
            with_velocity ? TAIYIN_APPARENT_VELOCITY : TAIYIN_APPARENT_SPHERICAL,
            &target)) {
        return false;
    }
    out->geometric.position_au = vector3_subtract(target.position_au, observer.position_au);
    out->geometric.velocity_au_per_day = with_velocity
        ? vector3_subtract(target.velocity_au_per_day, observer.velocity_au_per_day)
        : zero_vector();
    out->geometric.acceleration_au_per_day2 = zero_vector();
    out->light_time_delta = zero_state();
    out->deflection_delta = zero_state();
    out->aberration_delta = zero_state();
    return finite_vector(out->geometric.position_au) && vector3_norm(out->geometric.position_au) != 0.0
        && (!with_velocity || finite_vector(out->geometric.velocity_au_per_day));
}

void set_fast_apparent_diagnostic(
    EphemerisEvalDiagnostic* diagnostic,
    Status status,
    int target_id,
    int center_id,
    const SplitJulianDate& jd_tdb
) noexcept {
    if (!diagnostic) return;
    *diagnostic = EphemerisEvalDiagnostic();
    diagnostic->status = status;
    diagnostic->target_id = target_id;
    diagnostic->center_id = center_id;
    diagnostic->frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
    diagnostic->jd_tdb = jd_tdb;
}

Status body_2_failure_status(
    const RuntimeCompiledBlockData* data,
    size_t count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    for (size_t i = 0; i < count; ++i) {
        if (data[i].evaluated && data[i].last_status != TAIYIN_STATUS_OK) {
            copy_ephemeris_diagnostic(diagnostic, data[i].last_diagnostic);
            return data[i].last_status;
        }
    }
    return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

bool calc_matrix_for_fast_apparent_epoch(
    const NativeCalcContext& context,
    const ApparentOptions& apparent_options,
    const SplitJulianDate& jd_tt,
    double output_matrix[9],
    double output_matrix_dot[9]
) noexcept {
    double precession_matrix[9];
    double nutation_matrix[9];
    double output_matrix_ddot[9];
    return calc_apparent_matrices(
        jd_tt,
        apparent_options.flags,
        apparent_options.output_frame_id,
        context.model_context.precession_model_id,
        context.model_context.nutation_model_id,
        context.model_context.obliquity_model_id,
        context.model_context.frame_route_id,
        apparent_options.celestial_pole_offset_dx_rad,
        apparent_options.celestial_pole_offset_dy_rad,
        apparent_options.celestial_pole_offset_dx_rate_rad_per_day,
        apparent_options.celestial_pole_offset_dy_rate_rad_per_day,
        apparent_options.matrix_derivative_step_days,
        precession_matrix,
        nutation_matrix,
        output_matrix,
        output_matrix_dot,
        output_matrix_ddot,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        apparent_options.custom_output_frame_evaluator,
        apparent_options.custom_output_frame_data);
}

CartesianState state_from_arrays(const double pos[3], const double vel[3]) noexcept {
    CartesianState state;
    state.position_au = Vector3{pos[0], pos[1], pos[2]};
    state.velocity_au_per_day = vel ? Vector3{vel[0], vel[1], vel[2]} : zero_vector();
    state.acceleration_au_per_day2 = zero_vector();
    return state;
}

void fill_correction_body_sample(
    const double geometric_pos[3],
    const double geometric_vel[3],
    const double astrometric_pos[3],
    const double astrometric_vel[3],
    const double deflected_pos[3],
    const double deflected_vel[3],
    const double aberrated_pos[3],
    const double aberrated_vel[3],
    FastApparentCorrectionBodySample* out
) noexcept {
    const CartesianState geometric = state_from_arrays(geometric_pos, geometric_vel);
    const CartesianState astrometric = state_from_arrays(astrometric_pos, astrometric_vel);
    const CartesianState deflected = state_from_arrays(deflected_pos, deflected_vel);
    const CartesianState aberrated = state_from_arrays(aberrated_pos, aberrated_vel);
    out->geometric = geometric;
    out->light_time_delta = state_subtract(astrometric, geometric);
    out->deflection_delta = state_subtract(deflected, astrometric);
    out->aberration_delta = state_subtract(aberrated, deflected);
}

Status eval_body_2_correction_epoch_sample(
    const NativeCalcContext* context,
    const SplitJulianDate& jd_tt,
    int body_0_id,
    int body_1_id,
    const FastApparentOptions& options,
    FastApparentCorrectionEpochSample* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const int frame_id = output_frame_id(options.frame);
    if (frame_id < 0) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    const double tdb_minus_tt = dispatch::eval_tdb(context->model_context.tdb_model_id, jd_tt, nullptr);
    SplitJulianDate jd_tdb;
    if (!add_seconds_to_split_jd(jd_tt, tdb_minus_tt, &jd_tdb)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    ApparentOptions apparent_options = context->apparent_options;
    apparent_options.model_context = &context->model_context;
    apparent_options.flags = fast_apparent_flags(*context, options);
    apparent_options.output_frame_id = frame_id;
    if (options.true_position) {
        apparent_options.deflectors = nullptr;
        apparent_options.deflector_count = 0;
        apparent_options.solar_deflector_index = -1;
    }
    if ((apparent_options.flags & TAIYIN_APPARENT_TOPOCENTRIC) != 0u
        || apparent_options.deflector_count > 8) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    RuntimeStateEvalContext eval_context;
    eval_context.service = nullptr;
    eval_context.use_global = true;
    eval_context.route_rule_id = context->route_rule_id;
    eval_context.route_rules = context->route_rules;

    const uint32_t target_components = runtime_components_for_fast_apparent_flags(apparent_options.flags);
    RuntimeCompiledBlockData target_data[2] = {};
    target_data[0].context = eval_context;
    target_data[0].body_id = body_0_id;
    target_data[0].center_id = context->center_id;
    target_data[0].preferred_components = target_components;
    target_data[1].context = eval_context;
    target_data[1].body_id = body_1_id;
    target_data[1].center_id = context->center_id;
    target_data[1].preferred_components = target_components;
    internal::CompiledEphemerisBlock target_blocks_storage[2] = {
        make_runtime_compiled_block(&target_data[0]),
        make_runtime_compiled_block(&target_data[1])
    };

    RuntimeCompiledBlockData observer_data = {};
    observer_data.context = eval_context;
    observer_data.body_id = context->observer_id;
    observer_data.center_id = context->center_id;
    uint32_t observer_flags = apparent_options.flags;
    if ((apparent_options.flags & TAIYIN_APPARENT_ABERRATION) != 0u) {
        observer_flags |= TAIYIN_APPARENT_VELOCITY;
    }
    observer_data.preferred_components = runtime_components_for_fast_apparent_flags(observer_flags);
    internal::CompiledEphemerisBlock observer_block = make_runtime_compiled_block(&observer_data);

    RuntimeCompiledBlockData deflector_data[8] = {};
    internal::CompiledEphemerisBlock deflector_blocks_storage[8];
    const internal::CompiledEphemerisBlock* deflector_blocks[8] = {};
    int deflector_ids[8] = {};
    double deflector_schwarzschild_radius_au[8] = {};
    double deflector_limit[8] = {};
    for (size_t i = 0; i < apparent_options.deflector_count; ++i) {
        deflector_ids[i] = apparent_options.deflectors[i].body_id;
        if (deflector_ids[i] == body_0_id) {
            deflector_blocks[i] = &target_blocks_storage[0];
        } else if (deflector_ids[i] == body_1_id) {
            deflector_blocks[i] = &target_blocks_storage[1];
        } else {
            deflector_data[i].context = eval_context;
            deflector_data[i].body_id = deflector_ids[i];
            deflector_data[i].center_id = context->center_id;
            deflector_data[i].preferred_components = target_components;
            deflector_blocks_storage[i] = make_runtime_compiled_block(&deflector_data[i]);
            deflector_blocks[i] = &deflector_blocks_storage[i];
        }
        deflector_schwarzschild_radius_au[i] = apparent_options.deflectors[i].schwarzschild_radius_au;
        deflector_limit[i] = apparent_options.deflectors[i].limit;
    }

    double matrix_values[9];
    double matrix_dot_values[9];
    if (!calc_matrix_for_fast_apparent_epoch(*context, apparent_options, jd_tt, matrix_values, matrix_dot_values)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    for (int i = 0; i < 9; ++i) {
        out->matrix[i] = matrix_values[i];
        out->matrix_dot[i] = matrix_dot_values[i];
    }

    for (int body_index = 0; body_index < 2; ++body_index) {
        double geometric_pos[3] = {};
        double geometric_vel[3] = {};
        double astrometric_pos[3] = {};
        double astrometric_vel[3] = {};
        double deflected_pos[3] = {};
        double deflected_vel[3] = {};
        double aberrated_pos[3] = {};
        double aberrated_vel[3] = {};
        double apparent_pos[3] = {};
        double apparent_vel[3] = {};
        if (!calc_apparent_with_matrix(
                jd_tdb,
                body_index == 0 ? body_0_id : body_1_id,
                &target_blocks_storage[body_index],
                context->observer_id,
                &observer_block,
                nullptr,
                nullptr,
                nullptr,
                static_cast<int>(apparent_options.deflector_count),
                apparent_options.solar_deflector_index,
                apparent_options.deflector_count ? deflector_ids : nullptr,
                apparent_options.deflector_count ? deflector_blocks : nullptr,
                apparent_options.deflector_count ? deflector_schwarzschild_radius_au : nullptr,
                apparent_options.deflector_count ? deflector_limit : nullptr,
                apparent_options.flags | TAIYIN_APPARENT_USE_MATRIX,
                apparent_options.light_time_method_id,
                apparent_options.shapiro_delay_model_id,
                apparent_options.aberration_model_id,
                apparent_options.deflection_model_id,
                apparent_options.max_light_time_iterations,
                apparent_options.light_time_tolerance_days,
                matrix_values,
                matrix_dot_values,
                nullptr,
                geometric_pos,
                options.with_velocity ? geometric_vel : nullptr,
                nullptr,
                astrometric_pos,
                options.with_velocity ? astrometric_vel : nullptr,
                nullptr,
                deflected_pos,
                options.with_velocity ? deflected_vel : nullptr,
                nullptr,
                aberrated_pos,
                options.with_velocity ? aberrated_vel : nullptr,
                nullptr,
                apparent_pos,
                options.with_velocity ? apparent_vel : nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr)) {
            return body_2_failure_status(target_data, 2, diagnostic);
        }
        fill_correction_body_sample(
            geometric_pos,
            options.with_velocity ? geometric_vel : nullptr,
            astrometric_pos,
            options.with_velocity ? astrometric_vel : nullptr,
            deflected_pos,
            options.with_velocity ? deflected_vel : nullptr,
            aberrated_pos,
            options.with_velocity ? aberrated_vel : nullptr,
            body_index == 0 ? &out->body_0 : &out->body_1);
    }
    out->jd_tt = jd_tt;
    return TAIYIN_STATUS_OK;
}

}  // namespace

Status eval_body_correction_epoch_sample(
    const NativeCalcContext* context,
    const SplitJulianDate& jd_tt,
    int body_id,
    const FastApparentOptions& options,
    FastApparentCorrectionEpochSample* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const int frame_id = output_frame_id(options.frame);
    if (frame_id < 0) return TAIYIN_ERROR_UNSUPPORTED;
    const double tdb_minus_tt = dispatch::eval_tdb(context->model_context.tdb_model_id, jd_tt, nullptr);
    SplitJulianDate jd_tdb;
    if (!add_seconds_to_split_jd(jd_tt, tdb_minus_tt, &jd_tdb)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    ApparentOptions apparent_options = context->apparent_options;
    apparent_options.model_context = &context->model_context;
    apparent_options.flags = fast_apparent_flags(*context, options);
    apparent_options.output_frame_id = frame_id;
    if (options.true_position) {
        apparent_options.deflectors = nullptr;
        apparent_options.deflector_count = 0;
        apparent_options.solar_deflector_index = -1;
    }
    if ((apparent_options.flags & TAIYIN_APPARENT_TOPOCENTRIC) != 0u
        || apparent_options.deflector_count > 8) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    RuntimeStateEvalContext eval_context;
    eval_context.service = nullptr;
    eval_context.use_global = true;
    eval_context.route_rule_id = context->route_rule_id;
    eval_context.route_rules = context->route_rules;

    const uint32_t target_components = runtime_components_for_fast_apparent_flags(apparent_options.flags);
    RuntimeCompiledBlockData target_data = {};
    target_data.context = eval_context;
    target_data.body_id = body_id;
    target_data.center_id = context->center_id;
    target_data.preferred_components = target_components;
    internal::CompiledEphemerisBlock target_block = make_runtime_compiled_block(&target_data);

    RuntimeCompiledBlockData observer_data = {};
    observer_data.context = eval_context;
    observer_data.body_id = context->observer_id;
    observer_data.center_id = context->center_id;
    uint32_t observer_flags = apparent_options.flags;
    if ((apparent_options.flags & TAIYIN_APPARENT_ABERRATION) != 0u) {
        observer_flags |= TAIYIN_APPARENT_VELOCITY;
    }
    observer_data.preferred_components = runtime_components_for_fast_apparent_flags(observer_flags);
    internal::CompiledEphemerisBlock observer_block = make_runtime_compiled_block(&observer_data);

    RuntimeCompiledBlockData deflector_data[8] = {};
    internal::CompiledEphemerisBlock deflector_blocks_storage[8];
    const internal::CompiledEphemerisBlock* deflector_blocks[8] = {};
    int deflector_ids[8] = {};
    double deflector_schwarzschild_radius_au[8] = {};
    double deflector_limit[8] = {};
    for (size_t i = 0; i < apparent_options.deflector_count; ++i) {
        deflector_ids[i] = apparent_options.deflectors[i].body_id;
        if (deflector_ids[i] == body_id) {
            deflector_blocks[i] = &target_block;
        } else {
            deflector_data[i].context = eval_context;
            deflector_data[i].body_id = deflector_ids[i];
            deflector_data[i].center_id = context->center_id;
            deflector_data[i].preferred_components = target_components;
            deflector_blocks_storage[i] = make_runtime_compiled_block(&deflector_data[i]);
            deflector_blocks[i] = &deflector_blocks_storage[i];
        }
        deflector_schwarzschild_radius_au[i] = apparent_options.deflectors[i].schwarzschild_radius_au;
        deflector_limit[i] = apparent_options.deflectors[i].limit;
    }

    double matrix_values[9];
    double matrix_dot_values[9];
    if (!calc_matrix_for_fast_apparent_epoch(*context, apparent_options, jd_tt, matrix_values, matrix_dot_values)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    for (int i = 0; i < 9; ++i) {
        out->matrix[i] = matrix_values[i];
        out->matrix_dot[i] = matrix_dot_values[i];
    }

    double geometric_pos[3] = {};
    double geometric_vel[3] = {};
    double astrometric_pos[3] = {};
    double astrometric_vel[3] = {};
    double deflected_pos[3] = {};
    double deflected_vel[3] = {};
    double aberrated_pos[3] = {};
    double aberrated_vel[3] = {};
    double apparent_pos[3] = {};
    double apparent_vel[3] = {};
    if (!calc_apparent_with_matrix(
            jd_tdb,
            body_id,
            &target_block,
            context->observer_id,
            &observer_block,
            nullptr,
            nullptr,
            nullptr,
            static_cast<int>(apparent_options.deflector_count),
            apparent_options.solar_deflector_index,
            apparent_options.deflector_count ? deflector_ids : nullptr,
            apparent_options.deflector_count ? deflector_blocks : nullptr,
            apparent_options.deflector_count ? deflector_schwarzschild_radius_au : nullptr,
            apparent_options.deflector_count ? deflector_limit : nullptr,
            apparent_options.flags | TAIYIN_APPARENT_USE_MATRIX,
            apparent_options.light_time_method_id,
            apparent_options.shapiro_delay_model_id,
            apparent_options.aberration_model_id,
            apparent_options.deflection_model_id,
            apparent_options.max_light_time_iterations,
            apparent_options.light_time_tolerance_days,
            matrix_values,
            matrix_dot_values,
            nullptr,
            geometric_pos,
            options.with_velocity ? geometric_vel : nullptr,
            nullptr,
            astrometric_pos,
            options.with_velocity ? astrometric_vel : nullptr,
            nullptr,
            deflected_pos,
            options.with_velocity ? deflected_vel : nullptr,
            nullptr,
            aberrated_pos,
            options.with_velocity ? aberrated_vel : nullptr,
            nullptr,
            apparent_pos,
            options.with_velocity ? apparent_vel : nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr)) {
        const Status status = target_data.evaluated && target_data.last_status != TAIYIN_STATUS_OK
            ? target_data.last_status
            : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        if (target_data.evaluated && target_data.last_status != TAIYIN_STATUS_OK) {
            copy_ephemeris_diagnostic(diagnostic, target_data.last_diagnostic);
        }
        return status;
    }
    fill_correction_body_sample(
        geometric_pos,
        options.with_velocity ? geometric_vel : nullptr,
        astrometric_pos,
        options.with_velocity ? astrometric_vel : nullptr,
        deflected_pos,
        options.with_velocity ? deflected_vel : nullptr,
        aberrated_pos,
        options.with_velocity ? aberrated_vel : nullptr,
        &out->body_0);
    out->jd_tt = jd_tt;
    return TAIYIN_STATUS_OK;
}

size_t correction_interpolation_sample_count(FastApparentCorrectionInterpolationKind kind) noexcept {
    switch (kind) {
    case FAST_APPARENT_CORRECTION_INTERPOLATION_LINEAR:
        return 2;
    case FAST_APPARENT_CORRECTION_INTERPOLATION_QUADRATIC:
        return 3;
    case FAST_APPARENT_CORRECTION_INTERPOLATION_CUBIC:
    case FAST_APPARENT_CORRECTION_INTERPOLATION_CATMULL_ROM:
        return 4;
    default:
        return 0;
    }
}

Status eval_fast_correction_sample(
    const NativeCalcContext* context,
    int body_0_id,
    int body_1_id,
    const FastApparentOptions& options,
    const SplitJulianDate& jd_tt,
    FastApparentCorrectionEpochSample* out,
    EphemerisEvalDiagnostic* diagnostic
) {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    FastApparentOptions sample_options = options;
    sample_options.correction_sample = nullptr;
    return body_1_id != 0
        ? eval_body_2_correction_epoch_sample(
            context, jd_tt, body_0_id, body_1_id, sample_options, out, diagnostic)
        : eval_body_correction_epoch_sample(
            context, jd_tt, body_0_id, sample_options, out, diagnostic);
}

Status prepend_fast_correction_sample(
    const NativeCalcContext* context,
    int body_0_id,
    int body_1_id,
    const FastApparentOptions& options,
    FastApparentCorrectionSeries* series,
    EphemerisEvalDiagnostic* diagnostic
) {
    SplitJulianDate jd;
    if (!add_days_to_split_jd(series->start_jd_tt, -series->sample_step_days, &jd)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    FastApparentCorrectionEpochSample sample;
    const Status status = eval_fast_correction_sample(
        context, body_0_id, body_1_id, options, jd, &sample, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    series->samples.insert(series->samples.begin(), sample);
    series->start_jd_tt = jd;
    return TAIYIN_STATUS_OK;
}

Status append_fast_correction_sample(
    const NativeCalcContext* context,
    int body_0_id,
    int body_1_id,
    const FastApparentOptions& options,
    FastApparentCorrectionSeries* series,
    EphemerisEvalDiagnostic* diagnostic
) {
    SplitJulianDate jd;
    if (!add_days_to_split_jd(series->end_jd_tt, series->sample_step_days, &jd)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    FastApparentCorrectionEpochSample sample;
    const Status status = eval_fast_correction_sample(
        context, body_0_id, body_1_id, options, jd, &sample, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    series->samples.push_back(sample);
    series->end_jd_tt = jd;
    return TAIYIN_STATUS_OK;
}

Status ensure_fast_correction_range(
    const NativeCalcContext* context,
    int body_0_id,
    int body_1_id,
    const FastApparentOptions& options,
    const FastApparentCorrectionConfig& config,
    const SplitJulianDate& jd_tt,
    FastApparentCorrectionSeries* series,
    EphemerisEvalDiagnostic* diagnostic
) {
    if (!context || !series || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const uint64_t identity_hash = fast_correction_series_identity(*context, body_0_id, body_1_id, options);
    if (!series->samples.empty() && series->identity_hash != identity_hash) {
        *series = FastApparentCorrectionSeries();
    }
    if (series->samples.empty()) {
        const size_t wanted = correction_interpolation_sample_count(config.interpolation_kind);
        if (!std::isfinite(config.initial_half_days)
            || !std::isfinite(config.sample_step_days)
            || !(config.initial_half_days > 0.0)
            || !(config.sample_step_days > 0.0)
            || wanted < 2 || wanted > 4) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        const int interpolation_radius = static_cast<int>((wanted + 1) / 2);
        const double configured_radius_value = std::ceil(config.initial_half_days / config.sample_step_days);
        if (!std::isfinite(configured_radius_value)
            || configured_radius_value > static_cast<double>(kMaxFastCorrectionSampleRadius)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        const int configured_radius = static_cast<int>(configured_radius_value);
        const int sample_radius = std::max(interpolation_radius, configured_radius);
        if (sample_radius <= 0 || sample_radius > kMaxFastCorrectionSampleRadius) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        series->sample_step_days = config.sample_step_days;
        series->interpolation_kind = config.interpolation_kind;
        series->identity_hash = identity_hash;
        if (!add_days_to_split_jd(
                jd_tt,
                -static_cast<double>(sample_radius) * config.sample_step_days,
                &series->start_jd_tt)
            || !add_days_to_split_jd(
                jd_tt,
                static_cast<double>(sample_radius) * config.sample_step_days,
                &series->end_jd_tt)) {
            *series = FastApparentCorrectionSeries();
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        series->samples.reserve(static_cast<size_t>(2 * sample_radius + 1));
        for (int i = -sample_radius; i <= sample_radius; ++i) {
            SplitJulianDate sample_jd;
            if (!add_days_to_split_jd(
                    jd_tt,
                    static_cast<double>(i) * config.sample_step_days,
                    &sample_jd)) {
                *series = FastApparentCorrectionSeries();
                return TAIYIN_ERROR_INVALID_ARGUMENT;
            }
            FastApparentCorrectionEpochSample sample;
            const Status status = eval_fast_correction_sample(
                context, body_0_id, body_1_id, options, sample_jd, &sample, diagnostic);
            if (status != TAIYIN_STATUS_OK) {
                *series = FastApparentCorrectionSeries();
                return status;
            }
            series->samples.push_back(sample);
        }
    }
    const size_t wanted = correction_interpolation_sample_count(series->interpolation_kind);
    if (!std::isfinite(series->sample_step_days)
        || !(series->sample_step_days > 0.0)
        || wanted < 2 || wanted > 4) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double padding_days = static_cast<double>(wanted) * 0.5 * series->sample_step_days;
    SplitJulianDate required_start;
    SplitJulianDate required_end;
    if (!add_days_to_split_jd(jd_tt, -padding_days, &required_start)
        || !add_days_to_split_jd(jd_tt, padding_days, &required_end)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    while (!series->samples.empty() && required_start < series->start_jd_tt) {
        const Status status = prepend_fast_correction_sample(
            context, body_0_id, body_1_id, options, series, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
    }
    while (!series->samples.empty() && required_end > series->end_jd_tt) {
        const Status status = append_fast_correction_sample(
            context, body_0_id, body_1_id, options, series, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
    }
    return TAIYIN_STATUS_OK;
}

Status interpolate_fast_correction_samples(
    const FastApparentCorrectionSeries& series,
    FastApparentCorrectionInterpolationKind kind,
    const SplitJulianDate& jd_tt,
    FastApparentCorrectionEpochSample* out
) noexcept {
    if (!out || series.samples.empty() || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const size_t wanted = correction_interpolation_sample_count(kind);
    if (wanted < 2 || wanted > 4 || series.samples.size() < wanted) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    size_t upper = 0;
    while (upper < series.samples.size() && series.samples[upper].jd_tt < jd_tt) {
        ++upper;
    }
    if (upper >= series.samples.size()) upper = series.samples.size() - 1;

    size_t start = 0;
    if (wanted == 2) {
        start = upper == 0 ? 0 : upper - 1;
    } else {
        start = upper > wanted / 2 ? upper - wanted / 2 : 0;
    }
    if (start + wanted > series.samples.size()) {
        start = series.samples.size() - wanted;
    }

    const FastApparentCorrectionEpochSample* sample_ptrs[4] = {};
    double xs[4] = {};
    for (size_t i = 0; i < wanted; ++i) {
        sample_ptrs[i] = &series.samples[start + i];
        xs[i] = days_between_split_jd(jd_tt, sample_ptrs[i]->jd_tt);
    }
    interpolate_epoch_sample_values(sample_ptrs, xs, wanted, 0.0, jd_tt, kind, out);
    return TAIYIN_STATUS_OK;
}

Status get_fast_correction(
    const NativeCalcContext* context,
    int body_0_id,
    int body_1_id,
    const FastApparentOptions& options,
    const FastApparentCorrectionConfig& config,
    const SplitJulianDate& jd_tt,
    FastApparentCorrectionSeries* series,
    EphemerisEvalDiagnostic* diagnostic,
    FastApparentCorrectionEpochSample* out
) {
    if (!context || !series || !out || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    Status status = ensure_fast_correction_range(
        context, body_0_id, body_1_id, options, config, jd_tt, series, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    return interpolate_fast_correction_samples(*series, series->interpolation_kind, jd_tt, out);
}

Status init_fast_correction_series(
    const NativeCalcContext* context,
    int body_0_id,
    int body_1_id,
    const FastApparentOptions& options,
    const FastApparentCorrectionConfig& config,
    const SplitJulianDate& center_jd_tt,
    FastApparentCorrectionSeries* series,
    EphemerisEvalDiagnostic* diagnostic
) {
    if (!series) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *series = FastApparentCorrectionSeries();
    FastApparentCorrectionEpochSample sample;
    return get_fast_correction(
        context,
        body_0_id,
        body_1_id,
        options,
        config,
        center_jd_tt,
        series,
        diagnostic,
        &sample);
}

Status eval_fast_apparent_body_2_tdb(
    const NativeCalcContext* context,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    int body_0_id,
    int body_1_id,
    const FastApparentOptions& options,
    FastApparentBody2State* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tdb)
        || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = FastApparentBody2State();
    const int frame = output_frame_id(options.frame);
    if (frame < 0) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    ApparentOptions apparent_options = context->apparent_options;
    apparent_options.model_context = &context->model_context;
    apparent_options.flags = fast_apparent_flags(*context, options);
    apparent_options.output_frame_id = frame;
    if (options.true_position) {
        apparent_options.deflectors = nullptr;
        apparent_options.deflector_count = 0;
        apparent_options.solar_deflector_index = -1;
    }

    if ((apparent_options.flags & TAIYIN_APPARENT_TOPOCENTRIC) != 0u) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    if (apparent_options.deflector_count > 8) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    if (!valid_solar_deflector_index_for_fast_apparent(
            apparent_options.deflector_count,
            apparent_options.solar_deflector_index)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    RuntimeStateEvalContext eval_context;
    eval_context.service = nullptr;
    eval_context.use_global = true;
    eval_context.route_rule_id = context->route_rule_id;
    eval_context.route_rules = context->route_rules;

    const uint32_t target_components = runtime_components_for_fast_apparent_flags(apparent_options.flags);
    RuntimeCompiledBlockData target_data[2] = {};
    target_data[0].context = eval_context;
    target_data[0].body_id = body_0_id;
    target_data[0].center_id = context->center_id;
    target_data[0].preferred_components = target_components;
    target_data[1].context = eval_context;
    target_data[1].body_id = body_1_id;
    target_data[1].center_id = context->center_id;
    target_data[1].preferred_components = target_components;
    internal::CompiledEphemerisBlock target_blocks_storage[2] = {
        make_runtime_compiled_block(&target_data[0]),
        make_runtime_compiled_block(&target_data[1])
    };
    const internal::CompiledEphemerisBlock* target_blocks[2] = {
        &target_blocks_storage[0], &target_blocks_storage[1]
    };
    const int target_ids[2] = {body_0_id, body_1_id};

    RuntimeCompiledBlockData observer_data = {};
    observer_data.context = eval_context;
    observer_data.body_id = context->observer_id;
    observer_data.center_id = context->center_id;
    observer_data.preferred_components = runtime_components_for_fast_apparent_flags(apparent_options.flags);
    internal::CompiledEphemerisBlock observer_block = make_runtime_compiled_block(&observer_data);

    RuntimeCompiledBlockData deflector_data[8] = {};
    internal::CompiledEphemerisBlock deflector_blocks_storage[8];
    const internal::CompiledEphemerisBlock* deflector_blocks[8] = {};
    int deflector_ids[8] = {};
    double deflector_schwarzschild_radius_au[8] = {};
    double deflector_limit[8] = {};
    for (size_t i = 0; i < apparent_options.deflector_count; ++i) {
        deflector_ids[i] = apparent_options.deflectors[i].body_id;
        if (deflector_ids[i] == body_0_id) {
            deflector_blocks[i] = &target_blocks_storage[0];
        } else if (deflector_ids[i] == body_1_id) {
            deflector_blocks[i] = &target_blocks_storage[1];
        } else {
            deflector_data[i].context = eval_context;
            deflector_data[i].body_id = deflector_ids[i];
            deflector_data[i].center_id = context->center_id;
            deflector_data[i].preferred_components = runtime_components_for_fast_apparent_flags(apparent_options.flags);
            deflector_blocks_storage[i] = make_runtime_compiled_block(&deflector_data[i]);
            deflector_blocks[i] = &deflector_blocks_storage[i];
        }
        deflector_schwarzschild_radius_au[i] = apparent_options.deflectors[i].schwarzschild_radius_au;
        deflector_limit[i] = apparent_options.deflectors[i].limit;
    }

    double output_matrix[9] = {};
    double output_matrix_dot[9] = {};
    double output_matrix_ddot[9] = {};
    double precession_matrix[9] = {};
    double nutation_matrix[9] = {};
    const bool use_correction_sample = options.correction_sample != nullptr;
    if (use_correction_sample) {
        for (int i = 0; i < 9; ++i) {
            output_matrix[i] = options.correction_sample->matrix[i];
            output_matrix_dot[i] = options.correction_sample->matrix_dot[i];
        }
    } else if (!calc_apparent_matrices(
            jd_tt,
            apparent_options.flags,
            apparent_options.output_frame_id,
            context->model_context.precession_model_id,
            context->model_context.nutation_model_id,
            context->model_context.obliquity_model_id,
            context->model_context.frame_route_id,
            apparent_options.celestial_pole_offset_dx_rad,
            apparent_options.celestial_pole_offset_dy_rad,
            apparent_options.celestial_pole_offset_dx_rate_rad_per_day,
            apparent_options.celestial_pole_offset_dy_rate_rad_per_day,
            apparent_options.matrix_derivative_step_days,
            precession_matrix,
            nutation_matrix,
            output_matrix,
            output_matrix_dot,
            output_matrix_ddot,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            apparent_options.custom_output_frame_evaluator,
            apparent_options.custom_output_frame_data)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    if (use_correction_sample || (!options.with_velocity
        && (apparent_options.flags & TAIYIN_APPARENT_SHAPIRO_DELAY) == 0u)) {
        uint32_t observer_eval_flags = apparent_options.flags;
        if ((apparent_options.flags & TAIYIN_APPARENT_ABERRATION) != 0u) {
            observer_eval_flags |= TAIYIN_APPARENT_VELOCITY;
        }

        CartesianState observer;
        if (!eval_block_state_for_fast_apparent(jd_tdb, &observer_block, observer_eval_flags, &observer)) {
            const Status status = observer_data.evaluated && observer_data.last_status != TAIYIN_STATUS_OK
                ? observer_data.last_status
                : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
            if (observer_data.evaluated && observer_data.last_status != TAIYIN_STATUS_OK) {
                copy_ephemeris_diagnostic(diagnostic, observer_data.last_diagnostic);
            } else {
                set_fast_apparent_diagnostic(diagnostic, status, 0, context->observer_id, jd_tdb);
            }
            return status;
        }
        const internal::CompiledEphemerisBlock* solar_block = nullptr;
        if (body_0_id == TAIYIN_BODY_SUN) {
            solar_block = &target_blocks_storage[0];
        } else if (body_1_id == TAIYIN_BODY_SUN) {
            solar_block = &target_blocks_storage[1];
        } else if (apparent_options.solar_deflector_index >= 0) {
            solar_block = deflector_blocks[apparent_options.solar_deflector_index];
        }
        CartesianState solar;
        if (solar_block && !eval_block_state_for_fast_apparent(jd_tdb, solar_block, apparent_options.flags, &solar)) {
            const Status status = body_2_failure_status(target_data, 2, diagnostic);
            if (status == TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED) {
                set_fast_apparent_diagnostic(diagnostic, status, TAIYIN_BODY_SUN, context->observer_id, jd_tdb);
            }
            return status;
        }

        const Matrix3x3 matrix = matrix_from_array9(output_matrix);
        const Matrix3x3 matrix_dot = matrix_from_array9(output_matrix_dot);
        Vector3 body_0_position;
        Vector3 body_1_position;
        Vector3 body_0_velocity = zero_vector();
        Vector3 body_1_velocity = zero_vector();
        if (use_correction_sample) {
            FastApparentCorrectionBodySample body_0_current;
            FastApparentCorrectionBodySample body_1_current;
            CartesianState body_0_state;
            CartesianState body_1_state;
            if (!eval_fast_apparent_target_geometric_sample(
                    jd_tdb,
                    body_0_id,
                    &target_blocks_storage[0],
                    observer,
                    solar,
                    options.with_velocity,
                    &body_0_current)
                || !eval_fast_apparent_target_geometric_sample(
                    jd_tdb,
                    body_1_id,
                    &target_blocks_storage[1],
                    observer,
                    solar,
                    options.with_velocity,
                    &body_1_current)
                ) {
                const Status status = body_2_failure_status(target_data, 2, diagnostic);
                if (status == TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED) {
                    set_fast_apparent_diagnostic(diagnostic, status, 0, context->observer_id, jd_tdb);
                }
                return status;
            }
            bool corrected = false;
            corrected = eval_fast_apparent_target_position_from_correction(
                    body_0_current,
                    options.correction_sample->body_0,
                    matrix,
                    matrix_dot,
                    options.with_velocity,
                    &body_0_state)
                && eval_fast_apparent_target_position_from_correction(
                    body_1_current,
                    options.correction_sample->body_1,
                    matrix,
                    matrix_dot,
                    options.with_velocity,
                    &body_1_state);
            if (!corrected) {
                return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
            }
            body_0_position = body_0_state.position_au;
            body_1_position = body_1_state.position_au;
            body_0_velocity = body_0_state.velocity_au_per_day;
            body_1_velocity = body_1_state.velocity_au_per_day;
        } else if (!eval_fast_apparent_target_position_direct(
                jd_tdb,
                body_0_id,
                &target_blocks_storage[0],
                observer,
                solar,
                apparent_options.deflector_count,
                apparent_options.solar_deflector_index,
                apparent_options.deflector_count ? deflector_ids : nullptr,
                apparent_options.deflector_count ? deflector_schwarzschild_radius_au : nullptr,
                apparent_options.deflector_count ? deflector_limit : nullptr,
                apparent_options.flags | TAIYIN_APPARENT_USE_MATRIX,
                apparent_options.light_time_method_id,
                apparent_options.shapiro_delay_model_id,
                apparent_options.aberration_model_id,
                apparent_options.deflection_model_id,
                apparent_options.max_light_time_iterations,
                apparent_options.light_time_tolerance_days,
                matrix,
                &body_0_position)
            || !eval_fast_apparent_target_position_direct(
                jd_tdb,
                body_1_id,
                &target_blocks_storage[1],
                observer,
                solar,
                apparent_options.deflector_count,
                apparent_options.solar_deflector_index,
                apparent_options.deflector_count ? deflector_ids : nullptr,
                apparent_options.deflector_count ? deflector_schwarzschild_radius_au : nullptr,
                apparent_options.deflector_count ? deflector_limit : nullptr,
                apparent_options.flags | TAIYIN_APPARENT_USE_MATRIX,
                apparent_options.light_time_method_id,
                apparent_options.shapiro_delay_model_id,
                apparent_options.aberration_model_id,
                apparent_options.deflection_model_id,
                apparent_options.max_light_time_iterations,
                apparent_options.light_time_tolerance_days,
                matrix,
                &body_1_position)) {
            const Status status = body_2_failure_status(target_data, 2, diagnostic);
            if (status == TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED) {
                set_fast_apparent_diagnostic(diagnostic, status, 0, context->observer_id, jd_tdb);
            }
            return status;
        }

        out->body_0.position_au = body_0_position;
        out->body_1.position_au = body_1_position;
        if (options.with_velocity) {
            out->body_0.velocity_au_per_day = body_0_velocity;
            out->body_1.velocity_au_per_day = body_1_velocity;
        }
        set_fast_apparent_diagnostic(diagnostic, TAIYIN_STATUS_OK, 0, context->observer_id, jd_tdb);
        return TAIYIN_STATUS_OK;
    }

    double observer_offset_pos[3] = {0.0, 0.0, 0.0};
    double observer_offset_vel[3] = {0.0, 0.0, 0.0};
    double observer_offset_acc[3] = {0.0, 0.0, 0.0};

    double geometric_pos[6] = {};
    double geometric_vel[6] = {};
    double astrometric_pos[6] = {};
    double astrometric_vel[6] = {};
    double deflected_pos[6] = {};
    double deflected_vel[6] = {};
    double aberrated_pos[6] = {};
    double aberrated_vel[6] = {};
    double apparent_pos[6] = {};
    double apparent_vel[6] = {};
    double lon_rad[2] = {};
    double lat_rad[2] = {};
    double distance_au[2] = {};
    double lon_rate[2] = {};
    double lat_rate[2] = {};
    double distance_rate[2] = {};
    double light_time[2] = {};
    double light_time_rate[2] = {};
    int light_time_iterations[2] = {};

    const bool ok = calc_apparent_batch_with_matrix(
        jd_tdb,
        2,
        target_ids,
        target_blocks,
        context->observer_id,
        &observer_block,
        observer_offset_pos,
        observer_offset_vel,
        observer_offset_acc,
        static_cast<int>(apparent_options.deflector_count),
        apparent_options.solar_deflector_index,
        apparent_options.deflector_count ? deflector_ids : nullptr,
        apparent_options.deflector_count ? deflector_blocks : nullptr,
        apparent_options.deflector_count ? deflector_schwarzschild_radius_au : nullptr,
        apparent_options.deflector_count ? deflector_limit : nullptr,
        apparent_options.flags | TAIYIN_APPARENT_USE_MATRIX,
        apparent_options.light_time_method_id,
        apparent_options.shapiro_delay_model_id,
        apparent_options.aberration_model_id,
        apparent_options.deflection_model_id,
        apparent_options.max_light_time_iterations,
        apparent_options.light_time_tolerance_days,
        output_matrix,
        output_matrix_dot,
        output_matrix_ddot,
        geometric_pos,
        options.with_velocity ? geometric_vel : nullptr,
        nullptr,
        astrometric_pos,
        options.with_velocity ? astrometric_vel : nullptr,
        nullptr,
        deflected_pos,
        options.with_velocity ? deflected_vel : nullptr,
        nullptr,
        aberrated_pos,
        options.with_velocity ? aberrated_vel : nullptr,
        nullptr,
        apparent_pos,
        options.with_velocity ? apparent_vel : nullptr,
        nullptr,
        lon_rad,
        lat_rad,
        distance_au,
        options.with_velocity ? lon_rate : nullptr,
        options.with_velocity ? lat_rate : nullptr,
        options.with_velocity ? distance_rate : nullptr,
        nullptr,
        nullptr,
        nullptr,
        light_time,
        options.with_velocity ? light_time_rate : nullptr,
        nullptr,
        light_time_iterations);

    if (!ok) {
        Status status = TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        if (target_data[0].evaluated && target_data[0].last_status != TAIYIN_STATUS_OK) {
            status = target_data[0].last_status;
            copy_ephemeris_diagnostic(diagnostic, target_data[0].last_diagnostic);
        } else if (target_data[1].evaluated && target_data[1].last_status != TAIYIN_STATUS_OK) {
            status = target_data[1].last_status;
            copy_ephemeris_diagnostic(diagnostic, target_data[1].last_diagnostic);
        } else {
            set_fast_apparent_diagnostic(diagnostic, status, 0, context->observer_id, jd_tdb);
        }
        return status;
    }

    out->body_0.position_au = {apparent_pos[0], apparent_pos[1], apparent_pos[2]};
    out->body_1.position_au = {apparent_pos[3], apparent_pos[4], apparent_pos[5]};
    if (options.with_velocity) {
        out->body_0.velocity_au_per_day = {apparent_vel[0], apparent_vel[1], apparent_vel[2]};
        out->body_1.velocity_au_per_day = {apparent_vel[3], apparent_vel[4], apparent_vel[5]};
    }
    set_fast_apparent_diagnostic(diagnostic, TAIYIN_STATUS_OK, 0, context->observer_id, jd_tdb);
    return TAIYIN_STATUS_OK;
}

Status eval_fast_apparent_body_tdb(
    const NativeCalcContext* context,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    int body_id,
    const FastApparentOptions& options,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tdb)
        || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = CartesianState();
    const int frame = output_frame_id(options.frame);
    if (frame < 0) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    ApparentOptions apparent_options = context->apparent_options;
    apparent_options.model_context = &context->model_context;
    apparent_options.flags = fast_apparent_flags(*context, options);
    apparent_options.output_frame_id = frame;
    if (options.true_position) {
        apparent_options.deflectors = nullptr;
        apparent_options.deflector_count = 0;
        apparent_options.solar_deflector_index = -1;
    }
    if ((apparent_options.flags & TAIYIN_APPARENT_TOPOCENTRIC) != 0u) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    if (apparent_options.deflector_count > 8) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    if (!valid_solar_deflector_index_for_fast_apparent(
            apparent_options.deflector_count,
            apparent_options.solar_deflector_index)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    RuntimeStateEvalContext eval_context;
    eval_context.service = nullptr;
    eval_context.use_global = true;
    eval_context.route_rule_id = context->route_rule_id;
    eval_context.route_rules = context->route_rules;

    const uint32_t target_components = runtime_components_for_fast_apparent_flags(apparent_options.flags);
    RuntimeCompiledBlockData target_data = {};
    target_data.context = eval_context;
    target_data.body_id = body_id;
    target_data.center_id = context->center_id;
    target_data.preferred_components = target_components;
    internal::CompiledEphemerisBlock target_block = make_runtime_compiled_block(&target_data);

    RuntimeCompiledBlockData observer_data = {};
    observer_data.context = eval_context;
    observer_data.body_id = context->observer_id;
    observer_data.center_id = context->center_id;
    observer_data.preferred_components = runtime_components_for_fast_apparent_flags(apparent_options.flags);
    internal::CompiledEphemerisBlock observer_block = make_runtime_compiled_block(&observer_data);

    RuntimeCompiledBlockData deflector_data[8] = {};
    internal::CompiledEphemerisBlock deflector_blocks_storage[8];
    const internal::CompiledEphemerisBlock* deflector_blocks[8] = {};
    int deflector_ids[8] = {};
    double deflector_schwarzschild_radius_au[8] = {};
    double deflector_limit[8] = {};
    for (size_t i = 0; i < apparent_options.deflector_count; ++i) {
        deflector_ids[i] = apparent_options.deflectors[i].body_id;
        if (deflector_ids[i] == body_id) {
            deflector_blocks[i] = &target_block;
        } else {
            deflector_data[i].context = eval_context;
            deflector_data[i].body_id = deflector_ids[i];
            deflector_data[i].center_id = context->center_id;
            deflector_data[i].preferred_components = runtime_components_for_fast_apparent_flags(apparent_options.flags);
            deflector_blocks_storage[i] = make_runtime_compiled_block(&deflector_data[i]);
            deflector_blocks[i] = &deflector_blocks_storage[i];
        }
        deflector_schwarzschild_radius_au[i] = apparent_options.deflectors[i].schwarzschild_radius_au;
        deflector_limit[i] = apparent_options.deflectors[i].limit;
    }

    double output_matrix[9] = {};
    double output_matrix_dot[9] = {};
    double output_matrix_ddot[9] = {};
    double precession_matrix[9] = {};
    double nutation_matrix[9] = {};
    const bool use_correction_sample = options.correction_sample != nullptr;
    if (use_correction_sample) {
        for (int i = 0; i < 9; ++i) {
            output_matrix[i] = options.correction_sample->matrix[i];
            output_matrix_dot[i] = options.correction_sample->matrix_dot[i];
        }
    } else if (!calc_apparent_matrices(
            jd_tt,
            apparent_options.flags,
            apparent_options.output_frame_id,
            context->model_context.precession_model_id,
            context->model_context.nutation_model_id,
            context->model_context.obliquity_model_id,
            context->model_context.frame_route_id,
            apparent_options.celestial_pole_offset_dx_rad,
            apparent_options.celestial_pole_offset_dy_rad,
            apparent_options.celestial_pole_offset_dx_rate_rad_per_day,
            apparent_options.celestial_pole_offset_dy_rate_rad_per_day,
            apparent_options.matrix_derivative_step_days,
            precession_matrix,
            nutation_matrix,
            output_matrix,
            output_matrix_dot,
            output_matrix_ddot,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            apparent_options.custom_output_frame_evaluator,
            apparent_options.custom_output_frame_data)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    uint32_t observer_eval_flags = apparent_options.flags;
    if ((apparent_options.flags & TAIYIN_APPARENT_ABERRATION) != 0u) {
        observer_eval_flags |= TAIYIN_APPARENT_VELOCITY;
    }
    CartesianState observer;
    if (!eval_block_state_for_fast_apparent(jd_tdb, &observer_block, observer_eval_flags, &observer)) {
        const Status status = observer_data.evaluated && observer_data.last_status != TAIYIN_STATUS_OK
            ? observer_data.last_status
            : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        if (observer_data.evaluated && observer_data.last_status != TAIYIN_STATUS_OK) {
            copy_ephemeris_diagnostic(diagnostic, observer_data.last_diagnostic);
        } else {
            set_fast_apparent_diagnostic(diagnostic, status, 0, context->observer_id, jd_tdb);
        }
        return status;
    }
    const internal::CompiledEphemerisBlock* solar_block = nullptr;
    if (body_id == TAIYIN_BODY_SUN) {
        solar_block = &target_block;
    } else if (apparent_options.solar_deflector_index >= 0) {
        solar_block = deflector_blocks[apparent_options.solar_deflector_index];
    }
    CartesianState solar;
    if (solar_block && !eval_block_state_for_fast_apparent(jd_tdb, solar_block, apparent_options.flags, &solar)) {
        const Status status = target_data.evaluated && target_data.last_status != TAIYIN_STATUS_OK
            ? target_data.last_status
            : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        if (target_data.evaluated && target_data.last_status != TAIYIN_STATUS_OK) {
            copy_ephemeris_diagnostic(diagnostic, target_data.last_diagnostic);
        } else {
            set_fast_apparent_diagnostic(diagnostic, status, TAIYIN_BODY_SUN, context->observer_id, jd_tdb);
        }
        return status;
    }

    const Matrix3x3 matrix = matrix_from_array9(output_matrix);
    const Matrix3x3 matrix_dot = matrix_from_array9(output_matrix_dot);
    if (use_correction_sample) {
        FastApparentCorrectionBodySample current;
        if (!eval_fast_apparent_target_geometric_sample(
                jd_tdb,
                body_id,
                &target_block,
                observer,
                solar,
                options.with_velocity,
                &current)) {
            const Status status = target_data.evaluated && target_data.last_status != TAIYIN_STATUS_OK
                ? target_data.last_status
                : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
            set_fast_apparent_diagnostic(diagnostic, status, body_id, context->observer_id, jd_tdb);
            return status;
        }
        bool corrected = false;
        corrected = eval_fast_apparent_target_position_from_correction(
            current,
            options.correction_sample->body_0,
            matrix,
            matrix_dot,
            options.with_velocity,
            out);
        if (!corrected) {
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }
    } else {
        Vector3 position;
        if (!eval_fast_apparent_target_position_direct(
                jd_tdb,
                body_id,
                &target_block,
                observer,
                solar,
                apparent_options.deflector_count,
                apparent_options.solar_deflector_index,
                apparent_options.deflector_count ? deflector_ids : nullptr,
                apparent_options.deflector_count ? deflector_schwarzschild_radius_au : nullptr,
                apparent_options.deflector_count ? deflector_limit : nullptr,
                apparent_options.flags | TAIYIN_APPARENT_USE_MATRIX,
                apparent_options.light_time_method_id,
                apparent_options.shapiro_delay_model_id,
                apparent_options.aberration_model_id,
                apparent_options.deflection_model_id,
                apparent_options.max_light_time_iterations,
                apparent_options.light_time_tolerance_days,
                matrix,
                &position)) {
            const Status status = target_data.evaluated && target_data.last_status != TAIYIN_STATUS_OK
                ? target_data.last_status
                : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
            set_fast_apparent_diagnostic(diagnostic, status, body_id, context->observer_id, jd_tdb);
            return status;
        }
        out->position_au = position;
    }
    set_fast_apparent_diagnostic(diagnostic, TAIYIN_STATUS_OK, body_id, context->observer_id, jd_tdb);
    return TAIYIN_STATUS_OK;
}

}  // namespace runtime
}  // namespace taiyin
