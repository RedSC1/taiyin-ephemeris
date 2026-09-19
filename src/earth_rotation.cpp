#include "taiyin/earth_rotation.h"

#include "taiyin/angle.h"
#include "taiyin/coordinates.h"
#include "taiyin/dispatch.h"
#include "taiyin/physical_constants.h"

#include <cmath>

namespace taiyin {
namespace {

bool eval_model_nutation_angles(
    int precession_model_id,
    int nutation_model_id,
    const SplitJulianDate& jd_tt,
    NutationAngles* out
) noexcept {
    if (!out || !split_julian_date_is_finite(jd_tt)) {
        return false;
    }
    Matrix3x3 precession;
    double mean_obliquity = 0.0;
    if (!dispatch::eval_precession(precession_model_id, jd_tt, 0, &precession, &mean_obliquity)
        || !dispatch::eval_nutation(nutation_model_id, jd_tt, 0, out)) {
        return false;
    }
    out->mean_obliquity_rad = mean_obliquity;
    out->true_obliquity_rad = mean_obliquity + out->deps_rad;
    return true;
}

}  // namespace

double earth_rotation_angle_rad(const SplitJulianDate& jd_ut1) noexcept {
    const double days_since_j2000 =
        -days_between_split_jd_and_double(jd_ut1, JD_J2000);
    return normalize_radians(
        TAIYIN_TWO_PI * (0.7790572732640 + 1.00273781191135448 * days_since_j2000));
}

double gmst_minus_era_rad(const SplitJulianDate& jd_tt) noexcept {
    const double t = julian_centuries_from_j2000(jd_tt);
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double t4 = t3 * t;
    const double t5 = t4 * t;
    const double polynomial_arcsec = 0.014506
        + 4612.156534 * t
        + 1.3915817 * t2
        - 0.00000044 * t3
        - 0.000029956 * t4
        - 0.0000000368 * t5;
    return polynomial_arcsec * TAIYIN_ARCSEC_TO_RAD;
}

bool gmst_minus_era_model_rad(
    int precession_model_id,
    const SplitJulianDate& jd_tt,
    double* out
) noexcept {
    if (!out || !split_julian_date_is_finite(jd_tt)) {
        return false;
    }

    dispatch::PrecessionModelEntry selected;
    if (!dispatch::select_precession_model(precession_model_id, &selected)) {
        return false;
    }
    if (selected.model_id == dispatch::PRECESSION_IAU2006) {
        *out = gmst_minus_era_rad(jd_tt);
        return true;
    }

    Matrix3x3 reference_precession;
    Matrix3x3 selected_precession;
    if (!dispatch::eval_precession(
            dispatch::PRECESSION_IAU2006,
            jd_tt,
            nullptr,
            &reference_precession)
        || !dispatch::eval_precession(
            selected.model_id,
            jd_tt,
            nullptr,
            &selected_precession)) {
        return false;
    }

    // ERA is independent of the equinox.  The IAU 2006 polynomial locates
    // the conventional mean equinox relative to ERA; carry that same
    // physical Greenwich meridian through ICRF into the selected mean frame.
    // This keeps a custom rotation of the equinox from rotating the Earth or
    // changing a topocentric observer's physical ICRF position.
    const double reference_angle = gmst_minus_era_rad(jd_tt);
    const Vector3 reference_meridian = {
        std::cos(reference_angle),
        std::sin(reference_angle),
        0.0,
    };
    const Vector3 meridian_icrf = matrix3x3_multiply_vector(
        matrix3x3_transpose(reference_precession),
        reference_meridian);
    const Vector3 selected_meridian = matrix3x3_multiply_vector(
        selected_precession,
        meridian_icrf);
    if (!std::isfinite(selected_meridian.x)
        || !std::isfinite(selected_meridian.y)
        || !(std::hypot(selected_meridian.x, selected_meridian.y) > 0.0)) {
        return false;
    }
    *out = std::atan2(selected_meridian.y, selected_meridian.x);
    return true;
}

double gmst_minus_era_rate_rad_per_day(const SplitJulianDate& jd_tt) noexcept {
    const double t = julian_centuries_from_j2000(jd_tt);
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double t4 = t3 * t;
    const double polynomial_rate_arcsec_per_century = 4612.156534
        + 2.0 * 1.3915817 * t
        + 3.0 * -0.00000044 * t2
        + 4.0 * -0.000029956 * t3
        + 5.0 * -0.0000000368 * t4;
    return polynomial_rate_arcsec_per_century * TAIYIN_ARCSEC_TO_RAD / DAYS_PER_JULIAN_CENTURY;
}

double gmst_minus_era_acceleration_rad_per_day2(const SplitJulianDate& jd_tt) noexcept {
    const double t = julian_centuries_from_j2000(jd_tt);
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double polynomial_acceleration_arcsec_per_century2 = 2.0 * 1.3915817
        + 6.0 * -0.00000044 * t
        + 12.0 * -0.000029956 * t2
        + 20.0 * -0.0000000368 * t3;
    return polynomial_acceleration_arcsec_per_century2 * TAIYIN_ARCSEC_TO_RAD / (DAYS_PER_JULIAN_CENTURY * DAYS_PER_JULIAN_CENTURY);
}

double gmst_rad(const SplitJulianDate& jd_ut1, const SplitJulianDate& jd_tt) noexcept {
    return normalize_radians(earth_rotation_angle_rad(jd_ut1) + gmst_minus_era_rad(jd_tt));
}

bool gmst_model_rad(
    int precession_model_id,
    const SplitJulianDate& jd_ut1,
    const SplitJulianDate& jd_tt,
    double* out
) noexcept {
    if (!out || !split_julian_date_is_finite(jd_ut1)
        || !split_julian_date_is_finite(jd_tt)) {
        return false;
    }
    double offset = 0.0;
    if (!gmst_minus_era_model_rad(precession_model_id, jd_tt, &offset)) {
        return false;
    }
    *out = normalize_radians(earth_rotation_angle_rad(jd_ut1) + offset);
    return true;
}

double gmst_rate_rad_per_day(const SplitJulianDate& jd_tt, double dut1_rate_seconds_per_day, double lod_seconds) noexcept {
    const double ut1_rate_days_per_day = 1.0 + dut1_rate_seconds_per_day / SECONDS_PER_DAY - lod_seconds / SECONDS_PER_DAY;
    return TAIYIN_EARTH_ROTATION_RATE_RAD_PER_DAY * ut1_rate_days_per_day + gmst_minus_era_rate_rad_per_day(jd_tt);
}

double gmst_acceleration_rad_per_day2(const SplitJulianDate& jd_tt, double lod_rate_seconds_per_day) noexcept {
    return -TAIYIN_EARTH_ROTATION_RATE_RAD_PER_DAY * lod_rate_seconds_per_day / SECONDS_PER_DAY
        + gmst_minus_era_acceleration_rad_per_day2(jd_tt);
}

bool equation_of_equinoxes_model_rad(
    int precession_model_id,
    int nutation_model_id,
    const SplitJulianDate& jd_tt,
    double* out
) noexcept {
    if (!out) {
        return false;
    }
    NutationAngles nutation;
    if (!eval_model_nutation_angles(precession_model_id, nutation_model_id, jd_tt, &nutation)) {
        return false;
    }
    *out = nutation.dpsi_rad * std::cos(nutation.true_obliquity_rad);
    return true;
}

double equation_of_equinoxes_iau2000b_rad(const SplitJulianDate& jd_tt) noexcept {
    NutationAngles nutation;
    if (!iau2000b_nutation(jd_tt, &nutation)) {
        return 0.0;
    }
    return nutation.dpsi_rad * std::cos(nutation.true_obliquity_rad);
}

double equation_of_equinoxes_iau2000a_rad(const SplitJulianDate& jd_tt) noexcept {
    NutationAngles nutation;
    if (!iau2000a_nutation(jd_tt, &nutation)) {
        return 0.0;
    }
    return nutation.dpsi_rad * std::cos(nutation.true_obliquity_rad);
}

bool equation_of_equinoxes_rate_model_rad_per_day(
    int precession_model_id,
    int nutation_model_id,
    const SplitJulianDate& jd_tt,
    double step_days,
    double* out
) noexcept {
    if (!out || step_days <= 0.0) {
        return false;
    }
    double previous = 0.0;
    double next = 0.0;
    SplitJulianDate previous_jd;
    SplitJulianDate next_jd;
    if (!add_days_to_split_jd(jd_tt, -step_days, &previous_jd)
        || !add_days_to_split_jd(jd_tt, step_days, &next_jd)
        || !equation_of_equinoxes_model_rad(precession_model_id, nutation_model_id, previous_jd, &previous)
        || !equation_of_equinoxes_model_rad(precession_model_id, nutation_model_id, next_jd, &next)) {
        return false;
    }
    *out = (next - previous) / (2.0 * step_days);
    return true;
}

double equation_of_equinoxes_rate_iau2000b_rad_per_day(const SplitJulianDate& jd_tt, double step_days) noexcept {
    if (step_days <= 0.0) {
        return 0.0;
    }
    SplitJulianDate previous_jd;
    SplitJulianDate next_jd;
    if (!add_days_to_split_jd(jd_tt, -step_days, &previous_jd)
        || !add_days_to_split_jd(jd_tt, step_days, &next_jd)) {
        return 0.0;
    }
    return (equation_of_equinoxes_iau2000b_rad(next_jd)
        - equation_of_equinoxes_iau2000b_rad(previous_jd))
        / (2.0 * step_days);
}

double equation_of_equinoxes_acceleration_iau2000b_rad_per_day2(const SplitJulianDate& jd_tt, double step_days) noexcept {
    if (step_days <= 0.0) {
        return 0.0;
    }
    SplitJulianDate previous_jd;
    SplitJulianDate next_jd;
    if (!add_days_to_split_jd(jd_tt, -step_days, &previous_jd)
        || !add_days_to_split_jd(jd_tt, step_days, &next_jd)) {
        return 0.0;
    }
    return (equation_of_equinoxes_iau2000b_rad(next_jd)
        - 2.0 * equation_of_equinoxes_iau2000b_rad(jd_tt)
        + equation_of_equinoxes_iau2000b_rad(previous_jd)) / (step_days * step_days);
}

bool gast_model_rad(
    int precession_model_id,
    int nutation_model_id,
    const SplitJulianDate& jd_ut1,
    const SplitJulianDate& jd_tt,
    double* out
) noexcept {
    if (!out) {
        return false;
    }
    double mean_sidereal = 0.0;
    double equation = 0.0;
    if (!gmst_model_rad(
            precession_model_id, jd_ut1, jd_tt, &mean_sidereal)
        || !equation_of_equinoxes_model_rad(
            precession_model_id, nutation_model_id, jd_tt, &equation)) {
        return false;
    }
    *out = normalize_radians(mean_sidereal + equation);
    return true;
}

double gast_iau2000b_rad(const SplitJulianDate& jd_ut1, const SplitJulianDate& jd_tt) noexcept {
    return normalize_radians(gmst_rad(jd_ut1, jd_tt) + equation_of_equinoxes_iau2000b_rad(jd_tt));
}

double gast_iau2000a_rad(const SplitJulianDate& jd_ut1, const SplitJulianDate& jd_tt) noexcept {
    return normalize_radians(gmst_rad(jd_ut1, jd_tt) + equation_of_equinoxes_iau2000a_rad(jd_tt));
}

bool gast_rate_model_rad_per_day(
    int precession_model_id,
    int nutation_model_id,
    const SplitJulianDate& jd_tt,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double equation_step_days,
    double* out
) noexcept {
    if (!out) {
        return false;
    }
    if (!(equation_step_days > 0.0)) {
        return false;
    }
    SplitJulianDate previous_jd;
    SplitJulianDate next_jd;
    double previous_offset = 0.0;
    double next_offset = 0.0;
    double previous_equation = 0.0;
    double next_equation = 0.0;
    if (!add_days_to_split_jd(jd_tt, -equation_step_days, &previous_jd)
        || !add_days_to_split_jd(jd_tt, equation_step_days, &next_jd)
        || !gmst_minus_era_model_rad(
            precession_model_id, previous_jd, &previous_offset)
        || !gmst_minus_era_model_rad(
            precession_model_id, next_jd, &next_offset)
        || !equation_of_equinoxes_model_rad(
            precession_model_id, nutation_model_id,
            previous_jd, &previous_equation)
        || !equation_of_equinoxes_model_rad(
            precession_model_id, nutation_model_id,
            next_jd, &next_equation)) {
        return false;
    }
    const double frame_rate = normalize_signed_radians(
        (next_offset + next_equation)
        - (previous_offset + previous_equation))
        / (2.0 * equation_step_days);
    const double ut1_rate_days_per_day = 1.0
        + dut1_rate_seconds_per_day / SECONDS_PER_DAY
        - lod_seconds / SECONDS_PER_DAY;
    *out = TAIYIN_EARTH_ROTATION_RATE_RAD_PER_DAY
        * ut1_rate_days_per_day + frame_rate;
    return true;
}

double gast_rate_iau2000b_rad_per_day(
    const SplitJulianDate& jd_tt,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double equation_step_days
) noexcept {
    return gmst_rate_rad_per_day(jd_tt, dut1_rate_seconds_per_day, lod_seconds)
        + equation_of_equinoxes_rate_iau2000b_rad_per_day(jd_tt, equation_step_days);
}

double gast_acceleration_iau2000b_rad_per_day2(
    const SplitJulianDate& jd_tt,
    double lod_rate_seconds_per_day,
    double equation_step_days
) noexcept {
    return gmst_acceleration_rad_per_day2(jd_tt, lod_rate_seconds_per_day)
        + equation_of_equinoxes_acceleration_iau2000b_rad_per_day2(jd_tt, equation_step_days);
}

}  // namespace taiyin
