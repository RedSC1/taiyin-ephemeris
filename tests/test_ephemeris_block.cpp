#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/physical_constants.h"
#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/custom_ephemeris_source_registry.h"
#include "taiyin/internal/ephemeris_cache.h"
#include "taiyin/internal/ephemeris_catalog.h"
#include "taiyin/internal/spk.h"
#include "taiyin/runtime/ephemeris_service.h"
#include "test_env.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

const char* VSOP87_MERCURY_PATH = taiyin_test::getenv_path("TAIYIN_VSOP87_MERCURY_PATH");
const char* DE441_PATH = taiyin_test::getenv_path("TAIYIN_DE441_PATH");

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

std::vector<uint8_t> make_segment_stream_payload() {
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
    return payload;
}

std::vector<uint8_t> make_opm4(uint32_t body_id, double quant_unit_km) {
    std::vector<uint8_t> payload = make_segment_stream_payload();
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
    push_i32_le(&data, static_cast<int32_t>(body_id));
    push_i32_le(&data, 10);
    push_i32_le(&data, 1);
    push_i32_le(&data, 1);
    push_f64_le(&data, 100.0);
    push_f64_le(&data, 110.0);
    push_u32_le(&data, 1);
    push_u8(&data, 1);
    push_u8(&data, 0);
    push_u8(&data, 1);
    push_u8(&data, 1);
    push_u8(&data, 1);
    for (int i = 0; i < 7; ++i) push_u8(&data, 0);
    push_f64_le(&data, quant_unit_km);
    while (data.size() < 128) push_u8(&data, 0);
    data.insert(data.end(), payload.begin(), payload.end());
    return data;
}

void expect_true(bool value, const char* label) {
    if (!value) {
        std::fprintf(stderr, "expected true: %s\n", label);
        assert(false);
    }
}

void expect_false(bool value, const char* label) {
    if (value) {
        std::fprintf(stderr, "expected false: %s\n", label);
        assert(false);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label) {
    if (std::fabs(actual - expected) > tolerance) {
        std::fprintf(
            stderr,
            "%s mismatch: actual %.17g expected %.17g diff %.17g tolerance %.17g\n",
            label,
            actual,
            expected,
            actual - expected,
            tolerance);
        assert(false);
    }
}

struct CustomStateData {
    double bias;
    int* destroy_count;
};

struct ZiQiData {
    double epoch_jd_tdb;
    double longitude_at_epoch_rad;
    double period_days;
    int* destroy_count;
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

bool file_exists(const char* path) {
    if (!path) {
        return false;
    }
    std::ifstream file(path);
    return static_cast<bool>(file);
}

double vector_norm(const taiyin::Vector3& vector) {
    return std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
}

double position_diff_norm_au(const taiyin::CartesianState& lhs, const taiyin::CartesianState& rhs) {
    const double dx = lhs.position_au.x - rhs.position_au.x;
    const double dy = lhs.position_au.y - rhs.position_au.y;
    const double dz = lhs.position_au.z - rhs.position_au.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double angular_position_diff_arcsec(const taiyin::CartesianState& lhs, const taiyin::CartesianState& rhs) {
    const double ax = lhs.position_au.x;
    const double ay = lhs.position_au.y;
    const double az = lhs.position_au.z;
    const double bx = rhs.position_au.x;
    const double by = rhs.position_au.y;
    const double bz = rhs.position_au.z;
    const double an = vector_norm(lhs.position_au);
    const double bn = vector_norm(rhs.position_au);
    if (an == 0.0 || bn == 0.0) {
        return 0.0;
    }

    const double cx = ay * bz - az * by;
    const double cy = az * bx - ax * bz;
    const double cz = ax * by - ay * bx;
    const double cross_norm = std::sqrt(cx * cx + cy * cy + cz * cz);
    const double dot = ax * bx + ay * by + az * bz;
    return std::atan2(cross_norm, dot) * taiyin::TAIYIN_RAD_TO_ARCSEC;
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

bool custom_eval(double jd_tdb, const void* data, taiyin::CartesianState* out) {
    const CustomStateData* custom = static_cast<const CustomStateData*>(data);
    if (!custom || !out) {
        return false;
    }
    out->position_au.x = jd_tdb + custom->bias;
    out->position_au.y = custom->bias;
    out->position_au.z = 0.0;
    out->velocity_au_per_day.x = 1.0;
    out->velocity_au_per_day.y = 0.0;
    out->velocity_au_per_day.z = 0.0;
    out->acceleration_au_per_day2.x = 0.0;
    out->acceleration_au_per_day2.y = 0.0;
    out->acceleration_au_per_day2.z = 0.0;
    return true;
}

bool custom_position(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const CustomStateData* custom = static_cast<const CustomStateData*>(data);
    if (!custom || !out) {
        return false;
    }
    out->x = jd_tdb + custom->bias;
    out->y = custom->bias;
    out->z = 0.0;
    return true;
}

bool custom_velocity(double, const void* data, taiyin::Vector3* out) {
    const CustomStateData* custom = static_cast<const CustomStateData*>(data);
    if (!custom || !out) {
        return false;
    }
    out->x = 1.0;
    out->y = 0.0;
    out->z = 0.0;
    return true;
}

bool custom_acceleration(double, const void* data, taiyin::Vector3* out) {
    const CustomStateData* custom = static_cast<const CustomStateData*>(data);
    if (!custom || !out) {
        return false;
    }
    out->x = 0.0;
    out->y = 0.0;
    out->z = 0.0;
    return true;
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

void compute_vsop87_mercury_position(double jd_tdb, const Vsop87MercuryData* data, taiyin::Vector3* out) {
    const double t = (jd_tdb - taiyin::JD_J2000) / taiyin::DAYS_PER_JULIAN_MILLENNIUM;
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

bool vsop87_mercury_position(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const Vsop87MercuryData* mercury = static_cast<const Vsop87MercuryData*>(data);
    if (!mercury || !out) {
        return false;
    }

    taiyin::Vector3 position_ecliptic;
    compute_vsop87_mercury_position(jd_tdb, mercury, &position_ecliptic);
    ecliptic_to_equatorial(position_ecliptic, out);
    return true;
}

bool vsop87_mercury_velocity(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const Vsop87MercuryData* mercury = static_cast<const Vsop87MercuryData*>(data);
    if (!mercury || !out) {
        return false;
    }

    const double h = 1e-3;
    taiyin::Vector3 p_minus;
    taiyin::Vector3 p_plus;
    compute_vsop87_mercury_position(jd_tdb - h, mercury, &p_minus);
    compute_vsop87_mercury_position(jd_tdb + h, mercury, &p_plus);

    taiyin::Vector3 velocity_ecliptic;
    velocity_ecliptic.x = (p_plus.x - p_minus.x) / (2.0 * h);
    velocity_ecliptic.y = (p_plus.y - p_minus.y) / (2.0 * h);
    velocity_ecliptic.z = (p_plus.z - p_minus.z) / (2.0 * h);
    ecliptic_to_equatorial(velocity_ecliptic, out);
    return true;
}

bool vsop87_mercury_acceleration(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const Vsop87MercuryData* mercury = static_cast<const Vsop87MercuryData*>(data);
    if (!mercury || !out) {
        return false;
    }

    const double h = 1e-3;
    taiyin::Vector3 p0;
    taiyin::Vector3 p_minus;
    taiyin::Vector3 p_plus;
    compute_vsop87_mercury_position(jd_tdb, mercury, &p0);
    compute_vsop87_mercury_position(jd_tdb - h, mercury, &p_minus);
    compute_vsop87_mercury_position(jd_tdb + h, mercury, &p_plus);

    taiyin::Vector3 acceleration_ecliptic;
    acceleration_ecliptic.x = (p_plus.x - 2.0 * p0.x + p_minus.x) / (h * h);
    acceleration_ecliptic.y = (p_plus.y - 2.0 * p0.y + p_minus.y) / (h * h);
    acceleration_ecliptic.z = (p_plus.z - 2.0 * p0.z + p_minus.z) / (h * h);

    ecliptic_to_equatorial(acceleration_ecliptic, out);
    return true;
}

bool ziqi_position(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const ZiQiData* ziqi = static_cast<const ZiQiData*>(data);
    if (!ziqi || !out || ziqi->period_days <= 0.0) {
        return false;
    }

    const double rate = 2.0 * taiyin::TAIYIN_PI / ziqi->period_days;
    const double longitude = ziqi->longitude_at_epoch_rad + rate * (jd_tdb - ziqi->epoch_jd_tdb);
    out->x = std::cos(longitude);
    out->y = std::sin(longitude);
    out->z = 0.0;
    return true;
}

void custom_destroy(void* data) {
    CustomStateData* custom = static_cast<CustomStateData*>(data);
    if (custom && custom->destroy_count) {
        ++(*custom->destroy_count);
    }
    delete custom;
}

void ziqi_destroy(void* data) {
    ZiQiData* ziqi = static_cast<ZiQiData*>(data);
    if (ziqi && ziqi->destroy_count) {
        ++(*ziqi->destroy_count);
    }
    delete ziqi;
}

void vsop87_mercury_destroy(void* data) {
    Vsop87MercuryData* mercury = static_cast<Vsop87MercuryData*>(data);
    if (mercury && mercury->destroy_count) {
        ++(*mercury->destroy_count);
    }
    delete mercury;
}


int g_vsop87_file_load_count = 0;
int g_vsop87_file_destroy_count = 0;

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

void test_vsop87_mercury_custom_registry_path() {
    using taiyin::runtime::EphemerisRequest;
    using taiyin::runtime::EphemerisResult;
    using taiyin::runtime::EphemerisService;
    using taiyin::internal::CustomEphemerisFileSourceDefinition;
    using taiyin::internal::CustomEphemerisSourceDefinition;
    using taiyin::internal::EphemerisBlockCache;
    using taiyin::internal::EphemerisBlockCatalog;
    using taiyin::internal::EphemerisBlockDescriptor;
    using taiyin::internal::EphemerisFrame;
    using taiyin::internal::clear_custom_ephemeris_sources;
    using taiyin::internal::make_custom_ephemeris_descriptor;
    using taiyin::internal::register_custom_ephemeris_file_source;
    using taiyin::internal::register_custom_ephemeris_source;

    clear_custom_ephemeris_sources();
    g_vsop87_file_load_count = 0;
    g_vsop87_file_destroy_count = 0;

    CustomEphemerisFileSourceDefinition mercury_definition;
    mercury_definition.target_id = taiyin::TAIYIN_BODY_MERCURY_BARYCENTER;
    mercury_definition.center_id = taiyin::TAIYIN_BODY_SUN;
    mercury_definition.method_id = 87001;
    mercury_definition.frame = EphemerisFrame::IcrfJ2000Equatorial;
    mercury_definition.jd_tdb_start = taiyin::JD_J2000 - 36525.0;
    mercury_definition.jd_tdb_end = taiyin::JD_J2000 + 36525.0;
    mercury_definition.path = VSOP87_MERCURY_PATH;
    mercury_definition.load = &load_vsop87_mercury_file_source;
    mercury_definition.position = &vsop87_mercury_position;
    mercury_definition.velocity = &vsop87_mercury_velocity;
    mercury_definition.acceleration = &vsop87_mercury_acceleration;
    mercury_definition.destroy = &vsop87_mercury_destroy;

    uint64_t mercury_source_id = 0;
    expect_true(register_custom_ephemeris_file_source(mercury_definition, &mercury_source_id), "register VSOP87A Mercury file-backed custom source");

    EphemerisBlockDescriptor mercury_descriptor;
    expect_true(make_custom_ephemeris_descriptor(mercury_source_id, &mercury_descriptor), "make VSOP87A Mercury custom descriptor");
    expect_true(mercury_descriptor.path == VSOP87_MERCURY_PATH, "VSOP87A Mercury descriptor stores path");

    CustomStateData synthetic_data;
    synthetic_data.bias = 11.0;
    synthetic_data.destroy_count = 0;
    CustomEphemerisSourceDefinition synthetic_definition;
    synthetic_definition.target_id = 900087;
    synthetic_definition.center_id = taiyin::TAIYIN_BODY_SUN;
    synthetic_definition.method_id = 87001;
    synthetic_definition.frame = EphemerisFrame::IcrfJ2000Equatorial;
    synthetic_definition.jd_tdb_start = taiyin::JD_J2000 - 10.0;
    synthetic_definition.jd_tdb_end = taiyin::JD_J2000 + 10.0;
    synthetic_definition.data = &synthetic_data;
    synthetic_definition.bytes = sizeof(CustomStateData);
    synthetic_definition.position = &custom_position;
    synthetic_definition.velocity = &custom_velocity;
    synthetic_definition.acceleration = &custom_acceleration;

    uint64_t synthetic_source_id = 0;
    expect_true(register_custom_ephemeris_source(synthetic_definition, &synthetic_source_id), "register synthetic eviction source");
    EphemerisBlockDescriptor synthetic_descriptor;
    expect_true(make_custom_ephemeris_descriptor(synthetic_source_id, &synthetic_descriptor), "make synthetic eviction descriptor");

    EphemerisBlockCatalog catalog;
    expect_true(catalog.add(mercury_descriptor), "add VSOP87A Mercury descriptor");
    expect_true(catalog.add(synthetic_descriptor), "add synthetic eviction descriptor");

    EphemerisBlockCache cache(1);
    EphemerisService service;
    service.set_catalog(&catalog);
    service.set_cache(&cache);

    EphemerisRequest mercury_request;
    mercury_request.target_id = taiyin::TAIYIN_BODY_MERCURY_BARYCENTER;
    mercury_request.center_id = taiyin::TAIYIN_BODY_SUN;
    mercury_request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    mercury_request.jd_tdb = taiyin::JD_J2000;

    EphemerisResult mercury_result;
    expect_true(service.eval_state(mercury_request, &mercury_result), "eval VSOP87A Mercury through file-backed custom cache");
    expect_true(!mercury_result.cache_hit, "VSOP87A Mercury first file-backed eval is cache miss");
    expect_true(g_vsop87_file_load_count == 1, "VSOP87A Mercury file loaded once");
    expect_near(mercury_result.state.position_au.x, -0.13009360356893562, 1e-13, "VSOP87A Mercury file-backed J2000 x");
    expect_true(cache.contains(mercury_descriptor.route_key), "cache contains VSOP87A Mercury file-backed block");

    EphemerisResult mercury_hit;
    expect_true(service.eval_state(mercury_request, &mercury_hit), "eval VSOP87A Mercury file-backed cache hit");
    expect_true(mercury_hit.cache_hit, "VSOP87A Mercury second file-backed eval is cache hit");
    expect_true(g_vsop87_file_load_count == 1, "VSOP87A Mercury cache hit does not reload file");

    EphemerisRequest synthetic_request = mercury_request;
    synthetic_request.target_id = 900087;
    EphemerisResult synthetic_result;
    expect_true(service.eval_state(synthetic_request, &synthetic_result), "eval synthetic source to evict VSOP87A Mercury");
    expect_true(!cache.contains(mercury_descriptor.route_key), "VSOP87A Mercury file-backed block evicted");
    expect_true(cache.contains(synthetic_descriptor.route_key), "cache contains synthetic eviction source");
    expect_true(g_vsop87_file_destroy_count == 1, "VSOP87A Mercury cache clone destroyed on eviction");

    EphemerisResult mercury_reload;
    expect_true(service.eval_state(mercury_request, &mercury_reload), "reload VSOP87A Mercury custom source from descriptor path after eviction");
    expect_true(!mercury_reload.cache_hit, "VSOP87A Mercury reload is cache miss");
    expect_true(g_vsop87_file_load_count == 2, "VSOP87A Mercury reload reads file again");
    expect_true(cache.contains(mercury_descriptor.route_key), "cache contains reloaded VSOP87A Mercury block");
    expect_near(mercury_reload.state.position_au.y, -0.40059371691764684, 1e-13, "VSOP87A Mercury file-backed reload y");

    cache.clear();
    expect_true(g_vsop87_file_destroy_count == 2, "VSOP87A Mercury reloaded cache clone destroyed on cache clear");
    clear_custom_ephemeris_sources();
    expect_true(g_vsop87_file_destroy_count == 2, "VSOP87A Mercury registry clear does not destroy parsed file data");
}

}  // namespace

int main() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::EphemerisBlockCacheKey;
    using taiyin::internal::EphemerisBlockCacheMetadata;
    using taiyin::internal::EphemerisBlockCompileOptions;
    using taiyin::internal::EphemerisBlockFormat;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_ephemeris_block;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::eval_compiled_ephemeris_block_acceleration;
    using taiyin::internal::eval_compiled_ephemeris_block_position;
    using taiyin::internal::eval_compiled_ephemeris_block_velocity;
    using taiyin::internal::make_compiled_ephemeris_block;
    using taiyin::internal::make_ephemeris_block_cache_metadata;

    CompiledEphemerisBlock block;
    CartesianState state;
    taiyin::Vector3 component;

    const std::vector<uint8_t> venus = make_opm4(2, 0.08);
    StorageEphemerisBlock storage;
    expect_true(compile_ephemeris_block(&venus[0], venus.size(), 0, &storage), "compile opm4 block");
    expect_true(storage.format == EphemerisBlockFormat::Opm4, "opm4 format");
    get_compiled_block_from_storage(&storage, 0, &block);
    expect_true(block.data && block.position && block.velocity && block.acceleration && block.bytes > 0, "opm4 compiled fields");
    expect_true(eval_compiled_ephemeris_block(105.0, &block, &state), "eval opm4 block");
    expect_near(state.position_au.x, 0.08 / taiyin::TAIYIN_AU_KM, 1e-18, "opm4 position x");
    expect_near(state.velocity_au_per_day.y, 0.064 / taiyin::TAIYIN_AU_KM, 1e-18, "opm4 velocity y");
    expect_true(eval_compiled_ephemeris_block_position(105.0, &block, &component), "eval opm4 position component");
    expect_near(component.x, state.position_au.x, 0.0, "opm4 position component x");
    expect_true(eval_compiled_ephemeris_block_velocity(105.0, &block, &component), "eval opm4 velocity component");
    expect_near(component.y, state.velocity_au_per_day.y, 0.0, "opm4 velocity component y");
    expect_true(eval_compiled_ephemeris_block_acceleration(105.0, &block, &component), "eval opm4 acceleration component");
    expect_near(component.y, state.acceleration_au_per_day2.y, 0.0, "opm4 acceleration component y");
    destroy_storage_ephemeris_block(&storage);

    EphemerisBlockCompileOptions range_options;
    range_options.has_required_jd_tdb_range = true;
    range_options.required_jd_tdb_start = 101.0;
    range_options.required_jd_tdb_end = 109.0;
    expect_true(compile_ephemeris_block(&venus[0], venus.size(), &range_options, &storage), "compile opm4 covering required range");
    destroy_storage_ephemeris_block(&storage);

    range_options.required_jd_tdb_start = 99.0;
    range_options.required_jd_tdb_end = 105.0;
    expect_false(compile_ephemeris_block(&venus[0], venus.size(), &range_options, &storage), "reject opm4 before required range");

    range_options.required_jd_tdb_start = 105.0;
    range_options.required_jd_tdb_end = 111.0;
    expect_false(compile_ephemeris_block(&venus[0], venus.size(), &range_options, &storage), "reject opm4 after required range");

    const std::vector<uint8_t> pluto = make_opm4(9, 4.0);
    expect_true(compile_ephemeris_block(&pluto[0], pluto.size(), 0, &storage), "compile pluto opm4 block");
    expect_true(storage.format == EphemerisBlockFormat::Opm4, "pluto opm4 format");
    get_compiled_block_from_storage(&storage, 0, &block);
    expect_true(eval_compiled_ephemeris_block(105.0, &block, &state), "eval pluto opm4 block");
    expect_near(state.position_au.x, 4.0 / taiyin::TAIYIN_AU_KM, 1e-18, "pluto opm4 position x");
    expect_near(state.velocity_au_per_day.x, 1.6 / taiyin::TAIYIN_AU_KM, 1e-18, "pluto opm4 velocity x");
    destroy_storage_ephemeris_block(&storage);

    const uint8_t bad[] = { 'O', 'P', 'M', '2' };
    expect_false(compile_ephemeris_block(bad, sizeof(bad), 0, &storage), "old opm2 rejected");

    int destroy_count = 0;
    CustomStateData* custom = new CustomStateData();
    custom->bias = 7.0;
    custom->destroy_count = &destroy_count;
    expect_true(
        make_compiled_ephemeris_block(
            custom,
            sizeof(CustomStateData),
            custom_position,
            custom_velocity,
            custom_acceleration,
            &block),
        "make custom compiled block");
    expect_true(block.format == EphemerisBlockFormat::FormatUnknown, "custom block has unknown format");
    expect_true(block.bytes == sizeof(CustomStateData), "custom block byte size");
    expect_true(eval_compiled_ephemeris_block(10.0, &block, &state), "eval custom block");
    expect_near(state.position_au.x, 17.0, 0.0, "custom position x");
    expect_near(state.velocity_au_per_day.x, 1.0, 0.0, "custom velocity x");
    expect_true(eval_compiled_ephemeris_block_position(10.0, &block, &component), "eval custom position component");
    expect_near(component.x, 17.0, 0.0, "custom position component x");
    expect_true(eval_compiled_ephemeris_block_velocity(10.0, &block, &component), "eval custom velocity component");
    expect_near(component.x, 1.0, 0.0, "custom velocity component x");
    expect_true(eval_compiled_ephemeris_block_acceleration(10.0, &block, &component), "eval custom acceleration component");
    expect_near(component.x, 0.0, 0.0, "custom acceleration component x");

    EphemerisBlockCacheMetadata metadata;
    expect_true(
        make_ephemeris_block_cache_metadata(
            EphemerisBlockCacheKey(900001, 10, 42),
            1.0,
            2.0,
            100,
            &block,
            &metadata),
        "make custom cache metadata");
    expect_true(metadata.key.target_id == 900001, "metadata target");
    expect_true(metadata.key.center_id == 10, "metadata center");
    expect_true(metadata.key.source_id == 42, "metadata source");
    expect_near(metadata.jd_tdb_start, 1.0, 0.0, "metadata start");
    expect_near(metadata.jd_tdb_end, 2.0, 0.0, "metadata end");
    expect_true(metadata.priority == 100, "metadata priority");
    expect_true(metadata.bytes == sizeof(CustomStateData), "metadata bytes");
    expect_false(
        make_ephemeris_block_cache_metadata(
            EphemerisBlockCacheKey(900001, 10, 42),
            3.0,
            2.0,
            100,
            &block,
            &metadata),
        "reject reversed cache metadata range");

    custom_destroy(const_cast<void*>(block.data));
    block = CompiledEphemerisBlock();
    expect_true(destroy_count == 1, "custom destroy called once");

    int position_only_destroy_count = 0;
    CustomStateData* position_only = new CustomStateData();
    position_only->bias = 2.0;
    position_only->destroy_count = &position_only_destroy_count;
    expect_true(
        make_compiled_ephemeris_block(
            position_only,
            sizeof(CustomStateData),
            custom_position,
            0,
            0,
            &block),
        "make position-only custom block");
    expect_true(eval_compiled_ephemeris_block_position(3.0, &block, &component), "eval position-only position");
    expect_near(component.x, 5.0, 0.0, "position-only position x");
    expect_false(eval_compiled_ephemeris_block_velocity(3.0, &block, &component), "position-only velocity unavailable");
    expect_false(eval_compiled_ephemeris_block_acceleration(3.0, &block, &component), "position-only acceleration unavailable");
    custom_destroy(const_cast<void*>(block.data));
    block = CompiledEphemerisBlock();
    expect_true(position_only_destroy_count == 1, "position-only destroy called once");

    CustomStateData dummy_custom;
    dummy_custom.bias = 0.0;
    dummy_custom.destroy_count = 0;
    expect_false(
        make_compiled_ephemeris_block(
            &dummy_custom,
            sizeof(CustomStateData),
            0,
            custom_velocity,
            0,
            &block),
        "reject custom block without position component");

    expect_false(
        make_compiled_ephemeris_block(
            0,
            sizeof(CustomStateData),
            custom_position,
            custom_velocity,
            custom_acceleration,
            &block),
        "reject custom block without data");
    expect_false(
        make_compiled_ephemeris_block(
            &dummy_custom,
            sizeof(CustomStateData),
            0,
            custom_velocity,
            custom_acceleration,
            &block),
        "reject custom block without position");

    int ziqi_destroy_count = 0;
    ZiQiData* ziqi = new ZiQiData();
    ziqi->epoch_jd_tdb = 2451545.0;
    ziqi->longitude_at_epoch_rad = 0.0;
    ziqi->period_days = 28.0 * 365.25;
    ziqi->destroy_count = &ziqi_destroy_count;
    expect_true(
        make_compiled_ephemeris_block(ziqi, sizeof(ZiQiData), ziqi_position, 0, 0, &block),
        "make ziqi custom point block");
    expect_true(eval_compiled_ephemeris_block(2451545.0, &block, &state), "eval ziqi epoch");
    expect_near(state.position_au.x, 1.0, 1e-14, "ziqi epoch x");
    expect_near(state.position_au.y, 0.0, 1e-14, "ziqi epoch y");
    expect_true(eval_compiled_ephemeris_block(2451545.0 + 7.0 * 365.25, &block, &state), "eval ziqi quarter cycle");
    expect_near(state.position_au.x, 0.0, 1e-12, "ziqi quarter x");
    expect_near(state.position_au.y, 1.0, 1e-12, "ziqi quarter y");
    expect_true(
        make_ephemeris_block_cache_metadata(
            EphemerisBlockCacheKey(900002, 399, 1002),
            2451545.0,
            2451545.0 + ziqi->period_days,
            10,
            &block,
            &metadata),
        "make ziqi cache metadata");
    expect_true(metadata.key.target_id == 900002, "ziqi metadata target");
    expect_true(metadata.priority == 10, "ziqi metadata priority");
    expect_true(metadata.bytes == sizeof(ZiQiData), "ziqi metadata bytes");
    ziqi_destroy(const_cast<void*>(block.data));
    block = CompiledEphemerisBlock();
    expect_true(ziqi_destroy_count == 1, "ziqi destroy called once");

    if (file_exists(VSOP87_MERCURY_PATH)) {
        int vsop_destroy_count = 0;
        Vsop87MercuryData* mercury_vsop = 0;
        expect_true(
            load_vsop87_mercury_data(VSOP87_MERCURY_PATH, &vsop_destroy_count, &mercury_vsop),
            "load VSOP87A mercury data");
        const size_t vsop_bytes = sizeof(Vsop87MercuryData) + mercury_vsop->terms.size() * sizeof(Vsop87Term);
        expect_true(
            make_compiled_ephemeris_block(
                mercury_vsop,
                vsop_bytes,
                vsop87_mercury_position,
                vsop87_mercury_velocity,
                vsop87_mercury_acceleration,
                &block),
            "make VSOP87A mercury custom block");
        expect_true(eval_compiled_ephemeris_block(taiyin::JD_J2000, &block, &state), "eval VSOP87A mercury at J2000");
        expect_near(state.position_au.x, -0.13009360356893562, 1e-13, "VSOP87A mercury J2000 x");
        expect_near(state.position_au.y, -0.40059371691764684, 1e-13, "VSOP87A mercury J2000 y");
        expect_near(state.position_au.z, -0.20048931479048393, 1e-13, "VSOP87A mercury J2000 z");
        expect_true(std::fabs(state.velocity_au_per_day.x) > 1e-4, "VSOP87A mercury velocity x nonzero");

        if (file_exists(DE441_PATH)) {
            StorageEphemerisBlock spk_storage;
            expect_true(
                compile_spk_ephemeris_block_from_file(
                    DE441_PATH,
                    1,
                    10,
                    taiyin::JD_J2000 - 365.25,
                    taiyin::JD_J2000 + 365.25,
                    &spk_storage),
                "compile DE441 Mercury block");
            CompiledEphemerisBlock spk_block;
            get_compiled_block_from_storage(&spk_storage, 0, &spk_block);

            double max_position_error_au = 0.0;
            double max_angular_error_arcsec = 0.0;
            const double samples[] = {
                taiyin::JD_J2000 - 365.25,
                taiyin::JD_J2000 - 30.0,
                taiyin::JD_J2000,
                taiyin::JD_J2000 + 30.0,
                taiyin::JD_J2000 + 365.25,
            };
            for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
                CartesianState vsop_state;
                CartesianState spk_state;
                expect_true(eval_compiled_ephemeris_block(samples[i], &block, &vsop_state), "eval VSOP87A Mercury sample");
                expect_true(eval_compiled_ephemeris_block(samples[i], &spk_block, &spk_state), "eval DE441 Mercury sample");

                const double position_error_au = position_diff_norm_au(vsop_state, spk_state);
                const double angular_error_arcsec = angular_position_diff_arcsec(vsop_state, spk_state);
                if (position_error_au > max_position_error_au) {
                    max_position_error_au = position_error_au;
                }
                if (angular_error_arcsec > max_angular_error_arcsec) {
                    max_angular_error_arcsec = angular_error_arcsec;
                }
            }
            std::printf(
                "custom VSOP87A Mercury vs DE441 over +/-1y: max position %.17g au, max angular %.17g arcsec\n",
                max_position_error_au,
                max_angular_error_arcsec);
            expect_true(max_position_error_au < 2e-6, "VSOP87A Mercury position close to DE441");
            expect_true(max_angular_error_arcsec < 2.0, "VSOP87A Mercury angular close to DE441");
            destroy_storage_ephemeris_block(&spk_storage);
        } else {
            std::printf("skipping custom VSOP87A Mercury vs DE441 comparison; local DE441 is absent\n");
        }

        expect_true(
            make_ephemeris_block_cache_metadata(
                EphemerisBlockCacheKey(1, 10, 87001),
                taiyin::JD_J2000 - 36525.0,
                taiyin::JD_J2000 + 36525.0,
                20,
                &block,
                &metadata),
            "make VSOP87A mercury cache metadata");
        expect_true(metadata.key.target_id == 1, "VSOP87A metadata target");
        expect_true(metadata.key.center_id == 10, "VSOP87A metadata center");
        expect_true(metadata.key.source_id == 87001, "VSOP87A metadata source");
        expect_true(metadata.bytes == vsop_bytes, "VSOP87A metadata bytes");

        test_vsop87_mercury_custom_registry_path();

        vsop87_mercury_destroy(const_cast<void*>(block.data));
        block = CompiledEphemerisBlock();
        expect_true(vsop_destroy_count == 1, "VSOP87A mercury destroy called once");
    } else {
        std::printf("skipping custom VSOP87A Mercury block test; local VSOP87A.mer is absent\n");
    }

    return 0;
}
