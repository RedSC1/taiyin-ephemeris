#include "taiyin/internal/semi_analytic.h"

#include "taiyin/body_id.h"
#include "taiyin/physical_constants.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>

namespace taiyin {
namespace internal {
namespace {

constexpr double kJ2000 = 2451545.0;
constexpr double kDaysPerJulianCentury = 36525.0;
constexpr double kArcsecondsPerRadian = 206264.80624709636;
const double kJ2000ObliquityRad = TAIYIN_J2000_MEAN_OBLIQUITY_RAD;
constexpr size_t kMaximumPlanetAngles = 9;
constexpr size_t kMaximumPlanetHarmonics = 20;
constexpr size_t kMaximumCarrierHarmonics = 7;
constexpr size_t kMaximumChebyshevDegree = 5;

struct PlanetFactor {
    uint16_t angle_index;
    uint16_t harmonic;
    bool negative;
};

struct PlanetArgument {
    uint32_t factor_offset;
    uint16_t factor_count;
    int16_t carrier_multiplier;
    uint8_t amplitude_degree;
    uint32_t amplitude_offset;
};

struct PlanetModel {
    int body_id;
    double jd_start;
    double jd_end;
    double epoch_jd;
    double half_span_days;
    double radius_scale_km;
    uint8_t secular_degree;
    uint32_t secular_offset;
    uint32_t angle_offset;
    uint32_t angle_coefficient_offset;
    uint16_t angle_count;
    uint32_t max_harmonic_offset;
    uint32_t argument_offset;
    uint16_t argument_count;
    uint32_t carrier_offset;
    uint16_t carrier_coefficient_count;
    uint16_t carrier_max_harmonic;
};

struct Xl1Term {
    double amplitude;
    double phase[5];
};

struct TableRange {
    uint32_t offset;
    uint32_t count;
};

struct LunarCorrectionTerm {
    uint8_t order;
    double phase[5];
    uint32_t cosine_offset;
    uint8_t cosine_count;
    uint32_t sine_offset;
    uint8_t sine_count;
};

struct LunarCorrectionChannel {
    uint8_t source_coordinate;
    uint8_t modulation_degree;
    uint32_t secular_offset;
    uint8_t secular_count;
    uint32_t term_offset;
    uint16_t term_count;
};

struct LunarCorrectionModel {
    double epoch_jd;
    double half_span_days;
    uint8_t secular_degree;
    double radius_scale_km;
};

#include "internal/semi_analytic_coefficients.inc"

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

struct ComplexJet2 {
    Jet2 real;
    Jet2 imaginary;
};

struct JetVector3 {
    Jet2 x;
    Jet2 y;
    Jet2 z;
};

struct SemiAnalyticEphemerisData {
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

Jet2 jet_exp(const Jet2& value) noexcept {
    const double exponential = std::exp(value.value);
    return Jet2(
        exponential,
        exponential * value.first,
        exponential * (value.second + value.first * value.first));
}

Jet2 jet_sqrt(const Jet2& value) noexcept {
    const double root = std::sqrt(value.value);
    if (!(root > 0.0)) {
        return Jet2(root, 0.0, 0.0);
    }
    const double first = value.first / (2.0 * root);
    return Jet2(
        root,
        first,
        value.second / (2.0 * root) - first * first / root);
}

Jet2 jet_atan2(const Jet2& y, const Jet2& x) noexcept {
    const double radius2 = x.value * x.value + y.value * y.value;
    if (!(radius2 > 0.0)) {
        return Jet2(std::atan2(y.value, x.value), 0.0, 0.0);
    }
    const double numerator = x.value * y.first - y.value * x.first;
    const double radius2_first = 2.0 * (x.value * x.first + y.value * y.first);
    const double numerator_first = x.value * y.second - y.value * x.second;
    return Jet2(
        std::atan2(y.value, x.value),
        numerator / radius2,
        numerator_first / radius2
            - numerator * radius2_first / (radius2 * radius2));
}

Jet2 jet_power(const Jet2& value, unsigned exponent) noexcept {
    Jet2 result(1.0);
    for (unsigned i = 0; i < exponent; ++i) {
        result = result * value;
    }
    return result;
}

Jet2 eval_polynomial(const double* coefficients, size_t count, const Jet2& x) noexcept {
    if (!coefficients || count == 0) {
        return Jet2();
    }
    Jet2 result(coefficients[count - 1]);
    for (size_t i = count - 1; i > 0; --i) {
        result = result * x + Jet2(coefficients[i - 1]);
    }
    return result;
}

ComplexJet2 complex_multiply(
    const ComplexJet2& left,
    const ComplexJet2& right
) noexcept {
    ComplexJet2 result;
    result.real = left.real * right.real - left.imaginary * right.imaginary;
    result.imaginary = left.real * right.imaginary + left.imaginary * right.real;
    return result;
}

ComplexJet2 complex_conjugate(const ComplexJet2& value) noexcept {
    ComplexJet2 result = value;
    result.imaginary = -result.imaginary;
    return result;
}

ComplexJet2 complex_phase(const Jet2& angle) noexcept {
    ComplexJet2 result;
    result.real = jet_cos(angle);
    result.imaginary = jet_sin(angle);
    return result;
}

const PlanetModel* find_planet_model(int body_id) noexcept {
    for (size_t i = 0; i < sizeof(kPlanetModels) / sizeof(kPlanetModels[0]); ++i) {
        if (kPlanetModels[i].body_id == body_id) {
            return &kPlanetModels[i];
        }
    }
    return 0;
}

bool eval_planet_ecliptic(
    const PlanetModel& model,
    const SplitJulianDate& jd_tdb,
    JetVector3* out
) noexcept {
    SplitJulianDate start;
    SplitJulianDate end;
    SplitJulianDate epoch;
    if (!out
        || !split_julian_date_from_double(model.jd_start, &start)
        || !split_julian_date_from_double(model.jd_end, &end)
        || !split_julian_date_from_double(model.epoch_jd, &epoch)
        || jd_tdb < start || jd_tdb > end
        || model.angle_count > kMaximumPlanetAngles) {
        return false;
    }
    const Jet2 u(
        days_between_split_jd(epoch, jd_tdb) / model.half_span_days,
        1.0 / model.half_span_days,
        0.0);
    Jet2 channels[3];
    for (size_t column = 0; column < 3; ++column) {
        Jet2 value(kPlanetSecularCoefficients[
            model.secular_offset + static_cast<size_t>(model.secular_degree) * 3 + column]);
        for (size_t row = model.secular_degree; row > 0; --row) {
            value = value * u + Jet2(kPlanetSecularCoefficients[
                model.secular_offset + (row - 1) * 3 + column]);
        }
        channels[column] = value;
    }

    ComplexJet2 harmonics[kMaximumPlanetAngles][kMaximumPlanetHarmonics];
    size_t coefficient_cursor = model.angle_coefficient_offset;
    for (size_t angle_index = 0; angle_index < model.angle_count; ++angle_index) {
        const size_t degree = kPlanetAngleDegrees[model.angle_offset + angle_index];
        const Jet2 angle = eval_polynomial(
            &kPlanetAngleCoefficients[coefficient_cursor], degree + 1, u);
        coefficient_cursor += degree + 1;
        const size_t maximum = kPlanetMaxHarmonics[
            model.max_harmonic_offset + angle_index];
        if (maximum > kMaximumPlanetHarmonics) {
            return false;
        }
        if (maximum > 0) {
            const ComplexJet2 base = complex_phase(angle);
            harmonics[angle_index][0] = base;
            for (size_t harmonic = 1; harmonic < maximum; ++harmonic) {
                harmonics[angle_index][harmonic] = complex_multiply(
                    harmonics[angle_index][harmonic - 1], base);
            }
        }
    }

    ComplexJet2 carrier_harmonics[kMaximumCarrierHarmonics];
    if (model.carrier_coefficient_count > 0) {
        if (model.carrier_max_harmonic > kMaximumCarrierHarmonics) {
            return false;
        }
        const Jet2 angle = eval_polynomial(
            &kPlanetCarrierCoefficients[model.carrier_offset],
            model.carrier_coefficient_count,
            u);
        const ComplexJet2 base = complex_phase(angle);
        carrier_harmonics[0] = base;
        for (size_t harmonic = 1; harmonic < model.carrier_max_harmonic; ++harmonic) {
            carrier_harmonics[harmonic] = complex_multiply(
                carrier_harmonics[harmonic - 1], base);
        }
    }

    for (size_t argument_index = 0; argument_index < model.argument_count; ++argument_index) {
        const PlanetArgument& argument = kPlanetArguments[
            model.argument_offset + argument_index];
        ComplexJet2 phase;
        phase.real = Jet2(1.0);
        phase.imaginary = Jet2(0.0);
        if (argument.carrier_multiplier != 0) {
            const size_t harmonic = static_cast<size_t>(
                std::abs(static_cast<int>(argument.carrier_multiplier)) - 1);
            if (harmonic >= model.carrier_max_harmonic) {
                return false;
            }
            phase = carrier_harmonics[harmonic];
            if (argument.carrier_multiplier < 0) {
                phase = complex_conjugate(phase);
            }
        } else {
            for (size_t factor_index = 0; factor_index < argument.factor_count; ++factor_index) {
                const PlanetFactor& factor = kPlanetFactors[
                    argument.factor_offset + factor_index];
                if (factor.angle_index >= model.angle_count || factor.harmonic == 0
                    || factor.harmonic > kPlanetMaxHarmonics[
                        model.max_harmonic_offset + factor.angle_index]) {
                    return false;
                }
                ComplexJet2 term = harmonics[factor.angle_index][factor.harmonic - 1];
                if (factor.negative) {
                    term = complex_conjugate(term);
                }
                phase = complex_multiply(phase, term);
            }
        }

        const size_t polynomial_count = argument.amplitude_degree + 1;
        for (size_t column = 0; column < 3; ++column) {
            const size_t amplitude_base = argument.amplitude_offset
                + column * polynomial_count * 2;
            Jet2 real(kPlanetAmplitudes[amplitude_base + (polynomial_count - 1) * 2]);
            Jet2 imaginary(kPlanetAmplitudes[
                amplitude_base + (polynomial_count - 1) * 2 + 1]);
            for (size_t power = polynomial_count - 1; power > 0; --power) {
                real = real * u + Jet2(kPlanetAmplitudes[
                    amplitude_base + (power - 1) * 2]);
                imaginary = imaginary * u + Jet2(kPlanetAmplitudes[
                    amplitude_base + (power - 1) * 2 + 1]);
            }
            channels[column] += real * phase.real - imaginary * phase.imaginary;
        }
    }

    const Jet2 longitude = channels[0];
    const Jet2 latitude = channels[1];
    const Jet2 radius = jet_exp(channels[2]) * model.radius_scale_km;
    const Jet2 cosine_latitude = jet_cos(latitude);
    out->x = radius * cosine_latitude * jet_cos(longitude);
    out->y = radius * cosine_latitude * jet_sin(longitude);
    out->z = radius * jet_sin(latitude);
    return true;
}

Jet2 p03_angle(const Jet2& t, const double coefficients[6]) noexcept {
    return eval_polynomial(coefficients, 6, t) / kArcsecondsPerRadian;
}

JetVector3 rotate_x(const JetVector3& vector, const Jet2& angle) noexcept {
    const Jet2 cosine = jet_cos(angle);
    const Jet2 sine = jet_sin(angle);
    JetVector3 result;
    result.x = vector.x;
    result.y = cosine * vector.y - sine * vector.z;
    result.z = sine * vector.y + cosine * vector.z;
    return result;
}

JetVector3 rotate_z(const JetVector3& vector, const Jet2& angle) noexcept {
    const Jet2 cosine = jet_cos(angle);
    const Jet2 sine = jet_sin(angle);
    JetVector3 result;
    result.x = cosine * vector.x - sine * vector.y;
    result.y = sine * vector.x + cosine * vector.y;
    result.z = vector.z;
    return result;
}

void date_ecliptic_to_j2000(
    const Jet2& t,
    Jet2* longitude,
    Jet2* latitude
) noexcept {
    static const double phi[6] = {
        0.0, 5038.481507, -1.0790069, -0.00114045, 0.000132851, -9.51e-8};
    static const double omega[6] = {
        84381.406, -0.025754, 0.0512623, -0.00772503, -4.67e-7, 3.337e-7};
    static const double epsilon[6] = {
        84381.406, -46.836769, -0.0001831, 0.00200340, -5.76e-7, -4.34e-8};
    static const double chi[6] = {
        0.0, 10.556403, -2.3814292, -0.00121197, 0.000170663, -5.60e-8};

    const Jet2 cosine_latitude = jet_cos(*latitude);
    JetVector3 vector;
    vector.x = cosine_latitude * jet_cos(*longitude);
    vector.y = cosine_latitude * jet_sin(*longitude);
    vector.z = jet_sin(*latitude);
    vector = rotate_x(vector, p03_angle(t, epsilon));
    vector = rotate_z(vector, p03_angle(t, chi));
    vector = rotate_x(vector, -p03_angle(t, omega));
    vector = rotate_z(vector, -p03_angle(t, phi));
    *longitude = jet_atan2(vector.y, vector.x);
    *latitude = jet_atan2(
        vector.z,
        jet_sqrt(vector.x * vector.x + vector.y * vector.y));
}

Jet2 eval_xl1_coordinate(size_t coordinate, const Jet2& t) noexcept {
    Jet2 value;
    if (coordinate == 0) {
        const Jet2 t2 = t * t;
        const Jet2 t3 = t2 * t;
        const Jet2 t4 = t3 * t;
        const Jet2 t5 = t4 * t;
        value += (Jet2(3.81034409)
            + t * 8399.684730072
            - t2 * 3.319e-05
            + t3 * 3.11e-08
            - t4 * 2.033e-10) * kArcsecondsPerRadian;
        value += t * 5028.792262
            + t2 * 1.1124406
            + t3 * 0.00007699
            - t4 * 0.000023479
            - t5 * 0.0000000178;
        if (t.value > 10.0) {
            const Jet2 offset = t - Jet2(10.0);
            value += Jet2(-0.866) + offset * 1.43 + offset * offset * 0.054;
        }
    }

    const Jet2 phase_t2 = t * t / 1.0e4;
    const Jet2 phase_t3 = t * t * t / 1.0e8;
    const Jet2 phase_t4 = t * t * t * t / 1.0e8;
    Jet2 envelope(1.0);
    const TableRange coordinate_range = kXl1Coordinates[coordinate];
    for (size_t series_index = 0; series_index < coordinate_range.count; ++series_index) {
        const TableRange series = kXl1Series[coordinate_range.offset + series_index];
        Jet2 subtotal;
        for (size_t term_index = 0; term_index < series.count; ++term_index) {
            const Xl1Term& term = kXl1Terms[series.offset + term_index];
            const Jet2 phase = Jet2(term.phase[0])
                + t * term.phase[1]
                + phase_t2 * term.phase[2]
                + phase_t3 * term.phase[3]
                + phase_t4 * term.phase[4];
            subtotal += jet_cos(phase) * term.amplitude;
        }
        value += envelope * subtotal;
        envelope = envelope * t;
    }
    return coordinate == 2 ? value : value / kArcsecondsPerRadian;
}

void make_chebyshev(const Jet2& value, size_t degree, Jet2 out[6]) noexcept {
    out[0] = Jet2(1.0);
    if (degree == 0) {
        return;
    }
    out[1] = value;
    for (size_t index = 2; index <= degree; ++index) {
        out[index] = value * out[index - 1] * 2.0 - out[index - 2];
    }
}

Jet2 correction_dot(uint32_t offset, size_t count, const Jet2 values[6]) noexcept {
    Jet2 result;
    for (size_t index = 0; index < count; ++index) {
        result += values[index] * kLunarCorrectionCoefficients[offset + index];
    }
    return result;
}

Jet2 eval_lunar_correction_channel(
    const LunarCorrectionChannel& channel,
    const Jet2& t,
    const Jet2& u
) noexcept {
    const size_t degree = std::max<size_t>(
        kLunarCorrectionModel.secular_degree, channel.modulation_degree);
    if (degree > kMaximumChebyshevDegree) {
        return Jet2(NAN, NAN, NAN);
    }
    if (channel.secular_count > kMaximumChebyshevDegree + 1) {
        return Jet2(NAN, NAN, NAN);
    }
    Jet2 chebyshev[kMaximumChebyshevDegree + 1];
    make_chebyshev(u, degree, chebyshev);
    Jet2 value = correction_dot(
        channel.secular_offset, channel.secular_count, chebyshev);
    const Jet2 phase_t2 = t * t / 1.0e4;
    const Jet2 phase_t3 = t * t * t / 1.0e8;
    const Jet2 phase_t4 = t * t * t * t / 1.0e8;
    for (size_t term_index = 0; term_index < channel.term_count; ++term_index) {
        const LunarCorrectionTerm& term = kLunarCorrectionTerms[
            channel.term_offset + term_index];
        if (term.cosine_count > kMaximumChebyshevDegree + 1
            || term.sine_count > kMaximumChebyshevDegree + 1) {
            return Jet2(NAN, NAN, NAN);
        }
        const Jet2 phase = Jet2(term.phase[0])
            + t * term.phase[1]
            + phase_t2 * term.phase[2]
            + phase_t3 * term.phase[3]
            + phase_t4 * term.phase[4];
        const Jet2 cosine_amplitude = correction_dot(
            term.cosine_offset, term.cosine_count, chebyshev);
        const Jet2 sine_amplitude = correction_dot(
            term.sine_offset, term.sine_count, chebyshev);
        value += jet_power(t, term.order)
            * (cosine_amplitude * jet_cos(phase)
                + sine_amplitude * jet_sin(phase));
    }
    return value;
}

bool lunar_coverage(double* start, double* end) noexcept {
    if (!start || !end) {
        return false;
    }
    *start = kLunarCorrectionModel.epoch_jd - kLunarCorrectionModel.half_span_days;
    *end = kLunarCorrectionModel.epoch_jd + kLunarCorrectionModel.half_span_days;
    return true;
}

bool eval_moon_geocentric_ecliptic(const SplitJulianDate& jd_tdb, JetVector3* out) noexcept {
    if (!out) {
        return false;
    }
    double start = 0.0;
    double end = 0.0;
    SplitJulianDate start_jd;
    SplitJulianDate end_jd;
    SplitJulianDate correction_epoch;
    if (!lunar_coverage(&start, &end)
        || !split_julian_date_from_double(start, &start_jd)
        || !split_julian_date_from_double(end, &end_jd)
        || !split_julian_date_from_double(kLunarCorrectionModel.epoch_jd, &correction_epoch)
        || jd_tdb < start_jd || jd_tdb > end_jd) {
        return false;
    }
    const Jet2 t(
        days_between_split_jd(SPLIT_JD_J2000, jd_tdb) / kDaysPerJulianCentury,
        1.0 / kDaysPerJulianCentury,
        0.0);
    const Jet2 u(
        days_between_split_jd(correction_epoch, jd_tdb)
            / kLunarCorrectionModel.half_span_days,
        1.0 / kLunarCorrectionModel.half_span_days,
        0.0);
    Jet2 channels[3] = {
        eval_xl1_coordinate(0, t),
        eval_xl1_coordinate(1, t),
        eval_xl1_coordinate(2, t),
    };
    date_ecliptic_to_j2000(t, &channels[0], &channels[1]);
    channels[2] = Jet2(std::log(
        channels[2].value / kLunarCorrectionModel.radius_scale_km),
        channels[2].first / channels[2].value,
        channels[2].second / channels[2].value
            - channels[2].first * channels[2].first
                / (channels[2].value * channels[2].value));
    for (size_t channel_index = 0;
         channel_index < sizeof(kLunarCorrectionChannels)
            / sizeof(kLunarCorrectionChannels[0]);
         ++channel_index) {
        const LunarCorrectionChannel& correction = kLunarCorrectionChannels[channel_index];
        if (correction.source_coordinate >= 3) {
            return false;
        }
        channels[correction.source_coordinate] += eval_lunar_correction_channel(
            correction, t, u);
    }
    const Jet2 radius = jet_exp(channels[2]) * kLunarCorrectionModel.radius_scale_km;
    const Jet2 cosine_latitude = jet_cos(channels[1]);
    out->x = radius * cosine_latitude * jet_cos(channels[0]);
    out->y = radius * cosine_latitude * jet_sin(channels[0]);
    out->z = radius * jet_sin(channels[1]);
    return true;
}

JetVector3 vector_add(
    const JetVector3& left,
    const JetVector3& right,
    double right_scale
) noexcept {
    JetVector3 result;
    result.x = left.x + right.x * right_scale;
    result.y = left.y + right.y * right_scale;
    result.z = left.z + right.z * right_scale;
    return result;
}

JetVector3 ecliptic_to_icrf(const JetVector3& vector) noexcept {
    const double cosine = std::cos(kJ2000ObliquityRad);
    const double sine = std::sin(kJ2000ObliquityRad);
    JetVector3 result;
    result.x = vector.x;
    result.y = vector.y * cosine - vector.z * sine;
    result.z = vector.y * sine + vector.z * cosine;
    return result;
}

bool eval_route_ecliptic(
    int target_id,
    int center_id,
    const SplitJulianDate& jd_tdb,
    JetVector3* out
) noexcept {
    if (!out) {
        return false;
    }
    if (center_id == TAIYIN_BODY_SUN) {
        const PlanetModel* model = find_planet_model(target_id);
        if (model) {
            return eval_planet_ecliptic(*model, jd_tdb, out);
        }
        if (target_id == TAIYIN_BODY_EARTH) {
            const PlanetModel* emb = find_planet_model(TAIYIN_BODY_EMB);
            JetVector3 emb_state;
            JetVector3 moon_state;
            if (!emb
                || !eval_planet_ecliptic(*emb, jd_tdb, &emb_state)
                || !eval_moon_geocentric_ecliptic(jd_tdb, &moon_state)) {
                return false;
            }
            *out = vector_add(
                emb_state,
                moon_state,
                -1.0 / (TAIYIN_EARTH_MOON_MASS_RATIO + 1.0));
            return true;
        }
    }
    if (target_id == TAIYIN_BODY_MOON && center_id == TAIYIN_BODY_EARTH) {
        return eval_moon_geocentric_ecliptic(jd_tdb, out);
    }
    return false;
}

bool calc_semi_analytic_state_void(
    const SplitJulianDate& jd_tdb,
    const void* opaque,
    CartesianState* out
) noexcept {
    const SemiAnalyticEphemerisData* data =
        static_cast<const SemiAnalyticEphemerisData*>(opaque);
    SplitJulianDate start;
    SplitJulianDate end;
    if (!data || !out || !split_julian_date_is_finite(jd_tdb)
        || !split_julian_date_from_double(data->jd_tdb_start, &start)
        || !split_julian_date_from_double(data->jd_tdb_end, &end)
        || jd_tdb < start || jd_tdb > end) {
        return false;
    }
    JetVector3 ecliptic;
    if (!eval_route_ecliptic(data->target_id, data->center_id, jd_tdb, &ecliptic)) {
        return false;
    }
    const JetVector3 icrf = ecliptic_to_icrf(ecliptic);
    *out = CartesianState();
    out->position_au = Vector3{
        icrf.x.value / TAIYIN_AU_KM,
        icrf.y.value / TAIYIN_AU_KM,
        icrf.z.value / TAIYIN_AU_KM};
    out->velocity_au_per_day = Vector3{
        icrf.x.first / TAIYIN_AU_KM,
        icrf.y.first / TAIYIN_AU_KM,
        icrf.z.first / TAIYIN_AU_KM};
    out->acceleration_au_per_day2 = Vector3{
        icrf.x.second / TAIYIN_AU_KM,
        icrf.y.second / TAIYIN_AU_KM,
        icrf.z.second / TAIYIN_AU_KM};
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

bool calc_semi_analytic_position_void(
    const SplitJulianDate& jd_tdb,
    const void* data,
    Vector3* out
) noexcept {
    CartesianState state;
    if (!out || !calc_semi_analytic_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.position_au;
    return true;
}

bool calc_semi_analytic_velocity_void(
    const SplitJulianDate& jd_tdb,
    const void* data,
    Vector3* out
) noexcept {
    CartesianState state;
    if (!out || !calc_semi_analytic_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.velocity_au_per_day;
    return true;
}

bool calc_semi_analytic_acceleration_void(
    const SplitJulianDate& jd_tdb,
    const void* data,
    Vector3* out
) noexcept {
    CartesianState state;
    if (!out || !calc_semi_analytic_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.acceleration_au_per_day2;
    return true;
}

void destroy_semi_analytic_data(void* data) noexcept {
    delete static_cast<SemiAnalyticEphemerisData*>(data);
}

}  // namespace

const char* builtin_semi_analytic_source_revision() noexcept {
    return kSemiAnalyticSourceCommit;
}

const char* builtin_semi_analytic_coefficients_sha256() noexcept {
    return kSemiAnalyticCoefficientSha256;
}

bool get_builtin_semi_analytic_coverage(
    int target_id,
    int center_id,
    double* out_jd_tdb_start,
    double* out_jd_tdb_end
) noexcept {
    if (!out_jd_tdb_start || !out_jd_tdb_end) {
        return false;
    }
    if (center_id == TAIYIN_BODY_SUN) {
        const PlanetModel* model = find_planet_model(target_id);
        if (model) {
            *out_jd_tdb_start = model->jd_start;
            *out_jd_tdb_end = model->jd_end;
            return true;
        }
        if (target_id == TAIYIN_BODY_EARTH) {
            const PlanetModel* emb = find_planet_model(TAIYIN_BODY_EMB);
            double moon_start = 0.0;
            double moon_end = 0.0;
            if (!emb || !lunar_coverage(&moon_start, &moon_end)) {
                return false;
            }
            *out_jd_tdb_start = std::max(emb->jd_start, moon_start);
            *out_jd_tdb_end = std::min(emb->jd_end, moon_end);
            return *out_jd_tdb_end > *out_jd_tdb_start;
        }
    }
    if (target_id == TAIYIN_BODY_MOON && center_id == TAIYIN_BODY_EARTH) {
        return lunar_coverage(out_jd_tdb_start, out_jd_tdb_end);
    }
    return false;
}

bool compile_builtin_semi_analytic_ephemeris_block(
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
    if (!get_builtin_semi_analytic_coverage(
            target_id, center_id, &coverage_start, &coverage_end)
        || jd_tdb_start < coverage_start || jd_tdb_end > coverage_end) {
        return false;
    }
    SemiAnalyticEphemerisData* data =
        new (std::nothrow) SemiAnalyticEphemerisData();
    if (!data) {
        return false;
    }
    data->target_id = target_id;
    data->center_id = center_id;
    data->jd_tdb_start = jd_tdb_start;
    data->jd_tdb_end = jd_tdb_end;
    try {
        out->cache_id = 0;
        out->format = EphemerisBlockFormat::SemiAnalytic;
        out->position = calc_semi_analytic_position_void;
        out->velocity = calc_semi_analytic_velocity_void;
        out->acceleration = calc_semi_analytic_acceleration_void;
        out->state = calc_semi_analytic_state_void;
        out->destroy_element = destroy_semi_analytic_data;
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
