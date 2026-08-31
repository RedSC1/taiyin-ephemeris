#include "taiyin/internal/semi_analytic.h"

#include "taiyin/internal/long_range_analytic.h"

#include "taiyin/body_id.h"
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

constexpr double kJ2000 = 2451545.0;
constexpr double kDaysPerJulianCentury = 36525.0;
const double kJ2000ObliquityRad = TAIYIN_J2000_MEAN_OBLIQUITY_RAD;
constexpr size_t kMaximumPlanetAngles = 9;
constexpr size_t kMaximumPlanetHarmonics = 20;
constexpr size_t kMaximumCarrierHarmonics = 7;
// MAR099's compact Phobos residual fit uses degree six. Keep a small fixed
// bound for stack-only evaluation while allowing the checked-in satellite
// artifacts to express their documented local secular trend.
constexpr size_t kMaximumChebyshevDegree = 8;

// DE440 barycenter gravitational parameters, in km^3/s^2. The semi-analytic
// planet models are heliocentric barycenter states; mass-weighting those
// states supplies the matching Sun-to-SSB state without mixing origins.
constexpr double kSunGmKm3PerS2 = 1.3271244004127942e11;

struct PlanetGm {
    int body_id;
    double gm_km3_per_s2;
};

const PlanetGm kPlanetGms[] = {
    {TAIYIN_BODY_MERCURY_BARYCENTER, 2.2031868551400003e4},
    {TAIYIN_BODY_VENUS_BARYCENTER, 3.2485859200000000e5},
    {TAIYIN_BODY_EMB, 4.0350323562548019e5},
    {TAIYIN_BODY_MARS_BARYCENTER, 4.2828375815756102e4},
    {TAIYIN_BODY_JUPITER_BARYCENTER, 1.2671276409999998e8},
    {TAIYIN_BODY_SATURN_BARYCENTER, 3.7940584841799997e7},
    {TAIYIN_BODY_URANUS_BARYCENTER, 5.7945563999999985e6},
    {TAIYIN_BODY_NEPTUNE_BARYCENTER, 6.8365271005803989e6},
    {TAIYIN_BODY_PLUTO_BARYCENTER, 9.7550000000000000e2},
};

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

struct LunarSeriesTerm {
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

struct SatelliteResidualTable {
    const char* artifact_sha256;
    double jd_start;
    double jd_end;
    double segment_days;
    double blend_days;
    size_t segment_count;
    const double (*plane_ecliptic)[3];
    const double* carrier_coefficients;
    size_t carrier_coefficient_count;
    const double* base_coefficients;
    size_t base_coefficient_count;
    size_t base_harmonic_count;
    size_t base_polynomial_degree;
    const double* residual_coefficients;
    size_t residual_coefficient_count;
    size_t residual_polynomial_degree;
    size_t residual_harmonic_count;
    size_t residual_harmonic_amplitude_degree;
};

struct SatellitePoissonTerm {
    int16_t carrier_multiplier;
    int16_t perturber_multiplier;
};

struct SatellitePoissonTable {
    const char* artifact_sha256;
    double jd_start;
    double jd_end;
    const double (*plane_ecliptic)[3];
    const double* carrier_coefficients;
    size_t carrier_coefficient_count;
    const double* perturber_carrier_coefficients;
    size_t perturber_carrier_coefficient_count;
    const SatellitePoissonTerm* terms;
    size_t term_count;
    size_t channel_polynomial_degree;
    const double* coefficients;
    size_t coefficient_count;
};

// Compact orbital-element series retained from Astronomy Engine's MIT-licensed
// L1.2 Galilean-moon implementation.  The table itself lives in third_party;
// this file supplies Taiyin's frame conversion and differentiated evaluation.
struct AstronomyEngineSeriesTerm {
    double amplitude;
    double phase;
    double frequency;
};

struct AstronomyEngineSeries {
    size_t term_count;
    const AstronomyEngineSeriesTerm* terms;
};

struct AstronomyEngineJupiterMoonModel {
    double gravitational_parameter_au3_per_day2;
    double mean_longitude_phase;
    double mean_longitude_frequency;
    AstronomyEngineSeries semi_major_axis;
    AstronomyEngineSeries longitude;
    AstronomyEngineSeries eccentricity;
    AstronomyEngineSeries inclination;
};

// The former primary planet/Moon fit is retained only as a bounded legacy
// component for the Pluto and Sun/SSB composite routes. New primary planet
// and lunar requests use long_range_analytic.cpp.
#include "legacy/compact_primary_coefficients.inc"
#include "internal/charon_plu060_coefficients.inc"
#include "internal/mars_satellite_coefficients.inc"
#include "internal/triton_nep098_coefficients.inc"
#include "internal/pluto_small_satellite_coefficients.inc"
#include "third_party/astronomy_engine/jupiter_moons_l1.inc"

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

// PLU060's embedded file summary gives the fitted Pluto-system GMs in
// km^3/s^2.  Each mass-bearing companion has an independently generated
// relative-state route, allowing the physical Pluto center to be reconstructed
// from the same complete-system denominator as the source SPK.
constexpr double kPlutoSystemGm = 975.4308664317557;
constexpr double kPlutoCharonMassFraction =
    106.1011388236118 / kPlutoSystemGm;
constexpr double kPlutoNixMassFraction =
    0.001496176095836919 / kPlutoSystemGm;
constexpr double kPlutoHydraMassFraction =
    0.002007962473606101 / kPlutoSystemGm;
constexpr double kPlutoKerberosMassFraction =
    0.00006038450780269370 / kPlutoSystemGm;
constexpr double kPlutoStyxMassFraction =
    0.00004045391585487917 / kPlutoSystemGm;

// MAR099's embedded file summary gives these SATORBINT fitted system GMs in
// km^3/s^2.  Phobos and Deimos are the only mass-bearing satellite entries in
// this Mars-system kernel, so their explicit sum reconstructs Mars (499) from
// Mars barycenter (4) without an approximate-center route.
constexpr double kMarsSystemGm = 4.282837566226656e4;
constexpr double kMarsPhobosMassFraction =
    7.087546066894452e-4 / kMarsSystemGm;
constexpr double kMarsDeimosMassFraction =
    9.615569648120313e-5 / kMarsSystemGm;

// NEP098's own file summary gives Triton GM 1428.495462910464 and total
// Neptunian-system GM 6836531.640925204 km^3/s^2.  The residual table models
// Triton only.  The denominator is the complete system mass, so this is a
// deliberately labelled Triton-dominant correction rather than a claim that
// every mass-bearing satellite has been reconstructed.
constexpr double kNeptuneTritonMassFraction =
    1428.495462910464 / 6836531.640925204;

// JUP365 publishes these Galilean and complete-system GMs.  The compact L1.2
// table has no states for Amalthea, Thebe, Adrastea, or Metis, so this is
// intentionally a Galilean-dominant Jupiter-center reconstruction.
constexpr double kJupiterSystemGm = 1.267127618414429e8;
constexpr double kJupiterIoMassFraction =
    5959.915466180539 / kJupiterSystemGm;
constexpr double kJupiterEuropaMassFraction =
    3202.712099607295 / kJupiterSystemGm;
constexpr double kJupiterGanymedeMassFraction =
    9887.832752719638 / kJupiterSystemGm;
constexpr double kJupiterCallistoMassFraction =
    7179.283402579837 / kJupiterSystemGm;
constexpr double kJupiterL1ValidatedStart = 2305456.5;
constexpr double kJupiterL1ValidatedEnd = 2524602.5;

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

Jet2 jet_inverse(const Jet2& value) noexcept {
    const double inverse = 1.0 / value.value;
    const double inverse_squared = inverse * inverse;
    return Jet2(
        inverse,
        -value.first * inverse_squared,
        2.0 * value.first * value.first * inverse_squared * inverse
            - value.second * inverse_squared);
}

Jet2 jet_divide(const Jet2& numerator, const Jet2& denominator) noexcept {
    return numerator * jet_inverse(denominator);
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

bool eval_sun_ssb_ecliptic(
    const SplitJulianDate& jd_tdb,
    JetVector3* out
) noexcept {
    if (!out) {
        return false;
    }
    JetVector3 weighted_planets;
    double total_gm = kSunGmKm3PerS2;
    for (size_t index = 0;
         index < sizeof(kPlanetGms) / sizeof(kPlanetGms[0]);
         ++index) {
        const PlanetGm& body = kPlanetGms[index];
        const PlanetModel* model = find_planet_model(body.body_id);
        JetVector3 heliocentric;
        if (!model || !eval_planet_ecliptic(*model, jd_tdb, &heliocentric)) {
            return false;
        }
        weighted_planets.x += heliocentric.x * body.gm_km3_per_s2;
        weighted_planets.y += heliocentric.y * body.gm_km3_per_s2;
        weighted_planets.z += heliocentric.z * body.gm_km3_per_s2;
        total_gm += body.gm_km3_per_s2;
    }
    const double scale = -1.0 / total_gm;
    out->x = weighted_planets.x * scale;
    out->y = weighted_planets.y * scale;
    out->z = weighted_planets.z * scale;
    return true;
}

void lunar_elp_ecliptic_to_j2000(
    const Jet2& t,
    Jet2* longitude,
    Jet2* latitude
) noexcept {
    const Jet2 cosine_latitude = jet_cos(*latitude);
    JetVector3 vector;
    vector.x = cosine_latitude * jet_cos(*longitude);
    vector.y = cosine_latitude * jet_sin(*longitude);
    vector.z = jet_sin(*latitude);
    const Jet2 p = eval_polynomial(
        kLunarPrecessionPCoefficients,
        sizeof(kLunarPrecessionPCoefficients)
            / sizeof(kLunarPrecessionPCoefficients[0]),
        t) * t;
    const Jet2 q = eval_polynomial(
        kLunarPrecessionQCoefficients,
        sizeof(kLunarPrecessionQCoefficients)
            / sizeof(kLunarPrecessionQCoefficients[0]),
        t) * t;
    const Jet2 p2 = p * p;
    const Jet2 q2 = q * q;
    const Jet2 ra = jet_sqrt(Jet2(1.0) - p2 - q2) * 2.0;
    const Jet2 pq2 = p * q * 2.0;
    const Jet2 pp = Jet2(1.0) - p2 * 2.0;
    const Jet2 qq = Jet2(1.0) - q2 * 2.0;
    const Jet2 pra = p * ra;
    const Jet2 qra = q * ra;
    const JetVector3 original = vector;
    vector.x = pp * original.x + pq2 * original.y + pra * original.z;
    vector.y = pq2 * original.x + qq * original.y - qra * original.z;
    vector.z = -pra * original.x + qra * original.y
        + (pp + qq - Jet2(1.0)) * original.z;
    *longitude = jet_atan2(vector.y, vector.x);
    *latitude = jet_atan2(
        vector.z,
        jet_sqrt(vector.x * vector.x + vector.y * vector.y));
}

Jet2 eval_lunar_elp_coordinate(size_t coordinate, const Jet2& t) noexcept {
    Jet2 value;
    if (coordinate == 0) {
        value = eval_polynomial(
            kLunarMeanLongitudeCoefficients,
            sizeof(kLunarMeanLongitudeCoefficients)
                / sizeof(kLunarMeanLongitudeCoefficients[0]),
            t);
    }

    const Jet2 phase_t2 = t * t;
    const Jet2 phase_t3 = phase_t2 * t;
    const Jet2 phase_t4 = phase_t3 * t;
    Jet2 envelope(1.0);
    const TableRange coordinate_range = kLunarCoordinates[coordinate];
    for (size_t series_index = 0; series_index < coordinate_range.count; ++series_index) {
        const TableRange series = kLunarSeries[coordinate_range.offset + series_index];
        Jet2 subtotal;
        for (size_t term_index = 0; term_index < series.count; ++term_index) {
            const LunarSeriesTerm& term = kLunarSeriesTerms[series.offset + term_index];
            const Jet2 phase = Jet2(term.phase[0])
                + t * term.phase[1]
                + phase_t2 * term.phase[2]
                + phase_t3 * term.phase[3]
                + phase_t4 * term.phase[4];
            subtotal += jet_sin(phase) * term.amplitude;
        }
        value += envelope * subtotal;
        envelope = envelope * t;
    }
    return value;
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

bool satellite_residual_coverage(
    const SatelliteResidualTable& model,
    double* start,
    double* end
) noexcept {
    if (!start || !end) {
        return false;
    }
    *start = model.jd_start;
    *end = model.jd_end;
    return true;
}

Jet2 satellite_residual_carrier(
    const SatelliteResidualTable& model,
    const SplitJulianDate& jd_tdb
) noexcept {
    const Jet2 t(
        days_between_split_jd(SPLIT_JD_J2000, jd_tdb)
            / kDaysPerJulianCentury,
        1.0 / kDaysPerJulianCentury,
        0.0);
    return eval_polynomial(
        model.carrier_coefficients, model.carrier_coefficient_count, t);
}

Jet2 eval_satellite_residual_base_channel(
    const SatelliteResidualTable& model,
    size_t channel,
    const Jet2& global_u,
    const Jet2& carrier
) noexcept {
    if (model.base_polynomial_degree > kMaximumChebyshevDegree) {
        return Jet2(NAN, NAN, NAN);
    }
    Jet2 chebyshev[kMaximumChebyshevDegree + 1];
    make_chebyshev(global_u, model.base_polynomial_degree, chebyshev);
    const double* coefficients = model.base_coefficients
        + channel * model.base_coefficient_count;
    Jet2 value;
    size_t cursor = 0;
    for (size_t index = 0; index <= model.base_polynomial_degree; ++index) {
        value += coefficients[cursor++] * chebyshev[index];
    }
    ComplexJet2 harmonic = complex_phase(carrier);
    const ComplexJet2 fundamental = harmonic;
    for (size_t index = 0; index < model.base_harmonic_count; ++index) {
        const double cosine_coefficient = coefficients[cursor++];
        const double sine_coefficient = coefficients[cursor++];
        value += cosine_coefficient * harmonic.real
            + sine_coefficient * harmonic.imaginary;
        if (index + 1 < model.base_harmonic_count) {
            harmonic = complex_multiply(harmonic, fundamental);
        }
    }
    return value;
}

Jet2 satellite_residual_segment_coordinate(
    const SatelliteResidualTable& model,
    const SplitJulianDate& jd_tdb,
    size_t index
) noexcept {
    const double segment_start = model.jd_start
        + static_cast<double>(index) * model.segment_days;
    const double segment_end = index + 1 < model.segment_count
        ? segment_start + model.segment_days
        : model.jd_end;
    const double half_span = (segment_end - segment_start) / 2.0;
    const double midpoint = (segment_start + segment_end) / 2.0;
    return Jet2(
        (days_between_split_jd(SPLIT_JD_J2000, jd_tdb)
            - (midpoint - kJ2000)) / half_span,
        1.0 / half_span,
        0.0);
}

Jet2 eval_satellite_residual_segment_channel(
    const SatelliteResidualTable& model,
    size_t segment_index,
    size_t channel,
    const SplitJulianDate& jd_tdb,
    const Jet2& carrier
) noexcept {
    if (model.residual_polynomial_degree > kMaximumChebyshevDegree
        || model.residual_harmonic_amplitude_degree > kMaximumChebyshevDegree) {
        return Jet2(NAN, NAN, NAN);
    }
    Jet2 polynomial[kMaximumChebyshevDegree + 1];
    Jet2 amplitude[kMaximumChebyshevDegree + 1];
    make_chebyshev(
        satellite_residual_segment_coordinate(model, jd_tdb, segment_index),
        model.residual_polynomial_degree, polynomial);
    make_chebyshev(
        satellite_residual_segment_coordinate(model, jd_tdb, segment_index),
        model.residual_harmonic_amplitude_degree, amplitude);
    const double* coefficients = model.residual_coefficients
        + (segment_index * 3 + channel) * model.residual_coefficient_count;
    Jet2 value;
    size_t cursor = 0;
    for (size_t index = 0; index <= model.residual_polynomial_degree; ++index) {
        value += coefficients[cursor++] * polynomial[index];
    }
    ComplexJet2 harmonic = complex_phase(carrier);
    const ComplexJet2 fundamental = harmonic;
    for (size_t harmonic_index = 0;
         harmonic_index < model.residual_harmonic_count;
         ++harmonic_index) {
        Jet2 cosine_amplitude;
        Jet2 sine_amplitude;
        for (size_t index = 0;
             index <= model.residual_harmonic_amplitude_degree;
             ++index) {
            cosine_amplitude += coefficients[cursor++] * amplitude[index];
        }
        for (size_t index = 0;
             index <= model.residual_harmonic_amplitude_degree;
             ++index) {
            sine_amplitude += coefficients[cursor++] * amplitude[index];
        }
        value += cosine_amplitude * harmonic.real
            + sine_amplitude * harmonic.imaginary;
        if (harmonic_index + 1 < model.residual_harmonic_count) {
            harmonic = complex_multiply(harmonic, fundamental);
        }
    }
    return value;
}

Jet2 satellite_residual_blend_weight(
    const SatelliteResidualTable& model,
    const SplitJulianDate& jd_tdb,
    double start_jd
) noexcept {
    const Jet2 x(
        days_between_split_jd(SPLIT_JD_J2000, jd_tdb)
            - (start_jd - kJ2000),
        1.0,
        0.0);
    const Jet2 unit = x / model.blend_days;
    return unit * unit * (Jet2(3.0) - unit * 2.0);
}

Jet2 eval_satellite_residual_channel(
    const SatelliteResidualTable& model,
    size_t channel,
    const SplitJulianDate& jd_tdb,
    const Jet2& carrier
) noexcept {
    const double elapsed_days = days_between_split_jd(SPLIT_JD_J2000, jd_tdb)
        - (model.jd_start - kJ2000);
    size_t index = static_cast<size_t>(std::floor(
        elapsed_days / model.segment_days));
    if (index >= model.segment_count) {
        index = model.segment_count - 1;
    }
    const double segment_start = model.jd_start
        + static_cast<double>(index) * model.segment_days;
    const double local_days = elapsed_days
        - static_cast<double>(index) * model.segment_days;
    const double half_blend = model.blend_days / 2.0;
    if (model.blend_days > 0.0 && index > 0 && local_days < half_blend) {
        const double boundary = segment_start;
        const Jet2 left = eval_satellite_residual_segment_channel(
            model, index - 1, channel, jd_tdb, carrier);
        const Jet2 right = eval_satellite_residual_segment_channel(
            model, index, channel, jd_tdb, carrier);
        const Jet2 weight = satellite_residual_blend_weight(
            model, jd_tdb, boundary - half_blend);
        return left * (Jet2(1.0) - weight) + right * weight;
    }
    if (model.blend_days > 0.0 && index + 1 < model.segment_count
        && local_days > model.segment_days - half_blend) {
        const double boundary = segment_start + model.segment_days;
        const Jet2 left = eval_satellite_residual_segment_channel(
            model, index, channel, jd_tdb, carrier);
        const Jet2 right = eval_satellite_residual_segment_channel(
            model, index + 1, channel, jd_tdb, carrier);
        const Jet2 weight = satellite_residual_blend_weight(
            model, jd_tdb, boundary - half_blend);
        return left * (Jet2(1.0) - weight) + right * weight;
    }
    return eval_satellite_residual_segment_channel(
        model, index, channel, jd_tdb, carrier);
}

bool eval_satellite_residual_relative_ecliptic(
    const SatelliteResidualTable& model,
    const SplitJulianDate& jd_tdb,
    JetVector3* out
) noexcept {
    SplitJulianDate start;
    SplitJulianDate end;
    if (!out
        || !split_julian_date_from_double(model.jd_start, &start)
        || !split_julian_date_from_double(model.jd_end, &end)
        || jd_tdb < start || jd_tdb > end) {
        return false;
    }
    const double midpoint = (model.jd_start + model.jd_end) / 2.0;
    const double half_span = (model.jd_end - model.jd_start) / 2.0;
    const Jet2 global_u(
        (days_between_split_jd(SPLIT_JD_J2000, jd_tdb) - (midpoint - kJ2000))
            / half_span,
        1.0 / half_span,
        0.0);
    const Jet2 carrier = satellite_residual_carrier(model, jd_tdb);
    const Jet2 radius = eval_satellite_residual_base_channel(
        model, 0, global_u, carrier)
        + eval_satellite_residual_channel(model, 0, jd_tdb, carrier);
    const Jet2 phase = carrier
        + eval_satellite_residual_base_channel(model, 1, global_u, carrier)
        + eval_satellite_residual_channel(model, 1, jd_tdb, carrier);
    const Jet2 height = eval_satellite_residual_base_channel(
        model, 2, global_u, carrier)
        + eval_satellite_residual_channel(model, 2, jd_tdb, carrier);
    const Jet2 x = radius * jet_cos(phase);
    const Jet2 y = radius * jet_sin(phase);
    out->x = x * model.plane_ecliptic[0][0]
        + y * model.plane_ecliptic[1][0]
        + height * model.plane_ecliptic[2][0];
    out->y = x * model.plane_ecliptic[0][1]
        + y * model.plane_ecliptic[1][1]
        + height * model.plane_ecliptic[2][1];
    out->z = x * model.plane_ecliptic[0][2]
        + y * model.plane_ecliptic[1][2]
        + height * model.plane_ecliptic[2][2];
    return true;
}

bool satellite_poisson_coverage(
    const SatellitePoissonTable& model,
    double* start,
    double* end
) noexcept {
    if (!start || !end) {
        return false;
    }
    *start = model.jd_start;
    *end = model.jd_end;
    return true;
}

Jet2 eval_satellite_poisson_channel(
    const SatellitePoissonTable& model,
    size_t channel,
    const Jet2& u,
    const Jet2& carrier,
    const Jet2& perturber
) noexcept {
    if (model.channel_polynomial_degree > kMaximumChebyshevDegree
        || channel >= 3 || model.coefficient_count
            != model.channel_polynomial_degree + 1 + 2 * model.term_count) {
        return Jet2(NAN, NAN, NAN);
    }
    Jet2 chebyshev[kMaximumChebyshevDegree + 1];
    make_chebyshev(u, model.channel_polynomial_degree, chebyshev);
    const double* coefficients = model.coefficients
        + channel * model.coefficient_count;
    Jet2 value;
    size_t cursor = 0;
    for (size_t index = 0; index <= model.channel_polynomial_degree; ++index) {
        value += coefficients[cursor++] * chebyshev[index];
    }
    for (size_t index = 0; index < model.term_count; ++index) {
        const SatellitePoissonTerm& term = model.terms[index];
        const ComplexJet2 phase = complex_phase(
            carrier * static_cast<double>(term.carrier_multiplier)
            + perturber * static_cast<double>(term.perturber_multiplier));
        const double cosine_coefficient = coefficients[cursor++];
        const double sine_coefficient = coefficients[cursor++];
        value += cosine_coefficient * phase.real
            + sine_coefficient * phase.imaginary;
    }
    return value;
}

bool eval_satellite_poisson_relative_ecliptic(
    const SatellitePoissonTable& model,
    const SplitJulianDate& jd_tdb,
    JetVector3* out
) noexcept {
    SplitJulianDate start;
    SplitJulianDate end;
    if (!out
        || !split_julian_date_from_double(model.jd_start, &start)
        || !split_julian_date_from_double(model.jd_end, &end)
        || jd_tdb < start || jd_tdb > end
        || model.carrier_coefficient_count == 0
        || model.perturber_carrier_coefficient_count == 0) {
        return false;
    }
    const double midpoint = (model.jd_start + model.jd_end) / 2.0;
    const double half_span = (model.jd_end - model.jd_start) / 2.0;
    const Jet2 u(
        (days_between_split_jd(SPLIT_JD_J2000, jd_tdb) - (midpoint - kJ2000))
            / half_span,
        1.0 / half_span,
        0.0);
    const Jet2 t(
        days_between_split_jd(SPLIT_JD_J2000, jd_tdb) / kDaysPerJulianCentury,
        1.0 / kDaysPerJulianCentury,
        0.0);
    const Jet2 carrier = eval_polynomial(
        model.carrier_coefficients, model.carrier_coefficient_count, t);
    const Jet2 perturber = eval_polynomial(
        model.perturber_carrier_coefficients,
        model.perturber_carrier_coefficient_count, t);
    const Jet2 radius = eval_satellite_poisson_channel(
        model, 0, u, carrier, perturber);
    const Jet2 phase = carrier + eval_satellite_poisson_channel(
        model, 1, u, carrier, perturber);
    const Jet2 height = eval_satellite_poisson_channel(
        model, 2, u, carrier, perturber);
    const Jet2 x = radius * jet_cos(phase);
    const Jet2 y = radius * jet_sin(phase);
    out->x = x * model.plane_ecliptic[0][0]
        + y * model.plane_ecliptic[1][0]
        + height * model.plane_ecliptic[2][0];
    out->y = x * model.plane_ecliptic[0][1]
        + y * model.plane_ecliptic[1][1]
        + height * model.plane_ecliptic[2][1];
    out->z = x * model.plane_ecliptic[0][2]
        + y * model.plane_ecliptic[1][2]
        + height * model.plane_ecliptic[2][2];
    return true;
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
    const Jet2 phase_t2 = t * t;
    const Jet2 phase_t3 = phase_t2 * t;
    const Jet2 phase_t4 = phase_t3 * t;
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
        eval_lunar_elp_coordinate(0, t),
        eval_lunar_elp_coordinate(1, t),
        eval_lunar_elp_coordinate(2, t),
    };
    lunar_elp_ecliptic_to_j2000(t, &channels[0], &channels[1]);
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

Jet2 eval_astronomy_engine_series(
    const AstronomyEngineSeries& series,
    const Jet2& time_days,
    bool sine
) noexcept {
    Jet2 result;
    for (size_t index = 0; index < series.term_count; ++index) {
        const AstronomyEngineSeriesTerm& term = series.terms[index];
        const Jet2 argument = Jet2(term.phase) + time_days * term.frequency;
        result += (sine ? jet_sin(argument) : jet_cos(argument)) * term.amplitude;
    }
    return result;
}

bool eval_astronomy_engine_jupiter_moon_ecliptic(
    int target_id,
    const SplitJulianDate& jd_tdb,
    JetVector3* out
) noexcept {
    if (!out || target_id < TAIYIN_BODY_IO
        || target_id > TAIYIN_BODY_CALLISTO) {
        return false;
    }
    SplitJulianDate start;
    SplitJulianDate end;
    if (!split_julian_date_from_double(kJupiterL1ValidatedStart, &start)
        || !split_julian_date_from_double(kJupiterL1ValidatedEnd, &end)
        || jd_tdb < start || jd_tdb > end) {
        return false;
    }

    const AstronomyEngineJupiterMoonModel& model =
        kAstronomyEngineJupiterMoonModels[target_id - TAIYIN_BODY_IO];
    // L1.2's independent variable is TT days from 1950-01-01.  TDB is the
    // runtime's state epoch and differs from TT only at the millisecond scale,
    // far below this deliberately compact model's validated position error.
    const Jet2 time_days(
        days_between_split_jd(SPLIT_JD_J2000, jd_tdb) + 18262.5,
        1.0,
        0.0);
    const Jet2 semi_major_axis = eval_astronomy_engine_series(
        model.semi_major_axis, time_days, false);
    const Jet2 mean_longitude = Jet2(model.mean_longitude_phase)
        + time_days * model.mean_longitude_frequency
        + eval_astronomy_engine_series(model.longitude, time_days, true);
    const Jet2 eccentricity_k = eval_astronomy_engine_series(
        model.eccentricity, time_days, false);
    const Jet2 eccentricity_h = eval_astronomy_engine_series(
        model.eccentricity, time_days, true);
    const Jet2 inclination_q = eval_astronomy_engine_series(
        model.inclination, time_days, false);
    const Jet2 inclination_p = eval_astronomy_engine_series(
        model.inclination, time_days, true);

    // The following element-to-state formulation is from the compact L1.2
    // implementation.  Keeping the Newton iteration in Jet2 form yields a
    // continuous position, velocity, and acceleration for the block API.
    Jet2 eccentric_anomaly = mean_longitude
        + eccentricity_k * jet_sin(mean_longitude)
        - eccentricity_h * jet_cos(mean_longitude);
    for (size_t iteration = 0; iteration < 6; ++iteration) {
        const Jet2 cosine_eccentric_anomaly = jet_cos(eccentric_anomaly);
        const Jet2 sine_eccentric_anomaly = jet_sin(eccentric_anomaly);
        const Jet2 delta = jet_divide(
            mean_longitude - eccentric_anomaly
                + eccentricity_k * sine_eccentric_anomaly
                - eccentricity_h * cosine_eccentric_anomaly,
            Jet2(1.0) - eccentricity_k * cosine_eccentric_anomaly
                - eccentricity_h * sine_eccentric_anomaly);
        eccentric_anomaly += delta;
    }
    const Jet2 cosine_eccentric_anomaly = jet_cos(eccentric_anomaly);
    const Jet2 sine_eccentric_anomaly = jet_sin(eccentric_anomaly);
    const Jet2 dle = eccentricity_h * cosine_eccentric_anomaly
        - eccentricity_k * sine_eccentric_anomaly;
    const Jet2 rsam1 = -eccentricity_k * cosine_eccentric_anomaly
        - eccentricity_h * sine_eccentric_anomaly;
    const Jet2 phi = jet_sqrt(
        Jet2(1.0) - eccentricity_k * eccentricity_k
            - eccentricity_h * eccentricity_h);
    const Jet2 psi = jet_inverse(Jet2(1.0) + phi);
    const Jet2 x1 = semi_major_axis * (
        cosine_eccentric_anomaly - eccentricity_k
            - psi * eccentricity_h * dle);
    const Jet2 y1 = semi_major_axis * (
        sine_eccentric_anomaly - eccentricity_h
            + psi * eccentricity_k * dle);
    const Jet2 inclination_factor = Jet2(2.0) * jet_sqrt(
        Jet2(1.0) - inclination_q * inclination_q
            - inclination_p * inclination_p);
    const Jet2 p2 = Jet2(1.0) - inclination_p * inclination_p * 2.0;
    const Jet2 q2 = Jet2(1.0) - inclination_q * inclination_q * 2.0;
    const Jet2 pq = inclination_p * inclination_q * 2.0;

    JetVector3 jupiter_equatorial;
    jupiter_equatorial.x = x1 * p2 + y1 * pq;
    jupiter_equatorial.y = x1 * pq + y1 * q2;
    jupiter_equatorial.z = (inclination_q * y1 - x1 * inclination_p)
        * inclination_factor;

    // Astronomy Engine's fixed JUP-to-EQJ/J2000 rotation, then Taiyin's
    // internal ecliptic representation used by the semi-analytic backend.
    JetVector3 icrf;
    icrf.x = jupiter_equatorial.x * 0.999432765338654
        + jupiter_equatorial.y * 0.0303959428906285
        + jupiter_equatorial.z * -0.014499455961337;
    icrf.y = jupiter_equatorial.x * -0.0336771074697641
        + jupiter_equatorial.y * 0.902057912352809
        + jupiter_equatorial.z * -0.430299169401;
    icrf.z = jupiter_equatorial.y * 0.430543388542295
        + jupiter_equatorial.z * 0.9025698812754;
    const double cosine = std::cos(kJ2000ObliquityRad);
    const double sine = std::sin(kJ2000ObliquityRad);
    out->x = icrf.x * TAIYIN_AU_KM;
    out->y = (icrf.y * cosine + icrf.z * sine) * TAIYIN_AU_KM;
    out->z = (-icrf.y * sine + icrf.z * cosine) * TAIYIN_AU_KM;
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

bool cartesian_state_is_finite(const CartesianState& state) noexcept {
    return std::isfinite(state.position_au.x)
        && std::isfinite(state.position_au.y)
        && std::isfinite(state.position_au.z)
        && std::isfinite(state.velocity_au_per_day.x)
        && std::isfinite(state.velocity_au_per_day.y)
        && std::isfinite(state.velocity_au_per_day.z)
        && std::isfinite(state.acceleration_au_per_day2.x)
        && std::isfinite(state.acceleration_au_per_day2.y)
        && std::isfinite(state.acceleration_au_per_day2.z);
}

CartesianState cartesian_state_add(
    const CartesianState& left,
    const CartesianState& right,
    double right_scale
) noexcept {
    CartesianState result;
    result.position_au = Vector3{
        left.position_au.x + right.position_au.x * right_scale,
        left.position_au.y + right.position_au.y * right_scale,
        left.position_au.z + right.position_au.z * right_scale};
    result.velocity_au_per_day = Vector3{
        left.velocity_au_per_day.x + right.velocity_au_per_day.x * right_scale,
        left.velocity_au_per_day.y + right.velocity_au_per_day.y * right_scale,
        left.velocity_au_per_day.z + right.velocity_au_per_day.z * right_scale};
    result.acceleration_au_per_day2 = Vector3{
        left.acceleration_au_per_day2.x
            + right.acceleration_au_per_day2.x * right_scale,
        left.acceleration_au_per_day2.y
            + right.acceleration_au_per_day2.y * right_scale,
        left.acceleration_au_per_day2.z
            + right.acceleration_au_per_day2.z * right_scale};
    return result;
}

Jet2 state_component(
    const CartesianState& state,
    size_t axis
) noexcept {
    return Jet2(
        axis == 0 ? state.position_au.x
            : (axis == 1 ? state.position_au.y : state.position_au.z),
        axis == 0 ? state.velocity_au_per_day.x
            : (axis == 1 ? state.velocity_au_per_day.y
                : state.velocity_au_per_day.z),
        axis == 0 ? state.acceleration_au_per_day2.x
            : (axis == 1 ? state.acceleration_au_per_day2.y
                : state.acceleration_au_per_day2.z));
}

bool blend_cartesian_states(
    const CartesianState& first,
    const CartesianState& second,
    const Jet2& normalized_time,
    CartesianState* out
) noexcept {
    if (!out) {
        return false;
    }
    const Jet2 weight = normalized_time * normalized_time * normalized_time
        * (Jet2(10.0) - normalized_time * 15.0
            + normalized_time * normalized_time * 6.0);
    Jet2 blended[3];
    for (size_t axis = 0; axis < 3; ++axis) {
        const Jet2 a = state_component(first, axis);
        const Jet2 b = state_component(second, axis);
        blended[axis] = a + weight * (b - a);
    }
    *out = CartesianState();
    out->position_au = Vector3{
        blended[0].value, blended[1].value, blended[2].value};
    out->velocity_au_per_day = Vector3{
        blended[0].first, blended[1].first, blended[2].first};
    out->acceleration_au_per_day2 = Vector3{
        blended[0].second, blended[1].second, blended[2].second};
    return cartesian_state_is_finite(*out);
}

bool eval_legacy_planet_state(
    int target_id,
    const SplitJulianDate& jd_tdb,
    CartesianState* out
) noexcept {
    const PlanetModel* model = find_planet_model(target_id);
    JetVector3 ecliptic;
    if (!out || !model || !eval_planet_ecliptic(*model, jd_tdb, &ecliptic)) {
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
    return cartesian_state_is_finite(*out);
}

bool eval_composite_pluto_state(
    const SplitJulianDate& jd_tdb,
    CartesianState* out
) noexcept {
    const PlanetModel* legacy = find_planet_model(TAIYIN_BODY_PLUTO_BARYCENTER);
    SplitJulianDate legacy_start;
    SplitJulianDate legacy_end;
    SplitJulianDate j2000;
    if (!out || !legacy
        || !split_julian_date_from_double(legacy->jd_start, &legacy_start)
        || !split_julian_date_from_double(legacy->jd_end, &legacy_end)
        || !split_julian_date_from_double(kJ2000, &j2000)) {
        return false;
    }
    const double year = 2000.0
        + days_between_split_jd(j2000, jd_tdb) / 365.25;
    if (year >= 1600.0 && year <= 2200.0) {
        return eval_builtin_long_range_pluto_near_state(jd_tdb, out);
    }
    if (year > 1590.0 && year < 1600.0) {
        CartesianState old_state;
        CartesianState near_state;
        return eval_legacy_planet_state(
                TAIYIN_BODY_PLUTO_BARYCENTER, jd_tdb, &old_state)
            && eval_builtin_long_range_pluto_near_state(jd_tdb, &near_state)
            && blend_cartesian_states(
                old_state,
                near_state,
                Jet2((year - 1590.0) / 10.0, 1.0 / (3652.5), 0.0),
                out);
    }
    if (year > 2200.0 && year < 2210.0) {
        CartesianState near_state;
        CartesianState old_state;
        return eval_builtin_long_range_pluto_near_state(jd_tdb, &near_state)
            && eval_legacy_planet_state(
                TAIYIN_BODY_PLUTO_BARYCENTER, jd_tdb, &old_state)
            && blend_cartesian_states(
                near_state,
                old_state,
                Jet2((year - 2200.0) / 10.0, 1.0 / (3652.5), 0.0),
                out);
    }

    constexpr double kOuterBlendDays = 3652.5;
    if (jd_tdb < legacy_start || jd_tdb > legacy_end) {
        return eval_builtin_long_range_pluto_fallback_state(jd_tdb, out);
    }
    if (days_between_split_jd(legacy_start, jd_tdb) < kOuterBlendDays) {
        CartesianState far_state;
        CartesianState old_state;
        const double elapsed = days_between_split_jd(legacy_start, jd_tdb);
        return eval_builtin_long_range_pluto_fallback_state(jd_tdb, &far_state)
            && eval_legacy_planet_state(
                TAIYIN_BODY_PLUTO_BARYCENTER, jd_tdb, &old_state)
            && blend_cartesian_states(
                far_state,
                old_state,
                Jet2(elapsed / kOuterBlendDays,
                    1.0 / kOuterBlendDays, 0.0),
                out);
    }
    if (days_between_split_jd(jd_tdb, legacy_end) < kOuterBlendDays) {
        CartesianState old_state;
        CartesianState far_state;
        const double elapsed = kOuterBlendDays
            - days_between_split_jd(jd_tdb, legacy_end);
        return eval_legacy_planet_state(
                TAIYIN_BODY_PLUTO_BARYCENTER, jd_tdb, &old_state)
            && eval_builtin_long_range_pluto_fallback_state(jd_tdb, &far_state)
            && blend_cartesian_states(
                old_state,
                far_state,
                Jet2(elapsed / kOuterBlendDays,
                    1.0 / kOuterBlendDays, 0.0),
                out);
    }
    return eval_legacy_planet_state(
        TAIYIN_BODY_PLUTO_BARYCENTER, jd_tdb, out);
}

bool eval_primary_state_icrf(
    int target_id,
    int center_id,
    const SplitJulianDate& jd_tdb,
    CartesianState* out
) noexcept {
    if (!out) {
        return false;
    }
    if (target_id == TAIYIN_BODY_MOON && center_id == TAIYIN_BODY_EARTH) {
        return eval_builtin_long_range_analytic_state(
            target_id, center_id, jd_tdb, out);
    }
    if (center_id == TAIYIN_BODY_SUN) {
        if (target_id == TAIYIN_BODY_PLUTO_BARYCENTER) {
            return eval_composite_pluto_state(jd_tdb, out);
        }
        if (target_id == TAIYIN_BODY_EMB) {
            CartesianState earth;
            CartesianState moon;
            if (!eval_builtin_long_range_analytic_state(
                    TAIYIN_BODY_EARTH, TAIYIN_BODY_SUN, jd_tdb, &earth)
                || !eval_builtin_long_range_analytic_state(
                    TAIYIN_BODY_MOON, TAIYIN_BODY_EARTH, jd_tdb, &moon)) {
                return false;
            }
            *out = cartesian_state_add(
                earth,
                moon,
                1.0 / (TAIYIN_EARTH_MOON_MASS_RATIO + 1.0));
            return cartesian_state_is_finite(*out);
        }
        return eval_builtin_long_range_analytic_state(
            target_id, center_id, jd_tdb, out);
    }
    if (target_id == TAIYIN_BODY_SUN && center_id == TAIYIN_BODY_SSB) {
        const PlanetModel* legacy = find_planet_model(TAIYIN_BODY_PLUTO_BARYCENTER);
        SplitJulianDate legacy_start;
        SplitJulianDate legacy_end;
        if (!legacy
            || !split_julian_date_from_double(legacy->jd_start, &legacy_start)
            || !split_julian_date_from_double(legacy->jd_end, &legacy_end)) {
            return false;
        }
        constexpr double kSunOuterBlendDays = 3652.5;
        const bool in_legacy_range = jd_tdb >= legacy_start && jd_tdb <= legacy_end;
        const double legacy_from_start = in_legacy_range
            ? days_between_split_jd(legacy_start, jd_tdb) : 0.0;
        const double legacy_to_end = in_legacy_range
            ? days_between_split_jd(jd_tdb, legacy_end) : 0.0;
        if (in_legacy_range
            && legacy_from_start >= kSunOuterBlendDays
            && legacy_to_end >= kSunOuterBlendDays) {
            JetVector3 legacy_ecliptic;
            if (!eval_sun_ssb_ecliptic(jd_tdb, &legacy_ecliptic)) {
                return false;
            }
            const JetVector3 legacy_icrf = ecliptic_to_icrf(legacy_ecliptic);
            *out = CartesianState();
            out->position_au = Vector3{
                legacy_icrf.x.value / TAIYIN_AU_KM,
                legacy_icrf.y.value / TAIYIN_AU_KM,
                legacy_icrf.z.value / TAIYIN_AU_KM};
            out->velocity_au_per_day = Vector3{
                legacy_icrf.x.first / TAIYIN_AU_KM,
                legacy_icrf.y.first / TAIYIN_AU_KM,
                legacy_icrf.z.first / TAIYIN_AU_KM};
            out->acceleration_au_per_day2 = Vector3{
                legacy_icrf.x.second / TAIYIN_AU_KM,
                legacy_icrf.y.second / TAIYIN_AU_KM,
                legacy_icrf.z.second / TAIYIN_AU_KM};
            return cartesian_state_is_finite(*out);
        }
        CartesianState weighted = CartesianState();
        double total_gm = kSunGmKm3PerS2;
        for (size_t index = 0;
             index < sizeof(kPlanetGms) / sizeof(kPlanetGms[0]);
             ++index) {
            CartesianState state;
            if (!eval_primary_state_icrf(
                    kPlanetGms[index].body_id,
                    TAIYIN_BODY_SUN,
                    jd_tdb,
                    &state)) {
                return false;
            }
            weighted = cartesian_state_add(
                weighted, state, kPlanetGms[index].gm_km3_per_s2);
            total_gm += kPlanetGms[index].gm_km3_per_s2;
        }
        const CartesianState long_range_state = cartesian_state_add(
            CartesianState(), weighted, -1.0 / total_gm);
        if (jd_tdb < legacy_start || jd_tdb > legacy_end) {
            *out = long_range_state;
            return cartesian_state_is_finite(*out);
        }
        JetVector3 legacy_ecliptic;
        if (!eval_sun_ssb_ecliptic(jd_tdb, &legacy_ecliptic)) {
            return false;
        }
        const JetVector3 legacy_icrf = ecliptic_to_icrf(legacy_ecliptic);
        CartesianState legacy_state;
        legacy_state.position_au = Vector3{
            legacy_icrf.x.value / TAIYIN_AU_KM,
            legacy_icrf.y.value / TAIYIN_AU_KM,
            legacy_icrf.z.value / TAIYIN_AU_KM};
        legacy_state.velocity_au_per_day = Vector3{
            legacy_icrf.x.first / TAIYIN_AU_KM,
            legacy_icrf.y.first / TAIYIN_AU_KM,
            legacy_icrf.z.first / TAIYIN_AU_KM};
        legacy_state.acceleration_au_per_day2 = Vector3{
            legacy_icrf.x.second / TAIYIN_AU_KM,
            legacy_icrf.y.second / TAIYIN_AU_KM,
            legacy_icrf.z.second / TAIYIN_AU_KM};
        const double from_start = days_between_split_jd(legacy_start, jd_tdb);
        if (from_start < kSunOuterBlendDays) {
            return blend_cartesian_states(
                long_range_state,
                legacy_state,
                Jet2(from_start / kSunOuterBlendDays,
                    1.0 / kSunOuterBlendDays,
                    0.0),
                out);
        }
        const double to_end = days_between_split_jd(jd_tdb, legacy_end);
        if (to_end < kSunOuterBlendDays) {
            return blend_cartesian_states(
                legacy_state,
                long_range_state,
                Jet2((kSunOuterBlendDays - to_end) / kSunOuterBlendDays,
                    1.0 / kSunOuterBlendDays,
                    0.0),
                out);
        }
        *out = legacy_state;
        return cartesian_state_is_finite(*out);
    }
    return false;
}

bool is_primary_state_route(int target_id, int center_id) noexcept {
    if ((target_id == TAIYIN_BODY_SUN && center_id == TAIYIN_BODY_SSB)
        || (target_id == TAIYIN_BODY_EMB && center_id == TAIYIN_BODY_SUN)) {
        return true;
    }
    double start = 0.0;
    double end = 0.0;
    return get_builtin_long_range_analytic_coverage(
        target_id, center_id, &start, &end);
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
    if (target_id == TAIYIN_BODY_PHOBOS && center_id == TAIYIN_BODY_MARS) {
        return eval_satellite_residual_relative_ecliptic(
            kPhobosMar099Table, jd_tdb, out);
    }
    if (target_id == TAIYIN_BODY_DEIMOS && center_id == TAIYIN_BODY_MARS) {
        return eval_satellite_residual_relative_ecliptic(
            kDeimosMar099Table, jd_tdb, out);
    }
    if (target_id == TAIYIN_BODY_MARS
        && center_id == TAIYIN_BODY_MARS_BARYCENTER) {
        JetVector3 phobos;
        JetVector3 deimos;
        if (!eval_satellite_residual_relative_ecliptic(
                kPhobosMar099Table, jd_tdb, &phobos)
            || !eval_satellite_residual_relative_ecliptic(
                kDeimosMar099Table, jd_tdb, &deimos)) {
            return false;
        }
        *out = vector_add(JetVector3(), phobos, -kMarsPhobosMassFraction);
        *out = vector_add(*out, deimos, -kMarsDeimosMassFraction);
        return true;
    }
    if (target_id >= TAIYIN_BODY_IO && target_id <= TAIYIN_BODY_CALLISTO
        && center_id == TAIYIN_BODY_JUPITER) {
        return eval_astronomy_engine_jupiter_moon_ecliptic(
            target_id, jd_tdb, out);
    }
    if (target_id == TAIYIN_BODY_JUPITER
        && center_id == TAIYIN_BODY_JUPITER_BARYCENTER) {
        JetVector3 io;
        JetVector3 europa;
        JetVector3 ganymede;
        JetVector3 callisto;
        if (!eval_astronomy_engine_jupiter_moon_ecliptic(
                TAIYIN_BODY_IO, jd_tdb, &io)
            || !eval_astronomy_engine_jupiter_moon_ecliptic(
                TAIYIN_BODY_EUROPA, jd_tdb, &europa)
            || !eval_astronomy_engine_jupiter_moon_ecliptic(
                TAIYIN_BODY_GANYMEDE, jd_tdb, &ganymede)
            || !eval_astronomy_engine_jupiter_moon_ecliptic(
                TAIYIN_BODY_CALLISTO, jd_tdb, &callisto)) {
            return false;
        }
        *out = vector_add(JetVector3(), io, -kJupiterIoMassFraction);
        *out = vector_add(*out, europa, -kJupiterEuropaMassFraction);
        *out = vector_add(*out, ganymede, -kJupiterGanymedeMassFraction);
        *out = vector_add(*out, callisto, -kJupiterCallistoMassFraction);
        return true;
    }
    if (target_id == TAIYIN_BODY_CHARON && center_id == TAIYIN_BODY_PLUTO) {
        return eval_satellite_residual_relative_ecliptic(
            kCharonPlu060Table, jd_tdb, out);
    }
    if (target_id == TAIYIN_BODY_NIX && center_id == TAIYIN_BODY_PLUTO) {
        return eval_satellite_poisson_relative_ecliptic(
            kNixPlu060Table, jd_tdb, out);
    }
    if (target_id == TAIYIN_BODY_HYDRA && center_id == TAIYIN_BODY_PLUTO) {
        return eval_satellite_poisson_relative_ecliptic(
            kHydraPlu060Table, jd_tdb, out);
    }
    if (target_id == TAIYIN_BODY_KERBEROS && center_id == TAIYIN_BODY_PLUTO) {
        return eval_satellite_poisson_relative_ecliptic(
            kKerberosPlu060Table, jd_tdb, out);
    }
    if (target_id == TAIYIN_BODY_STYX && center_id == TAIYIN_BODY_PLUTO) {
        return eval_satellite_poisson_relative_ecliptic(
            kStyxPlu060Table, jd_tdb, out);
    }
    if (target_id == TAIYIN_BODY_PLUTO
        && center_id == TAIYIN_BODY_PLUTO_BARYCENTER) {
        JetVector3 charon;
        JetVector3 nix;
        JetVector3 hydra;
        JetVector3 kerberos;
        JetVector3 styx;
        if (!eval_satellite_residual_relative_ecliptic(
                kCharonPlu060Table, jd_tdb, &charon)
            || !eval_satellite_poisson_relative_ecliptic(
                kNixPlu060Table, jd_tdb, &nix)
            || !eval_satellite_poisson_relative_ecliptic(
                kHydraPlu060Table, jd_tdb, &hydra)
            || !eval_satellite_poisson_relative_ecliptic(
                kKerberosPlu060Table, jd_tdb, &kerberos)
            || !eval_satellite_poisson_relative_ecliptic(
                kStyxPlu060Table, jd_tdb, &styx)) {
            return false;
        }
        *out = vector_add(JetVector3(), charon, -kPlutoCharonMassFraction);
        *out = vector_add(*out, nix, -kPlutoNixMassFraction);
        *out = vector_add(*out, hydra, -kPlutoHydraMassFraction);
        *out = vector_add(*out, kerberos, -kPlutoKerberosMassFraction);
        *out = vector_add(*out, styx, -kPlutoStyxMassFraction);
        return true;
    }
    if (target_id == TAIYIN_BODY_TRITON && center_id == TAIYIN_BODY_NEPTUNE) {
        return eval_satellite_residual_relative_ecliptic(
            kTritonNep098Table, jd_tdb, out);
    }
    if (target_id == TAIYIN_BODY_NEPTUNE
        && center_id == TAIYIN_BODY_NEPTUNE_BARYCENTER) {
        JetVector3 triton;
        if (!eval_satellite_residual_relative_ecliptic(
                kTritonNep098Table, jd_tdb, &triton)) {
            return false;
        }
        *out = vector_add(JetVector3(), triton, -kNeptuneTritonMassFraction);
        return true;
    }
    if (target_id == TAIYIN_BODY_SUN && center_id == TAIYIN_BODY_SSB) {
        return eval_sun_ssb_ecliptic(jd_tdb, out);
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
    if (is_primary_state_route(data->target_id, data->center_id)) {
        // Primary routes are authoritative throughout their advertised
        // coverage.  Do not hide a model failure by silently reviving the
        // former compact planet or lunar implementation below.
        return eval_primary_state_icrf(
            data->target_id, data->center_id, jd_tdb, out);
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
    return builtin_long_range_analytic_source_revision();
}

const char* builtin_semi_analytic_coefficients_sha256() noexcept {
    return builtin_long_range_analytic_coefficients_sha256();
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
    double long_range_start = 0.0;
    double long_range_end = 0.0;
    if ((target_id == TAIYIN_BODY_SUN && center_id == TAIYIN_BODY_SSB)
        || (target_id == TAIYIN_BODY_EMB && center_id == TAIYIN_BODY_SUN)) {
        if (!get_builtin_long_range_analytic_coverage(
                TAIYIN_BODY_EARTH,
                TAIYIN_BODY_SUN,
                &long_range_start,
                &long_range_end)) {
            return false;
        }
        *out_jd_tdb_start = long_range_start;
        *out_jd_tdb_end = long_range_end;
        return true;
    }
    if (get_builtin_long_range_analytic_coverage(
            target_id,
            center_id,
            &long_range_start,
            &long_range_end)) {
        *out_jd_tdb_start = long_range_start;
        *out_jd_tdb_end = long_range_end;
        return true;
    }
    if (target_id == TAIYIN_BODY_SUN && center_id == TAIYIN_BODY_SSB) {
        double start = -INFINITY;
        double end = INFINITY;
        for (size_t index = 0;
             index < sizeof(kPlanetGms) / sizeof(kPlanetGms[0]);
             ++index) {
            const PlanetModel* model = find_planet_model(
                kPlanetGms[index].body_id);
            if (!model) {
                return false;
            }
            start = std::max(start, model->jd_start);
            end = std::min(end, model->jd_end);
        }
        if (!(end > start)) {
            return false;
        }
        *out_jd_tdb_start = start;
        *out_jd_tdb_end = end;
        return true;
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
    if (target_id == TAIYIN_BODY_PHOBOS && center_id == TAIYIN_BODY_MARS) {
        return satellite_residual_coverage(
            kPhobosMar099Table, out_jd_tdb_start, out_jd_tdb_end);
    }
    if (target_id == TAIYIN_BODY_DEIMOS && center_id == TAIYIN_BODY_MARS) {
        return satellite_residual_coverage(
            kDeimosMar099Table, out_jd_tdb_start, out_jd_tdb_end);
    }
    if (target_id == TAIYIN_BODY_MARS
        && center_id == TAIYIN_BODY_MARS_BARYCENTER) {
        double phobos_start = 0.0;
        double phobos_end = 0.0;
        double deimos_start = 0.0;
        double deimos_end = 0.0;
        if (!satellite_residual_coverage(
                kPhobosMar099Table, &phobos_start, &phobos_end)
            || !satellite_residual_coverage(
                kDeimosMar099Table, &deimos_start, &deimos_end)) {
            return false;
        }
        *out_jd_tdb_start = std::max(phobos_start, deimos_start);
        *out_jd_tdb_end = std::min(phobos_end, deimos_end);
        return *out_jd_tdb_end >= *out_jd_tdb_start;
    }
    if (((target_id >= TAIYIN_BODY_IO
            && target_id <= TAIYIN_BODY_CALLISTO)
            && center_id == TAIYIN_BODY_JUPITER)
        || (target_id == TAIYIN_BODY_JUPITER
            && center_id == TAIYIN_BODY_JUPITER_BARYCENTER)) {
        *out_jd_tdb_start = kJupiterL1ValidatedStart;
        *out_jd_tdb_end = kJupiterL1ValidatedEnd;
        return true;
    }
    if (target_id == TAIYIN_BODY_CHARON && center_id == TAIYIN_BODY_PLUTO) {
        return satellite_residual_coverage(
            kCharonPlu060Table, out_jd_tdb_start, out_jd_tdb_end);
    }
    if (target_id == TAIYIN_BODY_NIX && center_id == TAIYIN_BODY_PLUTO) {
        return satellite_poisson_coverage(
            kNixPlu060Table, out_jd_tdb_start, out_jd_tdb_end);
    }
    if (target_id == TAIYIN_BODY_HYDRA && center_id == TAIYIN_BODY_PLUTO) {
        return satellite_poisson_coverage(
            kHydraPlu060Table, out_jd_tdb_start, out_jd_tdb_end);
    }
    if (target_id == TAIYIN_BODY_KERBEROS && center_id == TAIYIN_BODY_PLUTO) {
        return satellite_poisson_coverage(
            kKerberosPlu060Table, out_jd_tdb_start, out_jd_tdb_end);
    }
    if (target_id == TAIYIN_BODY_STYX && center_id == TAIYIN_BODY_PLUTO) {
        return satellite_poisson_coverage(
            kStyxPlu060Table, out_jd_tdb_start, out_jd_tdb_end);
    }
    if (target_id == TAIYIN_BODY_PLUTO
        && center_id == TAIYIN_BODY_PLUTO_BARYCENTER) {
        const SatellitePoissonTable* tables[] = {
            &kNixPlu060Table, &kHydraPlu060Table,
            &kKerberosPlu060Table, &kStyxPlu060Table};
        if (!satellite_residual_coverage(
                kCharonPlu060Table, out_jd_tdb_start, out_jd_tdb_end)) {
            return false;
        }
        for (size_t index = 0; index < sizeof(tables) / sizeof(tables[0]); ++index) {
            double start = 0.0;
            double end = 0.0;
            if (!satellite_poisson_coverage(*tables[index], &start, &end)) {
                return false;
            }
            *out_jd_tdb_start = std::max(*out_jd_tdb_start, start);
            *out_jd_tdb_end = std::min(*out_jd_tdb_end, end);
        }
        return *out_jd_tdb_end >= *out_jd_tdb_start;
    }
    if ((target_id == TAIYIN_BODY_TRITON && center_id == TAIYIN_BODY_NEPTUNE)
        || (target_id == TAIYIN_BODY_NEPTUNE
            && center_id == TAIYIN_BODY_NEPTUNE_BARYCENTER)) {
        return satellite_residual_coverage(
            kTritonNep098Table, out_jd_tdb_start, out_jd_tdb_end);
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
    const double positive_infinity =
        std::numeric_limits<double>::infinity();
    if (!get_builtin_semi_analytic_coverage(
            target_id, center_id, &coverage_start, &coverage_end)
        || jd_tdb_start < coverage_start
        || jd_tdb_end > std::nextafter(coverage_end, positive_infinity)) {
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
    data->jd_tdb_end = std::min(jd_tdb_end, coverage_end);
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
