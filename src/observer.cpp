#include "taiyin/observer.h"

#include "taiyin/angle.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/physical_constants.h"

#include <cmath>

namespace taiyin {
namespace {

const double WGS84_A_M = 6378137.0;
const double WGS84_F = 1.0 / 298.257223563;
const double WGS84_E2 = WGS84_F * (2.0 - WGS84_F);
const double ARCMIN_TO_RAD = TAIYIN_DEG_TO_RAD / 60.0;
const double REFRACTION_LOW_ALTITUDE_CUTOFF_RAD = -TAIYIN_DEG_TO_RAD;
const double SOFA_CELMIN = 1e-6;
const double SOFA_SELMIN = 0.05;
const double OBSERVER_DERIVATIVE_STEP_DAYS = 1e-3;

struct ObserverDerivatives {
    Vector3 position_au;
    Vector3 velocity_au_per_day;
    Vector3 acceleration_au_per_day2;
};

double clamp_value(double value, double lower, double upper) noexcept {
    return std::fmin(std::fmax(value, lower), upper);
}

Vector3 intermediate_observer_au(
    const Vector3& itrf_m,
    double xp_rad,
    double yp_rad,
    double sp_rad
) noexcept {
    return vector3_scale(
        matrix3x3_multiply_vector(polar_motion_matrix(xp_rad, yp_rad, sp_rad), itrf_m),
        1.0 / TAIYIN_AU_M);
}

ObserverDerivatives rotating_observer_derivatives_au(
    const Vector3& itrf_m,
    double sidereal_angle_rad,
    double sidereal_rate_rad_per_day,
    double sidereal_acceleration_rad_per_day2,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double derivative_step_days
) noexcept {
    const double h = derivative_step_days;
    const Vector3 intermediate_au = intermediate_observer_au(itrf_m, xp_rad, yp_rad, sp_rad);
    const Vector3 previous_intermediate_au = intermediate_observer_au(
        itrf_m,
        xp_rad - xp_rate_rad_per_day * h,
        yp_rad - yp_rate_rad_per_day * h,
        sp_rad - sp_rate_rad_per_day * h);
    const Vector3 next_intermediate_au = intermediate_observer_au(
        itrf_m,
        xp_rad + xp_rate_rad_per_day * h,
        yp_rad + yp_rate_rad_per_day * h,
        sp_rad + sp_rate_rad_per_day * h);
    const Vector3 intermediate_velocity_au_per_day = vector3_scale(
        vector3_subtract(next_intermediate_au, previous_intermediate_au),
        1.0 / (2.0 * h));
    const Vector3 intermediate_acceleration_au_per_day2 = vector3_scale(
        vector3_add(
            vector3_subtract(next_intermediate_au, vector3_scale(intermediate_au, 2.0)),
            previous_intermediate_au),
        1.0 / (h * h));

    ObserverDerivatives state;
    state.position_au = rotate_z(intermediate_au, sidereal_angle_rad);
    const Vector3 rotated_intermediate_velocity_au_per_day = rotate_z(intermediate_velocity_au_per_day, sidereal_angle_rad);
    const Vector3 rotated_intermediate_acceleration_au_per_day2 = rotate_z(intermediate_acceleration_au_per_day2, sidereal_angle_rad);

    const Vector3 d_position_dtheta = { -state.position_au.y, state.position_au.x, 0.0 };
    const Vector3 d2_position_dtheta2 = { -state.position_au.x, -state.position_au.y, 0.0 };
    const Vector3 d_velocity_dtheta = {
        -rotated_intermediate_velocity_au_per_day.y,
        rotated_intermediate_velocity_au_per_day.x,
        0.0,
    };
    state.velocity_au_per_day = vector3_add(
        vector3_scale(d_position_dtheta, sidereal_rate_rad_per_day),
        rotated_intermediate_velocity_au_per_day);
    state.acceleration_au_per_day2 = vector3_add(
        vector3_add(
            vector3_scale(d2_position_dtheta2, sidereal_rate_rad_per_day * sidereal_rate_rad_per_day),
            vector3_scale(d_position_dtheta, sidereal_acceleration_rad_per_day2)),
        vector3_add(
            vector3_scale(d_velocity_dtheta, 2.0 * sidereal_rate_rad_per_day),
            rotated_intermediate_acceleration_au_per_day2));
    return state;
}

double earth_rotation_ut1_rate_days_per_day(double dut1_rate_seconds_per_day, double lod_seconds) noexcept {
    return 1.0 + dut1_rate_seconds_per_day / SECONDS_PER_DAY - lod_seconds / SECONDS_PER_DAY;
}

double era_rate_rad_per_day(double dut1_rate_seconds_per_day, double lod_seconds) noexcept {
    return TAIYIN_EARTH_ROTATION_RATE_RAD_PER_DAY * earth_rotation_ut1_rate_days_per_day(dut1_rate_seconds_per_day, lod_seconds);
}

double era_acceleration_rad_per_day2(double lod_rate_seconds_per_day) noexcept {
    return -TAIYIN_EARTH_ROTATION_RATE_RAD_PER_DAY * lod_rate_seconds_per_day / SECONDS_PER_DAY;
}

ObserverDerivatives simple_observer_derivatives_au(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt
) noexcept {
    ObserverDerivatives state;
    state.position_au = observer_geocentric_simple_position_au(longitude_rad, latitude_rad, height_m, jd_ut1, jd_tt);
    const double angular_rate = gmst_rate_rad_per_day(jd_tt, 0.0, 0.0);
    const double angular_acceleration = gmst_acceleration_rad_per_day2(jd_tt, 0.0);
    const Vector3 d_position_dtheta = { -state.position_au.y, state.position_au.x, 0.0 };
    const Vector3 d2_position_dtheta2 = { -state.position_au.x, -state.position_au.y, 0.0 };
    state.velocity_au_per_day = vector3_scale(d_position_dtheta, angular_rate);
    state.acceleration_au_per_day2 = vector3_add(
        vector3_scale(d2_position_dtheta2, angular_rate * angular_rate),
        vector3_scale(d_position_dtheta, angular_acceleration));
    return state;
}

ObserverDerivatives true_equator_observer_derivatives_au(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double lod_rate_seconds_per_day,
    double derivative_step_days
) noexcept {
    return rotating_observer_derivatives_au(
        geodetic_to_ecef_m(longitude_rad, latitude_rad, height_m),
        gast_rad(jd_ut1, jd_tt),
        gast_rate_rad_per_day(jd_tt, dut1_rate_seconds_per_day, lod_seconds, OBSERVER_DERIVATIVE_STEP_DAYS),
        gast_acceleration_rad_per_day2(jd_tt, lod_rate_seconds_per_day, OBSERVER_DERIVATIVE_STEP_DAYS),
        xp_rad,
        yp_rad,
        sp_rad,
        xp_rate_rad_per_day,
        yp_rate_rad_per_day,
        sp_rate_rad_per_day,
        derivative_step_days);
}

ObserverDerivatives cirs_observer_derivatives_au(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double lod_rate_seconds_per_day,
    double derivative_step_days
) noexcept {
    return rotating_observer_derivatives_au(
        geodetic_to_ecef_m(longitude_rad, latitude_rad, height_m),
        earth_rotation_angle_rad(jd_ut1),
        era_rate_rad_per_day(dut1_rate_seconds_per_day, lod_seconds),
        era_acceleration_rad_per_day2(lod_rate_seconds_per_day),
        xp_rad,
        yp_rad,
        sp_rad,
        xp_rate_rad_per_day,
        yp_rate_rad_per_day,
        sp_rate_rad_per_day,
        derivative_step_days);
}

double bennett_refraction_derivative_rad(
    double altitude_rad,
    double pressure_mbar,
    double temperature_celsius
) noexcept {
    const double temperature_kelvin = 273.0 + temperature_celsius;
    if (pressure_mbar <= 0.0 || temperature_kelvin <= 0.0 || altitude_rad < REFRACTION_LOW_ALTITUDE_CUTOFF_RAD) {
        return 0.0;
    }

    const double altitude_deg = altitude_rad * TAIYIN_RAD_TO_DEG;
    const double argument_deg = altitude_deg + 10.3 / (altitude_deg + 5.11);
    const double argument_rad = argument_deg * TAIYIN_DEG_TO_RAD;
    const double tangent = std::tan(argument_rad);
    if (tangent <= 0.0) {
        return 0.0;
    }

    const double scale = (pressure_mbar / 1010.0) * (283.0 / temperature_kelvin);
    const double dargument_dalt = 1.0 - 10.3 / ((altitude_deg + 5.11) * (altitude_deg + 5.11));
    const double sin_argument = std::sin(argument_rad);
    return -1.02 * scale * ARCMIN_TO_RAD * dargument_dalt / (sin_argument * sin_argument);
}

const double AS_GLADSTONE = 0.00029241;
const double AS_R_EARTH = 6378390.0;
const double AS_G_ACCEL = 9.80655;
const double AS_R_GAS = 287.053;
const double AS_N_POLY = 5.0;
const double AS_H_TROPOPAUSE = 11019.0;

struct AuerStandishAtm {
    double r_w;
    double rho_w;
    double beta_w;
    double r_B;
    double rho_B;
    double gamma_B;
};

void auer_standish_k_and_dk(
    double r,
    const AuerStandishAtm& atm,
    double* k_out,
    double* dkdr_out
) noexcept {
    double rho = 0.0;
    double drho_dr = 0.0;
    if (r <= atm.r_B) {
        const double arg = 1.0 + atm.beta_w * (AS_R_EARTH / r - AS_R_EARTH / atm.r_w);
        if (arg <= 0.0) {
            *k_out = 1.0;
            *dkdr_out = 0.0;
            return;
        }
        rho = atm.rho_w * std::pow(arg, AS_N_POLY);
        drho_dr = atm.rho_w * AS_N_POLY * std::pow(arg, AS_N_POLY - 1.0) * (-atm.beta_w * AS_R_EARTH / (r * r));
    } else {
        const double exp_arg = atm.gamma_B * (AS_R_EARTH / r - AS_R_EARTH / atm.r_B);
        if (exp_arg < -50.0) {
            *k_out = 1.0;
            *dkdr_out = 0.0;
            return;
        }
        rho = atm.rho_B * std::exp(exp_arg);
        drho_dr = rho * (-atm.gamma_B * AS_R_EARTH / (r * r));
    }
    *k_out = 1.0 + AS_GLADSTONE * rho;
    *dkdr_out = AS_GLADSTONE * drho_dr;
}

double auer_standish_integrand(double t, double s_constant, const AuerStandishAtm& atm) noexcept {
    const double sin_t = std::sin(t);
    if (sin_t < 1e-15) {
        return 0.0;
    }

    double r = s_constant / sin_t;
    for (int iter = 0; iter < 10; ++iter) {
        double k = 0.0;
        double dkdr = 0.0;
        auer_standish_k_and_dk(r, atm, &k, &dkdr);
        const double f = k * r * sin_t - s_constant;
        const double fp = (dkdr * r + k) * sin_t;
        if (std::fabs(fp) < 1e-30) {
            break;
        }
        const double dr = -f / fp;
        r += dr;
        if (std::fabs(dr) < 1e-12 * r) {
            break;
        }
    }

    double k = 0.0;
    double dkdr = 0.0;
    auer_standish_k_and_dk(r, atm, &k, &dkdr);
    const double dlnk_dlnr = (dkdr * r) / k;
    const double denom = 1.0 + dlnk_dlnr;
    if (std::fabs(denom) < 1e-15) {
        return 0.0;
    }
    return -(dlnk_dlnr / denom);
}

double auer_standish_compute_refraction(
    double z_app_rad,
    double pressure_mbar,
    double temperature_celsius
) noexcept {
    const double t_w = temperature_celsius + 273.15;
    const double p_mm_hg = pressure_mbar * 760.0 / 1013.25;
    const double rho_w = (p_mm_hg / 760.0) * (273.15 / t_w);
    const double k_w = 1.0 + AS_GLADSTONE * rho_w;

    AuerStandishAtm atm;
    atm.r_w = AS_R_EARTH;
    atm.r_B = AS_R_EARTH + AS_H_TROPOPAUSE;
    atm.rho_w = rho_w;
    atm.beta_w = (AS_G_ACCEL * AS_R_EARTH) / (AS_R_GAS * t_w * (1.0 + AS_N_POLY));
    const double arg_b = 1.0 + atm.beta_w * (AS_R_EARTH / atm.r_B - AS_R_EARTH / atm.r_w);
    const double t_b = t_w * arg_b;
    atm.rho_B = rho_w * std::pow(std::fmax(0.0, arg_b), AS_N_POLY);
    atm.gamma_B = (AS_G_ACCEL * AS_R_EARTH) / (AS_R_GAS * t_b);

    const double s_constant = k_w * atm.r_w * std::sin(z_app_rad);
    const double s_max = std::sqrt(z_app_rad);
    const int n = 400;
    const double ds = s_max / static_cast<double>(n);
    if (ds <= 0.0) {
        return 0.0;
    }

    double sum = 0.0;
    for (int i = 0; i <= n; ++i) {
        const double s = static_cast<double>(i) * ds;
        const double t = z_app_rad - s * s;
        const double f_transformed = auer_standish_integrand(t, s_constant, atm) * 2.0 * s;
        const double weight = (i == 0 || i == n) ? 1.0 : (i % 2 == 1 ? 4.0 : 2.0);
        sum += weight * f_transformed;
    }
    return sum * ds / 3.0;
}

}  // namespace

Vector3 local_east_itrf(double longitude_rad, double) noexcept {
    const double sin_lon = std::sin(longitude_rad);
    const double cos_lon = std::cos(longitude_rad);
    return { -sin_lon, cos_lon, 0.0 };
}

Vector3 local_north_itrf(double longitude_rad, double latitude_rad) noexcept {
    const double sin_lon = std::sin(longitude_rad);
    const double cos_lon = std::cos(longitude_rad);
    const double sin_lat = std::sin(latitude_rad);
    const double cos_lat = std::cos(latitude_rad);
    return { -sin_lat * cos_lon, -sin_lat * sin_lon, cos_lat };
}

Vector3 local_up_itrf(double longitude_rad, double latitude_rad) noexcept {
    const double sin_lon = std::sin(longitude_rad);
    const double cos_lon = std::cos(longitude_rad);
    const double sin_lat = std::sin(latitude_rad);
    const double cos_lat = std::cos(latitude_rad);
    return { cos_lat * cos_lon, cos_lat * sin_lon, sin_lat };
}

Vector3 geodetic_to_ecef_m(double longitude_rad, double latitude_rad, double height_m) noexcept {
    const double sin_lat = std::sin(latitude_rad);
    const double cos_lat = std::cos(latitude_rad);
    const double sin_lon = std::sin(longitude_rad);
    const double cos_lon = std::cos(longitude_rad);
    const double n = WGS84_A_M / std::sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat);
    const Vector3 result = {
        (n + height_m) * cos_lat * cos_lon,
        (n + height_m) * cos_lat * sin_lon,
        (n * (1.0 - WGS84_E2) + height_m) * sin_lat,
    };
    return result;
}

Matrix3x3 polar_motion_matrix(double xp_rad, double yp_rad, double sp_rad) noexcept {
    return matrix3x3_multiply(
        rotation_z_matrix(-sp_rad),
        matrix3x3_multiply(rotation_y_matrix(xp_rad), rotation_x_matrix(yp_rad)));
}

Matrix3x3 earth_rotation_matrix(double sidereal_angle_rad) noexcept {
    return rotation_z_matrix(-sidereal_angle_rad);
}

Vector3 terrestrial_to_intermediate_m(
    const Vector3& itrf_m,
    double xp_rad,
    double yp_rad,
    double sp_rad
) noexcept {
    return matrix3x3_multiply_vector(polar_motion_matrix(xp_rad, yp_rad, sp_rad), itrf_m);
}

Matrix3x3 terrestrial_to_true_equator_of_date_matrix(
    double jd_ut1,
    double jd_tt,
    double xp_rad,
    double yp_rad,
    double sp_rad
) noexcept {
    return matrix3x3_multiply(earth_rotation_matrix(gast_rad(jd_ut1, jd_tt)), polar_motion_matrix(xp_rad, yp_rad, sp_rad));
}

Matrix3x3 terrestrial_to_cirs_matrix(
    double jd_ut1,
    double xp_rad,
    double yp_rad,
    double sp_rad
) noexcept {
    return matrix3x3_multiply(earth_rotation_matrix(earth_rotation_angle_rad(jd_ut1)), polar_motion_matrix(xp_rad, yp_rad, sp_rad));
}

Vector3 terrestrial_to_true_equator_of_date_au(
    const Vector3& itrf_m,
    double jd_ut1,
    double jd_tt,
    double xp_rad,
    double yp_rad,
    double sp_rad
) noexcept {
    return vector3_scale(
        matrix3x3_multiply_vector(terrestrial_to_true_equator_of_date_matrix(jd_ut1, jd_tt, xp_rad, yp_rad, sp_rad), itrf_m),
        1.0 / TAIYIN_AU_M);
}

Vector3 terrestrial_to_cirs_au(
    const Vector3& itrf_m,
    double jd_ut1,
    double xp_rad,
    double yp_rad,
    double sp_rad
) noexcept {
    return vector3_scale(
        matrix3x3_multiply_vector(terrestrial_to_cirs_matrix(jd_ut1, xp_rad, yp_rad, sp_rad), itrf_m),
        1.0 / TAIYIN_AU_M);
}

Vector3 observer_geocentric_simple_position_au(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt
) noexcept {
    const Vector3 ecef_au = vector3_scale(geodetic_to_ecef_m(longitude_rad, latitude_rad, height_m), 1.0 / TAIYIN_AU_M);
    return rotate_z(ecef_au, gmst_rad(jd_ut1, jd_tt));
}

bool observer_geocentric_simple_velocity_au_per_day(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt,
    Vector3* out_velocity_au_per_day
) noexcept {
    if (!out_velocity_au_per_day) {
        return false;
    }
    *out_velocity_au_per_day = simple_observer_derivatives_au(longitude_rad, latitude_rad, height_m, jd_ut1, jd_tt).velocity_au_per_day;
    return true;
}

bool observer_geocentric_simple_acceleration_au_per_day2(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt,
    Vector3* out_acceleration_au_per_day2
) noexcept {
    if (!out_acceleration_au_per_day2) {
        return false;
    }
    *out_acceleration_au_per_day2 = simple_observer_derivatives_au(longitude_rad, latitude_rad, height_m, jd_ut1, jd_tt).acceleration_au_per_day2;
    return true;
}

bool observer_geocentric_true_equator_of_date_position_au(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    Vector3* out_position_au
) noexcept {
    if (!out_position_au) {
        return false;
    }
    *out_position_au = terrestrial_to_true_equator_of_date_au(
        geodetic_to_ecef_m(longitude_rad, latitude_rad, height_m),
        jd_ut1,
        jd_tt,
        xp_rad,
        yp_rad,
        sp_rad);
    return true;
}

bool observer_geocentric_true_equator_of_date_velocity_au_per_day(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double derivative_step_days,
    Vector3* out_velocity_au_per_day
) noexcept {
    if (!out_velocity_au_per_day) {
        return false;
    }
    *out_velocity_au_per_day = true_equator_observer_derivatives_au(
        longitude_rad, latitude_rad, height_m, jd_ut1, jd_tt,
        xp_rad, yp_rad, sp_rad, xp_rate_rad_per_day, yp_rate_rad_per_day, sp_rate_rad_per_day,
        dut1_rate_seconds_per_day, lod_seconds, 0.0, derivative_step_days).velocity_au_per_day;
    return true;
}

bool observer_geocentric_true_equator_of_date_velocity_au_per_day(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    Vector3* out_velocity_au_per_day
) noexcept {
    return observer_geocentric_true_equator_of_date_velocity_au_per_day(
        longitude_rad, latitude_rad, height_m, jd_ut1, jd_tt,
        xp_rad, yp_rad, sp_rad, xp_rate_rad_per_day, yp_rate_rad_per_day, sp_rate_rad_per_day,
        dut1_rate_seconds_per_day, lod_seconds, OBSERVER_DERIVATIVE_STEP_DAYS, out_velocity_au_per_day);
}

bool observer_geocentric_true_equator_of_date_acceleration_au_per_day2(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double lod_rate_seconds_per_day,
    double derivative_step_days,
    Vector3* out_acceleration_au_per_day2
) noexcept {
    if (!out_acceleration_au_per_day2) {
        return false;
    }
    *out_acceleration_au_per_day2 = true_equator_observer_derivatives_au(
        longitude_rad, latitude_rad, height_m, jd_ut1, jd_tt,
        xp_rad, yp_rad, sp_rad, xp_rate_rad_per_day, yp_rate_rad_per_day, sp_rate_rad_per_day,
        dut1_rate_seconds_per_day, lod_seconds, lod_rate_seconds_per_day, derivative_step_days).acceleration_au_per_day2;
    return true;
}

bool observer_geocentric_true_equator_of_date_acceleration_au_per_day2(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double lod_rate_seconds_per_day,
    Vector3* out_acceleration_au_per_day2
) noexcept {
    return observer_geocentric_true_equator_of_date_acceleration_au_per_day2(
        longitude_rad, latitude_rad, height_m, jd_ut1, jd_tt,
        xp_rad, yp_rad, sp_rad, xp_rate_rad_per_day, yp_rate_rad_per_day, sp_rate_rad_per_day,
        dut1_rate_seconds_per_day, lod_seconds, lod_rate_seconds_per_day,
        OBSERVER_DERIVATIVE_STEP_DAYS, out_acceleration_au_per_day2);
}

bool observer_geocentric_cirs_position_au(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    Vector3* out_position_au
) noexcept {
    if (!out_position_au) {
        return false;
    }
    *out_position_au = terrestrial_to_cirs_au(
        geodetic_to_ecef_m(longitude_rad, latitude_rad, height_m),
        jd_ut1,
        xp_rad,
        yp_rad,
        sp_rad);
    return true;
}

bool observer_geocentric_cirs_velocity_au_per_day(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double derivative_step_days,
    Vector3* out_velocity_au_per_day
) noexcept {
    if (!out_velocity_au_per_day) {
        return false;
    }
    *out_velocity_au_per_day = cirs_observer_derivatives_au(
        longitude_rad, latitude_rad, height_m, jd_ut1,
        xp_rad, yp_rad, sp_rad, xp_rate_rad_per_day, yp_rate_rad_per_day, sp_rate_rad_per_day,
        dut1_rate_seconds_per_day, lod_seconds, 0.0, derivative_step_days).velocity_au_per_day;
    return true;
}

bool observer_geocentric_cirs_velocity_au_per_day(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    Vector3* out_velocity_au_per_day
) noexcept {
    return observer_geocentric_cirs_velocity_au_per_day(
        longitude_rad, latitude_rad, height_m, jd_ut1,
        xp_rad, yp_rad, sp_rad, xp_rate_rad_per_day, yp_rate_rad_per_day, sp_rate_rad_per_day,
        dut1_rate_seconds_per_day, lod_seconds, OBSERVER_DERIVATIVE_STEP_DAYS, out_velocity_au_per_day);
}

bool observer_geocentric_cirs_acceleration_au_per_day2(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double lod_rate_seconds_per_day,
    double derivative_step_days,
    Vector3* out_acceleration_au_per_day2
) noexcept {
    if (!out_acceleration_au_per_day2) {
        return false;
    }
    *out_acceleration_au_per_day2 = cirs_observer_derivatives_au(
        longitude_rad, latitude_rad, height_m, jd_ut1,
        xp_rad, yp_rad, sp_rad, xp_rate_rad_per_day, yp_rate_rad_per_day, sp_rate_rad_per_day,
        dut1_rate_seconds_per_day, lod_seconds, lod_rate_seconds_per_day, derivative_step_days).acceleration_au_per_day2;
    return true;
}

bool observer_geocentric_cirs_acceleration_au_per_day2(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double lod_rate_seconds_per_day,
    Vector3* out_acceleration_au_per_day2
) noexcept {
    return observer_geocentric_cirs_acceleration_au_per_day2(
        longitude_rad, latitude_rad, height_m, jd_ut1,
        xp_rad, yp_rad, sp_rad, xp_rate_rad_per_day, yp_rate_rad_per_day, sp_rate_rad_per_day,
        dut1_rate_seconds_per_day, lod_seconds, lod_rate_seconds_per_day,
        OBSERVER_DERIVATIVE_STEP_DAYS, out_acceleration_au_per_day2);
}

Vector3 topocentric_position_au(const Vector3& target_geocentric_au, const Vector3& observer_geocentric_au) noexcept {
    return vector3_subtract(target_geocentric_au, observer_geocentric_au);
}

Vector3 topocentric_velocity_au_per_day(
    const Vector3& target_geocentric_velocity_au_per_day,
    const Vector3& observer_geocentric_velocity_au_per_day
) noexcept {
    return vector3_subtract(target_geocentric_velocity_au_per_day, observer_geocentric_velocity_au_per_day);
}

Vector3 topocentric_acceleration_au_per_day2(
    const Vector3& target_geocentric_acceleration_au_per_day2,
    const Vector3& observer_geocentric_acceleration_au_per_day2
) noexcept {
    return vector3_subtract(target_geocentric_acceleration_au_per_day2, observer_geocentric_acceleration_au_per_day2);
}

HorizontalCoordinates topocentric_position_to_horizontal(
    const Vector3& topocentric_equatorial_au,
    double local_sidereal_rad,
    double latitude_rad
) noexcept {
    const double distance = vector3_norm(topocentric_equatorial_au);
    if (distance == 0.0) {
        const HorizontalCoordinates zero = { 0.0, 0.0, 0.0 };
        return zero;
    }

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

    const HorizontalCoordinates result = {
        normalize_radians(std::atan2(east, north)),
        std::atan2(up, std::sqrt(east * east + north * north)),
        distance,
    };
    return result;
}

bool topocentric_velocity_to_horizontal_rates(
    const Vector3& p,
    const Vector3& v,
    double local_sidereal_rad,
    double local_sidereal_rate_rad_per_day,
    double latitude_rad,
    HorizontalRates* out_rates
) noexcept {
    if (!out_rates) {
        return false;
    }

    const double distance = vector3_norm(p);
    if (distance == 0.0) {
        out_rates->azimuth_rate_rad_per_day = 0.0;
        out_rates->altitude_rate_rad_per_day = 0.0;
        out_rates->distance_rate_au_per_day = 0.0;
        return true;
    }

    const double sin_lst = std::sin(local_sidereal_rad);
    const double cos_lst = std::cos(local_sidereal_rad);
    const double sin_lat = std::sin(latitude_rad);
    const double cos_lat = std::cos(latitude_rad);
    const double east = -sin_lst * p.x + cos_lst * p.y;
    const double north = -sin_lat * cos_lst * p.x - sin_lat * sin_lst * p.y + cos_lat * p.z;
    const double up = cos_lat * cos_lst * p.x + cos_lat * sin_lst * p.y + sin_lat * p.z;
    const double east_dot = -sin_lst * v.x + cos_lst * v.y
        + local_sidereal_rate_rad_per_day * (-cos_lst * p.x - sin_lst * p.y);
    const double north_dot = -sin_lat * cos_lst * v.x - sin_lat * sin_lst * v.y + cos_lat * v.z
        + local_sidereal_rate_rad_per_day * (sin_lat * sin_lst * p.x - sin_lat * cos_lst * p.y);
    const double up_dot = cos_lat * cos_lst * v.x + cos_lat * sin_lst * v.y + sin_lat * v.z
        + local_sidereal_rate_rad_per_day * (-cos_lat * sin_lst * p.x + cos_lat * cos_lst * p.y);
    const double horizontal_distance2 = east * east + north * north;
    const double horizontal_distance = std::sqrt(horizontal_distance2);
    out_rates->distance_rate_au_per_day = vector3_dot(p, v) / distance;
    if (horizontal_distance2 <= 1e-30) {
        out_rates->azimuth_rate_rad_per_day = 0.0;
        out_rates->altitude_rate_rad_per_day = 0.0;
        return true;
    }

    const double horizontal_distance_dot = (east * east_dot + north * north_dot) / horizontal_distance;
    out_rates->azimuth_rate_rad_per_day = (north * east_dot - east * north_dot) / horizontal_distance2;
    out_rates->altitude_rate_rad_per_day = (horizontal_distance * up_dot - up * horizontal_distance_dot) / (distance * distance);
    return true;
}

double atmospheric_refraction_bennett_rad(double altitude_rad, double pressure_mbar, double temperature_celsius) noexcept {
    const double temperature_kelvin = 273.0 + temperature_celsius;
    if (pressure_mbar <= 0.0 || temperature_kelvin <= 0.0 || altitude_rad < REFRACTION_LOW_ALTITUDE_CUTOFF_RAD) {
        return 0.0;
    }

    const double altitude_deg = altitude_rad * TAIYIN_RAD_TO_DEG;
    const double argument_deg = altitude_deg + 10.3 / (altitude_deg + 5.11);
    const double tangent = std::tan(argument_deg * TAIYIN_DEG_TO_RAD);
    if (tangent <= 0.0) {
        return 0.0;
    }

    const double scale = (pressure_mbar / 1010.0) * (283.0 / temperature_kelvin);
    return (1.02 / tangent) * scale * ARCMIN_TO_RAD;
}

double atmospheric_refraction_skyfield_rad(
    double altitude_rad,
    double pressure_mbar,
    double temperature_celsius,
    int max_iterations,
    double tolerance_deg
) noexcept {
    const double temperature_kelvin = 273.0 + temperature_celsius;
    if (pressure_mbar <= 0.0 || temperature_kelvin <= 0.0 || altitude_rad < REFRACTION_LOW_ALTITUDE_CUTOFF_RAD) {
        return 0.0;
    }

    const double altitude_deg = altitude_rad * TAIYIN_RAD_TO_DEG;
    if (altitude_deg < -1.0 || altitude_deg > 89.9 || max_iterations <= 0 || tolerance_deg <= 0.0) {
        return 0.0;
    }

    double apparent_deg = altitude_deg;
    double refraction_deg = 0.0;
    for (int i = 0; i < max_iterations; ++i) {
        const double previous_deg = apparent_deg;
        const double argument_deg = apparent_deg + 7.31 / (apparent_deg + 4.4);
        refraction_deg = 0.016667 / std::tan(argument_deg * TAIYIN_DEG_TO_RAD) * (0.28 * pressure_mbar / temperature_kelvin);
        apparent_deg = altitude_deg + refraction_deg;
        if (std::fabs(apparent_deg - previous_deg) <= tolerance_deg) {
            break;
        }
    }
    return refraction_deg * TAIYIN_DEG_TO_RAD;
}

double atmospheric_refraction_skyfield_rad(double altitude_rad, double pressure_mbar, double temperature_celsius) noexcept {
    return atmospheric_refraction_skyfield_rad(altitude_rad, pressure_mbar, temperature_celsius, 10, 3.0e-5);
}

double atmospheric_refraction_hybrid_rad(double altitude_rad, double pressure_mbar, double temperature_celsius) noexcept {
    const double temperature_kelvin = 273.0 + temperature_celsius;
    if (pressure_mbar <= 0.0 || temperature_kelvin <= 0.0 || altitude_rad < REFRACTION_LOW_ALTITUDE_CUTOFF_RAD) {
        return 0.0;
    }

    const double alt_deg = altitude_rad * TAIYIN_RAD_TO_DEG;
    if (alt_deg < -2.0) {
        return 0.0;
    }

    const double z_deg = 90.0 - alt_deg;
    const double tan_z = std::tan(z_deg * TAIYIN_DEG_TO_RAD);
    const double smart_arcmin = (58.276 * tan_z - 0.0824 * tan_z * tan_z * tan_z) / 60.0;
    const double bennett_arcmin = 1.02 / std::tan((alt_deg + 10.3 / (alt_deg + 5.11)) * TAIYIN_DEG_TO_RAD);
    double r_arcmin = 0.0;
    if (alt_deg >= 16.0) {
        r_arcmin = smart_arcmin;
    } else if (alt_deg <= 14.0) {
        r_arcmin = bennett_arcmin;
    } else {
        const double weight = (alt_deg - 14.0) / 2.0;
        r_arcmin = bennett_arcmin * (1.0 - weight) + smart_arcmin * weight;
    }
    if (r_arcmin <= 0.0) {
        return 0.0;
    }
    const double scale = (pressure_mbar / 1010.0) * (283.0 / temperature_kelvin);
    return r_arcmin * scale * ARCMIN_TO_RAD;
}

double atmospheric_refraction_auer_standish_rad(
    double altitude_rad,
    double pressure_mbar,
    double temperature_celsius,
    int max_iterations,
    double tolerance_rad
) noexcept {
    const double temperature_kelvin = 273.0 + temperature_celsius;
    if (pressure_mbar <= 0.0 || temperature_kelvin <= 0.0 || altitude_rad < REFRACTION_LOW_ALTITUDE_CUTOFF_RAD) {
        return 0.0;
    }
    if (altitude_rad < -2.0 * TAIYIN_DEG_TO_RAD || max_iterations <= 0 || tolerance_rad <= 0.0) {
        return 0.0;
    }

    double apparent_rad = altitude_rad + (altitude_rad < 0.1 ? 0.01 : 0.0);
    for (int iter = 0; iter < max_iterations; ++iter) {
        double z_app = TAIYIN_PI / 2.0 - apparent_rad;
        if (z_app > TAIYIN_PI / 2.0) {
            z_app = TAIYIN_PI / 2.0;
        }

        const double refraction = auer_standish_compute_refraction(z_app, pressure_mbar, temperature_celsius);
        const double new_apparent_rad = altitude_rad + refraction;
        if (std::fabs(new_apparent_rad - apparent_rad) < tolerance_rad) {
            return refraction;
        }
        apparent_rad = new_apparent_rad;
    }
    return apparent_rad - altitude_rad;
}

double atmospheric_refraction_auer_standish_rad(double altitude_rad, double pressure_mbar, double temperature_celsius) noexcept {
    return atmospheric_refraction_auer_standish_rad(altitude_rad, pressure_mbar, temperature_celsius, 10, 1e-8);
}

double atmospheric_refraction_sofa_rad(
    double altitude_rad,
    double pressure_mbar,
    double temperature_celsius,
    double relative_humidity,
    double wavelength_micrometer
) noexcept {
    if (altitude_rad < REFRACTION_LOW_ALTITUDE_CUTOFF_RAD) {
        return 0.0;
    }

    const bool optic = wavelength_micrometer <= 100.0;
    const double t = clamp_value(temperature_celsius, -150.0, 200.0);
    const double p = clamp_value(pressure_mbar, 0.0, 10000.0);
    const double r = clamp_value(relative_humidity, 0.0, 1.0);
    const double w = clamp_value(wavelength_micrometer, 0.1, 1e6);
    double pw = 0.0;
    if (p > 0.0) {
        const double ps = std::pow(10.0, (0.7859 + 0.03477 * t) / (1.0 + 0.00412 * t))
            * (1.0 + p * (4.5e-6 + 6e-10 * t * t));
        pw = r * ps / (1.0 - (1.0 - r) * ps / p);
    }

    const double tk = t + 273.15;
    double gamma = 0.0;
    if (optic) {
        const double wlsq = w * w;
        gamma = ((77.53484e-6 + (4.39108e-7 + 3.666e-9 / wlsq) / wlsq) * p - 11.2684e-6 * pw) / tk;
    } else {
        gamma = (77.6890e-6 * p - (6.3938e-6 - 0.375463 / tk) * pw) / tk;
    }
    double beta = 4.4474e-6 * tk;
    if (!optic) {
        beta -= 0.0074 * pw * beta;
    }
    const double refa = gamma * (1.0 - beta);
    const double refb = -gamma * (beta - gamma / 2.0);

    const double raw_horizontal = std::cos(altitude_rad);
    const double raw_up = std::sin(altitude_rad);
    const double rr = raw_horizontal > SOFA_CELMIN ? raw_horizontal : SOFA_CELMIN;
    const double z = raw_up > SOFA_SELMIN ? raw_up : SOFA_SELMIN;
    const double tz = rr / z;
    const double ww = refb * tz * tz;
    const double delta = (refa + ww) * tz / (1.0 + (refa + 3.0 * ww) / (z * z));
    const double cos_delta = 1.0 - delta * delta / 2.0;
    const double horizontal_factor = cos_delta - delta * z / rr;
    const double observed_horizontal = raw_horizontal * horizontal_factor;
    const double observed_up = cos_delta * raw_up + delta * rr;
    return std::atan2(observed_up, observed_horizontal) - altitude_rad;
}

double atmospheric_refraction_rad(
    double altitude_rad,
    double pressure_mbar,
    double temperature_celsius,
    double relative_humidity,
    double wavelength_micrometer,
    RefractionModel model
) noexcept {
    switch (model) {
    case RefractionModel::Bennett:
        return atmospheric_refraction_bennett_rad(altitude_rad, pressure_mbar, temperature_celsius);
    case RefractionModel::Skyfield:
        return atmospheric_refraction_skyfield_rad(altitude_rad, pressure_mbar, temperature_celsius);
    case RefractionModel::Hybrid:
        return atmospheric_refraction_hybrid_rad(altitude_rad, pressure_mbar, temperature_celsius);
    case RefractionModel::AuerStandish:
        return atmospheric_refraction_auer_standish_rad(altitude_rad, pressure_mbar, temperature_celsius);
    case RefractionModel::Sofa:
        return atmospheric_refraction_sofa_rad(altitude_rad, pressure_mbar, temperature_celsius, relative_humidity, wavelength_micrometer);
    }
    return 0.0;
}

double atmospheric_refraction_derivative_rad(
    double altitude_rad,
    double pressure_mbar,
    double temperature_celsius,
    double relative_humidity,
    double wavelength_micrometer,
    RefractionModel model,
    double derivative_step_rad
) noexcept {
    if (model == RefractionModel::Bennett) {
        return bennett_refraction_derivative_rad(altitude_rad, pressure_mbar, temperature_celsius);
    }
    if (altitude_rad < REFRACTION_LOW_ALTITUDE_CUTOFF_RAD) {
        return 0.0;
    }
    if (derivative_step_rad <= 0.0) {
        return 0.0;
    }
    const double h = derivative_step_rad;
    const double lower_altitude = altitude_rad - h < REFRACTION_LOW_ALTITUDE_CUTOFF_RAD ? altitude_rad : altitude_rad - h;
    const double upper_altitude = altitude_rad + h;
    const double lower = atmospheric_refraction_rad(
        lower_altitude, pressure_mbar, temperature_celsius, relative_humidity, wavelength_micrometer, model);
    const double upper = atmospheric_refraction_rad(
        upper_altitude, pressure_mbar, temperature_celsius, relative_humidity, wavelength_micrometer, model);
    return (upper - lower) / (upper_altitude - lower_altitude);
}

double atmospheric_refraction_derivative_rad(
    double altitude_rad,
    double pressure_mbar,
    double temperature_celsius,
    double relative_humidity,
    double wavelength_micrometer,
    RefractionModel model
) noexcept {
    return atmospheric_refraction_derivative_rad(
        altitude_rad,
        pressure_mbar,
        temperature_celsius,
        relative_humidity,
        wavelength_micrometer,
        model,
        1e-6);
}

HorizontalCoordinates refract_horizontal_coordinates(
    const HorizontalCoordinates& horizontal,
    double pressure_mbar,
    double temperature_celsius,
    double relative_humidity,
    double wavelength_micrometer,
    RefractionModel model
) noexcept {
    HorizontalCoordinates refracted = horizontal;
    refracted.altitude_rad += atmospheric_refraction_rad(
        horizontal.altitude_rad,
        pressure_mbar,
        temperature_celsius,
        relative_humidity,
        wavelength_micrometer,
        model);
    return refracted;
}

HorizontalRates refract_horizontal_rates(
    const HorizontalCoordinates& unrefracted_position,
    const HorizontalRates& unrefracted_rates,
    double pressure_mbar,
    double temperature_celsius,
    double relative_humidity,
    double wavelength_micrometer,
    RefractionModel model,
    double derivative_step_rad
) noexcept {
    HorizontalRates refracted = unrefracted_rates;
    refracted.altitude_rate_rad_per_day *= 1.0 + atmospheric_refraction_derivative_rad(
        unrefracted_position.altitude_rad,
        pressure_mbar,
        temperature_celsius,
        relative_humidity,
        wavelength_micrometer,
        model,
        derivative_step_rad);
    return refracted;
}

HorizontalRates refract_horizontal_rates(
    const HorizontalCoordinates& unrefracted_position,
    const HorizontalRates& unrefracted_rates,
    double pressure_mbar,
    double temperature_celsius,
    double relative_humidity,
    double wavelength_micrometer,
    RefractionModel model
) noexcept {
    return refract_horizontal_rates(
        unrefracted_position,
        unrefracted_rates,
        pressure_mbar,
        temperature_celsius,
        relative_humidity,
        wavelength_micrometer,
        model,
        1e-6);
}

}  // namespace taiyin
