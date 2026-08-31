#include "taiyin/internal/long_range_analytic.h"

#include "taiyin/body_id.h"
#include "taiyin/coordinates.h"
#include "taiyin/physical_constants.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>

namespace taiyin {
namespace internal {
namespace {

constexpr double kEpochJd = 2451545.0;
constexpr double kScaleDays = 365250.0;
constexpr double kCoverageStartJd = -470455.0;  // astronomical year -6000
constexpr double kCoverageEndJd = 5373545.0;    // astronomical year +10000
constexpr size_t kMaximumDegree = 24;
constexpr size_t kMoonArgumentCount = 763;

struct LongRangeLbrTerm {
    double amplitude;
    double phase;
    double frequency;
};

struct LongRangeLbrGroup {
    uint32_t term_offset;
    uint32_t term_count;
    uint8_t coordinate;
    uint8_t degree;
};

struct LongRangeLbrModel {
    int body_id;
    uint32_t group_offset;
    uint16_t group_count;
    uint8_t maximum_degree;
};

struct LongRangeMoonTerm {
    double sine_amplitude;
    double cosine_amplitude;
    uint16_t argument_index;
};

struct LongRangeMoonGroup {
    uint32_t term_offset;
    uint32_t term_count;
    uint8_t coordinate;
    uint8_t degree;
};

#include "internal/long_range_lbr_coefficients.inc"

struct Jet2 {
    double value;
    double first;
    double second;

    Jet2() noexcept : value(0.0), first(0.0), second(0.0) {}
    explicit Jet2(double value_in) noexcept
        : value(value_in), first(0.0), second(0.0) {}
    Jet2(double value_in, double first_in, double second_in) noexcept
        : value(value_in), first(first_in), second(second_in) {}
};

struct JetVector3 {
    Jet2 x;
    Jet2 y;
    Jet2 z;
};

struct LongRangeAnalyticData {
    int target_id;
    int center_id;
    double jd_tdb_start;
    double jd_tdb_end;
};

Jet2 operator+(const Jet2& left, const Jet2& right) noexcept {
    return Jet2(
        left.value + right.value,
        left.first + right.first,
        left.second + right.second);
}

Jet2 operator-(const Jet2& left, const Jet2& right) noexcept {
    return Jet2(
        left.value - right.value,
        left.first - right.first,
        left.second - right.second);
}

Jet2 operator-(const Jet2& value) noexcept {
    return Jet2(-value.value, -value.first, -value.second);
}

Jet2 operator*(const Jet2& left, const Jet2& right) noexcept {
    return Jet2(
        left.value * right.value,
        left.first * right.value + left.value * right.first,
        left.second * right.value
            + 2.0 * left.first * right.first
            + left.value * right.second);
}

Jet2 operator*(const Jet2& value, double scale) noexcept {
    return Jet2(value.value * scale, value.first * scale, value.second * scale);
}

Jet2 operator*(double scale, const Jet2& value) noexcept {
    return value * scale;
}

Jet2 operator/(const Jet2& value, double divisor) noexcept {
    return value * (1.0 / divisor);
}

Jet2& operator+=(Jet2& left, const Jet2& right) noexcept {
    left = left + right;
    return left;
}

Jet2 jet_sin(const Jet2& value) noexcept {
    const double sine = std::sin(value.value);
    const double cosine = std::cos(value.value);
    return Jet2(
        sine,
        cosine * value.first,
        cosine * value.second - sine * value.first * value.first);
}

Jet2 jet_cos(const Jet2& value) noexcept {
    const double sine = std::sin(value.value);
    const double cosine = std::cos(value.value);
    return Jet2(
        cosine,
        -sine * value.first,
        -sine * value.second - cosine * value.first * value.first);
}

Jet2 jet_sqrt(const Jet2& value) noexcept {
    if (!(value.value > 0.0)) {
        return Jet2(NAN, NAN, NAN);
    }
    const double root = std::sqrt(value.value);
    return Jet2(
        root,
        value.first / (2.0 * root),
        value.second / (2.0 * root)
            - value.first * value.first / (4.0 * root * root * root));
}

Jet2 eval_polynomial(
    const double* coefficients,
    size_t count,
    const Jet2& x
) noexcept {
    Jet2 value;
    for (size_t index = count; index > 0; --index) {
        value = value * x + Jet2(coefficients[index - 1]);
    }
    return value;
}

const LongRangeLbrModel* find_model(int body_id) noexcept {
    for (size_t index = 0;
         index < sizeof(kLongRangeLbrModels) / sizeof(kLongRangeLbrModels[0]);
         ++index) {
        if (kLongRangeLbrModels[index].body_id == body_id) {
            return &kLongRangeLbrModels[index];
        }
    }
    return 0;
}

bool eval_lbr_model_ecliptic(
    const LongRangeLbrModel& model,
    const SplitJulianDate& jd_tdb,
    double basis_scale_days,
    bool legendre_basis,
    JetVector3* out
) noexcept {
    SplitJulianDate start;
    SplitJulianDate end;
    SplitJulianDate epoch;
    if (!out || model.maximum_degree > kMaximumDegree
        || !split_julian_date_from_double(kCoverageStartJd, &start)
        || !split_julian_date_from_double(kCoverageEndJd, &end)
        || !split_julian_date_from_double(kEpochJd, &epoch)
        || jd_tdb < start || jd_tdb > end) {
        return false;
    }

    const Jet2 phase_time(
        days_between_split_jd(epoch, jd_tdb) / kScaleDays,
        1.0 / kScaleDays,
        0.0);
    const Jet2 basis_time(
        days_between_split_jd(epoch, jd_tdb) / basis_scale_days,
        1.0 / basis_scale_days,
        0.0);
    Jet2 basis[kMaximumDegree + 1];
    basis[0] = Jet2(1.0);
    if (model.maximum_degree > 0) {
        basis[1] = basis_time;
    }
    for (size_t degree = 2; degree <= model.maximum_degree; ++degree) {
        if (legendre_basis) {
            basis[degree] = (
                basis_time * basis[degree - 1] * static_cast<double>(2 * degree - 1)
                    - basis[degree - 2] * static_cast<double>(degree - 1))
                / static_cast<double>(degree);
        } else {
            basis[degree] = basis[degree - 1] * basis_time;
        }
    }

    Jet2 coordinates[3];
    for (size_t group_index = 0;
         group_index < model.group_count;
         ++group_index) {
        const LongRangeLbrGroup& group =
            kLongRangeLbrGroups[model.group_offset + group_index];
        if (group.coordinate >= 3 || group.degree > model.maximum_degree) {
            return false;
        }
        Jet2 subtotal;
        for (size_t term_index = 0; term_index < group.term_count; ++term_index) {
            const LongRangeLbrTerm& term =
                kLongRangeLbrTerms[group.term_offset + term_index];
            const Jet2 argument = phase_time * term.frequency + Jet2(term.phase);
            subtotal += jet_cos(argument) * term.amplitude;
        }
        coordinates[group.coordinate] += basis[group.degree] * subtotal;
    }

    const Jet2 longitude = coordinates[0];
    const Jet2 latitude = coordinates[1];
    const Jet2 radius = coordinates[2];
    const Jet2 cosine_latitude = jet_cos(latitude);
    JetVector3 native;
    native.x = radius * cosine_latitude * jet_cos(longitude);
    native.y = radius * cosine_latitude * jet_sin(longitude);
    native.z = radius * jet_sin(latitude);

    JetVector3 j2000;
    j2000.x = native.x * kLongRangeTheoryToJ2000[0][0]
        + native.y * kLongRangeTheoryToJ2000[0][1]
        + native.z * kLongRangeTheoryToJ2000[0][2];
    j2000.y = native.x * kLongRangeTheoryToJ2000[1][0]
        + native.y * kLongRangeTheoryToJ2000[1][1]
        + native.z * kLongRangeTheoryToJ2000[1][2];
    j2000.z = native.x * kLongRangeTheoryToJ2000[2][0]
        + native.y * kLongRangeTheoryToJ2000[2][1]
        + native.z * kLongRangeTheoryToJ2000[2][2];

    *out = j2000;
    return true;
}

bool eval_model_ecliptic(
    const LongRangeLbrModel& model,
    const SplitJulianDate& jd_tdb,
    JetVector3* out
) noexcept {
    return eval_lbr_model_ecliptic(
        model, jd_tdb, kScaleDays, false, out);
}

const Matrix3x3& j2000_ecliptic_to_icrf_matrix() noexcept {
    // The generated theory-frame matrix ends in Taiyin's fixed mean-J2000
    // ecliptic frame.  Its exact inverse to ICRF includes the Vondrak 2011
    // J2000 precession/frame-bias matrix as well as the IAU 2006 obliquity.
    // A plain obliquity rotation is close, but its residual frame bias grows
    // into a visible Cartesian error for the outer planets.
    static const Matrix3x3 matrix = []() noexcept {
        SplitJulianDate j2000;
        Matrix3x3 precession = matrix3x3_identity();
        if (!split_julian_date_from_double(kEpochJd, &j2000)
            || !vondrak2011_precession_matrix(j2000, &precession)) {
            return matrix3x3_identity();
        }
        const Matrix3x3 icrf_to_mean_j2000 = matrix3x3_multiply(
            rotation_x_matrix(mean_obliquity_iau2006(j2000)),
            precession);
        return matrix3x3_transpose(icrf_to_mean_j2000);
    }();
    return matrix;
}

JetVector3 ecliptic_to_icrf(const JetVector3& ecliptic) noexcept {
    const Matrix3x3& matrix = j2000_ecliptic_to_icrf_matrix();
    JetVector3 result;
    result.x = ecliptic.x * matrix.m[0][0]
        + ecliptic.y * matrix.m[0][1]
        + ecliptic.z * matrix.m[0][2];
    result.y = ecliptic.x * matrix.m[1][0]
        + ecliptic.y * matrix.m[1][1]
        + ecliptic.z * matrix.m[1][2];
    result.z = ecliptic.x * matrix.m[2][0]
        + ecliptic.y * matrix.m[2][1]
        + ecliptic.z * matrix.m[2][2];
    return result;
}

bool eval_moon_ecliptic(
    const SplitJulianDate& jd_tdb,
    JetVector3* out
) noexcept {
    SplitJulianDate start;
    SplitJulianDate end;
    SplitJulianDate epoch;
    if (!out || sizeof(kLongRangeMoonArguments)
            / sizeof(kLongRangeMoonArguments[0]) != kMoonArgumentCount
        || !split_julian_date_from_double(kCoverageStartJd, &start)
        || !split_julian_date_from_double(kCoverageEndJd, &end)
        || !split_julian_date_from_double(kEpochJd, &epoch)
        || jd_tdb < start || jd_tdb > end) {
        return false;
    }
    const Jet2 x(
        days_between_split_jd(epoch, jd_tdb) / kLongRangeMoonScaleDays,
        1.0 / kLongRangeMoonScaleDays,
        0.0);
    Jet2 powers[7];
    powers[0] = Jet2(1.0);
    for (size_t degree = 1; degree < 7; ++degree) {
        powers[degree] = powers[degree - 1] * x;
    }
    Jet2 arguments[kMoonArgumentCount];
    bool argument_ready[kMoonArgumentCount] = {};
    Jet2 coordinates[3];
    for (size_t group_index = 0;
         group_index < sizeof(kLongRangeMoonGroups)
            / sizeof(kLongRangeMoonGroups[0]);
         ++group_index) {
        const LongRangeMoonGroup& group = kLongRangeMoonGroups[group_index];
        if (group.coordinate >= 3 || group.degree >= 7) {
            return false;
        }
        Jet2 subtotal;
        for (size_t term_index = 0; term_index < group.term_count; ++term_index) {
            const LongRangeMoonTerm& term =
                kLongRangeMoonTerms[group.term_offset + term_index];
            if (term.argument_index >= kMoonArgumentCount) {
                return false;
            }
            if (!argument_ready[term.argument_index]) {
                const double* p = kLongRangeMoonArguments[term.argument_index];
                arguments[term.argument_index] =
                    eval_polynomial(p, 8, x) * x;
                argument_ready[term.argument_index] = true;
            }
            const Jet2& argument = arguments[term.argument_index];
            subtotal += jet_sin(argument) * term.sine_amplitude
                + jet_cos(argument) * term.cosine_amplitude;
        }
        coordinates[group.coordinate] += powers[group.degree] * subtotal;
    }
    coordinates[0] += eval_polynomial(
        kLongRangeMoonMeanLongitude,
        sizeof(kLongRangeMoonMeanLongitude)
            / sizeof(kLongRangeMoonMeanLongitude[0]),
        x);

    const Jet2 longitude = coordinates[0];
    const Jet2 latitude = coordinates[1];
    const Jet2 radius = coordinates[2] / TAIYIN_AU_KM;
    const Jet2 cosine_latitude = jet_cos(latitude);
    JetVector3 native;
    native.x = radius * cosine_latitude * jet_cos(longitude);
    native.y = radius * cosine_latitude * jet_sin(longitude);
    native.z = radius * jet_sin(latitude);

    const Jet2 centuries = x * 80.0;
    const Jet2 p = eval_polynomial(
        kLongRangeMoonPrecessionP,
        sizeof(kLongRangeMoonPrecessionP)
            / sizeof(kLongRangeMoonPrecessionP[0]),
        centuries) * centuries;
    const Jet2 q = eval_polynomial(
        kLongRangeMoonPrecessionQ,
        sizeof(kLongRangeMoonPrecessionQ)
            / sizeof(kLongRangeMoonPrecessionQ[0]),
        centuries) * centuries;
    const Jet2 r = jet_sqrt(Jet2(1.0) - p * p - q * q) * 2.0;
    JetVector3 j2000;
    j2000.x = native.x * (Jet2(1.0) - p * p * 2.0)
        + native.y * (p * q * 2.0) + native.z * (p * r);
    j2000.y = native.x * (p * q * 2.0)
        + native.y * (Jet2(1.0) - q * q * 2.0) - native.z * (q * r);
    j2000.z = -native.x * (p * r) + native.y * (q * r)
        + native.z * (Jet2(1.0) - p * p * 2.0 - q * q * 2.0);
    *out = j2000;
    return true;
}

Jet2 eval_chebyshev(const double* coefficients, size_t count, const Jet2& x) noexcept {
    Jet2 b1;
    Jet2 b2;
    for (size_t index = count; index > 1; --index) {
        const Jet2 value = x * b1 * 2.0 - b2 + Jet2(coefficients[index - 1]);
        b2 = b1;
        b1 = value;
    }
    return x * b1 - b2 + Jet2(coefficients[0]);
}

bool eval_pluto_near_ecliptic(
    const SplitJulianDate& jd_tdb,
    JetVector3* out
) noexcept {
    SplitJulianDate epoch;
    SplitJulianDate j2000;
    if (!out
        || !split_julian_date_from_double(kLongRangePlutoNearEpochJd, &epoch)
        || !split_julian_date_from_double(kEpochJd, &j2000)) {
        return false;
    }
    const Jet2 x(
        days_between_split_jd(epoch, jd_tdb) / kLongRangePlutoNearScaleDays,
        1.0 / kLongRangePlutoNearScaleDays,
        0.0);
    Jet2 coordinates[3];
    for (size_t channel = 0; channel < 3; ++channel) {
        coordinates[channel] = eval_chebyshev(
            kLongRangePlutoNearCoefficients[channel], 385, x);
    }
    coordinates[0] += Jet2(kLongRangePlutoNearPhase)
        + Jet2(
            days_between_split_jd(j2000, jd_tdb) / kScaleDays,
            1.0 / kScaleDays,
            0.0) * kLongRangePlutoNearMotion;
    const Jet2 cosine_latitude = jet_cos(coordinates[1]);
    JetVector3 native;
    native.x = coordinates[2] * cosine_latitude * jet_cos(coordinates[0]);
    native.y = coordinates[2] * cosine_latitude * jet_sin(coordinates[0]);
    native.z = coordinates[2] * jet_sin(coordinates[1]);
    JetVector3 j2000_state;
    j2000_state.x = native.x * kLongRangeTheoryToJ2000[0][0]
        + native.y * kLongRangeTheoryToJ2000[0][1]
        + native.z * kLongRangeTheoryToJ2000[0][2];
    j2000_state.y = native.x * kLongRangeTheoryToJ2000[1][0]
        + native.y * kLongRangeTheoryToJ2000[1][1]
        + native.z * kLongRangeTheoryToJ2000[1][2];
    j2000_state.z = native.x * kLongRangeTheoryToJ2000[2][0]
        + native.y * kLongRangeTheoryToJ2000[2][1]
        + native.z * kLongRangeTheoryToJ2000[2][2];
    *out = j2000_state;
    return true;
}

bool eval_pluto_ecliptic(
    const SplitJulianDate& jd_tdb,
    JetVector3* out
) noexcept {
    SplitJulianDate j2000;
    if (!out || !split_julian_date_from_double(kEpochJd, &j2000)) {
        return false;
    }
    const Jet2 year(
        2000.0 + days_between_split_jd(j2000, jd_tdb) / 365.25,
        1.0 / 365.25,
        0.0);
    if (year.value >= 1600.0 && year.value <= 2200.0) {
        return eval_pluto_near_ecliptic(jd_tdb, out);
    }
    JetVector3 far;
    if (!eval_lbr_model_ecliptic(
            kLongRangePlutoFallbackModel,
            jd_tdb,
            kLongRangeMoonScaleDays,
            true,
            &far)) {
        return false;
    }
    if (year.value <= 1590.0 || year.value >= 2210.0) {
        *out = far;
        return true;
    }
    JetVector3 near;
    if (!eval_pluto_near_ecliptic(jd_tdb, &near)) {
        return false;
    }
    const Jet2 blend = year.value < 1600.0
        ? (year - Jet2(1590.0)) / 10.0
        : (Jet2(2210.0) - year) / 10.0;
    const Jet2 weight = blend * blend * blend
        * (Jet2(10.0) - blend * 15.0 + blend * blend * 6.0);
    out->x = far.x + weight * (near.x - far.x);
    out->y = far.y + weight * (near.y - far.y);
    out->z = far.z + weight * (near.z - far.z);
    return true;
}

bool eval_route(
    int target_id,
    int center_id,
    const SplitJulianDate& jd_tdb,
    JetVector3* out
) noexcept {
    if (!out || !split_julian_date_is_finite(jd_tdb)) {
        return false;
    }
    JetVector3 ecliptic;
    if (target_id == TAIYIN_BODY_MOON && center_id == TAIYIN_BODY_EARTH) {
        if (!eval_moon_ecliptic(jd_tdb, &ecliptic)) {
            return false;
        }
        *out = ecliptic_to_icrf(ecliptic);
        return true;
    }
    if (target_id == TAIYIN_BODY_PLUTO_BARYCENTER
        && center_id == TAIYIN_BODY_SUN) {
        if (!eval_pluto_ecliptic(jd_tdb, &ecliptic)) {
            return false;
        }
        *out = ecliptic_to_icrf(ecliptic);
        return true;
    }
    if (center_id == TAIYIN_BODY_SUN) {
        const LongRangeLbrModel* model = find_model(target_id);
        if (!model || !eval_model_ecliptic(*model, jd_tdb, &ecliptic)) {
            return false;
        }
        *out = ecliptic_to_icrf(ecliptic);
        return true;
    }
    return false;
}

bool calc_state_void(
    const SplitJulianDate& jd_tdb,
    const void* data,
    CartesianState* out
) noexcept {
    const LongRangeAnalyticData* route =
        static_cast<const LongRangeAnalyticData*>(data);
    if (!route || !out || !split_julian_date_is_finite(jd_tdb)) {
        return false;
    }
    SplitJulianDate start;
    SplitJulianDate end;
    if (!split_julian_date_from_double(route->jd_tdb_start, &start)
        || !split_julian_date_from_double(route->jd_tdb_end, &end)
        || jd_tdb < start || jd_tdb > end) {
        return false;
    }
    JetVector3 state;
    if (!eval_route(route->target_id, route->center_id, jd_tdb, &state)) {
        return false;
    }
    out->position_au.x = state.x.value;
    out->position_au.y = state.y.value;
    out->position_au.z = state.z.value;
    out->velocity_au_per_day.x = state.x.first;
    out->velocity_au_per_day.y = state.y.first;
    out->velocity_au_per_day.z = state.z.first;
    out->acceleration_au_per_day2.x = state.x.second;
    out->acceleration_au_per_day2.y = state.y.second;
    out->acceleration_au_per_day2.z = state.z.second;
    return true;
}

bool copy_jet_state(const JetVector3& state, CartesianState* out) noexcept {
    if (!out) {
        return false;
    }
    *out = CartesianState();
    out->position_au = Vector3{state.x.value, state.y.value, state.z.value};
    out->velocity_au_per_day = Vector3{state.x.first, state.y.first, state.z.first};
    out->acceleration_au_per_day2 = Vector3{
        state.x.second, state.y.second, state.z.second};
    return std::isfinite(out->position_au.x)
        && std::isfinite(out->position_au.y)
        && std::isfinite(out->position_au.z)
        && std::isfinite(out->velocity_au_per_day.x)
        && std::isfinite(out->velocity_au_per_day.y)
        && std::isfinite(out->velocity_au_per_day.z)
        && std::isfinite(out->acceleration_au_per_day2.x)
        && std::isfinite(out->acceleration_au_per_day2.y)
        && std::isfinite(out->acceleration_au_per_day2.z);
}

bool calc_position_void(
    const SplitJulianDate& jd_tdb,
    const void* data,
    Vector3* out
) noexcept {
    CartesianState state;
    if (!out || !calc_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.position_au;
    return true;
}

bool calc_velocity_void(
    const SplitJulianDate& jd_tdb,
    const void* data,
    Vector3* out
) noexcept {
    CartesianState state;
    if (!out || !calc_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.velocity_au_per_day;
    return true;
}

bool calc_acceleration_void(
    const SplitJulianDate& jd_tdb,
    const void* data,
    Vector3* out
) noexcept {
    CartesianState state;
    if (!out || !calc_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.acceleration_au_per_day2;
    return true;
}

void destroy_data(void* data) noexcept {
    delete static_cast<LongRangeAnalyticData*>(data);
}

}  // namespace

const char* builtin_long_range_analytic_source_revision() noexcept {
    return kLongRangeLbrSourceCommit;
}

const char* builtin_long_range_analytic_coefficients_sha256() noexcept {
    return kLongRangeLbrArtifactSha256;
}

bool get_builtin_long_range_analytic_coverage(
    int target_id,
    int center_id,
    double* out_jd_tdb_start,
    double* out_jd_tdb_end
) noexcept {
    if (!out_jd_tdb_start || !out_jd_tdb_end) {
        return false;
    }
    const bool supported = (center_id == TAIYIN_BODY_SUN
            && (find_model(target_id) != 0
                || target_id == TAIYIN_BODY_PLUTO_BARYCENTER))
        || (target_id == TAIYIN_BODY_MOON
            && center_id == TAIYIN_BODY_EARTH);
    if (!supported) {
        return false;
    }
    *out_jd_tdb_start = kCoverageStartJd;
    *out_jd_tdb_end = kCoverageEndJd;
    return true;
}

bool eval_builtin_long_range_analytic_state(
    int target_id,
    int center_id,
    const SplitJulianDate& jd_tdb,
    CartesianState* out
) noexcept {
    LongRangeAnalyticData data;
    data.target_id = target_id;
    data.center_id = center_id;
    data.jd_tdb_start = kCoverageStartJd;
    data.jd_tdb_end = kCoverageEndJd;
    return calc_state_void(jd_tdb, &data, out);
}


bool eval_builtin_long_range_pluto_near_state(
    const SplitJulianDate& jd_tdb,
    CartesianState* out
) noexcept {
    if (!out || !split_julian_date_is_finite(jd_tdb)) {
        return false;
    }
    JetVector3 ecliptic;
    return eval_pluto_near_ecliptic(jd_tdb, &ecliptic)
        && copy_jet_state(ecliptic_to_icrf(ecliptic), out);
}

bool eval_builtin_long_range_pluto_fallback_state(
    const SplitJulianDate& jd_tdb,
    CartesianState* out
) noexcept {
    if (!out || !split_julian_date_is_finite(jd_tdb)) {
        return false;
    }
    JetVector3 ecliptic;
    return eval_lbr_model_ecliptic(
            kLongRangePlutoFallbackModel,
            jd_tdb,
            kLongRangeMoonScaleDays,
            true,
            &ecliptic)
        && copy_jet_state(ecliptic_to_icrf(ecliptic), out);
}

bool compile_builtin_long_range_analytic_ephemeris_block(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept {
    if (!out || !std::isfinite(jd_tdb_start) || !std::isfinite(jd_tdb_end)
        || jd_tdb_end <= jd_tdb_start) {
        return false;
    }
    *out = StorageEphemerisBlock();
    double coverage_start = 0.0;
    double coverage_end = 0.0;
    if (!get_builtin_long_range_analytic_coverage(
            target_id, center_id, &coverage_start, &coverage_end)
        || jd_tdb_start < coverage_start
        || jd_tdb_end > std::nextafter(
            coverage_end, std::numeric_limits<double>::infinity())) {
        return false;
    }
    LongRangeAnalyticData* data = new (std::nothrow) LongRangeAnalyticData();
    if (!data) {
        return false;
    }
    data->target_id = target_id;
    data->center_id = center_id;
    data->jd_tdb_start = jd_tdb_start;
    data->jd_tdb_end = std::min(jd_tdb_end, coverage_end);
    try {
        out->cache_id = 0;
        out->format = EphemerisBlockFormat::SemiAnalytic;
        out->position = calc_position_void;
        out->velocity = calc_velocity_void;
        out->acceleration = calc_acceleration_void;
        out->state = calc_state_void;
        out->destroy_element = destroy_data;
        out->data_vector.push_back(data);
        out->total_bytes = sizeof(*data);
    } catch (...) {
        delete data;
        *out = StorageEphemerisBlock();
        return false;
    }
    return true;
}

}  // namespace internal
}  // namespace taiyin
