#include "taiyin/angle.h"
#include "taiyin/physical_constants.h"
#include "taiyin/runtime/taiyin_runtime.h"

#include "taiyin/dispatch.h"
#include "taiyin/internal/custom_ephemeris_source_registry.h"
#include "taiyin/internal/ephemeris_discovery.h"
#include "test_env.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace taiyin;
using namespace taiyin::internal;
using namespace taiyin::runtime;

const double JD0 = taiyin::JD_J2000;
const double DAYS_PER_MILLENNIUM = taiyin::DAYS_PER_JULIAN_MILLENNIUM;
const char* VSOP87_MERCURY_PATH = taiyin_test::getenv_path("TAIYIN_VSOP87_MERCURY_PATH");
const int CUSTOM_MERCURY_TARGET_ID = 199;
const int CUSTOM_MERCURY_METHOD_ID = 88001;
const int VSOP87_MERCURY_TARGET_ID = 1;
const int VSOP87_MERCURY_METHOD_ID = 87001;
const int CUSTOM_NUTATION_ID = taiyin::dispatch::NUTATION_CUSTOM_START + 501;
const int CUSTOM_PRECESSION_ID = taiyin::dispatch::PRECESSION_CUSTOM_START + 501;

const double VSOP87_L0[12] = {
    4.40260884240, 3.17614669689, 1.75347045953, 6.20347611291,
    0.59954649739, 0.87401675650, 5.48129387159, 5.31188628676,
    5.19846674103, 1.62790523337, 2.35555589827, 3.81034454697,
};

const double VSOP87_RATE[12] = {
    26087.9031415742, 10213.2855462110, 6283.0758499914, 3340.6124266998,
    529.6909650946, 213.2990954380, 74.7815985673, 38.1330356378,
    77713.7714681205, 84334.6615813083, 83286.9142695536, 83997.0911355954,
};

const double ECLIPTIC_TO_EQUATORIAL[3][3] = {
    { 1.000000000000,  0.000000440360, -0.000000190919 },
    { -0.000000479966, 0.917482137087, -0.397776982902 },
    { 0.000000000000,  0.397776982902,  0.917482137087 },
};

struct CustomMercuryData {
    double epoch_jd_tdb;
    double longitude_at_epoch_rad;
    double period_days;
    double radius_au;
};

struct Vsop87Term {
    int variable;
    int alpha;
    int multipliers[12];
    double sine_coeff;
    double cosine_coeff;
};

struct Vsop87MercuryData {
    std::vector<Vsop87Term> terms;
    int* destroy_count;
};

int g_custom_mercury_file_load_count = 0;
int g_custom_mercury_destroy_count = 0;
int g_vsop87_file_load_count = 0;
int g_vsop87_file_destroy_count = 0;

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_false(bool value, const char* label, int* failures) {
    if (value) {
        std::cerr << "FAIL: expected false: " << label << "\n";
        ++(*failures);
    }
}

void expect_int(int actual, int expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_size(size_t actual, size_t expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

void push_u8(std::vector<uint8_t>* data, uint8_t value) {
    data->push_back(value);
}

void push_u16_le(std::vector<uint8_t>* data, uint16_t value) {
    data->push_back(static_cast<uint8_t>(value & 0xFF));
    data->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void push_u32_le(std::vector<uint8_t>* data, uint32_t value) {
    data->push_back(static_cast<uint8_t>(value & 0xFF));
    data->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    data->push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    data->push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void push_i32_le(std::vector<uint8_t>* data, int32_t value) {
    push_u32_le(data, static_cast<uint32_t>(value));
}

void push_u64_le(std::vector<uint8_t>* data, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        data->push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

void push_f64_le(std::vector<uint8_t>* data, double value) {
    uint8_t bytes[8];
    std::memcpy(bytes, &value, sizeof(bytes));
    for (int i = 0; i < 8; ++i) {
        data->push_back(bytes[i]);
    }
}

void push_i8_coeffs(std::vector<uint8_t>* data, const int8_t* values, int count) {
    const int width_byte_count = (count + 3) / 4;
    for (int i = 0; i < width_byte_count; ++i) {
        data->push_back(0);
    }
    for (int i = 0; i < count; ++i) {
        data->push_back(static_cast<uint8_t>(values[i]));
    }
}

std::vector<uint8_t> make_test_opm4_file_bytes(int target_id, int center_id, int method_id) {
    std::vector<uint8_t> payload;
    push_f64_le(&payload, 100.0);
    push_f64_le(&payload, 110.0);
    push_f64_le(&payload, 0.0);
    push_f64_le(&payload, taiyin::TAIYIN_PI / 2.0);
    push_f64_le(&payload, 0.0);
    const int8_t cx[] = { 1, 2 };
    const int8_t cy[] = { 3, 4 };
    const int8_t cz[] = { 5 };
    push_i8_coeffs(&payload, cx, 2);
    push_i8_coeffs(&payload, cy, 2);
    push_i8_coeffs(&payload, cz, 1);

    std::vector<uint8_t> data;
    push_u8(&data, 'O');
    push_u8(&data, 'P');
    push_u8(&data, 'M');
    push_u8(&data, '4');
    push_u8(&data, 1);
    push_u8(&data, 0);
    push_u16_le(&data, 128);
    push_u64_le(&data, 128);
    push_u64_le(&data, static_cast<uint64_t>(payload.size()));
    push_i32_le(&data, target_id);
    push_i32_le(&data, center_id);
    push_i32_le(&data, method_id);
    push_i32_le(&data, 1);
    push_f64_le(&data, 100.0);
    push_f64_le(&data, 110.0);
    push_u32_le(&data, 1);
    push_u8(&data, 1);
    push_u8(&data, 0);
    push_u8(&data, 1);
    push_u8(&data, 1);
    push_u8(&data, 1);
    for (int i = 0; i < 7; ++i) {
        push_u8(&data, 0);
    }
    push_f64_le(&data, 0.005);
    while (data.size() < 128) {
        push_u8(&data, 0);
    }
    data.insert(data.end(), payload.begin(), payload.end());
    return data;
}

bool write_test_opm4_file_for(const std::string& path, int target_id, int center_id, int method_id) {
    const std::vector<uint8_t> bytes = make_test_opm4_file_bytes(target_id, center_id, method_id);
    std::ofstream file(path.c_str(), std::ios::binary);
    file.write(reinterpret_cast<const char*>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

std::string make_temp_dir(const char* pattern) {
    char templ[128];
    std::snprintf(templ, sizeof(templ), "%s", pattern);
    char* path = mkdtemp(templ);
    return path ? std::string(path) : std::string();
}

bool make_directory(const std::string& path) {
    return mkdir(path.c_str(), 0700) == 0;
}

void remove_file_if_present(const std::string& path) {
    std::remove(path.c_str());
}

void remove_dir_if_present(const std::string& path) {
    rmdir(path.c_str());
}

bool file_exists(const char* path) {
    if (!path) {
        return false;
    }
    std::ifstream file(path);
    return static_cast<bool>(file);
}

void ecliptic_to_equatorial(const Vector3& ecliptic, Vector3* out) {
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

bool parse_vsop87_header(const std::string& line, int* out_variable, int* out_alpha) {
    const std::string variable_marker = "VARIABLE";
    const std::string alpha_marker = "*T**";
    const size_t variable_pos = line.find(variable_marker);
    const size_t alpha_pos = line.find(alpha_marker);
    if (variable_pos == std::string::npos || alpha_pos == std::string::npos) {
        return false;
    }

    *out_variable = std::atoi(line.c_str() + variable_pos + variable_marker.size());
    *out_alpha = std::atoi(line.c_str() + alpha_pos + alpha_marker.size());
    return *out_variable >= 1 && *out_variable <= 3 && *out_alpha >= 0 && *out_alpha <= 5;
}

bool parse_vsop87_term_line(const std::string& line, int variable, int alpha, Vsop87Term* out) {
    const size_t multiplier_start = 10;
    const size_t multiplier_width = 36;
    if (!out || line.size() < multiplier_start + multiplier_width + 33) {
        return false;
    }

    Vsop87Term term;
    term.variable = variable;
    term.alpha = alpha;
    for (int i = 0; i < 12; ++i) {
        term.multipliers[i] = std::atoi(line.substr(multiplier_start + static_cast<size_t>(i) * 3, 3).c_str());
    }

    const std::string rest = line.substr(multiplier_start + multiplier_width);
    term.sine_coeff = std::atof(rest.substr(0, 15).c_str());
    term.cosine_coeff = std::atof(rest.substr(15, 18).c_str());
    *out = term;
    return true;
}

bool load_vsop87_mercury_data(const char* path, int* destroy_count, Vsop87MercuryData** out) {
    if (!path || !out) {
        return false;
    }
    *out = 0;

    std::ifstream file(path);
    if (!file) {
        return false;
    }

    Vsop87MercuryData* data = new Vsop87MercuryData();
    data->destroy_count = destroy_count;

    int current_variable = 0;
    int current_alpha = 0;
    std::string line;
    while (std::getline(file, line)) {
        int variable = 0;
        int alpha = 0;
        if (parse_vsop87_header(line, &variable, &alpha)) {
            current_variable = variable;
            current_alpha = alpha;
            continue;
        }

        if (current_variable == 0) {
            continue;
        }

        bool has_non_space = false;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] != ' ' && line[i] != '\t' && line[i] != '\r') {
                has_non_space = true;
                break;
            }
        }
        if (!has_non_space) {
            continue;
        }

        Vsop87Term term;
        if (!parse_vsop87_term_line(line, current_variable, current_alpha, &term)) {
            delete data;
            return false;
        }
        data->terms.push_back(term);
    }

    if (data->terms.empty()) {
        delete data;
        return false;
    }

    *out = data;
    return true;
}

void compute_vsop87_mercury_position(double jd_tdb, const Vsop87MercuryData* data, Vector3* out) {
    const double t = (jd_tdb - JD0) / DAYS_PER_MILLENNIUM;
    double lambda[12];
    for (int i = 0; i < 12; ++i) {
        lambda[i] = VSOP87_L0[i] + VSOP87_RATE[i] * t;
    }

    double powers[6];
    powers[0] = 1.0;
    for (int i = 1; i < 6; ++i) {
        powers[i] = powers[i - 1] * t;
    }

    double pos[3] = { 0.0, 0.0, 0.0 };
    for (size_t i = 0; i < data->terms.size(); ++i) {
        const Vsop87Term& term = data->terms[i];
        double phi = 0.0;
        for (int j = 0; j < 12; ++j) {
            phi += static_cast<double>(term.multipliers[j]) * lambda[j];
        }
        pos[term.variable - 1] += powers[term.alpha]
            * (term.sine_coeff * std::sin(phi) + term.cosine_coeff * std::cos(phi));
    }

    out->x = pos[0];
    out->y = pos[1];
    out->z = pos[2];
}

bool vsop87_mercury_position(double jd_tdb, const void* data, Vector3* out) {
    const Vsop87MercuryData* mercury = static_cast<const Vsop87MercuryData*>(data);
    if (!mercury || !out) {
        return false;
    }

    Vector3 position_ecliptic;
    compute_vsop87_mercury_position(jd_tdb, mercury, &position_ecliptic);
    ecliptic_to_equatorial(position_ecliptic, out);
    return true;
}

bool vsop87_mercury_velocity(double jd_tdb, const void* data, Vector3* out) {
    const Vsop87MercuryData* mercury = static_cast<const Vsop87MercuryData*>(data);
    if (!mercury || !out) {
        return false;
    }

    const double h = 1e-3;
    Vector3 p_minus;
    Vector3 p_plus;
    compute_vsop87_mercury_position(jd_tdb - h, mercury, &p_minus);
    compute_vsop87_mercury_position(jd_tdb + h, mercury, &p_plus);

    Vector3 velocity_ecliptic;
    velocity_ecliptic.x = (p_plus.x - p_minus.x) / (2.0 * h);
    velocity_ecliptic.y = (p_plus.y - p_minus.y) / (2.0 * h);
    velocity_ecliptic.z = (p_plus.z - p_minus.z) / (2.0 * h);
    ecliptic_to_equatorial(velocity_ecliptic, out);
    return true;
}

bool vsop87_mercury_acceleration(double jd_tdb, const void* data, Vector3* out) {
    const Vsop87MercuryData* mercury = static_cast<const Vsop87MercuryData*>(data);
    if (!mercury || !out) {
        return false;
    }

    const double h = 1e-3;
    Vector3 p0;
    Vector3 p_minus;
    Vector3 p_plus;
    compute_vsop87_mercury_position(jd_tdb, mercury, &p0);
    compute_vsop87_mercury_position(jd_tdb - h, mercury, &p_minus);
    compute_vsop87_mercury_position(jd_tdb + h, mercury, &p_plus);

    Vector3 acceleration_ecliptic;
    acceleration_ecliptic.x = (p_plus.x - 2.0 * p0.x + p_minus.x) / (h * h);
    acceleration_ecliptic.y = (p_plus.y - 2.0 * p0.y + p_minus.y) / (h * h);
    acceleration_ecliptic.z = (p_plus.z - 2.0 * p0.z + p_minus.z) / (h * h);
    ecliptic_to_equatorial(acceleration_ecliptic, out);
    return true;
}

bool load_vsop87_mercury_file_source(const char* path, void** out_data, size_t* out_bytes) {
    if (out_data) {
        *out_data = 0;
    }
    if (out_bytes) {
        *out_bytes = 0;
    }
    if (!path || !out_data || !out_bytes) {
        return false;
    }

    Vsop87MercuryData* data = 0;
    if (!load_vsop87_mercury_data(path, &g_vsop87_file_destroy_count, &data) || !data) {
        return false;
    }

    ++g_vsop87_file_load_count;
    *out_data = data;
    *out_bytes = sizeof(Vsop87MercuryData) + data->terms.size() * sizeof(Vsop87Term);
    return true;
}

void vsop87_mercury_destroy(void* data) {
    Vsop87MercuryData* mercury = static_cast<Vsop87MercuryData*>(data);
    if (mercury && mercury->destroy_count) {
        ++(*mercury->destroy_count);
    }
    delete mercury;
}

void write_custom_mercury_file(
    const std::string& path,
    double epoch_jd_tdb,
    double longitude_at_epoch_rad,
    double period_days,
    double radius_au
) {
    std::ofstream file(path.c_str(), std::ios::out | std::ios::trunc);
    file.precision(17);
    file << epoch_jd_tdb << "\n"
         << longitude_at_epoch_rad << "\n"
         << period_days << "\n"
         << radius_au << "\n";
}

double custom_mercury_longitude(double jd_tdb, const CustomMercuryData* data) {
    return data->longitude_at_epoch_rad
        + 2.0 * taiyin::TAIYIN_PI * (jd_tdb - data->epoch_jd_tdb) / data->period_days;
}

bool custom_mercury_file_load(const char* path, void** out_data, size_t* out_bytes) {
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

    CustomMercuryData* data = new CustomMercuryData();
    if (!(file >> data->epoch_jd_tdb
          >> data->longitude_at_epoch_rad
          >> data->period_days
          >> data->radius_au)) {
        delete data;
        return false;
    }

    ++g_custom_mercury_file_load_count;
    *out_data = data;
    *out_bytes = sizeof(CustomMercuryData);
    return true;
}

void custom_mercury_destroy(void* data) {
    CustomMercuryData* mercury = static_cast<CustomMercuryData*>(data);
    delete mercury;
    ++g_custom_mercury_destroy_count;
}

bool custom_mercury_position(double jd_tdb, const void* data, Vector3* out) {
    const CustomMercuryData* mercury = static_cast<const CustomMercuryData*>(data);
    if (!mercury || !out || mercury->period_days <= 0.0) {
        return false;
    }
    const double longitude = custom_mercury_longitude(jd_tdb, mercury);
    out->x = mercury->radius_au * std::cos(longitude);
    out->y = mercury->radius_au * std::sin(longitude);
    out->z = 0.0;
    return true;
}

bool custom_mercury_velocity(double jd_tdb, const void* data, Vector3* out) {
    const CustomMercuryData* mercury = static_cast<const CustomMercuryData*>(data);
    if (!mercury || !out || mercury->period_days <= 0.0) {
        return false;
    }
    const double longitude = custom_mercury_longitude(jd_tdb, mercury);
    const double rate = 2.0 * taiyin::TAIYIN_PI / mercury->period_days;
    out->x = -mercury->radius_au * rate * std::sin(longitude);
    out->y = mercury->radius_au * rate * std::cos(longitude);
    out->z = 0.0;
    return true;
}

bool custom_mercury_acceleration(double jd_tdb, const void* data, Vector3* out) {
    const CustomMercuryData* mercury = static_cast<const CustomMercuryData*>(data);
    if (!mercury || !out || mercury->period_days <= 0.0) {
        return false;
    }
    const double longitude = custom_mercury_longitude(jd_tdb, mercury);
    const double rate = 2.0 * taiyin::TAIYIN_PI / mercury->period_days;
    out->x = -mercury->radius_au * rate * rate * std::cos(longitude);
    out->y = -mercury->radius_au * rate * rate * std::sin(longitude);
    out->z = 0.0;
    return true;
}

bool custom_test_nutation(double jd_tt, const void*, NutationAngles* out) {
    if (!out) {
        return false;
    }
    out->dpsi_rad = jd_tt * 3.0e-12;
    out->deps_rad = jd_tt * 4.0e-12;
    out->mean_obliquity_rad = 0.41;
    out->true_obliquity_rad = out->mean_obliquity_rad + out->deps_rad;
    return true;
}

bool custom_test_precession(double jd_tt, const void*, Matrix3x3* out, double* out_mean_obliquity_rad) {
    if (!out) {
        return false;
    }
    *out = matrix3x3_identity();
    out->m[0][1] = jd_tt * 1.0e-6;
    out->m[1][0] = jd_tt * -1.0e-6;
    if (out_mean_obliquity_rad) {
        *out_mean_obliquity_rad = 0.5123;
    }
    return true;
}

EphemerisRequest make_request(int target_id, int center_id = 10) {
    EphemerisRequest request;
    request.target_id = target_id;
    request.center_id = center_id;
    request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = 105.0;
    return request;
}

bool make_global_bucket_for_request(
    const EphemerisRequest& request,
    EphemerisBlockDescriptor* out
) {
    EphemerisBlockDescriptor source;
    return find_global_ephemeris_descriptor(request, &source)
        && make_cache_bucket_descriptor_for_jd(source, request.jd_tdb, out);
}

void expect_global_service_bindings(const char* label, int* failures) {
    // Intentionally uses raw accessors to verify single-threaded pointer bindings.
    EphemerisService& service = global_ephemeris_service();
    expect_true(service.catalog() == &global_ephemeris_catalog(), label, failures);
    expect_true(service.priorities() == &global_ephemeris_priorities(), label, failures);
    expect_true(service.cache() == global_ephemeris_cache(), label, failures);
    expect_true(default_taiyin_runtime().ephemeris_cache() == global_ephemeris_cache(), label, failures);
    expect_true(&default_taiyin_runtime().ephemeris_service() == &global_ephemeris_service(), label, failures);
}

void initialize_empty_global_runtime(int* failures) {
    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 1024 * 1024;
    expect_true(initialize_global_ephemeris_runtime(config), "reset global runtime to empty", failures);
}

void test_locked_global_helper_apis(int* failures) {
    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 4096;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize locked-helper runtime", failures);

    EphemerisBlockDescriptor low;
    low.route_key = EphemerisRouteKey(820010, 10, 1, 0);
    low.target_id = 820010;
    low.center_id = 10;
    low.method_id = 1;
    low.frame = EphemerisFrame::IcrfJ2000Equatorial;
    low.format = EphemerisBlockFormat::Opm4;
    low.jd_tdb_start = 100.0;
    low.jd_tdb_end = 110.0;

    EphemerisBlockDescriptor high = low;
    high.route_key = EphemerisRouteKey(820010, 10, 99, 0);
    high.method_id = 99;

    expect_true(add_global_ephemeris_descriptor(low), "locked helper adds low descriptor", failures);
    expect_true(add_global_ephemeris_descriptor(high), "locked helper adds high descriptor", failures);
    expect_size(global_ephemeris_catalog_size(), 2, "locked helper catalog size", failures);
    expect_true(set_global_ephemeris_method_priority(99, 200), "locked helper sets global priority", failures);
    expect_true(set_global_ephemeris_target_method_priority(820010, 99, 300), "locked helper sets target priority", failures);

    EphemerisBlockDescriptor found;
    expect_true(find_global_ephemeris_descriptor(make_request(820010), &found), "locked helper finds descriptor", failures);
    expect_int(found.method_id, 99, "locked helper find returns priority winner", failures);
    found.method_id = 12345;
    EphemerisBlockDescriptor found_again;
    expect_true(find_global_ephemeris_descriptor(make_request(820010), &found_again), "locked helper finds descriptor again", failures);
    expect_int(found_again.method_id, 99, "locked helper find returns copy, not catalog pointer", failures);

    const std::string root = make_temp_dir("/tmp/taiyin-global-helper-cache-XXXXXX");
    expect_true(!root.empty(), "make locked-helper cache root", failures);
    const std::string path = root + "/helper.opm4";
    expect_true(write_test_opm4_file_for(path, 820011, 10, 1), "write locked-helper cache source", failures);

    const char* paths[] = { root.c_str() };
    EphemerisRuntimeConfig path_config;
    path_config.cache_max_bytes = 4096;
    path_config.source_paths = paths;
    path_config.source_path_count = 1;
    expect_true(initialize_global_ephemeris_runtime(path_config), "initialize locked-helper path runtime", failures);
    expect_size(global_ephemeris_catalog_size(), 1, "locked helper path catalog size", failures);
    expect_size(global_ephemeris_cache_entry_count(), 0, "locked helper cache starts empty", failures);
    expect_size(global_ephemeris_cache_total_bytes(), 0, "locked helper cache starts with zero bytes", failures);
    expect_size(global_ephemeris_cache_max_bytes(), 4096, "locked helper cache max bytes", failures);

    EphemerisRequest request = make_request(820011);
    EphemerisBlockDescriptor bucket;
    expect_true(make_global_bucket_for_request(request, &bucket), "locked helper makes cache bucket", failures);
    EphemerisResult result;
    expect_true(eval_global_ephemeris_state(request, &result), "locked helper eval loads cache", failures);
    expect_true(global_ephemeris_cache_contains(bucket.route_key), "locked helper cache contains loaded route", failures);
    expect_size(global_ephemeris_cache_entry_count(), 1, "locked helper cache entry count after load", failures);
    expect_true(global_ephemeris_cache_total_bytes() > 0, "locked helper cache total bytes after load", failures);
    clear_global_ephemeris_cache();
    expect_false(global_ephemeris_cache_contains(bucket.route_key), "locked helper cache clear removes route", failures);
    expect_size(global_ephemeris_cache_entry_count(), 0, "locked helper cache clear resets count", failures);

    remove_file_if_present(path);
    remove_dir_if_present(root);
}

void test_custom_mercury_global_runtime_reload_after_eviction(int* failures) {
    clear_custom_ephemeris_sources();
    g_custom_mercury_file_load_count = 0;
    g_custom_mercury_destroy_count = 0;

    const std::string root = make_temp_dir("/tmp/taiyin-global-mercury-XXXXXX");
    expect_true(!root.empty(), "make custom Mercury root", failures);
    const std::string mercury_path = root + "/custom_mercury.txt";
    const std::string evicting_path = root + "/evicting.opm4";
    write_custom_mercury_file(mercury_path, JD0, 0.0, 87.969, 0.39);
    expect_true(write_test_opm4_file_for(evicting_path, 830002, 10, 1), "write Mercury eviction source", failures);

    CustomEphemerisFileSourceDefinition mercury_definition;
    mercury_definition.target_id = CUSTOM_MERCURY_TARGET_ID;
    mercury_definition.center_id = 10;
    mercury_definition.method_id = CUSTOM_MERCURY_METHOD_ID;
    mercury_definition.frame = EphemerisFrame::IcrfJ2000Equatorial;
    mercury_definition.jd_tdb_start = JD0 - 1000.0;
    mercury_definition.jd_tdb_end = JD0 + 1000.0;
    mercury_definition.path = mercury_path.c_str();
    mercury_definition.load = &custom_mercury_file_load;
    mercury_definition.position = &custom_mercury_position;
    mercury_definition.velocity = &custom_mercury_velocity;
    mercury_definition.acceleration = &custom_mercury_acceleration;
    mercury_definition.destroy = &custom_mercury_destroy;

    uint64_t mercury_source_id = 0;
    expect_true(register_custom_ephemeris_file_source(mercury_definition, &mercury_source_id), "register file-backed custom Mercury", failures);
    EphemerisBlockDescriptor mercury_descriptor;
    expect_true(make_custom_ephemeris_descriptor(mercury_source_id, &mercury_descriptor), "make custom Mercury descriptor", failures);
    expect_true(mercury_descriptor.path == mercury_path, "custom Mercury descriptor stores file path", failures);

    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 1;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize tiny-cache custom Mercury runtime", failures);
    expect_true(add_global_ephemeris_descriptor(mercury_descriptor), "add custom Mercury descriptor to global catalog", failures);
    expect_true(add_global_ephemeris_source_path(evicting_path.c_str()), "add Mercury eviction source path", failures);
    expect_size(global_ephemeris_catalog_size(), 2, "custom Mercury runtime catalog count", failures);

    EphemerisService& user_a_service = global_ephemeris_service();
    EphemerisService& user_b_service = default_taiyin_runtime().ephemeris_service();
    EphemerisRequest mercury_request = make_request(CUSTOM_MERCURY_TARGET_ID);
    mercury_request.jd_tdb = JD0;
    EphemerisRequest evicting_request = make_request(830002);

    EphemerisResult mercury_first;
    expect_true(eval_global_ephemeris_state(mercury_request, &mercury_first), "user A eval custom Mercury", failures);
    expect_false(mercury_first.cache_hit, "custom Mercury first eval cache miss", failures);
    expect_int(g_custom_mercury_file_load_count, 1, "custom Mercury file loaded once", failures);
    expect_true(global_ephemeris_cache_contains(mercury_descriptor.route_key), "global cache contains custom Mercury route", failures);
    expect_near(mercury_first.state.position_au.x, 0.39, 1.0e-14, "custom Mercury initial radius", failures);

    EphemerisResult mercury_hit;
    expect_true(eval_global_ephemeris_state(mercury_request, &mercury_hit), "user B eval custom Mercury cache hit", failures);
    expect_true(mercury_hit.cache_hit, "user B custom Mercury eval is shared cache hit", failures);
    expect_int(g_custom_mercury_file_load_count, 1, "custom Mercury cache hit does not reload file", failures);

    EphemerisBlockDescriptor evicting_bucket;
    expect_true(make_global_bucket_for_request(evicting_request, &evicting_bucket), "make Mercury evicting bucket", failures);
    EphemerisResult evicting_result;
    expect_true(eval_global_ephemeris_state(evicting_request, &evicting_result), "user B eval evicting source", failures);
    expect_false(evicting_result.cache_hit, "evicting source first eval cache miss", failures);
    expect_true(!global_ephemeris_cache_contains(mercury_descriptor.route_key), "custom Mercury evicted from tiny global cache", failures);
    expect_true(global_ephemeris_cache_contains(evicting_bucket.route_key), "evicting source in tiny global cache", failures);
    expect_int(g_custom_mercury_destroy_count, 1, "custom Mercury cache clone destroyed on eviction", failures);

    write_custom_mercury_file(mercury_path, JD0, 0.0, 87.969, 0.42);
    EphemerisResult mercury_reload;
    expect_true(eval_global_ephemeris_state(mercury_request, &mercury_reload), "user A reload custom Mercury after eviction", failures);
    expect_false(mercury_reload.cache_hit, "custom Mercury reload after eviction is cache miss", failures);
    expect_int(g_custom_mercury_file_load_count, 2, "custom Mercury file reloaded from descriptor path", failures);
    expect_true(global_ephemeris_cache_contains(mercury_descriptor.route_key), "global cache contains reloaded custom Mercury", failures);
    expect_true(!global_ephemeris_cache_contains(evicting_bucket.route_key), "evicting source removed by custom Mercury reload", failures);
    expect_near(mercury_reload.state.position_au.x, 0.42, 1.0e-14, "custom Mercury reloaded radius", failures);

    clear_global_ephemeris_cache();
    clear_custom_ephemeris_sources();
    remove_file_if_present(mercury_path);
    remove_file_if_present(evicting_path);
    remove_dir_if_present(root);
}

void test_vsop87_mercury_custom_method_global_cache(int* failures) {
    if (!file_exists(VSOP87_MERCURY_PATH)) {
        std::cout << "skipping global VSOP87A Mercury custom method test; local VSOP87A.mer is absent\n";
        return;
    }

    clear_custom_ephemeris_sources();
    g_vsop87_file_load_count = 0;
    g_vsop87_file_destroy_count = 0;

    const std::string root = make_temp_dir("/tmp/taiyin-global-vsop87-mercury-XXXXXX");
    expect_true(!root.empty(), "make VSOP87 Mercury root", failures);
    const std::string evicting_path = root + "/evicting.opm4";
    expect_true(write_test_opm4_file_for(evicting_path, 830003, 10, 1), "write VSOP87 Mercury eviction source", failures);

    CustomEphemerisFileSourceDefinition mercury_definition;
    mercury_definition.target_id = VSOP87_MERCURY_TARGET_ID;
    mercury_definition.center_id = 10;
    mercury_definition.method_id = VSOP87_MERCURY_METHOD_ID;
    mercury_definition.frame = EphemerisFrame::IcrfJ2000Equatorial;
    mercury_definition.jd_tdb_start = JD0 - 36525.0;
    mercury_definition.jd_tdb_end = JD0 + 36525.0;
    mercury_definition.path = VSOP87_MERCURY_PATH;
    mercury_definition.load = &load_vsop87_mercury_file_source;
    mercury_definition.position = &vsop87_mercury_position;
    mercury_definition.velocity = &vsop87_mercury_velocity;
    mercury_definition.acceleration = &vsop87_mercury_acceleration;
    mercury_definition.destroy = &vsop87_mercury_destroy;

    uint64_t mercury_source_id = 0;
    expect_true(register_custom_ephemeris_file_source(mercury_definition, &mercury_source_id), "register global VSOP87A Mercury custom source", failures);
    EphemerisBlockDescriptor mercury_descriptor;
    expect_true(make_custom_ephemeris_descriptor(mercury_source_id, &mercury_descriptor), "make global VSOP87A Mercury descriptor", failures);
    expect_true(mercury_descriptor.path == VSOP87_MERCURY_PATH, "global VSOP87A Mercury descriptor stores path", failures);

    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 1;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize tiny-cache VSOP87 Mercury runtime", failures);
    expect_true(add_global_ephemeris_descriptor(mercury_descriptor), "add VSOP87A Mercury descriptor to global catalog", failures);
    expect_true(add_global_ephemeris_source_path(evicting_path.c_str()), "add VSOP87 Mercury eviction source path", failures);
    expect_size(global_ephemeris_catalog_size(), 2, "VSOP87 Mercury runtime catalog count", failures);

    EphemerisService& user_a_service = global_ephemeris_service();
    EphemerisService& user_b_service = default_taiyin_runtime().ephemeris_service();
    expect_true(user_a_service.cache() == user_b_service.cache(), "VSOP87 Mercury services share cache", failures);

    EphemerisRequest mercury_request = make_request(VSOP87_MERCURY_TARGET_ID);
    mercury_request.jd_tdb = JD0;
    EphemerisRequest evicting_request = make_request(830003);

    EphemerisResult mercury_first;
    expect_true(eval_global_ephemeris_state(mercury_request, &mercury_first), "user A eval VSOP87A Mercury custom method", failures);
    expect_false(mercury_first.cache_hit, "VSOP87A Mercury first global eval cache miss", failures);
    expect_int(g_vsop87_file_load_count, 1, "VSOP87A Mercury file loaded once through global runtime", failures);
    expect_int(mercury_first.descriptor.method_id, VSOP87_MERCURY_METHOD_ID, "VSOP87A Mercury custom method selected", failures);
    expect_true(global_ephemeris_cache_contains(mercury_descriptor.route_key), "global cache contains VSOP87A Mercury custom block", failures);
    expect_near(mercury_first.state.position_au.x, -0.13009360356893562, 1.0e-13, "VSOP87A Mercury global J2000 x", failures);
    expect_near(mercury_first.state.position_au.y, -0.40059371691764684, 1.0e-13, "VSOP87A Mercury global J2000 y", failures);
    expect_near(mercury_first.state.position_au.z, -0.20048931479048393, 1.0e-13, "VSOP87A Mercury global J2000 z", failures);

    EphemerisResult mercury_hit;
    expect_true(eval_global_ephemeris_state(mercury_request, &mercury_hit), "user B eval VSOP87A Mercury shared cache", failures);
    expect_true(mercury_hit.cache_hit, "user B VSOP87A Mercury eval is shared cache hit", failures);
    expect_int(g_vsop87_file_load_count, 1, "VSOP87A Mercury cache hit does not reload file", failures);

    EphemerisBlockDescriptor evicting_bucket;
    expect_true(make_global_bucket_for_request(evicting_request, &evicting_bucket), "make VSOP87 Mercury evicting bucket", failures);
    EphemerisResult evicting_result;
    expect_true(eval_global_ephemeris_state(evicting_request, &evicting_result), "user B evicts VSOP87A Mercury custom block", failures);
    expect_false(evicting_result.cache_hit, "VSOP87 Mercury evicting source first eval cache miss", failures);
    expect_true(!global_ephemeris_cache_contains(mercury_descriptor.route_key), "VSOP87A Mercury block evicted from tiny global cache", failures);
    expect_true(global_ephemeris_cache_contains(evicting_bucket.route_key), "VSOP87 Mercury evicting source in tiny cache", failures);
    expect_int(g_vsop87_file_destroy_count, 1, "VSOP87A Mercury cache clone destroyed on global eviction", failures);

    EphemerisResult mercury_reload;
    expect_true(eval_global_ephemeris_state(mercury_request, &mercury_reload), "user A reloads VSOP87A Mercury after global eviction", failures);
    expect_false(mercury_reload.cache_hit, "VSOP87A Mercury reload after global eviction is cache miss", failures);
    expect_int(g_vsop87_file_load_count, 2, "VSOP87A Mercury reload reads file again", failures);
    expect_true(global_ephemeris_cache_contains(mercury_descriptor.route_key), "global cache contains reloaded VSOP87A Mercury custom block", failures);
    expect_true(!global_ephemeris_cache_contains(evicting_bucket.route_key), "evicting source removed by VSOP87A Mercury reload", failures);
    expect_near(mercury_reload.state.position_au.y, mercury_first.state.position_au.y, 0.0, "VSOP87A Mercury reload state stable", failures);

    clear_global_ephemeris_cache();
    expect_int(g_vsop87_file_destroy_count, 2, "VSOP87A Mercury reloaded cache clone destroyed on cache clear", failures);
    clear_custom_ephemeris_sources();
    remove_file_if_present(evicting_path);
    remove_dir_if_present(root);
}

void test_concurrent_vsop87_mercury_global_eval_readers(int* failures) {
    if (!file_exists(VSOP87_MERCURY_PATH)) {
        std::cout << "skipping concurrent global VSOP87A Mercury test; local VSOP87A.mer is absent\n";
        return;
    }

    clear_custom_ephemeris_sources();
    g_vsop87_file_load_count = 0;
    g_vsop87_file_destroy_count = 0;

    CustomEphemerisFileSourceDefinition mercury_definition;
    mercury_definition.target_id = VSOP87_MERCURY_TARGET_ID;
    mercury_definition.center_id = 10;
    mercury_definition.method_id = VSOP87_MERCURY_METHOD_ID;
    mercury_definition.frame = EphemerisFrame::IcrfJ2000Equatorial;
    mercury_definition.jd_tdb_start = JD0 - 36525.0;
    mercury_definition.jd_tdb_end = JD0 + 36525.0;
    mercury_definition.path = VSOP87_MERCURY_PATH;
    mercury_definition.load = &load_vsop87_mercury_file_source;
    mercury_definition.position = &vsop87_mercury_position;
    mercury_definition.velocity = &vsop87_mercury_velocity;
    mercury_definition.acceleration = &vsop87_mercury_acceleration;
    mercury_definition.destroy = &vsop87_mercury_destroy;

    uint64_t mercury_source_id = 0;
    expect_true(register_custom_ephemeris_file_source(mercury_definition, &mercury_source_id), "register concurrent VSOP87A Mercury source", failures);
    EphemerisBlockDescriptor mercury_descriptor;
    expect_true(make_custom_ephemeris_descriptor(mercury_source_id, &mercury_descriptor), "make concurrent VSOP87A Mercury descriptor", failures);

    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 1024 * 1024;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize concurrent VSOP87A Mercury runtime", failures);
    expect_true(add_global_ephemeris_descriptor(mercury_descriptor), "add concurrent VSOP87A Mercury descriptor", failures);

    EphemerisRequest mercury_request = make_request(VSOP87_MERCURY_TARGET_ID);
    mercury_request.jd_tdb = JD0;

    const int reader_count = 6;
    const int iterations = 40;
    std::atomic<bool> start(false);
    std::atomic<int> read_failures(0);
    std::vector<std::thread> threads;

    for (int t = 0; t < reader_count; ++t) {
        threads.push_back(std::thread([&]() {
            while (!start.load()) {
                std::this_thread::yield();
            }

            for (int i = 0; i < iterations; ++i) {
                EphemerisResult result;
                if (!eval_global_ephemeris_state(mercury_request, &result)) {
                    ++read_failures;
                    continue;
                }
                if (result.descriptor.method_id != VSOP87_MERCURY_METHOD_ID) {
                    ++read_failures;
                }
                if (std::fabs(result.state.position_au.x - -0.13009360356893562) > 1.0e-13
                    || std::fabs(result.state.position_au.y - -0.40059371691764684) > 1.0e-13
                    || std::fabs(result.state.position_au.z - -0.20048931479048393) > 1.0e-13) {
                    ++read_failures;
                }
            }
        }));
    }

    start.store(true);
    for (size_t i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }

    expect_int(read_failures.load(), 0, "concurrent global VSOP87A Mercury readers see stable custom values", failures);
    expect_true(g_vsop87_file_load_count >= 1, "concurrent global VSOP87A Mercury lazy loads from file", failures);
    expect_true(global_ephemeris_cache_contains(mercury_descriptor.route_key), "concurrent global VSOP87A Mercury reaches shared cache", failures);
    expect_size(global_ephemeris_cache_entry_count(), 1, "concurrent global VSOP87A Mercury keeps one cache entry", failures);
    clear_global_ephemeris_cache();
    expect_true(g_vsop87_file_destroy_count >= 1, "concurrent global VSOP87A Mercury cache clones are destroyed", failures);
    clear_custom_ephemeris_sources();
}

void test_global_weighted_cache_keeps_high_priority_method(int* failures) {
    clear_custom_ephemeris_sources();

    CustomMercuryData high_data = { JD0, 0.0, 87.969, 0.31 };
    CustomMercuryData low_data = { JD0, 0.0, 87.969, 0.32 };
    CustomMercuryData third_data = { JD0, 0.0, 87.969, 0.33 };

    CustomEphemerisSourceDefinition high_definition;
    high_definition.target_id = 840101;
    high_definition.center_id = 10;
    high_definition.method_id = 91001;
    high_definition.frame = EphemerisFrame::IcrfJ2000Equatorial;
    high_definition.jd_tdb_start = JD0 - 10.0;
    high_definition.jd_tdb_end = JD0 + 10.0;
    high_definition.data = &high_data;
    high_definition.bytes = sizeof(high_data);
    high_definition.position = &custom_mercury_position;
    high_definition.velocity = &custom_mercury_velocity;
    high_definition.acceleration = &custom_mercury_acceleration;

    CustomEphemerisSourceDefinition low_definition = high_definition;
    low_definition.target_id = 840102;
    low_definition.method_id = 91002;
    low_definition.data = &low_data;
    low_definition.bytes = sizeof(low_data);

    CustomEphemerisSourceDefinition third_definition = high_definition;
    third_definition.target_id = 840103;
    third_definition.method_id = 91003;
    third_definition.data = &third_data;
    third_definition.bytes = sizeof(third_data);

    uint64_t high_source_id = 0;
    uint64_t low_source_id = 0;
    uint64_t third_source_id = 0;
    expect_true(register_custom_ephemeris_source(high_definition, &high_source_id), "register high-priority cache source", failures);
    expect_true(register_custom_ephemeris_source(low_definition, &low_source_id), "register low-priority cache source", failures);
    expect_true(register_custom_ephemeris_source(third_definition, &third_source_id), "register third cache source", failures);

    EphemerisBlockDescriptor high_descriptor;
    EphemerisBlockDescriptor low_descriptor;
    EphemerisBlockDescriptor third_descriptor;
    expect_true(make_custom_ephemeris_descriptor(high_source_id, &high_descriptor), "make high-priority cache descriptor", failures);
    expect_true(make_custom_ephemeris_descriptor(low_source_id, &low_descriptor), "make low-priority cache descriptor", failures);
    expect_true(make_custom_ephemeris_descriptor(third_source_id, &third_descriptor), "make third cache descriptor", failures);

    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 80;
    config.cache_policy.frequency_weight = 0.0;
    config.cache_policy.priority_weight = 2.0;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize weighted cache runtime", failures);
    expect_true(add_global_ephemeris_descriptor(high_descriptor), "add high-priority descriptor", failures);
    expect_true(add_global_ephemeris_descriptor(low_descriptor), "add low-priority descriptor", failures);
    expect_true(add_global_ephemeris_descriptor(third_descriptor), "add third descriptor", failures);
    expect_true(set_global_ephemeris_method_priority(91001, 100), "set high cache method priority", failures);
    expect_true(set_global_ephemeris_method_priority(91002, 0), "set low cache method priority", failures);
    expect_true(set_global_ephemeris_method_priority(91003, 0), "set third cache method priority", failures);

    EphemerisRequest high_request = make_request(840101);
    high_request.jd_tdb = JD0;
    EphemerisRequest low_request = make_request(840102);
    low_request.jd_tdb = JD0;
    EphemerisRequest third_request = make_request(840103);
    third_request.jd_tdb = JD0;

    EphemerisResult high_first;
    expect_true(eval_global_ephemeris_state(high_request, &high_first), "load high-priority weighted source", failures);
    expect_false(high_first.cache_hit, "high-priority weighted source first miss", failures);
    EphemerisResult low_first;
    expect_true(eval_global_ephemeris_state(low_request, &low_first), "load low-priority weighted source", failures);
    expect_false(low_first.cache_hit, "low-priority weighted source first miss", failures);
    expect_true(global_ephemeris_cache_contains(high_descriptor.route_key), "weighted cache contains high before eviction", failures);
    expect_true(global_ephemeris_cache_contains(low_descriptor.route_key), "weighted cache contains low before eviction", failures);

    EphemerisResult third_first;
    expect_true(eval_global_ephemeris_state(third_request, &third_first), "load third weighted source", failures);
    expect_false(third_first.cache_hit, "third weighted source first miss", failures);
    expect_true(global_ephemeris_cache_contains(high_descriptor.route_key), "weighted cache keeps older high-priority source", failures);
    expect_true(!global_ephemeris_cache_contains(low_descriptor.route_key), "weighted cache evicts newer low-priority source", failures);
    expect_true(global_ephemeris_cache_contains(third_descriptor.route_key), "weighted cache contains third source", failures);

    expect_size(global_ephemeris_cache_entry_count(), 2, "weighted cache retains two entries", failures);
    clear_global_ephemeris_cache();
    clear_custom_ephemeris_sources();
}

void test_no_default_scan_and_bindings(int* failures) {
    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 4096;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize without explicit paths", failures);
    expect_size(global_ephemeris_catalog_size(), 0, "no explicit paths leave catalog empty", failures);
    expect_true(global_ephemeris_cache_max_bytes() > 0, "global cache exists after no-path init", failures);
    expect_size(global_ephemeris_cache_entry_count(), 0, "no-path init leaves cache empty", failures);
    expect_size(global_ephemeris_cache_max_bytes(), 4096, "no-path init applies cache size", failures);
    expect_global_service_bindings("no-path global service binding", failures);

    EphemerisResult result;
    expect_false(eval_global_ephemeris_state(make_request(820001), &result), "no-path eval fails without descriptor", failures);
}

void test_explicit_multi_path_initialization_registers_sources_only(int* failures) {
    const std::string root_a = make_temp_dir("/tmp/taiyin-global-multipath-a-XXXXXX");
    const std::string root_b = make_temp_dir("/tmp/taiyin-global-multipath-b-XXXXXX");
    expect_true(!root_a.empty(), "make first multipath root", failures);
    expect_true(!root_b.empty(), "make second multipath root", failures);
    const std::string path_a = root_a + "/a.opm4";
    const std::string path_b = root_b + "/b.opm4";
    expect_true(write_test_opm4_file_for(path_a, 820101, 10, 1), "write multipath source A", failures);
    expect_true(write_test_opm4_file_for(path_b, 820102, 10, 1), "write multipath source B", failures);

    const char* paths[] = { root_a.c_str(), root_b.c_str() };
    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 1024 * 1024;
    config.source_paths = paths;
    config.source_path_count = 2;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize with two explicit directories", failures);
    expect_size(global_ephemeris_catalog_size(), 2, "two explicit paths register two descriptors", failures);
    expect_true(global_ephemeris_cache_max_bytes() > 0, "multipath cache exists", failures);
    expect_size(global_ephemeris_cache_entry_count(), 0, "multipath init does not preload cache", failures);

    EphemerisBlockDescriptor found_a;
    EphemerisBlockDescriptor found_b;
    expect_true(find_global_ephemeris_descriptor(make_request(820101), &found_a), "find source A descriptor", failures);
    expect_true(find_global_ephemeris_descriptor(make_request(820102), &found_b), "find source B descriptor", failures);
    expect_true(found_a.target_id == 820101, "source A descriptor target", failures);
    expect_true(found_b.target_id == 820102, "source B descriptor target", failures);

    remove_file_if_present(path_a);
    remove_file_if_present(path_b);
    remove_dir_if_present(root_a);
    remove_dir_if_present(root_b);
}

void test_two_user_requests_share_global_cache(int* failures) {
    const std::string root = make_temp_dir("/tmp/taiyin-global-shared-cache-XXXXXX");
    expect_true(!root.empty(), "make shared cache root", failures);
    const std::string path = root + "/shared.opm4";
    expect_true(write_test_opm4_file_for(path, 820201, 10, 1), "write shared cache source", failures);

    const char* paths[] = { root.c_str() };
    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 1024 * 1024;
    config.source_paths = paths;
    config.source_path_count = 1;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize shared cache runtime", failures);
    expect_true(global_ephemeris_cache_max_bytes() > 0, "shared cache exists", failures);
    expect_size(global_ephemeris_cache_entry_count(), 0, "shared cache starts empty", failures);

    EphemerisService& user_a_service = global_ephemeris_service();
    EphemerisService& user_b_service = default_taiyin_runtime().ephemeris_service();
    expect_true(user_a_service.cache() == user_b_service.cache(), "two user services share cache pointer", failures);
    expect_true(user_a_service.catalog() == user_b_service.catalog(), "two user services share catalog pointer", failures);

    EphemerisRequest request = make_request(820201);
    EphemerisResult result_a;
    expect_true(eval_global_ephemeris_state(request, &result_a), "user A loads shared cache source", failures);
    expect_false(result_a.cache_hit, "user A first eval is cache miss", failures);
    expect_size(global_ephemeris_cache_entry_count(), 1, "user A inserts one shared cache entry", failures);

    EphemerisResult result_b;
    expect_true(eval_global_ephemeris_state(request, &result_b), "user B reuses shared cache source", failures);
    expect_true(result_b.cache_hit, "user B eval is shared cache hit", failures);
    expect_size(global_ephemeris_cache_entry_count(), 1, "user B keeps one shared cache entry", failures);
    expect_near(result_a.state.position_au.x, result_b.state.position_au.x, 0.0, "shared cache position x stable", failures);
    expect_near(result_a.state.velocity_au_per_day.x, result_b.state.velocity_au_per_day.x, 0.0, "shared cache velocity x stable", failures);

    remove_file_if_present(path);
    remove_dir_if_present(root);
}

void test_concurrent_global_eval_readers(int* failures) {
    const std::string root = make_temp_dir("/tmp/taiyin-global-concurrent-read-XXXXXX");
    expect_true(!root.empty(), "make concurrent reader root", failures);
    const std::string path = root + "/concurrent.opm4";
    expect_true(write_test_opm4_file_for(path, 820701, 10, 1), "write concurrent reader source", failures);

    const char* paths[] = { root.c_str() };
    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 1024 * 1024;
    config.source_paths = paths;
    config.source_path_count = 1;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize concurrent reader runtime", failures);

    const int reader_count = 8;
    const int iterations = 200;
    std::atomic<bool> start(false);
    std::atomic<int> read_failures(0);
    std::vector<std::thread> threads;

    for (int t = 0; t < reader_count; ++t) {
        threads.push_back(std::thread([&]() {
            while (!start.load()) {
                std::this_thread::yield();
            }

            for (int i = 0; i < iterations; ++i) {
                EphemerisResult result;
                if (!eval_global_ephemeris_state(make_request(820701), &result)) {
                    ++read_failures;
                    continue;
                }
                if (std::fabs(result.state.position_au.x - 0.005 / taiyin::TAIYIN_AU_KM) > 1.0e-20) {
                    ++read_failures;
                }
            }
        }));
    }

    start.store(true);
    for (size_t i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }

    expect_int(read_failures.load(), 0, "concurrent global eval readers see stable values", failures);
    expect_true(global_ephemeris_cache_entry_count() == 1, "concurrent global eval uses one cache entry", failures);

    remove_file_if_present(path);
    remove_dir_if_present(root);
}

void test_two_users_eviction_and_reload_from_source_path(int* failures) {
    const std::string root = make_temp_dir("/tmp/taiyin-global-user-evict-XXXXXX");
    expect_true(!root.empty(), "make user eviction root", failures);
    const std::string path_a = root + "/user_a.opm4";
    const std::string path_b = root + "/user_b.opm4";
    expect_true(write_test_opm4_file_for(path_a, 830101, 10, 1), "write user eviction source A", failures);
    expect_true(write_test_opm4_file_for(path_b, 830102, 10, 1), "write user eviction source B", failures);

    const char* paths[] = { root.c_str() };
    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 1;
    config.source_paths = paths;
    config.source_path_count = 1;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize user eviction runtime", failures);
    expect_size(global_ephemeris_catalog_size(), 2, "user eviction catalog count", failures);

    EphemerisRequest request_a = make_request(830101);
    EphemerisRequest request_b = make_request(830102);
    EphemerisBlockDescriptor bucket_a;
    EphemerisBlockDescriptor bucket_b;
    expect_true(make_global_bucket_for_request(request_a, &bucket_a), "make user A cache bucket", failures);
    expect_true(make_global_bucket_for_request(request_b, &bucket_b), "make user B cache bucket", failures);

    EphemerisService& user_a_service = global_ephemeris_service();
    EphemerisService& user_b_service = default_taiyin_runtime().ephemeris_service();
    expect_true(user_a_service.cache() == user_b_service.cache(), "user eviction services share cache", failures);

    EphemerisResult first_a;
    expect_true(eval_global_ephemeris_state(request_a, &first_a), "user A loads source A", failures);
    expect_false(first_a.cache_hit, "user A source A first eval miss", failures);
    expect_true(global_ephemeris_cache_contains(bucket_a.route_key), "tiny cache contains source A", failures);
    expect_size(global_ephemeris_cache_entry_count(), 1, "tiny cache has one entry after source A", failures);

    EphemerisResult first_b;
    expect_true(eval_global_ephemeris_state(request_b, &first_b), "user B loads source B and evicts A", failures);
    expect_false(first_b.cache_hit, "user B source B first eval miss", failures);
    expect_true(!global_ephemeris_cache_contains(bucket_a.route_key), "source A evicted by user B", failures);
    expect_true(global_ephemeris_cache_contains(bucket_b.route_key), "tiny cache contains source B", failures);
    expect_size(global_ephemeris_cache_entry_count(), 1, "tiny cache has one entry after source B", failures);

    EphemerisResult reload_a;
    expect_true(eval_global_ephemeris_state(request_a, &reload_a), "user A reloads source A after eviction", failures);
    expect_false(reload_a.cache_hit, "user A source A reload miss", failures);
    expect_true(global_ephemeris_cache_contains(bucket_a.route_key), "source A reloaded into tiny cache", failures);
    expect_true(!global_ephemeris_cache_contains(bucket_b.route_key), "source B evicted by user A reload", failures);
    expect_near(reload_a.state.position_au.x, first_a.state.position_au.x, 0.0, "source A reload state stable", failures);

    EphemerisResult reload_b;
    expect_true(eval_global_ephemeris_state(request_b, &reload_b), "user B reloads source B after eviction", failures);
    expect_false(reload_b.cache_hit, "user B source B reload miss", failures);
    expect_true(!global_ephemeris_cache_contains(bucket_a.route_key), "source A evicted by user B reload", failures);
    expect_true(global_ephemeris_cache_contains(bucket_b.route_key), "source B reloaded into tiny cache", failures);
    expect_near(reload_b.state.position_au.x, first_b.state.position_au.x, 0.0, "source B reload state stable", failures);

    remove_file_if_present(path_a);
    remove_file_if_present(path_b);
    remove_dir_if_present(root);
}

void test_add_source_path_preserves_existing_cache(int* failures) {
    const std::string root_a = make_temp_dir("/tmp/taiyin-global-add-a-XXXXXX");
    const std::string root_b = make_temp_dir("/tmp/taiyin-global-add-b-XXXXXX");
    expect_true(!root_a.empty(), "make add-source root A", failures);
    expect_true(!root_b.empty(), "make add-source root B", failures);
    const std::string path_a = root_a + "/a.opm4";
    const std::string path_b = root_b + "/b.opm4";
    expect_true(write_test_opm4_file_for(path_a, 820301, 10, 1), "write add-source A", failures);
    expect_true(write_test_opm4_file_for(path_b, 820302, 10, 1), "write add-source B", failures);

    const char* paths[] = { root_a.c_str() };
    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 1024 * 1024;
    config.source_paths = paths;
    config.source_path_count = 1;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize add-source runtime", failures);

    EphemerisResult first_a;
    expect_true(eval_global_ephemeris_state(make_request(820301), &first_a), "load source A before adding B", failures);
    expect_false(first_a.cache_hit, "source A first eval cache miss", failures);
    expect_size(global_ephemeris_catalog_size(), 1, "add-source catalog starts with one descriptor", failures);
    expect_size(global_ephemeris_cache_entry_count(), 1, "source A load inserts one cache entry", failures);

    expect_true(add_global_ephemeris_source_path(root_b.c_str()), "add second explicit source path", failures);
    expect_size(global_ephemeris_catalog_size(), 2, "adding source path grows catalog", failures);
    expect_size(global_ephemeris_cache_entry_count(), 1, "adding source path preserves existing cache", failures);

    EphemerisResult second_a;
    expect_true(eval_global_ephemeris_state(make_request(820301), &second_a), "source A remains evaluable after adding B", failures);
    expect_true(second_a.cache_hit, "source A cache survives adding B", failures);

    EphemerisResult first_b;
    expect_true(eval_global_ephemeris_state(make_request(820302), &first_b), "source B lazy loads after add path", failures);
    expect_false(first_b.cache_hit, "source B first eval cache miss", failures);
    expect_size(global_ephemeris_cache_entry_count(), 2, "source B load adds second cache entry", failures);

    remove_file_if_present(path_a);
    remove_file_if_present(path_b);
    remove_dir_if_present(root_a);
    remove_dir_if_present(root_b);
}

void test_reinitialize_clears_catalog_and_cache(int* failures) {
    const std::string root = make_temp_dir("/tmp/taiyin-global-reset-XXXXXX");
    expect_true(!root.empty(), "make reset root", failures);
    const std::string path = root + "/reset.opm4";
    expect_true(write_test_opm4_file_for(path, 820401, 10, 1), "write reset source", failures);

    const char* paths[] = { root.c_str() };
    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 1024 * 1024;
    config.source_paths = paths;
    config.source_path_count = 1;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize before reset", failures);
    EphemerisBlockCache* cache_before_reset = global_ephemeris_cache();
    EphemerisResult loaded;
    expect_true(eval_global_ephemeris_state(make_request(820401), &loaded), "load before reset", failures);
    expect_size(global_ephemeris_catalog_size(), 1, "catalog populated before reset", failures);
    expect_size(global_ephemeris_cache_entry_count(), 1, "cache populated before reset", failures);

    EphemerisRuntimeConfig empty_config;
    empty_config.cache_max_bytes = 2048;
    expect_true(initialize_global_ephemeris_runtime(empty_config), "reinitialize empty runtime", failures);
    expect_size(global_ephemeris_catalog_size(), 0, "reset clears global catalog", failures);
    expect_true(global_ephemeris_cache_max_bytes() > 0, "reset recreates global cache", failures);
    expect_size(global_ephemeris_cache_entry_count(), 0, "reset clears global cache entries", failures);
    expect_size(global_ephemeris_cache_max_bytes(), 2048, "reset applies new cache size", failures);
    expect_true(global_ephemeris_cache() != cache_before_reset || global_ephemeris_cache_entry_count() == 0, "reset does not retain populated cache state", failures);
    expect_global_service_bindings("reset keeps global service bindings", failures);

    EphemerisResult after_reset;
    expect_false(eval_global_ephemeris_state(make_request(820401), &after_reset), "old target fails after reset", failures);

    remove_file_if_present(path);
    remove_dir_if_present(root);
}

void test_failed_initialization_rolls_back_to_empty_runtime(int* failures) {
    const std::string root = make_temp_dir("/tmp/taiyin-global-fail-XXXXXX");
    expect_true(!root.empty(), "make failed-init root", failures);
    const std::string path = root + "/before_fail.opm4";
    expect_true(write_test_opm4_file_for(path, 820501, 10, 1), "write failed-init setup source", failures);

    const char* good_paths[] = { root.c_str() };
    EphemerisRuntimeConfig good_config;
    good_config.cache_max_bytes = 1024 * 1024;
    good_config.source_paths = good_paths;
    good_config.source_path_count = 1;
    expect_true(initialize_global_ephemeris_runtime(good_config), "initialize before failed init", failures);
    EphemerisResult before_fail;
    expect_true(eval_global_ephemeris_state(make_request(820501), &before_fail), "load before failed init", failures);
    expect_size(global_ephemeris_catalog_size(), 1, "catalog populated before failed init", failures);
    expect_size(global_ephemeris_cache_entry_count(), 1, "cache populated before failed init", failures);

    const char* bad_paths[] = { "/tmp/taiyin-global-runtime-definitely-missing.opm4" };
    EphemerisRuntimeConfig bad_config;
    bad_config.cache_max_bytes = 1024 * 1024;
    bad_config.source_paths = bad_paths;
    bad_config.source_path_count = 1;
    expect_false(initialize_global_ephemeris_runtime(bad_config), "missing explicit path initialization fails", failures);
    expect_size(global_ephemeris_catalog_size(), 0, "failed init rolls catalog back to empty", failures);
    expect_true(global_ephemeris_cache_max_bytes() > 0, "failed init leaves cache initialized", failures);
    expect_size(global_ephemeris_cache_entry_count(), 0, "failed init rolls cache back to empty", failures);
    expect_global_service_bindings("failed init keeps global service bindings", failures);

    EphemerisResult after_fail;
    expect_false(eval_global_ephemeris_state(make_request(820501), &after_fail), "old source unavailable after failed init rollback", failures);

    remove_file_if_present(path);
    remove_dir_if_present(root);
}

void test_global_priority_override_selects_high_priority_source(int* failures) {
    const std::string root = make_temp_dir("/tmp/taiyin-global-priority-XXXXXX");
    expect_true(!root.empty(), "make priority root", failures);
    const std::string low_path = root + "/low_method.opm4";
    const std::string high_path = root + "/high_method.opm4";
    expect_true(write_test_opm4_file_for(low_path, 820601, 10, 1), "write low-priority method source", failures);
    expect_true(write_test_opm4_file_for(high_path, 820601, 10, 99), "write high-priority method source", failures);

    const char* paths[] = { root.c_str() };
    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 1024 * 1024;
    config.source_paths = paths;
    config.source_path_count = 1;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize priority runtime", failures);
    expect_size(global_ephemeris_catalog_size(), 2, "priority runtime registers two descriptors", failures);
    expect_true(set_global_ephemeris_method_priority(99, 200), "set custom method priority", failures);

    EphemerisResult result;
    expect_true(eval_global_ephemeris_state(make_request(820601), &result), "global priority eval succeeds", failures);
    expect_false(result.cache_hit, "priority winner first eval cache miss", failures);
    expect_int(result.descriptor.method_id, 99, "priority override selects custom method", failures);
    expect_near(result.state.position_au.x, 0.005 / taiyin::TAIYIN_AU_KM, 1.0e-20, "priority winner position", failures);

    EphemerisResult second;
    expect_true(eval_global_ephemeris_state(make_request(820601), &second), "global priority second eval succeeds", failures);
    expect_true(second.cache_hit, "priority winner second eval cache hit", failures);
    expect_int(second.descriptor.method_id, 99, "priority override remains selected", failures);

    remove_file_if_present(low_path);
    remove_file_if_present(high_path);
    remove_dir_if_present(root);
}

void test_custom_nutation_precession_selection_integration(int* failures) {
    using namespace taiyin::dispatch;

    expect_true(add_nutation_model(NutationModelEntry(CUSTOM_NUTATION_ID, &custom_test_nutation)), "add global test custom nutation", failures);
    expect_true(add_precession_model(PrecessionModelEntry(CUSTOM_PRECESSION_ID, &custom_test_precession)), "add global test custom precession", failures);

    const int nutation_order[] = { CUSTOM_NUTATION_ID, NUTATION_IAU2000B, NUTATION_IAU2000A };
    expect_true(set_nutation_priority_order(nutation_order, sizeof(nutation_order) / sizeof(nutation_order[0])), "set custom nutation first", failures);
    NutationModelEntry selected_nutation;
    expect_true(select_nutation_model(MODEL_SELECTION_DEFAULT, &selected_nutation), "select default custom nutation", failures);
    expect_int(selected_nutation.model_id, CUSTOM_NUTATION_ID, "default nutation selects custom", failures);

    NutationAngles nutation;
    expect_true(eval_selected_nutation(MODEL_SELECTION_DEFAULT, 2451545.0, 0, &nutation), "eval selected custom nutation", failures);
    expect_near(nutation.dpsi_rad, 2451545.0 * 3.0e-12, 1.0e-18, "custom nutation dpsi", failures);
    expect_near(nutation.deps_rad, 2451545.0 * 4.0e-12, 1.0e-18, "custom nutation deps", failures);
    expect_near(nutation.mean_obliquity_rad, 0.41, 0.0, "custom nutation mean obliquity", failures);

    const int precession_order[] = { CUSTOM_PRECESSION_ID, PRECESSION_IAU2006, PRECESSION_VONDRAK2011 };
    expect_true(set_precession_priority_order(precession_order, sizeof(precession_order) / sizeof(precession_order[0])), "set custom precession first", failures);
    PrecessionModelEntry selected_precession;
    expect_true(select_precession_model(MODEL_SELECTION_DEFAULT, &selected_precession), "select default custom precession", failures);
    expect_int(selected_precession.model_id, CUSTOM_PRECESSION_ID, "default precession selects custom", failures);

    Matrix3x3 precession;
    double mean_obliquity = 0.0;
    expect_true(eval_selected_precession(MODEL_SELECTION_DEFAULT, 123.0, 0, &precession, &mean_obliquity), "eval selected custom precession", failures);
    expect_near(precession.m[0][1], 123.0e-6, 1.0e-18, "custom precession matrix 01", failures);
    expect_near(precession.m[1][0], -123.0e-6, 1.0e-18, "custom precession matrix 10", failures);
    expect_near(mean_obliquity, 0.5123, 0.0, "custom precession mean obliquity", failures);

    expect_true(select_nutation_model(NUTATION_IAU2000B, &selected_nutation), "explicit builtin zero nutation still selects", failures);
    expect_int(selected_nutation.model_id, NUTATION_IAU2000B, "explicit builtin zero nutation id", failures);
    expect_true(select_precession_model(PRECESSION_VONDRAK2011, &selected_precession), "explicit builtin zero precession still selects", failures);
    expect_int(selected_precession.model_id, PRECESSION_VONDRAK2011, "explicit builtin zero precession id", failures);
}

}  // namespace

int main() {
    int failures = 0;
    initialize_empty_global_runtime(&failures);
    test_locked_global_helper_apis(&failures);
    test_custom_nutation_precession_selection_integration(&failures);
    test_custom_mercury_global_runtime_reload_after_eviction(&failures);
    test_vsop87_mercury_custom_method_global_cache(&failures);
    test_concurrent_vsop87_mercury_global_eval_readers(&failures);
    test_global_weighted_cache_keeps_high_priority_method(&failures);
    test_no_default_scan_and_bindings(&failures);
    test_explicit_multi_path_initialization_registers_sources_only(&failures);
    test_two_user_requests_share_global_cache(&failures);
    test_concurrent_global_eval_readers(&failures);
    test_two_users_eviction_and_reload_from_source_path(&failures);
    test_add_source_path_preserves_existing_cache(&failures);
    test_reinitialize_clears_catalog_and_cache(&failures);
    test_failed_initialization_rolls_back_to_empty_runtime(&failures);
    test_global_priority_override_selects_high_priority_source(&failures);

    if (failures == 0) {
        std::cout << "test_global_ephemeris_runtime: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_global_ephemeris_runtime failure(s)\n";
    return 1;
}
