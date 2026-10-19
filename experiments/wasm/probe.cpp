// Experimental adapters only: all astronomy goes through the existing C ABI.
#include "taiyin/c/taiyin.h"
#include "taiyin/internal/opm2.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
const int targets[] = {10, 301, 1, 2, 4, 5, 6, 7, 8, 9};
taiyin_split_julian_date split(double jd) {
    taiyin_split_julian_date value;
    value.day_number = static_cast<int64_t>(std::floor(jd));
    value.day_fraction = jd - std::floor(jd);
    return value;
}
void number(std::ostream& stream, double value) {
    if (std::isfinite(value)) stream << value;
    else stream << "null";
}
void result(std::ostream& stream, taiyin_call_result code) {
    stream << "\"status\":" << taiyin_call_result_status(code)
           << ",\"flags\":" << taiyin_call_result_flags(code);
}
char* copy_string(const std::string& value) {
    char* out = static_cast<char*>(std::malloc(value.size() + 1));
    if (out) std::memcpy(out, value.c_str(), value.size() + 1);
    return out;
}
}

extern "C" {
taiyin_call_result probe_initialize() {
    taiyin_runtime_config config;
    taiyin_runtime_config_init(&config);
    // This option also installs built-in semi-analytic descriptors. The test
    // driver runs native baselines in an empty cwd to exclude packaged files.
    config.load_packaged_data = 1;
    config.load_builtin_eop = 0;
    config.segment_cache_max_entries = 512;
    return taiyin_runtime_initialize(&config);
}

taiyin_call_result probe_create_context(uint32_t route, taiyin_context** out) {
    taiyin_call_result code = taiyin_context_create(out);
    if (code < 0) return code;
    code = taiyin_context_set_route_rule(*out, route);
    if (code < 0) {
        taiyin_context_destroy(*out);
        *out = 0;
    }
    return code;
}

taiyin_call_result probe_position(const taiyin_context* ctx, int target,
                                  double day, double fraction, uint32_t flags,
                                  double* out) {
    const taiyin_split_julian_date jd = {
        static_cast<int64_t>(day), fraction
    };
    return taiyin_calc_position_tt(ctx, target, &jd, flags, out, 0);
}

uint32_t probe_opm2_count() {
    uint32_t count = 0;
    for (size_t i = 0; i < taiyin_runtime_registered_data_source_count(); ++i) {
        taiyin_runtime_registered_data_source source;
        taiyin_runtime_registered_data_source_init(&source);
        size_t required = 0;
        const taiyin_call_result code = taiyin_runtime_get_registered_data_source(
            i, &source, 0, 0, &required);
        if (code >= 0 && source.format == TAIYIN_RUNTIME_DATA_FORMAT_OPM2)
            ++count;
    }
    return count;
}

char* probe_seams(const taiyin_context* ctx, const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return 0;
    const std::streamoff length = file.tellg();
    if (length <= 0 || length > 64 * 1024 * 1024) return 0;
    std::vector<char> bytes(static_cast<size_t>(length));
    file.seekg(0);
    if (!file.read(bytes.data(), length)) return 0;
    taiyin::internal::Opm2EpheSection ephe;
    taiyin::internal::Opm2GridSection grid;
    if (!taiyin::internal::parse_opm2_summary(bytes.data(), bytes.size(), &ephe, &grid)) return 0;
    taiyin_context* geometric = 0;
    if (taiyin_context_clone(ctx, &geometric) < 0) return 0;
    taiyin_apparent_config apparent;
    taiyin_apparent_config_init(&apparent);
    apparent.output_frame_id = TAIYIN_FRAME_J2000_ECLIPTIC;
    if (taiyin_context_set_apparent_config(geometric, &apparent) < 0) {
        taiyin_context_destroy(geometric);
        return 0;
    }
    const int64_t indices[] = {grid.first_segment_index,
        grid.first_segment_index + 1,
        grid.first_segment_index + static_cast<int64_t>(grid.segment_count / 2),
        grid.first_segment_index + static_cast<int64_t>(grid.segment_count) - 1,
        grid.first_segment_index + static_cast<int64_t>(grid.segment_count)};
    std::ostringstream json;
    json << std::setprecision(17) << '[';
    bool first = true;
    for (size_t i = 0; i < 5; ++i) for (int side = -1; side <= 1; ++side) {
        // Preserve a sub-ULP scalar-JD offset in the split fraction, including
        // just outside coverage. Use TDB directly so TT conversion cannot move
        // the seam. TT==TDB is intentional here: comparison uses J2000 geometry.
        taiyin_split_julian_date jd = split(grid.origin_jd + indices[i] * grid.period_days);
        jd.day_fraction += side * 1e-10;
        double out[6] = {};
        const uint32_t flags = TAIYIN_POSITION_XYZ | TAIYIN_POSITION_TRUEPOS
            | TAIYIN_POSITION_NO_ABERR | TAIYIN_POSITION_NO_GDEFL | TAIYIN_POSITION_NONUT;
        const taiyin_call_result code = taiyin_calc_position_tdb(
            geometric, ephe.target_id, &jd, &jd, flags, out, 0);
        if (!first) json << ',';
        first = false;
        json << '{'; result(json, code);
        json << ",\"values\":[";
        for (size_t k = 0; k < 3; ++k) { if (k) json << ','; number(json, out[k]); }
        json << "]}";
    }
    json << ']';
    taiyin_context_destroy(geometric);
    return copy_string(json.str());
}

char* probe_suite(const taiyin_context* ctx) {
    std::ostringstream json;
    json << std::setprecision(17) << "{\"positions\":[";
    // Remote dates exercise long-term frame transforms; precise data tests
    // include both ends of the common DE442 product coverage.
    const double epochs[] = {2000000.5, 2290000.5, 2415020.5, 2451545.0,
                             2460409.25, 2488069.5, 2687000.5, 3000000.5};
    const uint32_t modes[] = {0, TAIYIN_POSITION_SPEED,
                             TAIYIN_POSITION_XYZ | TAIYIN_POSITION_TRUEPOS};
    bool first = true;
    for (size_t e = 0; e < sizeof(epochs) / sizeof(*epochs); ++e) {
        for (size_t t = 0; t < sizeof(targets) / sizeof(*targets); ++t) {
            for (size_t m = 0; m < sizeof(modes) / sizeof(*modes); ++m) {
                double out[6] = {};
                taiyin_ephemeris_diagnostic diag;
                taiyin_ephemeris_diagnostic_init(&diag);
                const taiyin_split_julian_date jd = split(epochs[e]);
                const taiyin_call_result code = taiyin_calc_position_tt(
                    ctx, targets[t], &jd, modes[m], out, &diag);
                if (!first) json << ',';
                first = false;
                json << "{\"target\":" << targets[t] << ",\"jd\":"
                     << epochs[e] << ",\"inputFlags\":" << modes[m] << ',';
                result(json, code);
                json << ",\"method\":" << diag.attempted_method_id
                     << ",\"values\":[";
                const size_t count = (modes[m] & TAIYIN_POSITION_SPEED) ? 6 : 3;
                for (size_t k = 0; k < count; ++k) {
                    if (k) json << ',';
                    number(json, out[k]);
                }
                json << "]}";
            }
        }
    }
    json << "],\"calendar\":[";
    taiyin_chinese_calendar_config config;
    taiyin_chinese_calendar_config_init_china_standard_astronomical(&config, 480);
    taiyin_chinese_calendar_context* calendar = 0;
    const taiyin_call_result create = taiyin_chinese_calendar_context_create(
        ctx, &config, &calendar);
    const int dates[][3] = {{2025, 1, 29}, {2033, 12, 22}, {2034, 1, 20}};
    for (size_t i = 0; i < 3; ++i) {
        taiyin_solar_date date;
        taiyin_solar_date_init(&date);
        date.year = dates[i][0]; date.month = dates[i][1]; date.day = dates[i][2];
        taiyin_lunar_date lunar;
        taiyin_lunar_date_init(&lunar);
        taiyin_call_result code = create;
        if (code >= 0) code = taiyin_chinese_calendar_from_solar(
            calendar, &date, &lunar, 0);
        if (i) json << ',';
        json << '{'; result(json, code);
        json << ",\"values\":[" << lunar.year << ',' << unsigned(lunar.month)
             << ',' << unsigned(lunar.day) << ',' << unsigned(lunar.is_leap)
             << ',' << unsigned(lunar.month_days) << "]}";
    }
    taiyin_chinese_solar_term_event term;
    taiyin_chinese_solar_term_event_init(&term);
    taiyin_call_result code = create;
    if (code >= 0) code = taiyin_chinese_calendar_get_specific_jie_qi_ut(
        calendar, 2026, 6, &term, 0);
    json << "],\"summerSolstice\":{"; result(json, code);
    json << ",\"day\":" << term.jd_ut.day_number << ",\"fraction\":";
    number(json, term.jd_ut.day_fraction); json << '}';
    taiyin_chinese_calendar_context_destroy(calendar);

    const taiyin_split_julian_date start = split(2460310.5);
    taiyin_solar_eclipse_result_tt eclipse;
    taiyin_solar_eclipse_result_tt_init(&eclipse);
    const auto begin = std::chrono::steady_clock::now();
    code = taiyin_search_next_solar_eclipse_tt(ctx, &start, 0,
        TAIYIN_ECLIPSE_OPTION_INCLUDE_CONTACTS, &eclipse, 0);
    const double elapsed = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - begin).count();
    json << ",\"solarEclipse\":{"; result(json, code);
    json << ",\"kind\":" << eclipse.kind << ",\"day\":"
         << eclipse.maximum_jd_tt.day_number << ",\"fraction\":";
    number(json, eclipse.maximum_jd_tt.day_fraction);
    json << ",\"values\":[";
    number(json, eclipse.maximum_latitude_deg); json << ',';
    number(json, eclipse.maximum_longitude_deg);
    for (size_t i = 0; i < TAIYIN_C_SOLAR_ECLIPSE_CONTACT_COUNT; ++i) {
        json << ',' << eclipse.contact_jd_tt[i].day_number << ',';
        number(json, eclipse.contact_jd_tt[i].day_fraction);
    }
    json << "],\"elapsedUs\":" << elapsed << "}}";
    return copy_string(json.str());
}

// Returns microseconds per body, including the existing C ABI, but excluding
// JS -> Wasm marshalling. Epochs advance to avoid benchmarking an identical call.
double probe_benchmark(const taiyin_context* ctx, int iterations,
                       int target_count, uint32_t flags) {
    if (iterations <= 0 || target_count < 1 || target_count > 10) return -1;
    double out[6] = {};
    for (int i = 0; i < 200; ++i) {
        const taiyin_call_result code = probe_position(
            ctx, targets[i % target_count], 2460409.0, (i % 64) / 64.0, flags, out);
        if (code < 0) return -1;
    }
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        for (int t = 0; t < target_count; ++t) {
            const taiyin_call_result code = probe_position(
                ctx, targets[t], 2460409.0, (i % 64) / 64.0, flags, out);
            if (code < 0) return -1;
        }
    }
    return std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - begin).count() / iterations / target_count;
}

double probe_eclipse_benchmark(const taiyin_context* ctx) {
    const taiyin_split_julian_date start = split(2460310.5);
    double samples[5];
    for (int i = -1; i < 5; ++i) {
        taiyin_solar_eclipse_result_tt out;
        taiyin_solar_eclipse_result_tt_init(&out);
        const auto begin = std::chrono::steady_clock::now();
        const taiyin_call_result code = taiyin_search_next_solar_eclipse_tt(
            ctx, &start, 0, TAIYIN_ECLIPSE_OPTION_INCLUDE_CONTACTS, &out, 0);
        if (code < 0) return -1;
        if (i >= 0) samples[i] = std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - begin).count();
    }
    std::sort(samples, samples + 5);
    return samples[2];
}
}

#ifdef TAIYIN_PROBE_NATIVE_MAIN
int main(int argc, char** argv) {
    const auto begin = std::chrono::steady_clock::now();
    if (probe_initialize() < 0) return 1;
    const bool precise = argc > 1 && std::strcmp(argv[1], "-") != 0;
    const char* files[] = {"sun", "moon", "emb", "mercury", "venus", "mars",
                           "jupiter", "saturn", "uranus", "neptune", "pluto"};
    if (precise) {
        for (size_t i = 0; i < 11; ++i) {
            const std::string path = std::string(argv[1]) + '/' + files[i] + ".opm2";
            const taiyin_call_result code = taiyin_runtime_add_source_path(path.c_str());
            if (code < 0) { std::cerr << path << ": " << taiyin_call_result_status(code); return 2; }
        }
    }
    taiyin_context* ctx = 0;
    if (probe_create_context(precise ? 1 : 3, &ctx) < 0) return 3;
    const double init_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
    char* suite = probe_suite(ctx);
    if (!suite) return 4;
    std::cout << std::setprecision(17) << "{\"opm2Count\":" << probe_opm2_count()
              << ",\"initializationMs\":" << init_ms << ",\"suite\":" << suite;
    std::free(suite);
    std::cout << ",\"seams\":[";
    if (precise) for (size_t i = 0; i < 11; ++i) {
        const std::string path = std::string(argv[1]) + '/' + files[i] + ".opm2";
        char* seams = probe_seams(ctx, path.c_str());
        if (!seams) return 5;
        if (i) std::cout << ',';
        std::cout << seams;
        std::free(seams);
    }
    std::cout << ']';
    std::cout << ",\"benchmarks\":[";
    const int counts[] = {1, 3, 5, 7, 9, 10};
    bool first = true;
    for (size_t i = 0; i < 6; ++i) for (int mode = 0; mode < 2; ++mode) {
        if (!first) std::cout << ',';
        first = false;
        const uint32_t flags = mode ? TAIYIN_POSITION_TRUEPOS : 0;
        double repeats[5];
        for (size_t repeat = 0; repeat < 5; ++repeat)
            repeats[repeat] = probe_benchmark(ctx, 2000, counts[i], flags);
        std::sort(repeats, repeats + 5);
        std::cout << "{\"bodies\":" << counts[i] << ",\"inputFlags\":" << flags
                  << ",\"usPerBody\":" << repeats[2] << '}';
    }
    std::cout << "],\"eclipseBenchmarkUs\":" << probe_eclipse_benchmark(ctx) << "}\n";
    taiyin_context_destroy(ctx);
}
#endif
