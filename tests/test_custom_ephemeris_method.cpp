#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/internal/custom_ephemeris_method.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <new>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

const int METHOD_SIMPLE_MERCURY = 88101;
const int METHOD_SERIES_MERCURY = 88102;
const int METHOD_FILE_VECTOR = 88103;
const int METHOD_POSITION_ONLY = 88104;
const int FILE_TARGET_A = 910101;
const int FILE_TARGET_B = 910102;
const int POSITION_ONLY_TARGET = 910103;
const double JD0 = taiyin::JD_J2000;
const double DAYS_PER_MILLENNIUM = taiyin::DAYS_PER_JULIAN_MILLENNIUM;

// Tiny VSOP87-shaped constants used only as a realistic custom-method fixture.
// Taiyin does not ship an in-core VSOP87 planetary backend here.
const double SERIES_BASE_PHASE[12] = {
    4.40260884240, 3.17614669689, 1.75347045953, 6.20347611291,
    0.59954649739, 0.87401675650, 5.48129387159, 5.31188628676,
    5.19846674103, 1.62790523337, 2.35555589827, 3.81034454697,
};

const double SERIES_PHASE_RATE[12] = {
    26087.9031415742, 10213.2855462110, 6283.0758499914, 3340.6124266998,
    529.6909650946, 213.2990954380, 74.7815985673, 38.1330356378,
    77713.7714681205, 84334.6615813083, 83286.9142695536, 83997.0911355954,
};

const double ECLIPTIC_TO_EQUATORIAL[3][3] = {
    { 1.000000000000,  0.000000440360, -0.000000190919 },
    { -0.000000479966, 0.917482137087, -0.397776982902 },
    { 0.000000000000,  0.397776982902,  0.917482137087 },
};

struct SimpleOrbitData {
    double radius_au;
};

struct SeriesTerm {
    int variable;
    int alpha;
    int multipliers[12];
    double sine_coeff;
    double cosine_coeff;
};

struct SeriesMercuryData {
    std::vector<SeriesTerm> terms;
    int* destroy_count;
};

struct FileVectorData {
    double x;
    double y;
    double z;
};

struct QuadraticPositionData {
    double epoch_jd;
    double x0;
    double y0;
    double z0;
    double vx;
    double vy;
    double vz;
    double ax;
    double ay;
    double az;
};

int g_series_destroy_count = 0;
int g_file_load_count = 0;
int g_file_destroy_count = 0;

void expect_true(bool value, const char* label) {
    if (!value) {
        std::fprintf(stderr, "expected true: %s\n", label);
        assert(false);
    }
}

taiyin::SplitJulianDate split_jd(double jd) {
    taiyin::SplitJulianDate out;
    expect_true(taiyin::split_julian_date_from_double(jd, &out), "split JD");
    return out;
}

void expect_false(bool value, const char* label) {
    if (value) {
        std::fprintf(stderr, "expected false: %s\n", label);
        assert(false);
    }
}

void expect_int(int actual, int expected, const char* label) {
    if (actual != expected) {
        std::fprintf(stderr, "expected int %d got %d: %s\n", expected, actual, label);
        assert(false);
    }
}

void expect_string_equal(const std::string& actual, const std::string& expected, const char* label) {
    if (actual != expected) {
        std::fprintf(
            stderr,
            "expected string %s got %s: %s\n",
            expected.c_str(),
            actual.c_str(),
            label);
        assert(false);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label) {
    if (std::fabs(actual - expected) > tolerance) {
        std::fprintf(
            stderr,
            "expected near %.17g got %.17g tolerance %.17g: %s\n",
            expected,
            actual,
            tolerance,
            label);
        assert(false);
    }
}

void ecliptic_to_equatorial(const taiyin::Vector3& ecliptic, taiyin::Vector3* out) {
    out->x = ECLIPTIC_TO_EQUATORIAL[0][0] * ecliptic.x
        + ECLIPTIC_TO_EQUATORIAL[0][1] * ecliptic.y
        + ECLIPTIC_TO_EQUATORIAL[0][2] * ecliptic.z;
    out->y = ECLIPTIC_TO_EQUATORIAL[1][0] * ecliptic.x
        + ECLIPTIC_TO_EQUATORIAL[1][1] * ecliptic.y
        + ECLIPTIC_TO_EQUATORIAL[1][2] * ecliptic.z;
    out->z = ECLIPTIC_TO_EQUATORIAL[2][0] * ecliptic.x
        + ECLIPTIC_TO_EQUATORIAL[2][1] * ecliptic.y
        + ECLIPTIC_TO_EQUATORIAL[2][2] * ecliptic.z;
}

SeriesTerm make_term(
    int variable,
    int alpha,
    int multiplier_index,
    int multiplier,
    double sine_coeff,
    double cosine_coeff
) {
    SeriesTerm term;
    term.variable = variable;
    term.alpha = alpha;
    for (int i = 0; i < 12; ++i) {
        term.multipliers[i] = 0;
    }
    if (multiplier_index >= 0 && multiplier_index < 12) {
        term.multipliers[multiplier_index] = multiplier;
    }
    term.sine_coeff = sine_coeff;
    term.cosine_coeff = cosine_coeff;
    return term;
}

size_t series_bytes(const SeriesMercuryData* data) {
    return sizeof(SeriesMercuryData)
        + (data ? data->terms.size() * sizeof(SeriesTerm) : 0);
}

bool series_clone(
    const void* source_data,
    size_t,
    void** out_data,
    size_t* out_bytes
) {
    if (out_data) {
        *out_data = 0;
    }
    if (out_bytes) {
        *out_bytes = 0;
    }
    if (!source_data || !out_data || !out_bytes) {
        return false;
    }
    const SeriesMercuryData* source = static_cast<const SeriesMercuryData*>(source_data);
    SeriesMercuryData* clone = new (std::nothrow) SeriesMercuryData(*source);
    if (!clone) {
        return false;
    }
    *out_data = clone;
    *out_bytes = series_bytes(clone);
    return true;
}

void series_destroy(void* data) {
    SeriesMercuryData* mercury = static_cast<SeriesMercuryData*>(data);
    if (mercury && mercury->destroy_count) {
        ++(*mercury->destroy_count);
    }
    delete mercury;
}

void compute_series_mercury_position(
    double jd_tdb,
    const SeriesMercuryData* data,
    taiyin::Vector3* out
) {
    const double t = (jd_tdb - JD0) / DAYS_PER_MILLENNIUM;
    double lambda[12];
    for (int i = 0; i < 12; ++i) {
        lambda[i] = SERIES_BASE_PHASE[i] + SERIES_PHASE_RATE[i] * t;
    }

    double powers[6];
    powers[0] = 1.0;
    for (int i = 1; i < 6; ++i) {
        powers[i] = powers[i - 1] * t;
    }

    double pos[3] = { 0.0, 0.0, 0.0 };
    for (size_t i = 0; i < data->terms.size(); ++i) {
        const SeriesTerm& term = data->terms[i];
        double phi = 0.0;
        for (int j = 0; j < 12; ++j) {
            phi += static_cast<double>(term.multipliers[j]) * lambda[j];
        }
        pos[term.variable - 1] += powers[term.alpha]
            * (term.sine_coeff * std::sin(phi) + term.cosine_coeff * std::cos(phi));
    }

    taiyin::Vector3 ecliptic;
    ecliptic.x = pos[0];
    ecliptic.y = pos[1];
    ecliptic.z = pos[2];
    ecliptic_to_equatorial(ecliptic, out);
}

bool series_mercury_position(const taiyin::SplitJulianDate& jd_tdb, const void* data, taiyin::Vector3* out) {
    if (!data || !out) {
        return false;
    }
    compute_series_mercury_position(
        taiyin::split_julian_date_to_double(jd_tdb),
        static_cast<const SeriesMercuryData*>(data),
        out);
    return true;
}

bool series_mercury_velocity(const taiyin::SplitJulianDate& jd_tdb, const void* data, taiyin::Vector3* out) {
    if (!data || !out) {
        return false;
    }
    const double h = 1e-3;
    taiyin::Vector3 p_minus;
    taiyin::Vector3 p_plus;
    compute_series_mercury_position(
        taiyin::split_julian_date_to_double(jd_tdb) - h,
        static_cast<const SeriesMercuryData*>(data),
        &p_minus);
    compute_series_mercury_position(
        taiyin::split_julian_date_to_double(jd_tdb) + h,
        static_cast<const SeriesMercuryData*>(data),
        &p_plus);
    out->x = (p_plus.x - p_minus.x) / (2.0 * h);
    out->y = (p_plus.y - p_minus.y) / (2.0 * h);
    out->z = (p_plus.z - p_minus.z) / (2.0 * h);
    return true;
}

bool series_mercury_acceleration(const taiyin::SplitJulianDate& jd_tdb, const void* data, taiyin::Vector3* out) {
    if (!data || !out) {
        return false;
    }
    const double h = 1e-3;
    taiyin::Vector3 p0;
    taiyin::Vector3 p_minus;
    taiyin::Vector3 p_plus;
    const double scalar_jd_tdb = taiyin::split_julian_date_to_double(jd_tdb);
    compute_series_mercury_position(scalar_jd_tdb, static_cast<const SeriesMercuryData*>(data), &p0);
    compute_series_mercury_position(scalar_jd_tdb - h, static_cast<const SeriesMercuryData*>(data), &p_minus);
    compute_series_mercury_position(scalar_jd_tdb + h, static_cast<const SeriesMercuryData*>(data), &p_plus);
    out->x = (p_plus.x - 2.0 * p0.x + p_minus.x) / (h * h);
    out->y = (p_plus.y - 2.0 * p0.y + p_minus.y) / (h * h);
    out->z = (p_plus.z - 2.0 * p0.z + p_minus.z) / (h * h);
    return true;
}

bool simple_mercury_position(const taiyin::SplitJulianDate&, const void* data, taiyin::Vector3* out) {
    const SimpleOrbitData* simple = static_cast<const SimpleOrbitData*>(data);
    if (!simple || !out) {
        return false;
    }
    out->x = simple->radius_au;
    out->y = 0.0;
    out->z = 0.0;
    return true;
}

bool quadratic_position(const taiyin::SplitJulianDate& jd_tdb, const void* data, taiyin::Vector3* out) {
    const QuadraticPositionData* quadratic = static_cast<const QuadraticPositionData*>(data);
    if (!quadratic || !out) {
        return false;
    }
    const double dt = taiyin::days_between_split_jd_and_double(jd_tdb, quadratic->epoch_jd) * -1.0;
    out->x = quadratic->x0 + quadratic->vx * dt + 0.5 * quadratic->ax * dt * dt;
    out->y = quadratic->y0 + quadratic->vy * dt + 0.5 * quadratic->ay * dt * dt;
    out->z = quadratic->z0 + quadratic->vz * dt + 0.5 * quadratic->az * dt * dt;
    return true;
}

std::string make_temp_file_path(const char* name) {
    char templ[256];
    std::snprintf(templ, sizeof(templ), "/tmp/taiyin-%s-XXXXXX", name);
    const int fd = mkstemp(templ);
    if (fd >= 0) {
        close(fd);
    }
    return std::string(templ);
}

void write_vector_file(const std::string& path, double x, double y, double z) {
    std::ofstream file(path.c_str(), std::ios::out | std::ios::trunc);
    file.precision(17);
    file << x << "\n" << y << "\n" << z << "\n";
}

bool load_vector_file(const char* path, void** out_data, size_t* out_bytes) {
    if (out_data) {
        *out_data = 0;
    }
    if (out_bytes) {
        *out_bytes = 0;
    }
    if (!path || !out_data || !out_bytes) {
        return false;
    }

    std::ifstream file(path);
    if (!file) {
        return false;
    }

    FileVectorData* data = new (std::nothrow) FileVectorData();
    if (!data) {
        return false;
    }
    if (!(file >> data->x >> data->y >> data->z)) {
        delete data;
        return false;
    }

    ++g_file_load_count;
    *out_data = data;
    *out_bytes = sizeof(FileVectorData);
    return true;
}

void destroy_vector_file_data(void* data) {
    delete static_cast<FileVectorData*>(data);
    ++g_file_destroy_count;
}

bool file_vector_position(const taiyin::SplitJulianDate&, const void* data, taiyin::Vector3* out) {
    const FileVectorData* vector = static_cast<const FileVectorData*>(data);
    if (!vector || !out) {
        return false;
    }
    out->x = vector->x;
    out->y = vector->y;
    out->z = vector->z;
    return true;
}

bool file_vector_velocity(const taiyin::SplitJulianDate&, const void*, taiyin::Vector3* out) {
    if (!out) {
        return false;
    }
    out->x = 0.0;
    out->y = 0.0;
    out->z = 0.0;
    return true;
}

bool file_vector_acceleration(const taiyin::SplitJulianDate&, const void*, taiyin::Vector3* out) {
    if (!out) {
        return false;
    }
    out->x = 0.0;
    out->y = 0.0;
    out->z = 0.0;
    return true;
}

taiyin::internal::CustomEphemerisMethodDefinition make_common_definition(int method_id) {
    taiyin::internal::CustomEphemerisMethodDefinition definition;
    definition.target_id = taiyin::TAIYIN_BODY_MERCURY_BARYCENTER;
    definition.center_id = taiyin::TAIYIN_BODY_SUN;
    definition.method_id = method_id;
    definition.frame = taiyin::internal::IcrfJ2000Equatorial;
    definition.jd_tdb_start = JD0 - 100.0;
    definition.jd_tdb_end = JD0 + 100.0;
    return definition;
}

taiyin::internal::CustomEphemerisFileMethodDefinition make_file_definition(
    int target_id,
    const std::string& path
) {
    taiyin::internal::CustomEphemerisFileMethodDefinition definition;
    definition.target_id = target_id;
    definition.center_id = taiyin::TAIYIN_BODY_SUN;
    definition.method_id = METHOD_FILE_VECTOR;
    definition.frame = taiyin::internal::IcrfJ2000Equatorial;
    definition.jd_tdb_start = JD0 - 10.0;
    definition.jd_tdb_end = JD0 + 10.0;
    definition.path = path.c_str();
    definition.load = load_vector_file;
    definition.position = file_vector_position;
    definition.velocity = file_vector_velocity;
    definition.acceleration = file_vector_acceleration;
    definition.destroy = destroy_vector_file_data;
    definition.description = "file-backed vector method";
    return definition;
}

taiyin::internal::CustomEphemerisMethodDefinition make_position_only_definition(
    const QuadraticPositionData* data
) {
    taiyin::internal::CustomEphemerisMethodDefinition definition;
    definition.target_id = POSITION_ONLY_TARGET;
    definition.center_id = taiyin::TAIYIN_BODY_SUN;
    definition.method_id = METHOD_POSITION_ONLY;
    definition.frame = taiyin::internal::IcrfJ2000Equatorial;
    definition.jd_tdb_start = JD0 - 10.0;
    definition.jd_tdb_end = JD0 + 10.0;
    definition.data = data;
    definition.bytes = sizeof(QuadraticPositionData);
    definition.position = quadratic_position;
    definition.description = "position-only quadratic method";
    return definition;
}

}  // namespace

int main() {
    using taiyin::internal::EphemerisBlockDescriptor;
    using taiyin::runtime::EphemerisEvalDiagnostic;
    using taiyin::runtime::EphemerisRequest;
    using taiyin::runtime::EphemerisResult;

    taiyin::internal::clear_custom_ephemeris_methods();

    taiyin::runtime::EphemerisRuntimeConfig config;
    config.segment_cache_max_entries = 1;
    config.load_packaged_data = false;
    expect_true(taiyin::runtime::initialize_global_ephemeris_runtime(config), "initialize empty runtime");

    SimpleOrbitData simple;
    simple.radius_au = 9.0;
    taiyin::internal::CustomEphemerisMethodDefinition simple_definition =
        make_common_definition(METHOD_SIMPLE_MERCURY);
    simple_definition.data = &simple;
    simple_definition.bytes = sizeof(simple);
    simple_definition.position = simple_mercury_position;
    simple_definition.description = "simple low-priority mercury";

    EphemerisBlockDescriptor simple_descriptor;
    expect_true(
        taiyin::runtime::add_global_custom_ephemeris_method(
            simple_definition,
            100,
            "simple low-priority mercury",
            &simple_descriptor),
        "install low-priority custom method");

    SeriesMercuryData series;
    series.destroy_count = &g_series_destroy_count;
    series.terms.push_back(make_term(1, 0, 0, 1, 0.015, 0.310));
    series.terms.push_back(make_term(2, 0, 0, 1, -0.020, 0.190));
    series.terms.push_back(make_term(3, 0, 8, 1, 0.004, 0.012));
    series.terms.push_back(make_term(1, 1, -1, 0, 0.000, 0.020));
    series.terms.push_back(make_term(2, 1, -1, 0, 0.010, 0.000));

    taiyin::internal::CustomEphemerisMethodDefinition series_definition =
        make_common_definition(METHOD_SERIES_MERCURY);
    series_definition.data = &series;
    series_definition.bytes = series_bytes(&series);
    series_definition.position = series_mercury_position;
    series_definition.velocity = series_mercury_velocity;
    series_definition.acceleration = series_mercury_acceleration;
    series_definition.clone = series_clone;
    series_definition.destroy = series_destroy;
    series_definition.description = "reduced planetary-series Mercury";

    EphemerisBlockDescriptor series_descriptor;
    expect_true(
        taiyin::runtime::add_global_custom_ephemeris_method(
            series_definition,
            1000,
            "reduced planetary-series Mercury",
            &series_descriptor),
        "install high-priority custom planetary-series method");

    EphemerisRequest request;
    request.target_id = taiyin::TAIYIN_BODY_MERCURY_BARYCENTER;
    request.center_id = taiyin::TAIYIN_BODY_SUN;
    request.frame = taiyin::internal::IcrfJ2000Equatorial;
    request.jd_tdb = split_jd(JD0 + 23.5);

    EphemerisBlockDescriptor found;
    expect_true(taiyin::runtime::find_global_ephemeris_descriptor(request, &found), "find custom descriptor");
    expect_int(found.method_id, METHOD_SERIES_MERCURY, "descriptor search follows route rule priority");

    EphemerisResult result;
    EphemerisEvalDiagnostic diagnostic;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(request, &result, &diagnostic)),
        "eval custom planetary-series method");
    expect_int(result.descriptor.method_id, METHOD_SERIES_MERCURY, "eval selected high-priority custom method");
    expect_false(result.cache_hit, "first custom eval loads source segment");

    taiyin::Vector3 expected_position;
    taiyin::Vector3 expected_velocity;
    taiyin::Vector3 expected_acceleration;
    expect_true(series_mercury_position(request.jd_tdb, &series, &expected_position), "expected position");
    expect_true(series_mercury_velocity(request.jd_tdb, &series, &expected_velocity), "expected velocity");
    expect_true(series_mercury_acceleration(request.jd_tdb, &series, &expected_acceleration), "expected acceleration");

    expect_near(result.state.position_au.x, expected_position.x, 1e-13, "custom position x");
    expect_near(result.state.position_au.y, expected_position.y, 1e-13, "custom position y");
    expect_near(result.state.position_au.z, expected_position.z, 1e-13, "custom position z");
    expect_near(result.state.velocity_au_per_day.x, expected_velocity.x, 1e-13, "custom velocity x");
    expect_near(result.state.velocity_au_per_day.y, expected_velocity.y, 1e-13, "custom velocity y");
    expect_near(result.state.velocity_au_per_day.z, expected_velocity.z, 1e-13, "custom velocity z");
    expect_near(result.state.acceleration_au_per_day2.x, expected_acceleration.x, 1e-13, "custom acceleration x");
    expect_near(result.state.acceleration_au_per_day2.y, expected_acceleration.y, 1e-13, "custom acceleration y");
    expect_near(result.state.acceleration_au_per_day2.z, expected_acceleration.z, 1e-13, "custom acceleration z");

    EphemerisResult cached_result;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(request, &cached_result, &diagnostic)),
        "eval custom planetary-series method with reused source segment");
    expect_true(cached_result.cache_hit, "second custom eval reuses source segment");
    expect_int(cached_result.descriptor.method_id, METHOD_SERIES_MERCURY, "cached eval selected custom method");

    QuadraticPositionData quadratic;
    quadratic.epoch_jd = JD0;
    quadratic.x0 = 0.125;
    quadratic.y0 = -0.25;
    quadratic.z0 = 0.5;
    quadratic.vx = 0.01;
    quadratic.vy = -0.02;
    quadratic.vz = 0.03;
    quadratic.ax = 0.0004;
    quadratic.ay = -0.0005;
    quadratic.az = 0.0006;

    EphemerisBlockDescriptor position_only_descriptor;
    expect_true(
        taiyin::runtime::add_global_custom_ephemeris_method(
            make_position_only_definition(&quadratic),
            1300,
            "position-only quadratic method",
            &position_only_descriptor),
        "install position-only custom method");

    EphemerisRequest position_only_request;
    position_only_request.target_id = POSITION_ONLY_TARGET;
    position_only_request.center_id = taiyin::TAIYIN_BODY_SUN;
    position_only_request.frame = taiyin::internal::IcrfJ2000Equatorial;
    position_only_request.jd_tdb = split_jd(JD0 + 2.0);

    EphemerisResult position_only_result;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            position_only_request,
            &position_only_result,
            &diagnostic)),
        "eval position-only custom method with finite-difference derivatives");
    expect_int(position_only_result.descriptor.method_id, METHOD_POSITION_ONLY, "position-only method selected");

    const double dt = -taiyin::days_between_split_jd_and_double(
        position_only_request.jd_tdb,
        quadratic.epoch_jd);
    expect_near(
        position_only_result.state.position_au.x,
        quadratic.x0 + quadratic.vx * dt + 0.5 * quadratic.ax * dt * dt,
        1e-14,
        "position-only position x");
    expect_near(position_only_result.state.velocity_au_per_day.x, quadratic.vx + quadratic.ax * dt, 1e-8, "fallback velocity x");
    expect_near(position_only_result.state.velocity_au_per_day.y, quadratic.vy + quadratic.ay * dt, 1e-8, "fallback velocity y");
    expect_near(position_only_result.state.velocity_au_per_day.z, quadratic.vz + quadratic.az * dt, 1e-8, "fallback velocity z");
    expect_near(position_only_result.state.acceleration_au_per_day2.x, quadratic.ax, 1e-7, "fallback acceleration x");
    expect_near(position_only_result.state.acceleration_au_per_day2.y, quadratic.ay, 1e-7, "fallback acceleration y");
    expect_near(position_only_result.state.acceleration_au_per_day2.z, quadratic.az, 1e-7, "fallback acceleration z");

    const std::string path_a = make_temp_file_path("custom-file-a");
    const std::string path_b = make_temp_file_path("custom-file-b");
    write_vector_file(path_a, 1.25, 2.5, 3.75);
    write_vector_file(path_b, -4.0, 5.0, -6.0);

    EphemerisBlockDescriptor file_descriptor_a;
    EphemerisBlockDescriptor file_descriptor_b;
    expect_true(
        taiyin::runtime::add_global_custom_ephemeris_file_method(
            make_file_definition(FILE_TARGET_A, path_a),
            1200,
            "file-backed vector method",
            &file_descriptor_a),
        "install file-backed method A");
    expect_true(
        taiyin::runtime::add_global_custom_ephemeris_file_method(
            make_file_definition(FILE_TARGET_B, path_b),
            1200,
            "file-backed vector method",
            &file_descriptor_b),
        "install file-backed method B");
    expect_string_equal(file_descriptor_a.path, path_a, "file descriptor A keeps path");
    expect_string_equal(file_descriptor_b.path, path_b, "file descriptor B keeps path");

    EphemerisRequest file_request_a;
    file_request_a.target_id = FILE_TARGET_A;
    file_request_a.center_id = taiyin::TAIYIN_BODY_SUN;
    file_request_a.frame = taiyin::internal::IcrfJ2000Equatorial;
    file_request_a.jd_tdb = split_jd(JD0);

    EphemerisRequest file_request_b = file_request_a;
    file_request_b.target_id = FILE_TARGET_B;

    EphemerisBlockDescriptor found_file_a;
    expect_true(
        taiyin::runtime::find_global_ephemeris_descriptor(file_request_a, &found_file_a),
        "find file-backed descriptor A in catalog");
    expect_string_equal(found_file_a.path, path_a, "catalog descriptor A keeps reload path");

    EphemerisResult file_a_first;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(file_request_a, &file_a_first, &diagnostic)),
        "eval file-backed method A first");
    expect_false(file_a_first.cache_hit, "file-backed A first eval loads from disk");
    expect_int(g_file_load_count, 1, "file-backed A loaded once");
    expect_near(file_a_first.state.position_au.x, 1.25, 0.0, "file-backed A x");
    expect_near(file_a_first.state.position_au.y, 2.5, 0.0, "file-backed A y");
    expect_near(file_a_first.state.position_au.z, 3.75, 0.0, "file-backed A z");

    EphemerisResult file_a_cached;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(file_request_a, &file_a_cached, &diagnostic)),
        "eval file-backed method A cached");
    expect_true(file_a_cached.cache_hit, "file-backed A second eval hits cache");
    expect_int(g_file_load_count, 1, "file-backed A cache hit does not reload");

    EphemerisResult file_b_first;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(file_request_b, &file_b_first, &diagnostic)),
        "eval file-backed method B first");
    expect_false(file_b_first.cache_hit, "file-backed B first eval loads from disk");
    expect_int(g_file_load_count, 2, "file-backed B loaded and evicted A");

    EphemerisResult file_a_reloaded;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(file_request_a, &file_a_reloaded, &diagnostic)),
        "eval file-backed method A after eviction");
    expect_false(file_a_reloaded.cache_hit, "file-backed A reload after eviction misses cache");
    expect_int(g_file_load_count, 3, "file-backed A reloaded from catalog path");
    expect_near(file_a_reloaded.state.position_au.x, 1.25, 0.0, "file-backed A reloaded x");
    expect_near(file_a_reloaded.state.position_au.y, 2.5, 0.0, "file-backed A reloaded y");
    expect_near(file_a_reloaded.state.position_au.z, 3.75, 0.0, "file-backed A reloaded z");

    taiyin::runtime::clear_global_ephemeris_cache();
    taiyin::internal::clear_custom_ephemeris_methods();
    std::remove(path_a.c_str());
    std::remove(path_b.c_str());
    expect_true(g_series_destroy_count >= 2, "registered and cached VSOP clones destroyed");
    expect_true(g_file_destroy_count >= 3, "file-backed cached data destroyed");
    return 0;
}
