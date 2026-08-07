#include "taiyin/astrology/sidereal.h"

#include "sidereal_internal.h"

#include "taiyin/angle.h"
#include "taiyin/apparent_position.h"
#include "taiyin/dispatch.h"
#include "taiyin/geometry.h"
#include "taiyin/time.h"
#include "taiyin/vector3.h"

#include <cmath>

namespace taiyin {
namespace astrology {
namespace {

constexpr double kSiderealRateStepDays = 1.0e-3;
const double kSolarSystemPlaneNodeJ2000Rad = 107.582569 * TAIYIN_DEG_TO_RAD;
const double kSolarSystemPlaneInclinationRad = 1.578701 * TAIYIN_DEG_TO_RAD;

enum SiderealReferencePlaneId {
    kMeanEclipticOfDate,
    kFixedEclipticAtEpoch,
    kSolarSystemInvariable,
    kJ2000Ecliptic,
};

struct ResolvedSiderealReferenceFlags {
    uint32_t native_position_flags;
    SiderealReferencePlaneId plane;
    bool reference_epoch_is_ut1;
};

Status resolve_sidereal_reference_flags(
    uint64_t flags,
    SplitJulianDate reference_epoch_jd,
    ResolvedSiderealReferenceFlags* out
) noexcept {
    if (!out || (flags & ~TAIYIN_SIDEREAL_KNOWN_FLAGS) != 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const uint64_t plane_flags = flags & TAIYIN_SIDEREAL_REFERENCE_PLANE_FLAGS;
    const uint64_t policy_flags = flags & TAIYIN_SIDEREAL_PRECESSION_POLICY_FLAGS;
    if (plane_flags != 0u && (plane_flags & (plane_flags - 1u)) != 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (policy_flags != 0u && (policy_flags & (policy_flags - 1u)) != 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    out->native_position_flags = static_cast<uint32_t>(
        flags & TAIYIN_SIDEREAL_POSITION_FLAGS_MASK);
    out->reference_epoch_is_ut1 =
        (flags & TAIYIN_SIDEREAL_REFERENCE_EPOCH_UT1) != 0u;
    out->plane = plane_flags == TAIYIN_SIDEREAL_REFERENCE_ECL_T0
        ? kFixedEclipticAtEpoch
        : plane_flags == TAIYIN_SIDEREAL_REFERENCE_SSY_PLANE
            ? kSolarSystemInvariable
            : plane_flags == TAIYIN_SIDEREAL_REFERENCE_J2000_ECLIPTIC
                ? kJ2000Ecliptic
                : kMeanEclipticOfDate;
    const bool needs_epoch = out->plane == kFixedEclipticAtEpoch
        || out->plane == kSolarSystemInvariable;
    if (needs_epoch != split_julian_date_is_finite(reference_epoch_jd)
        || (out->reference_epoch_is_ut1 && !needs_epoch)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    return TAIYIN_STATUS_OK;
}

bool special_reference_plane(SiderealReferencePlaneId plane) noexcept {
    return plane != kMeanEclipticOfDate;
}

bool finite_vector(const Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Matrix3x3 matrix_from_array(const double values[9]) noexcept {
    Matrix3x3 result;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            result.m[row][column] = values[row * 3 + column];
        }
    }
    return result;
}

void matrix_to_array(const Matrix3x3& matrix, double out[9]) noexcept {
    if (!out) return;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            out[row * 3 + column] = matrix.m[row][column];
        }
    }
}

Status mean_ecliptic_matrix(
    const runtime::NativeCalcContext& context,
    SplitJulianDate jd_tt,
    bool with_rate,
    Matrix3x3* out_matrix,
    Matrix3x3* out_matrix_dot
) noexcept {
    if (!out_matrix || !out_matrix_dot || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    double matrix[9] = {};
    double matrix_dot[9] = {};
    if (!calc_apparent_matrices(
            jd_tt,
            with_rate ? TAIYIN_APPARENT_VELOCITY : 0u,
            TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE,
            context.model_context.precession_model_id,
            context.model_context.nutation_model_id,
            context.model_context.obliquity_model_id,
            context.model_context.frame_route_id,
            context.apparent_options.celestial_pole_offset_dx_rad,
            context.apparent_options.celestial_pole_offset_dy_rad,
            context.apparent_options.celestial_pole_offset_dx_rate_rad_per_day,
            context.apparent_options.celestial_pole_offset_dy_rate_rad_per_day,
            context.apparent_options.matrix_derivative_step_days,
            nullptr,
            nullptr,
            matrix,
            matrix_dot,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    *out_matrix = matrix_from_array(matrix);
    *out_matrix_dot = matrix_from_array(matrix_dot);
    return TAIYIN_STATUS_OK;
}

Status reference_epoch_tt(
    const runtime::NativeCalcContext& native,
    SplitJulianDate reference_epoch_jd,
    bool reference_epoch_is_ut1,
    SplitJulianDate* out_jd_tt
) noexcept {
    if (!out_jd_tt || !split_julian_date_is_finite(reference_epoch_jd)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (!reference_epoch_is_ut1) {
        *out_jd_tt = reference_epoch_jd;
        return TAIYIN_STATUS_OK;
    }
    const double delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
        native.delta_t_model_id,
        native.ephemeris_family_id,
        reference_epoch_jd,
        nullptr,
        nullptr);
    if (!std::isfinite(delta_t)) return TAIYIN_ERROR_UNSUPPORTED;
    return ut1_to_tt_split_jd(reference_epoch_jd, delta_t, out_jd_tt)
        ? TAIYIN_STATUS_OK : TAIYIN_ERROR_UNSUPPORTED;
}

Status special_reference_plane_matrix(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    uint64_t sidereal_flags,
    const runtime::NativeCalcContext& native,
    SiderealReferencePlaneId plane_id,
    SplitJulianDate reference_epoch_jd,
    bool reference_epoch_is_ut1,
    Matrix3x3* out_matrix,
    double* out_ayanamsha_rad,
    int32_t* out_coordinate_frame_id
) noexcept {
    if (!out_matrix || !out_ayanamsha_rad || !out_coordinate_frame_id) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate anchor_jd_tt;
    if (!split_julian_date_from_double(JD_J2000, &anchor_jd_tt)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    Status status = TAIYIN_STATUS_OK;
    if (plane_id != kJ2000Ecliptic) {
        status = reference_epoch_tt(
            native, reference_epoch_jd, reference_epoch_is_ut1, &anchor_jd_tt);
        if (status != TAIYIN_STATUS_OK) return status;
    }

    Matrix3x3 anchor_ecliptic;
    Matrix3x3 ignored_dot;
    status = mean_ecliptic_matrix(native, anchor_jd_tt, false, &anchor_ecliptic, &ignored_dot);
    if (status != TAIYIN_STATUS_OK) return status;
    status = internal::calc_ayanamsha_tt_with_position_flags(
        native_context, ayanamsha_id, anchor_jd_tt,
        internal::ayanamsha_evaluation_flags(
            static_cast<uint32_t>(sidereal_flags)),
        sidereal_flags, out_ayanamsha_rad);
    if (status != TAIYIN_STATUS_OK) return status;

    if (plane_id == kFixedEclipticAtEpoch) {
        *out_matrix = anchor_ecliptic;
        *out_coordinate_frame_id = TAIYIN_SIDEREAL_FRAME_FIXED_MEAN_ECLIPTIC_AT_EPOCH;
        return TAIYIN_STATUS_OK;
    }
    if (plane_id == kJ2000Ecliptic) {
        *out_matrix = anchor_ecliptic;
        *out_coordinate_frame_id = TAIYIN_SIDEREAL_FRAME_J2000_ECLIPTIC;
        return TAIYIN_STATUS_OK;
    }
    if (plane_id != kSolarSystemInvariable) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    Matrix3x3 j2000_ecliptic;
    SplitJulianDate j2000;
    if (!split_julian_date_from_double(JD_J2000, &j2000)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    status = mean_ecliptic_matrix(native, j2000, false, &j2000_ecliptic, &ignored_dot);
    if (status != TAIYIN_STATUS_OK) return status;
    Matrix3x3 plane = matrix3x3_multiply(
        rotation_x_matrix(kSolarSystemPlaneInclinationRad),
        matrix3x3_multiply(rotation_z_matrix(kSolarSystemPlaneNodeJ2000Rad), j2000_ecliptic));
    const Vector3 anchor_equinox_icrf = matrix3x3_multiply_vector(
        matrix3x3_transpose(anchor_ecliptic), Vector3{1.0, 0.0, 0.0});
    const Vector3 anchor_in_plane = matrix3x3_multiply_vector(plane, anchor_equinox_icrf);
    if (!finite_vector(anchor_in_plane)
        || !(anchor_in_plane.x * anchor_in_plane.x + anchor_in_plane.y * anchor_in_plane.y > 0.0)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    plane = matrix3x3_multiply(
        rotation_z_matrix(std::atan2(anchor_in_plane.y, anchor_in_plane.x)), plane);
    *out_matrix = plane;
    *out_coordinate_frame_id = TAIYIN_SIDEREAL_FRAME_SOLAR_SYSTEM_INVARIABLE;
    return TAIYIN_STATUS_OK;
}

int32_t coordinate_frame_for_plane(SiderealReferencePlaneId plane) noexcept {
    switch (plane) {
    case kFixedEclipticAtEpoch:
        return TAIYIN_SIDEREAL_FRAME_FIXED_MEAN_ECLIPTIC_AT_EPOCH;
    case kSolarSystemInvariable:
        return TAIYIN_SIDEREAL_FRAME_SOLAR_SYSTEM_INVARIABLE;
    case kJ2000Ecliptic:
        return TAIYIN_SIDEREAL_FRAME_J2000_ECLIPTIC;
    case kMeanEclipticOfDate:
    default:
        return TAIYIN_SIDEREAL_FRAME_MEAN_ECLIPTIC_OF_DATE;
    }
}

bool eval_sidereal_output_frame(
    const SplitJulianDate& jd_tt,
    const void* data,
    double out_matrix[9]
) noexcept {
    const AstrologyContext* context =
        static_cast<const AstrologyContext*>(data);
    if (!context || !out_matrix || !split_julian_date_is_finite(jd_tt)) return false;

    ResolvedSiderealReferenceFlags resolved;
    if (resolve_sidereal_reference_flags(
            context->sidereal_flags,
            context->reference_epoch_jd,
            &resolved) != TAIYIN_STATUS_OK) {
        return false;
    }

    runtime::NativeCalcContext evaluation_context = context->native_context;
    evaluation_context.apparent_options.model_context =
        &evaluation_context.model_context;
    evaluation_context.apparent_options.output_frame_id =
        TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE;
    evaluation_context.apparent_options.custom_output_frame_evaluator = 0;
    evaluation_context.apparent_options.custom_output_frame_data = 0;

    Matrix3x3 plane;
    double ayanamsha = 0.0;
    if (resolved.plane == kMeanEclipticOfDate) {
        Matrix3x3 ignored_dot;
        if (mean_ecliptic_matrix(
                evaluation_context, jd_tt, false,
                &plane, &ignored_dot) != TAIYIN_STATUS_OK
            || internal::calc_ayanamsha_tt_with_position_flags(
                &evaluation_context,
                context->ayanamsha_id,
                jd_tt,
                internal::ayanamsha_evaluation_flags(
                    static_cast<uint32_t>(context->sidereal_flags)),
                context->sidereal_flags,
                &ayanamsha) != TAIYIN_STATUS_OK) {
            return false;
        }
    } else {
        int32_t ignored_frame = 0;
        if (special_reference_plane_matrix(
                &evaluation_context,
                context->ayanamsha_id,
                context->sidereal_flags,
                evaluation_context,
                resolved.plane,
                context->reference_epoch_jd,
                resolved.reference_epoch_is_ut1,
                &plane,
                &ayanamsha,
                &ignored_frame) != TAIYIN_STATUS_OK) {
            return false;
        }
    }

    matrix_to_array(
        // rotation_z_matrix is the passive coordinate-frame rotation; a
        // sidereal longitude lambda - ayanamsha therefore uses +ayanamsha.
        matrix3x3_multiply(rotation_z_matrix(ayanamsha), plane),
        out_matrix);
    return true;
}

void bind_astrology_native_context(AstrologyContext* context) noexcept {
    if (!context) return;
    context->native_context.apparent_options.model_context =
        &context->native_context.model_context;
    context->native_context.apparent_options.output_frame_id =
        TAIYIN_APPARENT_FRAME_CUSTOM;
    context->native_context.apparent_options.custom_output_frame_evaluator =
        &eval_sidereal_output_frame;
    context->native_context.apparent_options.custom_output_frame_data = context;
}

Status calc_ayanamsha_rate(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    SplitJulianDate jd_tt,
    uint32_t native_position_flags,
    uint64_t sidereal_flags,
    double* out_rate_rad_per_day
) noexcept {
    if (!out_rate_rad_per_day) return TAIYIN_ERROR_INVALID_ARGUMENT;
    double before = 0.0;
    double after = 0.0;
    const uint32_t evaluation_flags =
        internal::ayanamsha_evaluation_flags(native_position_flags);
    Status status = internal::calc_ayanamsha_tt_with_position_flags(
        native_context, ayanamsha_id,
        jd_tt - kSiderealRateStepDays,
        evaluation_flags,
        sidereal_flags,
        &before);
    if (status != TAIYIN_STATUS_OK) return status;
    status = internal::calc_ayanamsha_tt_with_position_flags(
        native_context, ayanamsha_id,
        jd_tt + kSiderealRateStepDays,
        evaluation_flags,
        sidereal_flags,
        &after);
    if (status != TAIYIN_STATUS_OK) return status;
    *out_rate_rad_per_day = normalize_signed_radians(after - before)
        / (2.0 * kSiderealRateStepDays);
    return std::isfinite(*out_rate_rad_per_day)
        ? TAIYIN_STATUS_OK : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

Status calc_longitude_nutation_rate(
    const runtime::NativeCalcContext* native_context,
    SplitJulianDate jd_tt,
    uint32_t native_position_flags,
    double* out_rate_rad_per_day
) noexcept {
    if (!out_rate_rad_per_day) return TAIYIN_ERROR_INVALID_ARGUMENT;
    double before = 0.0;
    double after = 0.0;
    Status status = internal::calc_longitude_nutation_tt(
        native_context,
        jd_tt - kSiderealRateStepDays,
        native_position_flags,
        &before);
    if (status != TAIYIN_STATUS_OK) return status;
    status = internal::calc_longitude_nutation_tt(
        native_context,
        jd_tt + kSiderealRateStepDays,
        native_position_flags,
        &after);
    if (status != TAIYIN_STATUS_OK) return status;
    *out_rate_rad_per_day = normalize_signed_radians(after - before)
        / (2.0 * kSiderealRateStepDays);
    return std::isfinite(*out_rate_rad_per_day)
        ? TAIYIN_STATUS_OK : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

}  // namespace

AstrologyContext::AstrologyContext() noexcept
    : native_context(),
      ayanamsha_id(TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY),
      coordinate_frame_id(TAIYIN_SIDEREAL_FRAME_MEAN_ECLIPTIC_OF_DATE),
      sidereal_flags(0u),
      reference_epoch_jd(0, NAN) {
    bind_astrology_native_context(this);
}

AstrologyContext::AstrologyContext(const AstrologyContext& other) noexcept
    : native_context(other.native_context),
      ayanamsha_id(other.ayanamsha_id),
      coordinate_frame_id(other.coordinate_frame_id),
      sidereal_flags(other.sidereal_flags),
      reference_epoch_jd(other.reference_epoch_jd) {
    bind_astrology_native_context(this);
}

AstrologyContext& AstrologyContext::operator=(
    const AstrologyContext& other
) noexcept {
    if (this == &other) return *this;
    native_context = other.native_context;
    ayanamsha_id = other.ayanamsha_id;
    coordinate_frame_id = other.coordinate_frame_id;
    sidereal_flags = other.sidereal_flags;
    reference_epoch_jd = other.reference_epoch_jd;
    bind_astrology_native_context(this);
    return *this;
}

Status configure_astrology_context(
    AstrologyContext* out,
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    uint64_t sidereal_flags,
    SplitJulianDate reference_epoch_jd
) noexcept {
    ResolvedSiderealReferenceFlags resolved;
    if (!out || !native_context) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    Status status = resolve_sidereal_reference_flags(
        sidereal_flags, reference_epoch_jd, &resolved);
    if (status != TAIYIN_STATUS_OK) return status;

    runtime::NativeCalcContext effective;
    status = internal::effective_native_context(
        native_context, ayanamsha_id, sidereal_flags, &effective);
    if (status != TAIYIN_STATUS_OK) return status;

    out->native_context = effective;
    out->ayanamsha_id = ayanamsha_id;
    out->coordinate_frame_id = coordinate_frame_for_plane(resolved.plane);
    out->sidereal_flags = internal::ayanamsha_context_flags(sidereal_flags);
    out->reference_epoch_jd = reference_epoch_jd;
    bind_astrology_native_context(out);
    return TAIYIN_STATUS_OK;
}

SiderealPosition::SiderealPosition() noexcept
    : coordinate_frame_id(TAIYIN_SIDEREAL_FRAME_MEAN_ECLIPTIC_OF_DATE),
      tropical_longitude_rad(NAN),
      sidereal_longitude_rad(NAN),
      latitude_rad(NAN),
      distance_au(NAN),
      tropical_longitude_rate_rad_per_day(NAN),
      sidereal_longitude_rate_rad_per_day(NAN) {}

SiderealCoordinates::SiderealCoordinates() noexcept
    : coordinate_frame_id(TAIYIN_SIDEREAL_FRAME_MEAN_ECLIPTIC_OF_DATE),
      position_flags(0u),
      values{ NAN, NAN, NAN, NAN, NAN, NAN } {}

namespace {

Status fill_sidereal_position(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    int body_id,
    SplitJulianDate jd,
    SplitJulianDate jd_tt,
    bool use_ut,
    uint64_t flags,
    const ResolvedSiderealReferenceFlags& resolved,
    SiderealPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic,
    SplitJulianDate reference_epoch_jd
) noexcept {
    const uint32_t native_position_flags = resolved.native_position_flags;
    SiderealCoordinates coordinates;
    const uint64_t coordinate_flags =
        flags | runtime::TAIYIN_NATIVE_POSITION_RADIANS;
    Status status = use_ut
        ? calc_sidereal_coordinates_ut(
            native_context, ayanamsha_id, body_id, jd, coordinate_flags,
            &coordinates, diagnostic, reference_epoch_jd)
        : calc_sidereal_coordinates_tt(
            native_context, ayanamsha_id, body_id, jd, coordinate_flags,
            &coordinates, diagnostic, reference_epoch_jd);
    if (status != TAIYIN_STATUS_OK) return status;

    double ayanamsha = 0.0;
    if (special_reference_plane(resolved.plane)) {
        runtime::NativeCalcContext native;
        status = internal::effective_native_context(
            native_context, ayanamsha_id, flags, &native);
        if (status != TAIYIN_STATUS_OK) return status;
        Matrix3x3 ignored_matrix;
        int32_t ignored_frame_id = 0;
        status = special_reference_plane_matrix(
            native_context,
            ayanamsha_id,
            flags,
            native,
            resolved.plane,
            reference_epoch_jd,
            resolved.reference_epoch_is_ut1,
            &ignored_matrix,
            &ayanamsha,
            &ignored_frame_id);
    } else {
        status = internal::calc_ayanamsha_tt_with_position_flags(
            native_context,
            ayanamsha_id,
            jd_tt,
            native_position_flags,
            flags,
            &ayanamsha);
    }
    if (status != TAIYIN_STATUS_OK) return status;

    double reported_ayanamsha = ayanamsha;
    if (!special_reference_plane(resolved.plane)) {
        double dpsi_rad = 0.0;
        status = internal::calc_longitude_nutation_tt(
            native_context,
            jd_tt,
            native_position_flags,
            &dpsi_rad);
        if (status != TAIYIN_STATUS_OK) return status;
        reported_ayanamsha = normalize_radians(ayanamsha + dpsi_rad);
    }

    *out = SiderealPosition();
    out->coordinate_frame_id = coordinates.coordinate_frame_id;
    out->tropical_longitude_rad =
        normalize_radians(coordinates.values[0] + reported_ayanamsha);
    out->sidereal_longitude_rad = coordinates.values[0];
    out->latitude_rad = coordinates.values[1];
    out->distance_au = coordinates.values[2];
    if ((native_position_flags & runtime::TAIYIN_NATIVE_POSITION_SPEED) != 0u) {
        out->sidereal_longitude_rate_rad_per_day = coordinates.values[3];
        double ayanamsha_rate = 0.0;
        if (!special_reference_plane(resolved.plane)) {
            status = calc_ayanamsha_rate(
                native_context,
                ayanamsha_id,
                jd_tt,
                native_position_flags,
                flags,
                &ayanamsha_rate);
            if (status != TAIYIN_STATUS_OK) return status;
            double dpsi_rate = 0.0;
            status = calc_longitude_nutation_rate(
                native_context,
                jd_tt,
                native_position_flags,
                &dpsi_rate);
            if (status != TAIYIN_STATUS_OK) return status;
            ayanamsha_rate += dpsi_rate;
        }
        out->tropical_longitude_rate_rad_per_day =
            coordinates.values[3] + ayanamsha_rate;
    }
    return TAIYIN_STATUS_OK;
}

}  // namespace

Status calc_sidereal_position_tt(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    int body_id,
    SplitJulianDate jd_tt,
    uint64_t flags,
    SiderealPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic,
    SplitJulianDate reference_epoch_jd
) noexcept {
    ResolvedSiderealReferenceFlags resolved;
    Status status = resolve_sidereal_reference_flags(
        flags, reference_epoch_jd, &resolved);
    if (status != TAIYIN_STATUS_OK || !native_context || !out
        || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const uint32_t native_position_flags = resolved.native_position_flags;
    if ((native_position_flags & (runtime::TAIYIN_NATIVE_POSITION_XYZ
            | runtime::TAIYIN_NATIVE_POSITION_EQUATORIAL)) != 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    return fill_sidereal_position(
        native_context, ayanamsha_id, body_id, jd_tt, jd_tt, false,
        flags, resolved, out, diagnostic, reference_epoch_jd);
}

Status calc_sidereal_position_ut(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    int body_id,
    SplitJulianDate jd_ut,
    uint64_t flags,
    SiderealPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic,
    SplitJulianDate reference_epoch_jd
) noexcept {
    ResolvedSiderealReferenceFlags resolved;
    Status status = resolve_sidereal_reference_flags(
        flags, reference_epoch_jd, &resolved);
    if (status != TAIYIN_STATUS_OK || !native_context || !out
        || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const uint32_t native_position_flags = resolved.native_position_flags;
    if ((native_position_flags & (runtime::TAIYIN_NATIVE_POSITION_XYZ
            | runtime::TAIYIN_NATIVE_POSITION_EQUATORIAL)) != 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
        native_context->delta_t_model_id,
        native_context->ephemeris_family_id,
        jd_ut,
        nullptr,
        nullptr);
    SplitJulianDate jd_tt;
    if (!ut1_to_tt_split_jd(jd_ut, delta_t, &jd_tt)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    return fill_sidereal_position(
        native_context, ayanamsha_id, body_id, jd_ut, jd_tt, true,
        flags, resolved, out, diagnostic, reference_epoch_jd);
}

Status calc_sidereal_coordinates_tt(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    int body_id,
    SplitJulianDate jd_tt,
    uint64_t flags,
    SiderealCoordinates* out,
    runtime::EphemerisEvalDiagnostic* diagnostic,
    SplitJulianDate reference_epoch_jd
) noexcept {
    ResolvedSiderealReferenceFlags resolved;
    Status status = resolve_sidereal_reference_flags(
        flags, reference_epoch_jd, &resolved);
    if (status != TAIYIN_STATUS_OK || !native_context || !out
        || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = SiderealCoordinates();
    AstrologyContext astrology;
    status = configure_astrology_context(
        &astrology,
        native_context,
        ayanamsha_id,
        flags,
        reference_epoch_jd);
    if (status != TAIYIN_STATUS_OK) return status;

    const runtime::NativeCalcContext* calculation_context =
        (resolved.native_position_flags
            & runtime::TAIYIN_NATIVE_POSITION_EQUATORIAL) != 0u
        ? native_context : &astrology.native_context;
    status = runtime::calc_position_tt(
        calculation_context,
        body_id,
        jd_tt,
        resolved.native_position_flags,
        out->values,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    out->position_flags = resolved.native_position_flags;
    if ((resolved.native_position_flags
            & runtime::TAIYIN_NATIVE_POSITION_EQUATORIAL) != 0u) {
        out->coordinate_frame_id = (resolved.native_position_flags
                & runtime::TAIYIN_NATIVE_POSITION_NONUT) != 0u
            ? TAIYIN_SIDEREAL_FRAME_MEAN_EQUATOR_OF_DATE
            : TAIYIN_SIDEREAL_FRAME_TRUE_EQUATOR_OF_DATE;
    } else {
        out->coordinate_frame_id = astrology.coordinate_frame_id;
    }
    return TAIYIN_STATUS_OK;
}

Status calc_sidereal_coordinates_ut(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    int body_id,
    SplitJulianDate jd_ut,
    uint64_t flags,
    SiderealCoordinates* out,
    runtime::EphemerisEvalDiagnostic* diagnostic,
    SplitJulianDate reference_epoch_jd
) noexcept {
    ResolvedSiderealReferenceFlags resolved;
    Status status = resolve_sidereal_reference_flags(
        flags, reference_epoch_jd, &resolved);
    if (status != TAIYIN_STATUS_OK || !native_context || !out
        || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = SiderealCoordinates();
    AstrologyContext astrology;
    status = configure_astrology_context(
        &astrology,
        native_context,
        ayanamsha_id,
        flags,
        reference_epoch_jd);
    if (status != TAIYIN_STATUS_OK) return status;

    const runtime::NativeCalcContext* calculation_context =
        (resolved.native_position_flags
            & runtime::TAIYIN_NATIVE_POSITION_EQUATORIAL) != 0u
        ? native_context : &astrology.native_context;
    status = runtime::calc_position_ut(
        calculation_context,
        body_id,
        jd_ut,
        resolved.native_position_flags,
        out->values,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    out->position_flags = resolved.native_position_flags;
    if ((resolved.native_position_flags
            & runtime::TAIYIN_NATIVE_POSITION_EQUATORIAL) != 0u) {
        out->coordinate_frame_id = (resolved.native_position_flags
                & runtime::TAIYIN_NATIVE_POSITION_NONUT) != 0u
            ? TAIYIN_SIDEREAL_FRAME_MEAN_EQUATOR_OF_DATE
            : TAIYIN_SIDEREAL_FRAME_TRUE_EQUATOR_OF_DATE;
    } else {
        out->coordinate_frame_id = astrology.coordinate_frame_id;
    }
    return TAIYIN_STATUS_OK;
}

}  // namespace astrology
}  // namespace taiyin
