#include "taiyin/internal/custom_ephemeris_source_registry.h"
#include "taiyin/runtime/taiyin_runtime.h"
#include "taiyin/time.h"
#include "test_env.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace taiyin;
using namespace taiyin::internal;
using namespace taiyin::runtime;

const double JD0 = taiyin::JD_J2000;
const double DAYS_PER_MILLENNIUM = taiyin::DAYS_PER_JULIAN_MILLENNIUM;
const char* VSOP87_MERCURY_PATH = taiyin_test::getenv_path("TAIYIN_VSOP87_MERCURY_PATH");
const int VSOP87_MERCURY_TARGET_ID = 1;
const int VSOP87_MERCURY_METHOD_ID = 87001;

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

struct Vsop87Term {
    int variable;
    int alpha;
    int multipliers[12];
    double sine_coeff;
    double cosine_coeff;
};

struct Vsop87MercuryData {
    std::vector<Vsop87Term> terms;
};

std::atomic<int> g_vsop87_file_load_count(0);
std::atomic<int> g_vsop87_destroy_count(0);
std::atomic<int> g_active_position_evals(0);
std::atomic<int> g_peak_position_evals(0);

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_int(int actual, int expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

bool file_exists(const char* path) {
    if (!path) {
        return false;
    }
    std::ifstream file(path);
    return static_cast<bool>(file);
}

void reset_counters() {
    g_vsop87_file_load_count.store(0);
    g_vsop87_destroy_count.store(0);
    g_active_position_evals.store(0);
    g_peak_position_evals.store(0);
}

void record_position_eval_enter() {
    const int current = g_active_position_evals.fetch_add(1) + 1;
    int observed = g_peak_position_evals.load();
    while (current > observed
           && !g_peak_position_evals.compare_exchange_weak(observed, current)) {}
}

void record_position_eval_exit() {
    g_active_position_evals.fetch_sub(1);
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

bool load_vsop87_mercury_data(const char* path, Vsop87MercuryData** out) {
    if (!path || !out) {
        return false;
    }
    *out = 0;

    std::ifstream file(path);
    if (!file) {
        return false;
    }

    Vsop87MercuryData* data = new Vsop87MercuryData();
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

    record_position_eval_enter();
    Vector3 position_ecliptic;
    compute_vsop87_mercury_position(jd_tdb, mercury, &position_ecliptic);
    ecliptic_to_equatorial(position_ecliptic, out);
    record_position_eval_exit();
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
    if (!load_vsop87_mercury_data(path, &data) || !data) {
        return false;
    }

    g_vsop87_file_load_count.fetch_add(1);
    *out_data = data;
    *out_bytes = sizeof(Vsop87MercuryData) + data->terms.size() * sizeof(Vsop87Term);
    return true;
}

void vsop87_mercury_destroy(void* data) {
    delete static_cast<Vsop87MercuryData*>(data);
    g_vsop87_destroy_count.fetch_add(1);
}

EphemerisRequest make_mercury_request() {
    EphemerisRequest request;
    request.target_id = VSOP87_MERCURY_TARGET_ID;
    request.center_id = 0;
    request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = JD0;
    return request;
}

bool register_vsop87_mercury_descriptor(EphemerisBlockDescriptor* descriptor) {
    CustomEphemerisFileSourceDefinition definition;
    definition.target_id = VSOP87_MERCURY_TARGET_ID;
    definition.center_id = 0;
    definition.method_id = VSOP87_MERCURY_METHOD_ID;
    definition.frame = EphemerisFrame::IcrfJ2000Equatorial;
    definition.jd_tdb_start = JD0 - 1000.0;
    definition.jd_tdb_end = JD0 + 1000.0;
    definition.path = VSOP87_MERCURY_PATH;
    definition.load = load_vsop87_mercury_file_source;
    definition.position = vsop87_mercury_position;
    definition.velocity = vsop87_mercury_velocity;
    definition.acceleration = vsop87_mercury_acceleration;
    definition.destroy = vsop87_mercury_destroy;

    uint64_t source_id = 0;
    return register_custom_ephemeris_file_source(definition, &source_id)
        && make_custom_ephemeris_descriptor(source_id, descriptor);
}

void run_concurrent_mercury_eval(int thread_count, std::atomic<int>* failures) {
    std::atomic<bool> start(false);
    std::vector<std::thread> threads;
    for (int i = 0; i < thread_count; ++i) {
        threads.push_back(std::thread([&]() {
            while (!start.load()) {
                std::this_thread::yield();
            }
            EphemerisResult result;
            if (!eval_global_ephemeris_state(make_mercury_request(), &result)) {
                failures->fetch_add(1);
                return;
            }
            if (result.descriptor.method_id != VSOP87_MERCURY_METHOD_ID
                || std::fabs(result.state.position_au.x - (-0.13009360356893562)) > 1.0e-12
                || std::fabs(result.state.position_au.y - (-0.40059371691764684)) > 1.0e-12
                || std::fabs(result.state.position_au.z - (-0.20048931479048393)) > 1.0e-12) {
                failures->fetch_add(1);
            }
        }));
    }

    start.store(true);
    for (size_t i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }
}

void test_vsop87_mercury_global_mvp_end_to_end(int* failures) {
    if (!file_exists(VSOP87_MERCURY_PATH)) {
        std::cout << "SKIP: set TAIYIN_VSOP87_MERCURY_PATH to run the VSOP87 Mercury MVP end-to-end test\n";
        return;
    }

    reset_counters();
    clear_custom_ephemeris_sources();

    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 1024 * 1024;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize global runtime", failures);

    EphemerisBlockDescriptor descriptor;
    expect_true(register_vsop87_mercury_descriptor(&descriptor), "register VSOP87 Mercury descriptor", failures);
    expect_true(add_global_ephemeris_descriptor(descriptor), "add VSOP87 Mercury global descriptor", failures);
    expect_true(set_global_ephemeris_method_priority(VSOP87_MERCURY_METHOD_ID, 500),
                "set VSOP87 Mercury method priority", failures);

    std::atomic<int> eval_failures(0);
    run_concurrent_mercury_eval(8, &eval_failures);
    expect_int(eval_failures.load(), 0, "first concurrent VSOP87 Mercury eval", failures);
    expect_int(g_vsop87_file_load_count.load(), 1, "first miss singleflight load count", failures);
    expect_true(global_ephemeris_cache_entry_count() == 1, "Mercury cache entry after first miss", failures);

    g_peak_position_evals.store(0);
    g_active_position_evals.store(0);
    eval_failures.store(0);
    run_concurrent_mercury_eval(8, &eval_failures);
    expect_int(eval_failures.load(), 0, "cache-hit concurrent VSOP87 Mercury eval", failures);
    expect_int(g_vsop87_file_load_count.load(), 1, "cache-hit does not reload VSOP87 Mercury", failures);
    expect_true(g_peak_position_evals.load() > 1, "VSOP87 Mercury cache-hit callbacks overlap", failures);

    clear_global_ephemeris_cache();
    expect_true(global_ephemeris_cache_entry_count() == 0, "Mercury cache cleared before reload", failures);

    eval_failures.store(0);
    run_concurrent_mercury_eval(8, &eval_failures);
    expect_int(eval_failures.load(), 0, "concurrent VSOP87 Mercury reload after eviction", failures);
    expect_int(g_vsop87_file_load_count.load(), 2, "reload after eviction is singleflight", failures);
    expect_true(global_ephemeris_cache_entry_count() == 1, "Mercury cache entry after reload", failures);

    EphemerisRuntimeConfig empty_config;
    expect_true(initialize_global_ephemeris_runtime(empty_config), "reset global runtime after MVP test", failures);
    clear_custom_ephemeris_sources();
}

}  // namespace

int main() {
    int failures = 0;
    test_vsop87_mercury_global_mvp_end_to_end(&failures);

    if (failures != 0) {
        std::cerr << failures << " ephemeris MVP end-to-end test(s) failed\n";
        return 1;
    }
    return 0;
}
