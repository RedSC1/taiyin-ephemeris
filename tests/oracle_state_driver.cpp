#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/runtime/ephemeris_engine.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> split_source_paths(const std::string& input) {
    std::vector<std::string> out;
    std::string current;
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == ':') {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(input[i]);
        }
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

bool parse_int(const char* text, int* out) {
    if (!text || !out) {
        return false;
    }
    char* end = 0;
    const long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

bool parse_double(const char* text, double* out) {
    if (!text || !out) {
        return false;
    }
    char* end = 0;
    const double value = std::strtod(text, &end);
    if (!end || *end != '\0') {
        return false;
    }
    *out = value;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "usage: oracle_state_driver <source_path[:source_path...]> <target_id> <center_id> <jd_tdb>\n";
        return 2;
    }

    const std::vector<std::string> sources = split_source_paths(argv[1]);
    if (sources.empty()) {
        std::cerr << "empty source path list\n";
        return 2;
    }

    int target_id = 0;
    int center_id = 0;
    double jd_tdb = 0.0;
    if (!parse_int(argv[2], &target_id)
        || !parse_int(argv[3], &center_id)
        || !parse_double(argv[4], &jd_tdb)) {
        std::cerr << "invalid target, center, or jd\n";
        return 2;
    }

    std::vector<const char*> source_paths;
    source_paths.reserve(sources.size());
    for (size_t i = 0; i < sources.size(); ++i) {
        source_paths.push_back(sources[i].c_str());
    }

    taiyin::runtime::EphemerisRuntimeConfig config;
    config.source_paths = source_paths.empty() ? 0 : &source_paths[0];
    config.source_path_count = source_paths.size();
    config.load_packaged_data = false;
    config.strict_discovery = true;
    config.segment_cache_max_entries = 64;
    if (!taiyin::runtime::initialize_global_ephemeris_runtime(config)) {
        std::cerr << "failed to initialize runtime from source paths\n";
        return 3;
    }

    taiyin::runtime::EphemerisRequest request;
    request.target_id = target_id;
    request.center_id = center_id;
    request.frame = taiyin::internal::IcrfJ2000Equatorial;
    if (!taiyin::split_julian_date_from_double(jd_tdb, &request.jd_tdb)) {
        std::fprintf(stderr, "invalid split JD\n");
        return 1;
    }
    request.components = taiyin::internal::EPHEMERIS_BLOCK_COMPONENT_STATE;

    taiyin::runtime::EphemerisResult result;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    const taiyin::Status status = taiyin::runtime::eval_global_ephemeris_state(
        request,
        &result,
        &diagnostic);
    if (status != taiyin::TAIYIN_STATUS_OK) {
        char buffer[512];
        taiyin::runtime::format_ephemeris_eval_diagnostic(diagnostic, buffer, sizeof(buffer));
        std::cerr << "eval failed: " << status << " " << buffer << "\n";
        return 4;
    }

    std::cout << std::setprecision(17);
    std::cout << "position "
              << result.state.position_au.x << " "
              << result.state.position_au.y << " "
              << result.state.position_au.z << "\n";
    std::cout << "velocity "
              << result.state.velocity_au_per_day.x << " "
              << result.state.velocity_au_per_day.y << " "
              << result.state.velocity_au_per_day.z << "\n";
    std::cout << "acceleration "
              << result.state.acceleration_au_per_day2.x << " "
              << result.state.acceleration_au_per_day2.y << " "
              << result.state.acceleration_au_per_day2.z << "\n";
    std::cout << "descriptor "
              << result.descriptor.target_id << " "
              << result.descriptor.center_id << " "
              << result.descriptor.method_id << " "
              << result.descriptor.source_key.source_id << "\n";
    return 0;
}
