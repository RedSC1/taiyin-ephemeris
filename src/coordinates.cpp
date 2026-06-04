#include "taiyin/coordinates.h"

#include "taiyin/angle.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/physical_constants.h"

#include "internal/coordinate_model_data.h"
#include "internal/erfa_cirs_subset.h"

#include <cmath>

namespace taiyin {
namespace {

const double ARCSEC_PER_CIRCLE = 1296000.0;
const double FRAME_BIAS_DX_RAD = -0.016617 * TAIYIN_ARCSEC_TO_RAD;
const double FRAME_BIAS_DE_RAD = -0.0068192 * TAIYIN_ARCSEC_TO_RAD;
const double FRAME_BIAS_DR_RAD = -0.0146 * TAIYIN_ARCSEC_TO_RAD;

struct IAU2000BNutationTerm {
    int l;
    int lp;
    int f;
    int d;
    int om;
    double ps;
    double pst;
    double pc;
    double ec;
    double ect;
    double es;
};

const IAU2000BNutationTerm IAU2000B_NUTATION_TERMS[] = {
    { 0, 0, 0, 0, 1, -172064161, -174666, 33386, 92052331, 9086, 15377 },
    { 0, 0, 2, -2, 2, -13170906, -1675, -13696, 5730336, -3015, -4587 },
    { 0, 0, 2, 0, 2, -2276413, -234, 2796, 978459, -485, 1374 },
    { 0, 0, 0, 0, 2, 2074554, 207, -698, -897492, 470, -291 },
    { 0, 1, 0, 0, 0, 1475877, -3633, 11817, 73871, -184, -1924 },
    { 0, 1, 2, -2, 2, -516821, 1226, -524, 224386, -677, -174 },
    { 1, 0, 0, 0, 0, 711159, 73, -872, -6750, 0, 358 },
    { 0, 0, 2, 0, 1, -387298, -367, 380, 200728, 18, 318 },
    { 1, 0, 2, 0, 2, -301461, -36, 816, 129025, -63, 367 },
    { 0, -1, 2, -2, 2, 215829, -494, 111, -95929, 299, 132 },
    { 0, 0, 2, -2, 1, 128227, 137, 181, -68982, -9, 39 },
    { -1, 0, 2, 0, 2, 123457, 11, 19, -53311, 32, -4 },
    { -1, 0, 0, 2, 0, 156994, 10, -168, -1235, 0, 82 },
    { 1, 0, 0, 0, 1, 63110, 63, 27, -33228, 0, -9 },
    { -1, 0, 0, 0, 1, -57976, -63, -189, 31429, 0, -75 },
    { -1, 0, 2, 2, 2, -59641, -11, 149, 25543, -11, 66 },
    { 1, 0, 2, 0, 1, -51613, -42, 129, 26366, 0, 78 },
    { -2, 0, 2, 0, 1, 45893, 50, 31, -24236, -10, 20 },
    { 0, 0, 0, 2, 0, 63384, 11, -150, -1220, 0, 29 },
    { 0, 0, 2, 2, 2, -38571, -1, 158, 16452, -11, 68 },
    { 0, -2, 2, -2, 2, 32481, 0, 0, -13870, 0, 0 },
    { -2, 0, 0, 2, 0, -47722, 0, -18, 477, 0, -25 },
    { 2, 0, 2, 0, 2, -31046, -1, 131, 13238, -11, 59 },
    { 1, 0, 2, -2, 2, 28593, 0, -1, -12338, 10, -3 },
    { -1, 0, 2, 0, 1, 20441, 21, 10, -10758, 0, -3 },
    { 2, 0, 0, 0, 0, 29243, 0, -74, -609, 0, 13 },
    { 0, 0, 2, 0, 0, 25887, 0, -66, -550, 0, 11 },
    { 0, 1, 0, 0, 1, -14053, -25, 79, 8551, -2, -45 },
    { -1, 0, 0, 2, 1, 15164, 10, 11, -8001, 0, -1 },
    { 0, 2, 2, -2, 2, -15794, 72, -16, 6850, -42, -5 },
    { 0, 0, -2, 2, 0, 21783, 0, 13, -167, 0, 13 },
    { 1, 0, 0, -2, 1, -12873, -10, -37, 6953, 0, -14 },
    { 0, -1, 0, 0, 1, -12654, 11, 63, 6415, 0, 26 },
    { -1, 0, 2, 2, 1, -10204, 0, 25, 5222, 0, 15 },
    { 0, 2, 0, 0, 0, 16707, -85, -10, 168, -1, 10 },
    { 1, 0, 2, 2, 2, -7691, 0, 44, 3268, 0, 19 },
    { -2, 0, 2, 0, 0, -11024, 0, -14, 104, 0, 2 },
    { 0, 1, 2, 0, 2, 7566, -21, -11, -3250, 0, -5 },
    { 0, 0, 2, 2, 1, -6637, -11, 25, 3353, 0, 14 },
    { 0, -1, 2, 0, 2, -7141, 21, 8, 3070, 0, 4 },
    { 0, 0, 0, 2, 1, -6302, -11, 2, 3272, 0, 4 },
    { 1, 0, 2, -2, 1, 5800, 10, 2, -3045, 0, -1 },
    { 2, 0, 2, -2, 2, 6443, 0, -7, -2768, 0, -4 },
    { -2, 0, 0, 2, 1, -5774, -11, -15, 3041, 0, -5 },
    { 2, 0, 2, 0, 1, -5350, 0, 21, 2695, 0, 12 },
    { 0, -1, 2, -2, 1, -4752, -11, -3, 2719, 0, -3 },
    { 0, 0, 0, -2, 1, -4940, -11, -21, 2720, 0, -9 },
    { -1, -1, 0, 2, 0, 7350, 0, -8, -51, 0, 4 },
    { 2, 0, 0, -2, 1, 4065, 0, 6, -2206, 0, 1 },
    { 1, 0, 0, 2, 0, 6579, 0, -24, -199, 0, 2 },
    { 0, 1, 2, -2, 1, 3579, 0, 5, -1900, 0, 1 },
    { 1, -1, 0, 0, 0, 4725, 0, -6, -41, 0, 3 },
    { -2, 0, 2, 0, 2, -3075, 0, -2, 1313, 0, -1 },
    { 3, 0, 2, 0, 2, -2904, 0, 15, 1233, 0, 7 },
    { 0, -1, 0, 2, 0, 4348, 0, -10, -81, 0, 2 },
    { 1, -1, 2, 0, 2, -2878, 0, 8, 1232, 0, 4 },
    { 0, 0, 0, 1, 0, -4230, 0, 5, -20, 0, -2 },
    { -1, -1, 2, 2, 2, -2819, 0, 7, 1207, 0, 3 },
    { -1, 0, 2, 0, 0, -4056, 0, 5, 40, 0, -2 },
    { 0, -1, 2, 2, 2, -2647, 0, 11, 1129, 0, 5 },
    { -2, 0, 0, 0, 1, -2294, 0, -10, 1266, 0, -4 },
    { 1, 1, 2, 0, 2, 2481, 0, -7, -1062, 0, -3 },
    { 2, 0, 0, 0, 1, 2179, 0, -2, -1129, 0, -2 },
    { -1, 1, 0, 1, 0, 3276, 0, 1, -9, 0, 0 },
    { 1, 1, 0, 0, 0, -3389, 0, 5, 35, 0, -2 },
    { 1, 0, 2, 0, 0, 3339, 0, -13, -107, 0, 1 },
    { -1, 0, 2, -2, 1, -1987, 0, -6, 1073, 0, -2 },
    { 1, 0, 0, 0, 2, -1981, 0, 0, 854, 0, 0 },
    { -1, 0, 0, 1, 0, 4026, 0, -353, -553, 0, -139 },
    { 0, 0, 2, 1, 2, 1660, 0, -5, -710, 0, -2 },
    { -1, 0, 2, 4, 2, -1521, 0, 9, 647, 0, 4 },
    { -1, 1, 0, 1, 1, 1314, 0, 0, -700, 0, 0 },
    { 0, -2, 2, -2, 1, -1283, 0, 0, 672, 0, 0 },
    { 1, 0, 2, 2, 1, -1331, 0, 8, 663, 0, 4 },
    { -2, 0, 2, 2, 2, 1383, 0, -2, -594, 0, -2 },
    { -1, 0, 0, 0, 2, 1405, 0, 4, -610, 0, 2 },
    { 1, 1, 2, -2, 2, 1290, 0, 0, -556, 0, 0 },
};

double fmod_arcsec_to_rad(double arcsec) noexcept {
    return std::fmod(arcsec, ARCSEC_PER_CIRCLE) * TAIYIN_ARCSEC_TO_RAD;
}

double polynomial_4(const double coefficients[4], double t) noexcept {
    double result = 0.0;
    double power = 1.0;
    for (int i = 0; i < 4; ++i) {
        result += coefficients[i] * power;
        power *= t;
    }
    return result;
}

Matrix3x3 vondrak_frame_bias_matrix() noexcept {
    return matrix3x3_multiply(
        matrix3x3_multiply(rotation_x_matrix(-FRAME_BIAS_DE_RAD), rotation_y_matrix(FRAME_BIAS_DX_RAD)),
        rotation_z_matrix(FRAME_BIAS_DR_RAD));
}

double equation_of_origins_from_nutation(double jd_tt, const NutationAngles& nutation) noexcept {
    return -(gmst_minus_era_rad(jd_tt) + nutation.dpsi_rad * std::cos(nutation.true_obliquity_rad));
}

}  // namespace

Matrix3x3 matrix3x3_identity() noexcept {
    Matrix3x3 result = {{
        { 1.0, 0.0, 0.0 },
        { 0.0, 1.0, 0.0 },
        { 0.0, 0.0, 1.0 },
    }};
    return result;
}

Matrix3x3 matrix3x3_transpose(const Matrix3x3& matrix) noexcept {
    Matrix3x3 result;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            result.m[i][j] = matrix.m[j][i];
        }
    }
    return result;
}

Matrix3x3 matrix3x3_multiply(const Matrix3x3& a, const Matrix3x3& b) noexcept {
    Matrix3x3 result;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            result.m[i][j] = a.m[i][0] * b.m[0][j]
                + a.m[i][1] * b.m[1][j]
                + a.m[i][2] * b.m[2][j];
        }
    }
    return result;
}

Vector3 matrix3x3_multiply_vector(const Matrix3x3& matrix, const Vector3& vector) noexcept {
    Vector3 result = {
        matrix.m[0][0] * vector.x + matrix.m[0][1] * vector.y + matrix.m[0][2] * vector.z,
        matrix.m[1][0] * vector.x + matrix.m[1][1] * vector.y + matrix.m[1][2] * vector.z,
        matrix.m[2][0] * vector.x + matrix.m[2][1] * vector.y + matrix.m[2][2] * vector.z,
    };
    return result;
}

Matrix3x3 matrix3x3_add(const Matrix3x3& a, const Matrix3x3& b) noexcept {
    Matrix3x3 result;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            result.m[i][j] = a.m[i][j] + b.m[i][j];
        }
    }
    return result;
}

Matrix3x3 matrix3x3_subtract(const Matrix3x3& a, const Matrix3x3& b) noexcept {
    Matrix3x3 result;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            result.m[i][j] = a.m[i][j] - b.m[i][j];
        }
    }
    return result;
}

Matrix3x3 matrix3x3_scale(const Matrix3x3& matrix, double scale) noexcept {
    Matrix3x3 result;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            result.m[i][j] = matrix.m[i][j] * scale;
        }
    }
    return result;
}

Matrix3x3 rotation_x_matrix(double angle_rad) noexcept {
    const double c = std::cos(angle_rad);
    const double s = std::sin(angle_rad);
    Matrix3x3 result = {{
        { 1.0, 0.0, 0.0 },
        { 0.0, c, s },
        { 0.0, -s, c },
    }};
    return result;
}

Matrix3x3 rotation_y_matrix(double angle_rad) noexcept {
    const double c = std::cos(angle_rad);
    const double s = std::sin(angle_rad);
    Matrix3x3 result = {{
        { c, 0.0, -s },
        { 0.0, 1.0, 0.0 },
        { s, 0.0, c },
    }};
    return result;
}

Matrix3x3 rotation_z_matrix(double angle_rad) noexcept {
    const double c = std::cos(angle_rad);
    const double s = std::sin(angle_rad);
    Matrix3x3 result = {{
        { c, s, 0.0 },
        { -s, c, 0.0 },
        { 0.0, 0.0, 1.0 },
    }};
    return result;
}

Matrix3x3 frame_bias_matrix() noexcept {
    return matrix3x3_multiply(
        matrix3x3_multiply(rotation_x_matrix(FRAME_BIAS_DE_RAD), rotation_y_matrix(-FRAME_BIAS_DX_RAD)),
        rotation_z_matrix(-FRAME_BIAS_DR_RAD));
}

double mean_obliquity_iau2006(double jd_tt) noexcept {
    const double t = (jd_tt - JD_J2000) / DAYS_PER_JULIAN_CENTURY;
    return (((((-0.0000000434 * t - 0.000000576) * t + 0.00200340) * t
        - 0.0001831) * t - 46.836769) * t + 84381.406) * TAIYIN_ARCSEC_TO_RAD;
}

bool vondrak2011_precession_matrix(double jd_tt, Matrix3x3* out, double* out_mean_obliquity_rad) noexcept {
    if (!out) {
        return false;
    }

    const double t = (jd_tt - JD_J2000) / DAYS_PER_JULIAN_CENTURY;
    double p = 0.0;
    double q = 0.0;
    for (int i = 0; i < internal::kVondrakEclipticPeriodicCount; ++i) {
        const internal::VondrakPeriodicTerm& term = internal::kVondrakEclipticPeriodic[i];
        const double arg = TAIYIN_TWO_PI * t / term.period_centuries;
        p += std::cos(arg) * term.cos_0_arcsec + std::sin(arg) * term.sin_0_arcsec;
        q += std::cos(arg) * term.cos_1_arcsec + std::sin(arg) * term.sin_1_arcsec;
    }
    p = (p + polynomial_4(internal::kVondrakEclipticPolynomialPa, t)) * TAIYIN_ARCSEC_TO_RAD;
    q = (q + polynomial_4(internal::kVondrakEclipticPolynomialQa, t)) * TAIYIN_ARCSEC_TO_RAD;

    const double sin_eps0 = std::sin(TAIYIN_J2000_MEAN_OBLIQUITY_RAD);
    const double cos_eps0 = std::cos(TAIYIN_J2000_MEAN_OBLIQUITY_RAD);
    const double ecliptic_z = std::sqrt(std::fmax(1.0 - p * p - q * q, 0.0));
    const Vector3 ecliptic_pole = {
        p,
        -q * cos_eps0 - ecliptic_z * sin_eps0,
        -q * sin_eps0 + ecliptic_z * cos_eps0,
    };

    double x = 0.0;
    double y = 0.0;
    for (int i = 0; i < internal::kVondrakEquatorPeriodicCount; ++i) {
        const internal::VondrakPeriodicTerm& term = internal::kVondrakEquatorPeriodic[i];
        const double arg = TAIYIN_TWO_PI * t / term.period_centuries;
        x += std::cos(arg) * term.cos_0_arcsec + std::sin(arg) * term.sin_0_arcsec;
        y += std::cos(arg) * term.cos_1_arcsec + std::sin(arg) * term.sin_1_arcsec;
    }
    x = (x + polynomial_4(internal::kVondrakEquatorPolynomialXa, t)) * TAIYIN_ARCSEC_TO_RAD;
    y = (y + polynomial_4(internal::kVondrakEquatorPolynomialYa, t)) * TAIYIN_ARCSEC_TO_RAD;

    const double equator_z = std::sqrt(std::fmax(1.0 - x * x - y * y, 0.0));
    const Vector3 equator_pole = { x, y, equator_z };
    const Vector3 equinox_x = vector3_normalize(vector3_cross(equator_pole, ecliptic_pole));
    const Vector3 equinox_y = vector3_cross(equator_pole, equinox_x);

    const Matrix3x3 rp = {{
        { equinox_x.x, equinox_x.y, equinox_x.z },
        { equinox_y.x, equinox_y.y, equinox_y.z },
        { equator_pole.x, equator_pole.y, equator_pole.z },
    }};
    *out = matrix3x3_multiply(rp, vondrak_frame_bias_matrix());
    if (out_mean_obliquity_rad) {
        *out_mean_obliquity_rad = mean_obliquity_iau2006(jd_tt);
    }
    return true;
}

bool iau2006_precession_matrix(double jd_tt, Matrix3x3* out, double* out_mean_obliquity_rad) noexcept {
    if (!out) {
        return false;
    }

    const double t = (jd_tt - JD_J2000) / DAYS_PER_JULIAN_CENTURY;
    const double gamma_bar = (-0.052928 + (10.556378 + (0.4932044 + (-0.00031238 + (-0.000002788 + 0.0000000260 * t) * t) * t) * t) * t) * TAIYIN_ARCSEC_TO_RAD;
    const double phi_bar = (84381.412819 + (-46.811016 + (0.0511268 + (0.00053289 + (-0.000000440 - 0.0000000176 * t) * t) * t) * t) * t) * TAIYIN_ARCSEC_TO_RAD;
    const double psi_bar = (-0.041775 + (5038.481484 + (1.5584175 + (-0.00018522 + (-0.000026452 - 0.0000000148 * t) * t) * t) * t) * t) * TAIYIN_ARCSEC_TO_RAD;
    const double eps_a = mean_obliquity_iau2006(jd_tt);

    // PxB = R1(-eps_a) * R3(-psi_bar) * R1(phi_bar) * R3(gamma_bar)
    // Standard convention: use ERFA formula directly
    Matrix3x3 matrix = rotation_z_matrix(gamma_bar);
    matrix = matrix3x3_multiply(rotation_x_matrix(phi_bar), matrix);
    matrix = matrix3x3_multiply(rotation_z_matrix(-psi_bar), matrix);
    matrix = matrix3x3_multiply(rotation_x_matrix(-eps_a), matrix);
    *out = matrix;
    if (out_mean_obliquity_rad) {
        *out_mean_obliquity_rad = eps_a;
    }
    return true;
}

bool iau2000b_nutation(double jd_tt, NutationAngles* out) noexcept {
    if (!out) {
        return false;
    }

    const double t = (jd_tt - JD_J2000) / DAYS_PER_JULIAN_CENTURY;
    const double fa[5] = {
        fmod_arcsec_to_rad(485868.249036 + t * 1717915923.2178),
        fmod_arcsec_to_rad(1287104.79305 + t * 129596581.0481),
        fmod_arcsec_to_rad(335779.526232 + t * 1739527262.8478),
        fmod_arcsec_to_rad(1072260.70369 + t * 1602961601.2090),
        fmod_arcsec_to_rad(450160.398036 - t * 6962890.5431),
    };

    double dp = 0.0;
    double de = 0.0;
    const int count = static_cast<int>(sizeof(IAU2000B_NUTATION_TERMS) / sizeof(IAU2000B_NUTATION_TERMS[0]));
    for (int i = count - 1; i >= 0; --i) {
        const IAU2000BNutationTerm& term = IAU2000B_NUTATION_TERMS[i];
        const double arg = term.l * fa[0] + term.lp * fa[1] + term.f * fa[2] + term.d * fa[3] + term.om * fa[4];
        const double s = std::sin(arg);
        const double c = std::cos(arg);
        dp += (term.ps + term.pst * t) * s + term.pc * c;
        de += (term.ec + term.ect * t) * c + term.es * s;
    }

    const double dpsi_arcsec = -0.000135 + dp * 1.0e-7;
    const double deps_arcsec = 0.000388 + de * 1.0e-7;
    const double mean_obliquity = mean_obliquity_iau2006(jd_tt);
    out->dpsi_rad = dpsi_arcsec * TAIYIN_ARCSEC_TO_RAD;
    out->deps_rad = deps_arcsec * TAIYIN_ARCSEC_TO_RAD;
    out->mean_obliquity_rad = mean_obliquity;
    out->true_obliquity_rad = mean_obliquity + out->deps_rad;
    return true;
}

bool iau2000a_nutation(double jd_tt, NutationAngles* out) noexcept {
    if (!out) {
        return false;
    }

    double dpsi = 0.0;
    double deps = 0.0;
    internal::erfa_nut06a(jd_tt, &dpsi, &deps);
    const double mean_obliquity = mean_obliquity_iau2006(jd_tt);
    out->dpsi_rad = dpsi;
    out->deps_rad = deps;
    out->mean_obliquity_rad = mean_obliquity;
    out->true_obliquity_rad = mean_obliquity + deps;
    return true;
}

Matrix3x3 nutation_matrix(const NutationAngles& nutation) noexcept {
    Matrix3x3 result = rotation_x_matrix(nutation.mean_obliquity_rad);
    result = matrix3x3_multiply(rotation_z_matrix(-nutation.dpsi_rad), result);
    result = matrix3x3_multiply(rotation_x_matrix(-nutation.true_obliquity_rad), result);
    return result;
}

bool iau2006a_cip_xy(double jd_tt, CelestialIntermediatePole* out) noexcept {
    if (!out) {
        return false;
    }

    double s = 0.0;
    internal::erfa_xys06a(jd_tt, &out->x_rad, &out->y_rad, &s);
    return true;
}

double cio_locator_s_iau2006a_rad(double jd_tt, double x_rad, double y_rad) noexcept {
    double x = 0.0;
    double y = 0.0;
    double s = 0.0;
    internal::erfa_xys06a(jd_tt, &x, &y, &s);
    return s + 0.5 * (x * y - x_rad * y_rad);
}

Matrix3x3 celestial_intermediate_matrix(double x_rad, double y_rad, double s_rad) noexcept {
    const double r2 = x_rad * x_rad + y_rad * y_rad;
    const double e = r2 > 0.0 ? std::atan2(y_rad, x_rad) : 0.0;
    const double d = std::atan(std::sqrt(r2 / (1.0 - r2)));
    Matrix3x3 matrix = rotation_z_matrix(e);
    matrix = matrix3x3_multiply(rotation_y_matrix(d), matrix);
    matrix = matrix3x3_multiply(rotation_z_matrix(-(e + s_rad)), matrix);
    return matrix;
}

bool cirs_matrix_iau2006a(
    double jd_tt,
    double celestial_pole_offset_dx_rad,
    double celestial_pole_offset_dy_rad,
    Matrix3x3* out
) noexcept {
    if (!out) {
        return false;
    }

    CelestialIntermediatePole cip;
    if (!iau2006a_cip_xy(jd_tt, &cip)) {
        return false;
    }
    cip.x_rad += celestial_pole_offset_dx_rad;
    cip.y_rad += celestial_pole_offset_dy_rad;
    const double s = cio_locator_s_iau2006a_rad(jd_tt, cip.x_rad, cip.y_rad);
    *out = celestial_intermediate_matrix(cip.x_rad, cip.y_rad, s);
    return true;
}

double equation_of_origins_iau2000b_rad(double jd_tt) noexcept {
    NutationAngles nutation;
    if (!iau2000b_nutation(jd_tt, &nutation)) {
        return 0.0;
    }
    return equation_of_origins_from_nutation(jd_tt, nutation);
}

Matrix3x3 j2000_ecliptic_matrix() noexcept {
    return matrix3x3_multiply(rotation_x_matrix(TAIYIN_J2000_MEAN_OBLIQUITY_RAD), frame_bias_matrix());
}

Matrix3x3 icrf_to_j2000_mean_equatorial_matrix() noexcept {
    return frame_bias_matrix();
}

Matrix3x3 icrf_to_j2000_ecliptic_matrix() noexcept {
    return j2000_ecliptic_matrix();
}

Matrix3x3 true_equator_of_date_matrix(
    const Matrix3x3& precession_matrix,
    const NutationAngles& nutation
) noexcept {
    return matrix3x3_multiply(nutation_matrix(nutation), precession_matrix);
}

Matrix3x3 icrf_to_true_equator_of_date_matrix(
    const Matrix3x3& precession_matrix,
    const NutationAngles& nutation
) noexcept {
    return true_equator_of_date_matrix(precession_matrix, nutation);
}

Matrix3x3 icrf_to_cirs_iau2000b_matrix(
    double jd_tt,
    const Matrix3x3& precession_matrix,
    const NutationAngles& nutation
) noexcept {
    return matrix3x3_multiply(
        rotation_z_matrix(-equation_of_origins_from_nutation(jd_tt, nutation)),
        true_equator_of_date_matrix(precession_matrix, nutation));
}

Matrix3x3 true_ecliptic_of_date_matrix(
    const Matrix3x3& precession_matrix,
    const NutationAngles& nutation
) noexcept {
    return matrix3x3_multiply(
        rotation_x_matrix(nutation.true_obliquity_rad),
        true_equator_of_date_matrix(precession_matrix, nutation));
}

Matrix3x3 icrf_to_true_ecliptic_of_date_matrix(
    const Matrix3x3& precession_matrix,
    const NutationAngles& nutation
) noexcept {
    return true_ecliptic_of_date_matrix(precession_matrix, nutation);
}

Matrix3x3 reference_plane_transform_matrix(
    const Matrix3x3& from_icrf_matrix,
    const Matrix3x3& to_icrf_matrix
) noexcept {
    return matrix3x3_multiply(to_icrf_matrix, matrix3x3_transpose(from_icrf_matrix));
}

Vector3 transform_position_with_matrix(const Vector3& position, const Matrix3x3& matrix) noexcept {
    return matrix3x3_multiply_vector(matrix, position);
}

Vector3 transform_velocity_with_matrix(
    const Vector3& position,
    const Vector3& velocity,
    const Matrix3x3& matrix,
    const Matrix3x3& matrix_dot
) noexcept {
    return vector3_add(
        matrix3x3_multiply_vector(matrix, velocity),
        matrix3x3_multiply_vector(matrix_dot, position));
}

Vector3 transform_acceleration_with_matrix(
    const Vector3& position,
    const Vector3& velocity,
    const Vector3& acceleration,
    const Matrix3x3& matrix,
    const Matrix3x3& matrix_dot,
    const Matrix3x3& matrix_ddot
) noexcept {
    return vector3_add(
        matrix3x3_multiply_vector(matrix, acceleration),
        vector3_add(
            vector3_scale(matrix3x3_multiply_vector(matrix_dot, velocity), 2.0),
            matrix3x3_multiply_vector(matrix_ddot, position)));
}

bool matrix_derivative_central(
    MatrixEvalFn eval,
    const void* data,
    double jd,
    double step_days,
    Matrix3x3* out_matrix_dot
) noexcept {
    if (!eval || !out_matrix_dot || step_days <= 0.0) {
        return false;
    }

    Matrix3x3 previous;
    Matrix3x3 next;
    if (!eval(jd - step_days, data, &previous)
        || !eval(jd + step_days, data, &next)) {
        return false;
    }

    *out_matrix_dot = matrix3x3_scale(matrix3x3_subtract(next, previous), 1.0 / (2.0 * step_days));
    return true;
}

bool matrix_second_derivative_central(
    MatrixEvalFn eval,
    const void* data,
    double jd,
    double step_days,
    Matrix3x3* out_matrix_ddot
) noexcept {
    if (!eval || !out_matrix_ddot || step_days <= 0.0) {
        return false;
    }

    Matrix3x3 previous;
    Matrix3x3 current;
    Matrix3x3 next;
    if (!eval(jd - step_days, data, &previous)
        || !eval(jd, data, &current)
        || !eval(jd + step_days, data, &next)) {
        return false;
    }

    *out_matrix_ddot = matrix3x3_scale(
        matrix3x3_add(matrix3x3_subtract(next, matrix3x3_scale(current, 2.0)), previous),
        1.0 / (step_days * step_days));
    return true;
}

}  // namespace taiyin
