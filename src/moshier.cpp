#include "taiyin/internal/moshier.h"

#include "taiyin/angle.h"
#include "taiyin/physical_constants.h"
#include "taiyin/internal/ephemeris_block.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace taiyin {
namespace internal {
namespace {

const double VELOCITY_STEP_DAYS = 0.01;
const double MOON_VELOCITY_STEP_DAYS = 0.001;
// DE441-compatible Earth/Moon mass ratio used to split EMB into Earth body.
// The original AA/Moshier source used an older 81.300585 value.
const double DE441_EMB_SPLIT_EARTH_MOON_MASS_RATIO = 81.3005682;

const double freqs[] = {
    53810162868.8982,
    21066413643.3548,
    12959774228.3429,
    6890507749.3988,
    1092566037.7991,
    439960985.5372,
    154248119.3933,
    78655032.0744,
    52272245.1795
};

const double phases[] = {
    252.25090552 * 3600.0,
    181.97980085 * 3600.0,
    100.46645683 * 3600.0,
    355.43299958 * 3600.0,
    34.35151874 * 3600.0,
    50.07744430 * 3600.0,
    314.05500511 * 3600.0,
    304.34866548 * 3600.0,
    860492.1546,
};

bool finite_ordered_range(double start, double end) noexcept {
    return std::isfinite(start) && std::isfinite(end) && end >= start;
}

double mods3600(double value) noexcept {
    return value - 1296000.0 * std::floor(value / 1296000.0);
}

void prepare_sincos(int k, double arg, int n, double ss[MOSHIER_NARGS][31], double cc[MOSHIER_NARGS][31]) noexcept {
    if (k < 0 || k >= MOSHIER_NARGS || n <= 0) {
        return;
    }

    const int count = std::min(n, 31);
    const double su = std::sin(arg);
    const double cu = std::cos(arg);
    ss[k][0] = su;
    cc[k][0] = cu;
    if (count == 1) {
        return;
    }

    double sv = 2.0 * su * cu;
    double cv = cu * cu - su * su;
    ss[k][1] = sv;
    cc[k][1] = cv;
    for (int i = 2; i < count; ++i) {
        const double s = su * cv + cu * sv;
        cv = cu * cv - su * sv;
        sv = s;
        ss[k][i] = sv;
        cc[k][i] = cv;
    }
}

bool read_arg(const MoshierPlanetEphemerisData& data, size_t* cursor, int* out) noexcept {
    if (!cursor || !out || *cursor >= data.arg_tbl.size()) {
        return false;
    }
    *out = data.arg_tbl[*cursor];
    ++(*cursor);
    return true;
}

bool read_coeff(const std::vector<double>& coeffs, size_t* cursor, double* out) noexcept {
    if (!cursor || !out || *cursor >= coeffs.size()) {
        return false;
    }
    *out = coeffs[*cursor];
    ++(*cursor);
    return true;
}

bool accumulate_polynomial_term(
    double T,
    int nt,
    const std::vector<double>& coeffs,
    size_t* cursor,
    double* out
) noexcept {
    double value = 0.0;
    if (nt < 0 || !read_coeff(coeffs, cursor, &value)) {
        return false;
    }
    for (int ip = 0; ip < nt; ++ip) {
        double next = 0.0;
        if (!read_coeff(coeffs, cursor, &next)) {
            return false;
        }
        value = value * T + next;
    }
    *out = value;
    return true;
}

bool accumulate_periodic_term(
    double T,
    int nt,
    double cv,
    double sv,
    const std::vector<double>& coeffs,
    size_t* cursor,
    double* sum
) noexcept {
    double cu = 0.0;
    double su = 0.0;
    if (nt < 0 || !read_coeff(coeffs, cursor, &cu) || !read_coeff(coeffs, cursor, &su)) {
        return false;
    }
    for (int ip = 0; ip < nt; ++ip) {
        double next_cu = 0.0;
        double next_su = 0.0;
        if (!read_coeff(coeffs, cursor, &next_cu) || !read_coeff(coeffs, cursor, &next_su)) {
            return false;
        }
        cu = cu * T + next_cu;
        su = su * T + next_su;
    }
    *sum += cu * cv + su * sv;
    return true;
}

void mean_elements(double jd_tdb, double args[MOSHIER_NARGS], double* lp_equinox_arcsec) noexcept {
    std::fill(args, args + MOSHIER_NARGS, 0.0);
    if (lp_equinox_arcsec) {
        *lp_equinox_arcsec = 0.0;
    }

    const double T = (jd_tdb - JD_J2000) / DAYS_PER_JULIAN_CENTURY;
    const double T2 = T * T;
    double x = 0.0;

    x = mods3600(538101628.6889819 * T + 908103.213);
    x += (6.39e-6 * T - 0.0192789) * T2;
    args[0] = TAIYIN_ARCSEC_TO_RAD * x;

    x = mods3600(210664136.4335482 * T + 655127.236);
    x += (-6.27e-6 * T + 0.0059381) * T2;
    args[1] = TAIYIN_ARCSEC_TO_RAD * x;

    x = mods3600(129597742.283429 * T + 361679.198);
    x += (-5.23e-6 * T - 2.04411e-2) * T2;
    args[2] = TAIYIN_ARCSEC_TO_RAD * x;

    x = mods3600(68905077.493988 * T + 1279558.751);
    x += (-1.043e-5 * T + 0.0094264) * T2;
    args[3] = TAIYIN_ARCSEC_TO_RAD * x;

    x = mods3600(10925660.377991 * T + 123665.420);
    x += ((((-3.4e-10 * T + 5.91e-8) * T + 4.667e-6) * T + 5.706e-5) * T - 3.060378e-1) * T2;
    args[4] = TAIYIN_ARCSEC_TO_RAD * x;

    x = mods3600(4399609.855372 * T + 180278.752);
    x += ((((8.3e-10 * T - 1.452e-7) * T - 1.1484e-5) * T - 1.6618e-4) * T + 7.561614e-1) * T2;
    args[5] = TAIYIN_ARCSEC_TO_RAD * x;

    x = mods3600(1542481.193933 * T + 1130597.971) + (0.00002156 * T - 0.0175083) * T2;
    args[6] = TAIYIN_ARCSEC_TO_RAD * x;

    x = mods3600(786550.320744 * T + 1095655.149) + (-0.00000895 * T + 0.0021103) * T2;
    args[7] = TAIYIN_ARCSEC_TO_RAD * x;

    x = mods3600(1.6029616009939659e+09 * T + 1.0722612202445078e+06);
    x += (((((-3.207663637426e-13 * T + 2.555243317839e-11) * T + 2.560078201452e-9) * T
        - 3.702060118571e-5) * T + 6.9492746836058421e-3) * T - 6.7352202374457519e+0) * T2;
    args[9] = TAIYIN_ARCSEC_TO_RAD * x;

    x = mods3600(1.7395272628437717e+09 * T + 3.3577951412884740e+05);
    x += (((((4.474984866301e-13 * T + 4.189032191814e-11) * T - 2.790392351314e-9) * T
        - 2.165750777942e-6) * T - 7.5311878482337989e-4) * T - 1.3117809789650071e+1) * T2;
    args[10] = TAIYIN_ARCSEC_TO_RAD * x;

    x = mods3600(1.2959658102304320e+08 * T + 1.2871027407441526e+06);
    x += ((((((((1.62e-20 * T - 1.0390e-17) * T - 3.83508e-15) * T + 4.237343e-13) * T
        + 8.8555011e-11) * T - 4.77258489e-8) * T - 1.1297037031e-5) * T
        + 8.7473717367324703e-5) * T - 5.5281306421783094e-1) * T2;
    args[11] = TAIYIN_ARCSEC_TO_RAD * x;

    x = mods3600(1.7179159228846793e+09 * T + 4.8586817465825332e+05);
    x += (((((-1.755312760154e-12 * T + 3.452144225877e-11) * T - 2.506365935364e-8) * T
        - 2.536291235258e-4) * T + 5.2099641302735818e-2) * T + 3.1501359071894147e+1) * T2;
    args[12] = TAIYIN_ARCSEC_TO_RAD * x;

    x = mods3600(1.7325643720442266e+09 * T + 7.8593980921052420e+05);
    x += (((((7.200592540556e-14 * T + 2.235210987108e-10) * T - 1.024222633731e-8) * T
        - 6.073960534117e-5) * T + 6.9017248528380490e-3) * T - 5.6550460027471399e+0) * T2;
    if (lp_equinox_arcsec) {
        *lp_equinox_arcsec = x;
    }
    args[13] = TAIYIN_ARCSEC_TO_RAD * x;

    x = mods3600(4.48175409e7 * T + 8.060457e5);
    args[14] = TAIYIN_ARCSEC_TO_RAD * x;

    x = mods3600(5.36486787e6 * T - 391702.8);
    args[15] = TAIYIN_ARCSEC_TO_RAD * x;

    x = mods3600(1.73573e6 * T);
    args[17] = TAIYIN_ARCSEC_TO_RAD * x;
}

bool eval_moshier_spherical(double jd_tdb, const MoshierPlanetEphemerisData& data, double* lon, double* lat, double* rad) noexcept {
    const double T = (jd_tdb - JD_J2000) / data.timescale_days;
    double ss[MOSHIER_NARGS][31];
    double cc[MOSHIER_NARGS][31];
    std::memset(ss, 0, sizeof(ss));
    std::memset(cc, 0, sizeof(cc));

    double args[MOSHIER_NARGS];
    std::memset(args, 0, sizeof(args));

    if (data.evaluator == MoshierPlanetEvaluator::G3Plan) {
        mean_elements(jd_tdb, args, 0);
    }

    for (int i = 0; i < data.maxargs; ++i) {
        const int harmonic_count = data.max_harmonic[i];
        if (harmonic_count > 0) {
            const double arg = data.evaluator == MoshierPlanetEvaluator::G3Plan
                ? args[i]
                : (mods3600(freqs[i] * T) + phases[i]) * TAIYIN_ARCSEC_TO_RAD;
            prepare_sincos(i, arg, harmonic_count, ss, cc);
        }
    }

    size_t arg_cursor = 0;
    size_t lon_cursor = 0;
    size_t lat_cursor = 0;
    size_t rad_cursor = 0;
    double sl = 0.0;
    double sb = 0.0;
    double sr = 0.0;

    for (;;) {
        int np = 0;
        if (!read_arg(data, &arg_cursor, &np)) {
            return false;
        }
        if (np < 0) {
            break;
        }
        if (np == 0) {
            int nt = 0;
            double value = 0.0;
            if (!read_arg(data, &arg_cursor, &nt)) {
                return false;
            }
            if (!accumulate_polynomial_term(T, nt, data.lon_tbl, &lon_cursor, &value)) {
                return false;
            }
            sl += data.evaluator == MoshierPlanetEvaluator::G3Plan ? value : mods3600(value);
            if (!accumulate_polynomial_term(T, nt, data.lat_tbl, &lat_cursor, &value)) {
                return false;
            }
            sb += value;
            if (!accumulate_polynomial_term(T, nt, data.rad_tbl, &rad_cursor, &value)) {
                return false;
            }
            sr += value;
            continue;
        }

        double cv = 0.0;
        double sv = 0.0;
        bool has_angle = false;
        for (int ip = 0; ip < np; ++ip) {
            int harmonic = 0;
            int argument_index = 0;
            if (!read_arg(data, &arg_cursor, &harmonic) || !read_arg(data, &arg_cursor, &argument_index)) {
                return false;
            }
            if (harmonic == 0) {
                continue;
            }

            const int m = argument_index - 1;
            const int k = (harmonic < 0 ? -harmonic : harmonic) - 1;
            if (m < 0 || m >= MOSHIER_NARGS || k < 0 || k >= 31) {
                return false;
            }

            double su = ss[m][k];
            if (harmonic < 0) {
                su = -su;
            }
            const double cu = cc[m][k];
            if (!has_angle) {
                sv = su;
                cv = cu;
                has_angle = true;
            } else {
                const double t = su * cv + cu * sv;
                cv = cu * cv - su * sv;
                sv = t;
            }
        }

        int nt = 0;
        if (!read_arg(data, &arg_cursor, &nt)) {
            return false;
        }
        if (!has_angle) {
            cv = 1.0;
            sv = 0.0;
        }
        if (!accumulate_periodic_term(T, nt, cv, sv, data.lon_tbl, &lon_cursor, &sl)
            || !accumulate_periodic_term(T, nt, cv, sv, data.lat_tbl, &lat_cursor, &sb)
            || !accumulate_periodic_term(T, nt, cv, sv, data.rad_tbl, &rad_cursor, &sr)) {
            return false;
        }
    }

    if (data.evaluator == MoshierPlanetEvaluator::G3Plan) {
        if (data.objnum <= 0 || data.objnum > MOSHIER_NARGS) {
            return false;
        }
        *lon = args[data.objnum - 1] + TAIYIN_ARCSEC_TO_RAD * data.trunclvl * sl;
        *lat = TAIYIN_ARCSEC_TO_RAD * data.trunclvl * sb;
        *rad = data.distance_au * (1.0 + TAIYIN_ARCSEC_TO_RAD * data.trunclvl * sr);
    } else {
        *lon = TAIYIN_ARCSEC_TO_RAD * sl;
        *lat = TAIYIN_ARCSEC_TO_RAD * sb;
        *rad = TAIYIN_ARCSEC_TO_RAD * data.distance_au * sr + data.distance_au;
    }
    return std::isfinite(*lon) && std::isfinite(*lat) && std::isfinite(*rad);
}

void apply_de441_correction(double jd_tdb, const MoshierPlanetEphemerisData& data, double* lon, double* lat) noexcept {
    if (!data.apply_de441_correction || data.corrections.empty()) {
        return;
    }
    const double year = 2000.0 + (jd_tdb - JD_J2000) / DAYS_PER_JULIAN_YEAR;
    const MoshierCorrectionSegment* selected = 0;
    for (size_t i = 0; i < data.corrections.size(); ++i) {
        const MoshierCorrectionSegment& segment = data.corrections[i];
        if (year >= segment.start_year && year < segment.end_year) {
            selected = &segment;
            break;
        }
        if (i + 1 == data.corrections.size() && year >= segment.start_year && year <= segment.end_year) {
            selected = &segment;
            break;
        }
    }
    if (!selected || selected->half_width_years == 0.0) {
        return;
    }

    const double t = (year - selected->center_year) / selected->half_width_years;
    const double dlon = ((selected->lon_arcsec[0] * t + selected->lon_arcsec[1]) * t + selected->lon_arcsec[2]) * t + selected->lon_arcsec[3];
    const double dlat = ((selected->lat_arcsec[0] * t + selected->lat_arcsec[1]) * t + selected->lat_arcsec[2]) * t + selected->lat_arcsec[3];
    *lon -= dlon * TAIYIN_ARCSEC_TO_RAD;
    *lat -= dlat * TAIYIN_ARCSEC_TO_RAD;
}

void ecliptic_to_equatorial(double lon, double lat, double rad, Vector3* out) noexcept {
    const double cos_lat = std::cos(lat);
    const double x_ecl = rad * cos_lat * std::cos(lon);
    const double y_ecl = rad * cos_lat * std::sin(lon);
    const double z_ecl = rad * std::sin(lat);
    const double cos_eps = std::cos(TAIYIN_MOSHIER_J2000_OBLIQUITY_RAD);
    const double sin_eps = std::sin(TAIYIN_MOSHIER_J2000_OBLIQUITY_RAD);

    out->x = x_ecl;
    out->y = y_ecl * cos_eps - z_ecl * sin_eps;
    out->z = y_ecl * sin_eps + z_ecl * cos_eps;
}

bool eval_position(double jd_tdb, const MoshierPlanetEphemerisData& data, Vector3* out) noexcept {
    if (jd_tdb < data.jd_tdb_start || jd_tdb > data.jd_tdb_end) {
        return false;
    }

    double lon = 0.0;
    double lat = 0.0;
    double rad = 0.0;
    if (!eval_moshier_spherical(jd_tdb, data, &lon, &lat, &rad)) {
        return false;
    }
    apply_de441_correction(jd_tdb, data, &lon, &lat);
    ecliptic_to_equatorial(lon, lat, rad, out);
    return true;
}

double obliquity(double jd_tdb) noexcept {
    double T = (jd_tdb - JD_J2000) / DAYS_PER_JULIAN_CENTURY;
    T /= 10.0;
    const double eps_arcsec = (((((((((2.45e-10 * T + 5.79e-9) * T + 2.787e-7) * T
        + 7.12e-7) * T - 3.905e-5) * T - 2.4967e-3) * T
        - 5.138e-3) * T + 1.9989) * T - 0.0175) * T - 468.33960) * T
        + 84381.406173;
    return eps_arcsec * TAIYIN_ARCSEC_TO_RAD;
}

void precess_to_j2000(const Vector3& rect, double jd_tdb, Vector3* out) noexcept {
    if (jd_tdb == JD_J2000) {
        *out = rect;
        return;
    }

    double T = (jd_tdb - JD_J2000) / DAYS_PER_JULIAN_CENTURY;

    const double eps_date = obliquity(jd_tdb);
    const double cos_ed = std::cos(eps_date);
    const double sin_ed = std::sin(eps_date);

    double x0 = rect.x;
    double x1 = cos_ed * rect.y + sin_ed * rect.z;
    double x2 = -sin_ed * rect.y + cos_ed * rect.z;

    T /= 10.0;

    const double pAcof[] = {
        -8.66e-10, -4.759e-8, 2.424e-7, 1.3095e-5, 1.7451e-4,
        -1.8055e-3, -0.235316, 0.076, 110.5414, 50287.91959
    };
    double pA = pAcof[0];
    for (int i = 1; i < 10; ++i) {
        pA = pA * T + pAcof[i];
    }
    pA *= TAIYIN_ARCSEC_TO_RAD * T;

    const double nodecof[] = {
        6.6402e-16, -2.69151e-15, -1.547021e-12, 7.521313e-12, 1.9e-10,
        -3.54e-9, -1.8103e-7, 1.26e-7, 7.436169e-5, -0.04207794833, 3.052115282424
    };
    double W = nodecof[0];
    for (int i = 1; i < 11; ++i) {
        W = W * T + nodecof[i];
    }

    const double inclcof[] = {
        1.2147e-16, 7.3759e-17, -8.26287e-14, 2.503410e-13, 2.4650839e-11,
        -5.4000441e-11, 1.32115526e-9, -6.012e-7, -1.62442e-5, 0.00227850649, 0.0
    };
    double incl = inclcof[0];
    for (int i = 1; i < 11; ++i) {
        incl = incl * T + inclcof[i];
    }

    double z = W + pA;
    double B = std::cos(z);
    double A = std::sin(z);
    z = B * x0 + A * x1;
    x1 = -A * x0 + B * x1;
    x0 = z;

    B = std::cos(-incl);
    A = std::sin(-incl);
    z = B * x1 + A * x2;
    x2 = -A * x1 + B * x2;
    x1 = z;

    z = -W;
    B = std::cos(z);
    A = std::sin(z);
    z = B * x0 + A * x1;
    x1 = -A * x0 + B * x1;
    x0 = z;

    const double eps_j2000 = obliquity(JD_J2000);
    const double cos_e0 = std::cos(eps_j2000);
    const double sin_e0 = std::sin(eps_j2000);

    z = cos_e0 * x1 - sin_e0 * x2;
    x2 = sin_e0 * x1 + cos_e0 * x2;
    x1 = z;

    out->x = x0;
    out->y = x1;
    out->z = x2;
}

bool eval_moon_lr(double jd_tdb, const MoshierMoonEphemerisData& data, double* lon_arcsec, double* rad_arcsec) noexcept {
    const double T = (jd_tdb - JD_J2000) / data.lr_timescale_days;
    double args[MOSHIER_NARGS];
    double lp_equinox = 0.0;
    mean_elements(jd_tdb, args, &lp_equinox);

    double ss[MOSHIER_NARGS][31];
    double cc[MOSHIER_NARGS][31];
    std::memset(ss, 0, sizeof(ss));
    std::memset(cc, 0, sizeof(cc));
    for (int i = 0; i < data.lr_maxargs; ++i) {
        const int harmonic_count = data.lr_max_harmonic[i];
        if (harmonic_count > 0) {
            prepare_sincos(i, args[i], harmonic_count, ss, cc);
        }
    }

    size_t arg_cursor = 0;
    size_t lon_cursor = 0;
    size_t rad_cursor = 0;
    double sl = 0.0;
    double sr = 0.0;

    for (;;) {
        int np = 0;
        if (arg_cursor >= data.lr_arg_tbl.size()) {
            return false;
        }
        np = data.lr_arg_tbl[arg_cursor++];
        if (np < 0) {
            break;
        }
        if (np == 0) {
            if (arg_cursor >= data.lr_arg_tbl.size()) {
                return false;
            }
            const int nt = data.lr_arg_tbl[arg_cursor++];
            double value = 0.0;
            if (!accumulate_polynomial_term(T, nt, data.lr_lon_tbl, &lon_cursor, &value)) {
                return false;
            }
            sl += value;
            if (!accumulate_polynomial_term(T, nt, data.lr_rad_tbl, &rad_cursor, &value)) {
                return false;
            }
            sr += value;
            continue;
        }

        double cv = 0.0;
        double sv = 0.0;
        bool has_angle = false;
        for (int ip = 0; ip < np; ++ip) {
            if (arg_cursor + 1 >= data.lr_arg_tbl.size()) {
                return false;
            }
            const int harmonic = data.lr_arg_tbl[arg_cursor++];
            const int m = data.lr_arg_tbl[arg_cursor++] - 1;
            if (harmonic == 0) {
                continue;
            }
            const int k = (harmonic < 0 ? -harmonic : harmonic) - 1;
            if (m < 0 || m >= MOSHIER_NARGS || k < 0 || k >= 31) {
                return false;
            }
            double su = ss[m][k];
            if (harmonic < 0) {
                su = -su;
            }
            const double cu = cc[m][k];
            if (!has_angle) {
                sv = su;
                cv = cu;
                has_angle = true;
            } else {
                const double t = su * cv + cu * sv;
                cv = cu * cv - su * sv;
                sv = t;
            }
        }
        if (arg_cursor >= data.lr_arg_tbl.size()) {
            return false;
        }
        const int nt = data.lr_arg_tbl[arg_cursor++];
        if (!has_angle) {
            cv = 1.0;
            sv = 0.0;
        }
        if (!accumulate_periodic_term(T, nt, cv, sv, data.lr_lon_tbl, &lon_cursor, &sl)
            || !accumulate_periodic_term(T, nt, cv, sv, data.lr_rad_tbl, &rad_cursor, &sr)) {
            return false;
        }
    }

    *lon_arcsec = data.lr_trunclvl * sl + lp_equinox;
    if (*lon_arcsec < -648000.0) {
        *lon_arcsec += 1296000.0;
    }
    if (*lon_arcsec > 648000.0) {
        *lon_arcsec -= 1296000.0;
    }
    *rad_arcsec = data.lr_trunclvl * sr;
    return true;
}

bool eval_moon_lat(double jd_tdb, const MoshierMoonEphemerisData& data, double* lat_arcsec) noexcept {
    const double T = (jd_tdb - JD_J2000) / data.lat_timescale_days;
    double args[MOSHIER_NARGS];
    mean_elements(jd_tdb, args, 0);

    double ss[MOSHIER_NARGS][31];
    double cc[MOSHIER_NARGS][31];
    std::memset(ss, 0, sizeof(ss));
    std::memset(cc, 0, sizeof(cc));
    for (int i = 0; i < MOSHIER_NARGS; ++i) {
        const int harmonic_count = data.lat_max_harmonic[i];
        if (harmonic_count > 0) {
            prepare_sincos(i, args[i], harmonic_count, ss, cc);
        }
    }

    size_t arg_cursor = 0;
    size_t lon_cursor = 0;
    double sl = 0.0;

    for (;;) {
        if (arg_cursor >= data.lat_arg_tbl.size()) {
            return false;
        }
        const int np = data.lat_arg_tbl[arg_cursor++];
        if (np < 0) {
            break;
        }
        if (np == 0) {
            if (arg_cursor >= data.lat_arg_tbl.size()) {
                return false;
            }
            const int nt = data.lat_arg_tbl[arg_cursor++];
            double value = 0.0;
            if (!accumulate_polynomial_term(T, nt, data.lat_lon_tbl, &lon_cursor, &value)) {
                return false;
            }
            sl += value;
            continue;
        }

        double cv = 0.0;
        double sv = 0.0;
        bool has_angle = false;
        for (int ip = 0; ip < np; ++ip) {
            if (arg_cursor + 1 >= data.lat_arg_tbl.size()) {
                return false;
            }
            const int harmonic = data.lat_arg_tbl[arg_cursor++];
            const int m = data.lat_arg_tbl[arg_cursor++] - 1;
            if (harmonic == 0) {
                continue;
            }
            const int k = (harmonic < 0 ? -harmonic : harmonic) - 1;
            if (m < 0 || m >= MOSHIER_NARGS || k < 0 || k >= 31) {
                return false;
            }
            double su = ss[m][k];
            if (harmonic < 0) {
                su = -su;
            }
            const double cu = cc[m][k];
            if (!has_angle) {
                sv = su;
                cv = cu;
                has_angle = true;
            } else {
                const double t = su * cv + cu * sv;
                cv = cu * cv - su * sv;
                sv = t;
            }
        }
        if (arg_cursor >= data.lat_arg_tbl.size()) {
            return false;
        }
        const int nt = data.lat_arg_tbl[arg_cursor++];
        if (!has_angle) {
            cv = 1.0;
            sv = 0.0;
        }
        if (!accumulate_periodic_term(T, nt, cv, sv, data.lat_lon_tbl, &lon_cursor, &sl)) {
            return false;
        }
    }

    *lat_arcsec = data.lat_trunclvl * sl;
    return true;
}

bool eval_moon_position(double jd_tdb, const MoshierMoonEphemerisData& data, Vector3* out) noexcept {
    if (jd_tdb < data.jd_tdb_start || jd_tdb > data.jd_tdb_end) {
        return false;
    }

    double lon_arcsec = 0.0;
    double rad_arcsec = 0.0;
    double lat_arcsec = 0.0;
    if (!eval_moon_lr(jd_tdb, data, &lon_arcsec, &rad_arcsec)
        || !eval_moon_lat(jd_tdb, data, &lat_arcsec)) {
        return false;
    }

    const double lon = TAIYIN_ARCSEC_TO_RAD * lon_arcsec;
    const double lat = TAIYIN_ARCSEC_TO_RAD * lat_arcsec;
    const double dist = (1.0 + TAIYIN_ARCSEC_TO_RAD * rad_arcsec) * data.lr_distance_au;
    const double eps_date = obliquity(jd_tdb);
    const double cos_eps = std::cos(eps_date);
    const double sin_eps = std::sin(eps_date);
    const double cos_lat = std::cos(lat);
    const double sin_lat = std::sin(lat);
    const double cos_lon = std::cos(lon);
    const double sin_lon = std::sin(lon);

    Vector3 of_date;
    of_date.x = dist * cos_lat * cos_lon;
    const double y_ecl = dist * cos_lat * sin_lon;
    const double z_ecl = dist * sin_lat;
    of_date.y = cos_eps * y_ecl - sin_eps * z_ecl;
    of_date.z = sin_eps * y_ecl + cos_eps * z_ecl;
    precess_to_j2000(of_date, jd_tdb, out);
    return true;
}

bool valid_table(const MoshierPlanetTable& table) noexcept {
    return table.maxargs > 0
        && table.maxargs <= MOSHIER_NARGS
        && table.arg_tbl
        && table.arg_count > 0
        && table.lon_tbl
        && table.lon_count > 0
        && table.lat_tbl
        && table.lat_count > 0
        && table.rad_tbl
        && table.rad_count > 0
        && std::isfinite(table.distance_au)
        && std::isfinite(table.timescale_days)
        && std::isfinite(table.trunclvl)
        && table.distance_au > 0.0
        && table.timescale_days > 0.0;
}

bool valid_moon_lr_table(const MoshierMoonLRTable& table) noexcept {
    return table.maxargs > 0
        && table.maxargs <= MOSHIER_NARGS
        && table.arg_tbl
        && table.arg_count > 0
        && table.lon_tbl
        && table.lon_count > 0
        && table.rad_tbl
        && table.rad_count > 0
        && std::isfinite(table.distance_au)
        && std::isfinite(table.timescale_days)
        && std::isfinite(table.trunclvl)
        && table.distance_au > 0.0
        && table.timescale_days > 0.0;
}

bool valid_moon_lat_table(const MoshierMoonLatTable& table) noexcept {
    return table.maxargs > 0
        && table.maxargs <= MOSHIER_NARGS
        && table.arg_tbl
        && table.arg_count > 0
        && table.lon_tbl
        && table.lon_count > 0
        && std::isfinite(table.timescale_days)
        && std::isfinite(table.trunclvl)
        && table.timescale_days > 0.0;
}

size_t moshier_allocated_bytes(const MoshierPlanetEphemerisData& data) noexcept {
    return sizeof(MoshierPlanetEphemerisData)
        + data.arg_tbl.capacity() * sizeof(int8_t)
        + data.lon_tbl.capacity() * sizeof(double)
        + data.lat_tbl.capacity() * sizeof(double)
        + data.rad_tbl.capacity() * sizeof(double)
        + data.corrections.capacity() * sizeof(MoshierCorrectionSegment);
}

size_t moshier_allocated_bytes(const MoshierMoonEphemerisData& data) noexcept {
    return sizeof(MoshierMoonEphemerisData)
        + data.lr_arg_tbl.capacity() * sizeof(int8_t)
        + data.lr_lon_tbl.capacity() * sizeof(double)
        + data.lr_rad_tbl.capacity() * sizeof(double)
        + data.lat_arg_tbl.capacity() * sizeof(int8_t)
        + data.lat_lon_tbl.capacity() * sizeof(double);
}

size_t moshier_allocated_bytes(const MoshierEarthBodyEphemerisData& data) noexcept {
    size_t bytes = sizeof(MoshierEarthBodyEphemerisData);
    if (data.emb) {
        bytes += moshier_allocated_bytes(*data.emb);
    }
    if (data.moon) {
        bytes += moshier_allocated_bytes(*data.moon);
    }
    return bytes;
}

}  // namespace

bool make_moshier_planet_ephemeris_data(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    MoshierPlanetEvaluator evaluator,
    int objnum,
    const MoshierPlanetTable& table,
    const MoshierCorrectionSegment* corrections,
    size_t correction_count,
    MoshierPlanetEphemerisData** out
) noexcept {
    if (!out) {
        return false;
    }
    *out = 0;
    if (!finite_ordered_range(jd_tdb_start, jd_tdb_end)
        || jd_tdb_end <= jd_tdb_start
        || !valid_table(table)
        || (correction_count > 0 && !corrections)
        || (evaluator == MoshierPlanetEvaluator::G3Plan && (objnum <= 0 || objnum > MOSHIER_NARGS))) {
        return false;
    }

    MoshierPlanetEphemerisData* data = new (std::nothrow) MoshierPlanetEphemerisData();
    if (!data) {
        return false;
    }

    try {
        data->target_id = target_id;
        data->center_id = center_id;
        data->jd_tdb_start = jd_tdb_start;
        data->jd_tdb_end = jd_tdb_end;
        data->evaluator = evaluator;
        data->objnum = objnum;
        data->apply_de441_correction = correction_count > 0;
        data->maxargs = table.maxargs;
        std::copy(table.max_harmonic, table.max_harmonic + MOSHIER_NARGS, data->max_harmonic);
        data->max_power_of_t = table.max_power_of_t;
        data->distance_au = table.distance_au;
        data->timescale_days = table.timescale_days;
        data->trunclvl = table.trunclvl;
        data->arg_tbl.assign(table.arg_tbl, table.arg_tbl + table.arg_count);
        data->lon_tbl.assign(table.lon_tbl, table.lon_tbl + table.lon_count);
        data->lat_tbl.assign(table.lat_tbl, table.lat_tbl + table.lat_count);
        data->rad_tbl.assign(table.rad_tbl, table.rad_tbl + table.rad_count);
        if (correction_count > 0) {
            data->corrections.assign(corrections, corrections + correction_count);
        }
    } catch (...) {
        delete data;
        return false;
    }

    *out = data;
    return true;
}

bool make_moshier_moon_ephemeris_data(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    const MoshierMoonLRTable& moon_lr,
    const MoshierMoonLatTable& moon_lat,
    MoshierMoonEphemerisData** out
) noexcept {
    if (!out) {
        return false;
    }
    *out = 0;
    if (!finite_ordered_range(jd_tdb_start, jd_tdb_end)
        || jd_tdb_end <= jd_tdb_start
        || !valid_moon_lr_table(moon_lr)
        || !valid_moon_lat_table(moon_lat)) {
        return false;
    }

    MoshierMoonEphemerisData* data = new (std::nothrow) MoshierMoonEphemerisData();
    if (!data) {
        return false;
    }

    try {
        data->target_id = target_id;
        data->center_id = center_id;
        data->jd_tdb_start = jd_tdb_start;
        data->jd_tdb_end = jd_tdb_end;
        data->lr_maxargs = moon_lr.maxargs;
        std::copy(moon_lr.max_harmonic, moon_lr.max_harmonic + MOSHIER_NARGS, data->lr_max_harmonic);
        data->lr_max_power_of_t = moon_lr.max_power_of_t;
        data->lr_distance_au = moon_lr.distance_au;
        data->lr_timescale_days = moon_lr.timescale_days;
        data->lr_trunclvl = moon_lr.trunclvl;
        data->lat_maxargs = moon_lat.maxargs;
        std::copy(moon_lat.max_harmonic, moon_lat.max_harmonic + MOSHIER_NARGS, data->lat_max_harmonic);
        data->lat_max_power_of_t = moon_lat.max_power_of_t;
        data->lat_distance_au = moon_lat.distance_au;
        data->lat_timescale_days = moon_lat.timescale_days;
        data->lat_trunclvl = moon_lat.trunclvl;
        data->lr_arg_tbl.assign(moon_lr.arg_tbl, moon_lr.arg_tbl + moon_lr.arg_count);
        data->lr_lon_tbl.assign(moon_lr.lon_tbl, moon_lr.lon_tbl + moon_lr.lon_count);
        data->lr_rad_tbl.assign(moon_lr.rad_tbl, moon_lr.rad_tbl + moon_lr.rad_count);
        data->lat_arg_tbl.assign(moon_lat.arg_tbl, moon_lat.arg_tbl + moon_lat.arg_count);
        data->lat_lon_tbl.assign(moon_lat.lon_tbl, moon_lat.lon_tbl + moon_lat.lon_count);
    } catch (...) {
        delete data;
        return false;
    }

    *out = data;
    return true;
}

bool calc_moshier_planet_position_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    CartesianState state;
    if (!calc_moshier_planet_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.position_au;
    return true;
}

bool calc_moshier_planet_velocity_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    CartesianState state;
    if (!calc_moshier_planet_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.velocity_au_per_day;
    return true;
}

typedef bool (*MoshierPositionEvalFn)(double jd_tdb, const void* data, Vector3* out);

bool finite_difference_position_acceleration(
    double jd_tdb,
    double jd_tdb_start,
    double jd_tdb_end,
    double step_days,
    const void* data,
    MoshierPositionEvalFn eval_position_fn,
    Vector3* out
) noexcept {
    if (!out
        || !data
        || !eval_position_fn
        || !std::isfinite(jd_tdb)
        || !finite_ordered_range(jd_tdb_start, jd_tdb_end)
        || jd_tdb < jd_tdb_start
        || jd_tdb > jd_tdb_end
        || step_days <= 0.0) {
        return false;
    }

    Vector3 p0;
    if (!eval_position_fn(jd_tdb, data, &p0)) {
        return false;
    }

    double dt = step_days;
    if (jd_tdb - dt >= jd_tdb_start && jd_tdb + dt <= jd_tdb_end) {
        Vector3 p_minus;
        Vector3 p_plus;
        if (!eval_position_fn(jd_tdb - dt, data, &p_minus)
            || !eval_position_fn(jd_tdb + dt, data, &p_plus)) {
            return false;
        }
        const double scale = 1.0 / (dt * dt);
        out->x = (p_plus.x - 2.0 * p0.x + p_minus.x) * scale;
        out->y = (p_plus.y - 2.0 * p0.y + p_minus.y) * scale;
        out->z = (p_plus.z - 2.0 * p0.z + p_minus.z) * scale;
        return true;
    }

    if (jd_tdb_end - jd_tdb > 0.0 && jd_tdb - jd_tdb_start <= jd_tdb_end - jd_tdb) {
        dt = std::min(step_days, (jd_tdb_end - jd_tdb) * 0.5);
        Vector3 p1;
        Vector3 p2;
        if (dt <= 0.0
            || !eval_position_fn(jd_tdb + dt, data, &p1)
            || !eval_position_fn(jd_tdb + 2.0 * dt, data, &p2)) {
            return false;
        }
        const double scale = 1.0 / (dt * dt);
        out->x = (p0.x - 2.0 * p1.x + p2.x) * scale;
        out->y = (p0.y - 2.0 * p1.y + p2.y) * scale;
        out->z = (p0.z - 2.0 * p1.z + p2.z) * scale;
        return true;
    }

    if (jd_tdb - jd_tdb_start > 0.0) {
        dt = std::min(step_days, (jd_tdb - jd_tdb_start) * 0.5);
        Vector3 p1;
        Vector3 p2;
        if (dt <= 0.0
            || !eval_position_fn(jd_tdb - dt, data, &p1)
            || !eval_position_fn(jd_tdb - 2.0 * dt, data, &p2)) {
            return false;
        }
        const double scale = 1.0 / (dt * dt);
        out->x = (p0.x - 2.0 * p1.x + p2.x) * scale;
        out->y = (p0.y - 2.0 * p1.y + p2.y) * scale;
        out->z = (p0.z - 2.0 * p1.z + p2.z) * scale;
        return true;
    }

    return false;
}

bool eval_moshier_planet_position_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    const MoshierPlanetEphemerisData* planet = static_cast<const MoshierPlanetEphemerisData*>(data);
    return planet && out && eval_position(jd_tdb, *planet, out);
}

bool eval_moshier_moon_position_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    const MoshierMoonEphemerisData* moon = static_cast<const MoshierMoonEphemerisData*>(data);
    return moon && out && eval_moon_position(jd_tdb, *moon, out);
}

bool eval_moshier_earth_body_position_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    const MoshierEarthBodyEphemerisData* earth = static_cast<const MoshierEarthBodyEphemerisData*>(data);
    if (!earth || !earth->emb || !earth->moon || !out) {
        return false;
    }
    Vector3 emb_position;
    Vector3 moon_position;
    if (!eval_position(jd_tdb, *earth->emb, &emb_position)
        || !eval_moon_position(jd_tdb, *earth->moon, &moon_position)) {
        return false;
    }
    const double factor = 1.0 / (1.0 + DE441_EMB_SPLIT_EARTH_MOON_MASS_RATIO);
    out->x = emb_position.x - moon_position.x * factor;
    out->y = emb_position.y - moon_position.y * factor;
    out->z = emb_position.z - moon_position.z * factor;
    return true;
}

bool calc_moshier_planet_acceleration_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    const MoshierPlanetEphemerisData* planet = static_cast<const MoshierPlanetEphemerisData*>(data);
    return planet
        && finite_difference_position_acceleration(
            jd_tdb,
            planet->jd_tdb_start,
            planet->jd_tdb_end,
            VELOCITY_STEP_DAYS,
            data,
            eval_moshier_planet_position_void,
            out);
}

bool calc_moshier_moon_position_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    CartesianState state;
    if (!calc_moshier_moon_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.position_au;
    return true;
}

bool calc_moshier_moon_velocity_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    CartesianState state;
    if (!calc_moshier_moon_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.velocity_au_per_day;
    return true;
}

bool calc_moshier_moon_acceleration_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    const MoshierMoonEphemerisData* moon = static_cast<const MoshierMoonEphemerisData*>(data);
    return moon
        && finite_difference_position_acceleration(
            jd_tdb,
            moon->jd_tdb_start,
            moon->jd_tdb_end,
            MOON_VELOCITY_STEP_DAYS,
            data,
            eval_moshier_moon_position_void,
            out);
}

bool calc_moshier_earth_body_position_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    CartesianState state;
    if (!calc_moshier_earth_body_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.position_au;
    return true;
}

bool calc_moshier_earth_body_velocity_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    CartesianState state;
    if (!calc_moshier_earth_body_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.velocity_au_per_day;
    return true;
}

bool calc_moshier_earth_body_acceleration_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    const MoshierEarthBodyEphemerisData* earth = static_cast<const MoshierEarthBodyEphemerisData*>(data);
    return earth
        && finite_difference_position_acceleration(
            jd_tdb,
            earth->jd_tdb_start,
            earth->jd_tdb_end,
            MOON_VELOCITY_STEP_DAYS,
            data,
            eval_moshier_earth_body_position_void,
            out);
}

bool compile_moshier_planet_ephemeris_block(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    MoshierPlanetEvaluator evaluator,
    int objnum,
    const MoshierPlanetTable& table,
    const MoshierCorrectionSegment* corrections,
    size_t correction_count,
    StorageEphemerisBlock* out
) noexcept {
    if (!out) {
        return false;
    }
    *out = StorageEphemerisBlock();

    MoshierPlanetEphemerisData* data = 0;
    if (!make_moshier_planet_ephemeris_data(
            target_id,
            center_id,
            jd_tdb_start,
            jd_tdb_end,
            evaluator,
            objnum,
            table,
            corrections,
            correction_count,
            &data)) {
        return false;
    }

    out->cache_id = 0;
    out->format = EphemerisBlockFormat::Moshier;
    out->position = calc_moshier_planet_position_void;
    out->velocity = calc_moshier_planet_velocity_void;
    out->acceleration = calc_moshier_planet_acceleration_void;
    out->destroy_element = moshier_planet_ephemeris_data_destroy_void;
    out->data_vector.push_back(data);
    out->total_bytes = moshier_allocated_bytes(*data);
    return true;
}

bool compile_moshier_moon_ephemeris_block(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    const MoshierMoonLRTable& moon_lr,
    const MoshierMoonLatTable& moon_lat,
    StorageEphemerisBlock* out
) noexcept {
    if (!out) {
        return false;
    }
    *out = StorageEphemerisBlock();

    MoshierMoonEphemerisData* data = 0;
    if (!make_moshier_moon_ephemeris_data(
            target_id,
            center_id,
            jd_tdb_start,
            jd_tdb_end,
            moon_lr,
            moon_lat,
            &data)) {
        return false;
    }

    out->cache_id = 0;
    out->format = EphemerisBlockFormat::Moshier;
    out->position = calc_moshier_moon_position_void;
    out->velocity = calc_moshier_moon_velocity_void;
    out->acceleration = calc_moshier_moon_acceleration_void;
    out->destroy_element = moshier_moon_ephemeris_data_destroy_void;
    out->data_vector.push_back(data);
    out->total_bytes = moshier_allocated_bytes(*data);
    return true;
}

bool compile_moshier_earth_body_ephemeris_block(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    const MoshierPlanetTable& emb_table,
    const MoshierCorrectionSegment* emb_corrections,
    size_t emb_correction_count,
    const MoshierMoonLRTable& moon_lr,
    const MoshierMoonLatTable& moon_lat,
    StorageEphemerisBlock* out
) noexcept {
    if (!out) {
        return false;
    }
    *out = StorageEphemerisBlock();

    MoshierEarthBodyEphemerisData* data = new (std::nothrow) MoshierEarthBodyEphemerisData();
    if (!data) {
        return false;
    }
    data->target_id = target_id;
    data->center_id = center_id;
    data->jd_tdb_start = jd_tdb_start;
    data->jd_tdb_end = jd_tdb_end;
    data->emb = 0;
    data->moon = 0;

    if (!make_moshier_planet_ephemeris_data(
            3,
            center_id,
            jd_tdb_start,
            jd_tdb_end,
            MoshierPlanetEvaluator::G3Plan,
            3,
            emb_table,
            emb_corrections,
            emb_correction_count,
            &data->emb)
        || !make_moshier_moon_ephemeris_data(
            301,
            399,
            jd_tdb_start,
            jd_tdb_end,
            moon_lr,
            moon_lat,
            &data->moon)) {
        moshier_earth_body_ephemeris_data_destroy(data);
        return false;
    }

    out->cache_id = 0;
    out->format = EphemerisBlockFormat::Moshier;
    out->position = calc_moshier_earth_body_position_void;
    out->velocity = calc_moshier_earth_body_velocity_void;
    out->acceleration = calc_moshier_earth_body_acceleration_void;
    out->destroy_element = moshier_earth_body_ephemeris_data_destroy_void;
    out->data_vector.push_back(data);
    out->total_bytes = moshier_allocated_bytes(*data);
    return true;
}

bool calc_moshier_planet_state(
    double jd_tdb,
    const MoshierPlanetEphemerisData* data,
    CartesianState* out
) noexcept {
    if (!data || !out || !std::isfinite(jd_tdb)) {
        return false;
    }

    *out = CartesianState();
    if (!eval_position(jd_tdb, *data, &out->position_au)) {
        return false;
    }

    double dt = VELOCITY_STEP_DAYS;
    if (jd_tdb - dt < data->jd_tdb_start) {
        dt = std::min(dt, data->jd_tdb_end - jd_tdb);
        Vector3 p2;
        if (dt <= 0.0 || !eval_position(jd_tdb + dt, *data, &p2)) {
            return true;
        }
        out->velocity_au_per_day.x = (p2.x - out->position_au.x) / dt;
        out->velocity_au_per_day.y = (p2.y - out->position_au.y) / dt;
        out->velocity_au_per_day.z = (p2.z - out->position_au.z) / dt;
        calc_moshier_planet_acceleration_void(jd_tdb, data, &out->acceleration_au_per_day2);
        return true;
    }
    if (jd_tdb + dt > data->jd_tdb_end) {
        dt = std::min(dt, jd_tdb - data->jd_tdb_start);
        Vector3 p1;
        if (dt <= 0.0 || !eval_position(jd_tdb - dt, *data, &p1)) {
            return true;
        }
        out->velocity_au_per_day.x = (out->position_au.x - p1.x) / dt;
        out->velocity_au_per_day.y = (out->position_au.y - p1.y) / dt;
        out->velocity_au_per_day.z = (out->position_au.z - p1.z) / dt;
        calc_moshier_planet_acceleration_void(jd_tdb, data, &out->acceleration_au_per_day2);
        return true;
    }

    Vector3 p1;
    Vector3 p2;
    if (!eval_position(jd_tdb - dt, *data, &p1) || !eval_position(jd_tdb + dt, *data, &p2)) {
        return true;
    }
    const double inv_2dt = 1.0 / (2.0 * dt);
    out->velocity_au_per_day.x = (p2.x - p1.x) * inv_2dt;
    out->velocity_au_per_day.y = (p2.y - p1.y) * inv_2dt;
    out->velocity_au_per_day.z = (p2.z - p1.z) * inv_2dt;
    calc_moshier_planet_acceleration_void(jd_tdb, data, &out->acceleration_au_per_day2);
    return true;
}

bool calc_moshier_moon_state(
    double jd_tdb,
    const MoshierMoonEphemerisData* data,
    CartesianState* out
) noexcept {
    if (!data || !out || !std::isfinite(jd_tdb)) {
        return false;
    }

    *out = CartesianState();
    if (!eval_moon_position(jd_tdb, *data, &out->position_au)) {
        return false;
    }

    double dt = MOON_VELOCITY_STEP_DAYS;
    if (jd_tdb - dt < data->jd_tdb_start) {
        dt = std::min(dt, data->jd_tdb_end - jd_tdb);
        Vector3 p2;
        if (dt <= 0.0 || !eval_moon_position(jd_tdb + dt, *data, &p2)) {
            return true;
        }
        out->velocity_au_per_day.x = (p2.x - out->position_au.x) / dt;
        out->velocity_au_per_day.y = (p2.y - out->position_au.y) / dt;
        out->velocity_au_per_day.z = (p2.z - out->position_au.z) / dt;
        calc_moshier_moon_acceleration_void(jd_tdb, data, &out->acceleration_au_per_day2);
        return true;
    }
    if (jd_tdb + dt > data->jd_tdb_end) {
        dt = std::min(dt, jd_tdb - data->jd_tdb_start);
        Vector3 p1;
        if (dt <= 0.0 || !eval_moon_position(jd_tdb - dt, *data, &p1)) {
            return true;
        }
        out->velocity_au_per_day.x = (out->position_au.x - p1.x) / dt;
        out->velocity_au_per_day.y = (out->position_au.y - p1.y) / dt;
        out->velocity_au_per_day.z = (out->position_au.z - p1.z) / dt;
        calc_moshier_moon_acceleration_void(jd_tdb, data, &out->acceleration_au_per_day2);
        return true;
    }

    Vector3 p1;
    Vector3 p2;
    if (!eval_moon_position(jd_tdb - dt, *data, &p1) || !eval_moon_position(jd_tdb + dt, *data, &p2)) {
        return true;
    }
    const double inv_2dt = 1.0 / (2.0 * dt);
    out->velocity_au_per_day.x = (p2.x - p1.x) * inv_2dt;
    out->velocity_au_per_day.y = (p2.y - p1.y) * inv_2dt;
    out->velocity_au_per_day.z = (p2.z - p1.z) * inv_2dt;
    calc_moshier_moon_acceleration_void(jd_tdb, data, &out->acceleration_au_per_day2);
    return true;
}

bool calc_moshier_earth_body_state(
    double jd_tdb,
    const MoshierEarthBodyEphemerisData* data,
    CartesianState* out
) noexcept {
    if (!data || !data->emb || !data->moon || !out || !std::isfinite(jd_tdb)) {
        return false;
    }
    if (jd_tdb < data->jd_tdb_start || jd_tdb > data->jd_tdb_end) {
        return false;
    }

    CartesianState emb;
    CartesianState moon;
    if (!calc_moshier_planet_state(jd_tdb, data->emb, &emb)
        || !calc_moshier_moon_state(jd_tdb, data->moon, &moon)) {
        return false;
    }

    const double factor = 1.0 / (1.0 + DE441_EMB_SPLIT_EARTH_MOON_MASS_RATIO);
    *out = CartesianState();
    out->position_au.x = emb.position_au.x - moon.position_au.x * factor;
    out->position_au.y = emb.position_au.y - moon.position_au.y * factor;
    out->position_au.z = emb.position_au.z - moon.position_au.z * factor;
    out->velocity_au_per_day.x = emb.velocity_au_per_day.x - moon.velocity_au_per_day.x * factor;
    out->velocity_au_per_day.y = emb.velocity_au_per_day.y - moon.velocity_au_per_day.y * factor;
    out->velocity_au_per_day.z = emb.velocity_au_per_day.z - moon.velocity_au_per_day.z * factor;
    out->acceleration_au_per_day2.x = emb.acceleration_au_per_day2.x - moon.acceleration_au_per_day2.x * factor;
    out->acceleration_au_per_day2.y = emb.acceleration_au_per_day2.y - moon.acceleration_au_per_day2.y * factor;
    out->acceleration_au_per_day2.z = emb.acceleration_au_per_day2.z - moon.acceleration_au_per_day2.z * factor;
    return true;
}

bool calc_moshier_planet_state_void(double jd_tdb, const void* data, CartesianState* out) noexcept {
    return calc_moshier_planet_state(jd_tdb, static_cast<const MoshierPlanetEphemerisData*>(data), out);
}

bool calc_moshier_moon_state_void(double jd_tdb, const void* data, CartesianState* out) noexcept {
    return calc_moshier_moon_state(jd_tdb, static_cast<const MoshierMoonEphemerisData*>(data), out);
}

bool calc_moshier_earth_body_state_void(double jd_tdb, const void* data, CartesianState* out) noexcept {
    return calc_moshier_earth_body_state(jd_tdb, static_cast<const MoshierEarthBodyEphemerisData*>(data), out);
}

void moshier_planet_ephemeris_data_destroy(MoshierPlanetEphemerisData* data) noexcept {
    delete data;
}

void moshier_planet_ephemeris_data_destroy_void(void* data) noexcept {
    moshier_planet_ephemeris_data_destroy(static_cast<MoshierPlanetEphemerisData*>(data));
}

void moshier_moon_ephemeris_data_destroy(MoshierMoonEphemerisData* data) noexcept {
    delete data;
}

void moshier_moon_ephemeris_data_destroy_void(void* data) noexcept {
    moshier_moon_ephemeris_data_destroy(static_cast<MoshierMoonEphemerisData*>(data));
}

void moshier_earth_body_ephemeris_data_destroy(MoshierEarthBodyEphemerisData* data) noexcept {
    if (!data) {
        return;
    }
    moshier_planet_ephemeris_data_destroy(data->emb);
    moshier_moon_ephemeris_data_destroy(data->moon);
    delete data;
}

void moshier_earth_body_ephemeris_data_destroy_void(void* data) noexcept {
    moshier_earth_body_ephemeris_data_destroy(static_cast<MoshierEarthBodyEphemerisData*>(data));
}

}  // namespace internal
}  // namespace taiyin
