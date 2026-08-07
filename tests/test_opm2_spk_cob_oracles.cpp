#include "taiyin/body_id.h"
#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/opm2_catalog_discovery.h"
#include "taiyin/internal/spk.h"
#include "taiyin/state.h"
#include "taiyin/time.h"
#include "test_env.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

const char* kNasaBspRoot = taiyin_test::getenv_path("TAIYIN_NASA_BSP_ROOT");
const char* kJupiterSatellitesSpkPath = taiyin_test::getenv_path("TAIYIN_JUPITER_SATELLITES_SPK_PATH");
const char* kSaturnSatellitesSpkPath = taiyin_test::getenv_path("TAIYIN_SATURN_SATELLITES_SPK_PATH");
const char* kNeptuneSatellitesSpkPath = taiyin_test::getenv_path("TAIYIN_NEPTUNE_SATELLITES_SPK_PATH");
const char* kPlutoSatellitesSpkPath = taiyin_test::getenv_path("TAIYIN_PLUTO_SATELLITES_SPK_PATH");

bool file_exists(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::ifstream file(path.c_str(), std::ios::binary);
    return static_cast<bool>(file);
}

std::string join_path(const std::string& root, const std::string& relative) {
    if (root.empty()) {
        return std::string();
    }
    std::string path = root;
    if (!path.empty() && path[path.size() - 1] != '/') {
        path += "/";
    }
    path += relative;
    return path;
}

std::string repo_cob_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/cob/full";
    }
    return "../data/ephemerides/opm2/cob/full";
}

std::string spk_path(const char* explicit_path, const char* relative_path) {
    if (explicit_path && explicit_path[0] != '\0') {
        return std::string(explicit_path);
    }
    if (kNasaBspRoot && kNasaBspRoot[0] != '\0') {
        return join_path(kNasaBspRoot, relative_path);
    }
    return std::string();
}

double max_abs3(double x, double y, double z) {
    return std::fmax(std::fabs(x), std::fmax(std::fabs(y), std::fabs(z)));
}

double max_position_diff_au(const taiyin::CartesianState& lhs, const taiyin::CartesianState& rhs) {
    return max_abs3(
        lhs.position_au.x - rhs.position_au.x,
        lhs.position_au.y - rhs.position_au.y,
        lhs.position_au.z - rhs.position_au.z);
}

double max_velocity_diff_au_per_day(const taiyin::CartesianState& lhs, const taiyin::CartesianState& rhs) {
    return max_abs3(
        lhs.velocity_au_per_day.x - rhs.velocity_au_per_day.x,
        lhs.velocity_au_per_day.y - rhs.velocity_au_per_day.y,
        lhs.velocity_au_per_day.z - rhs.velocity_au_per_day.z);
}

double max_acceleration_diff_au_per_day2(const taiyin::CartesianState& lhs, const taiyin::CartesianState& rhs) {
    return max_abs3(
        lhs.acceleration_au_per_day2.x - rhs.acceleration_au_per_day2.x,
        lhs.acceleration_au_per_day2.y - rhs.acceleration_au_per_day2.y,
        lhs.acceleration_au_per_day2.z - rhs.acceleration_au_per_day2.z);
}

double position_norm_au(const taiyin::CartesianState& state) {
    const double x = state.position_au.x;
    const double y = state.position_au.y;
    const double z = state.position_au.z;
    return std::sqrt(x * x + y * y + z * z);
}

void expect_less(double actual, double limit, const std::string& label, int* failures) {
    if (!(actual < limit)) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " limit=" << limit << "\n";
        ++(*failures);
    }
}

const taiyin::internal::EphemerisBlockDescriptor* find_descriptor(
    const std::vector<taiyin::internal::EphemerisBlockDescriptor>& descriptors,
    int target_id,
    int center_id
) {
    for (size_t i = 0; i < descriptors.size(); ++i) {
        if (descriptors[i].target_id == target_id && descriptors[i].center_id == center_id) {
            return &descriptors[i];
        }
    }
    return 0;
}

bool load_opm2_cob_block(
    int target_id,
    int center_id,
    taiyin::internal::StorageEphemerisBlock* storage,
    taiyin::internal::CompiledEphemerisBlock* block,
    taiyin::internal::EphemerisBlockDescriptor* descriptor
) {
    std::vector<taiyin::internal::EphemerisBlockDescriptor> descriptors;
    if (!taiyin::internal::collect_opm2_descriptors_from_directory(repo_cob_root(), &descriptors)) {
        std::cerr << "FAIL: collect OPM2 COB descriptors\n";
        return false;
    }
    const taiyin::internal::EphemerisBlockDescriptor* found = find_descriptor(descriptors, target_id, center_id);
    if (!found) {
        std::cerr << "FAIL: missing OPM2 COB descriptor target=" << target_id << " center=" << center_id << "\n";
        return false;
    }
    if (!taiyin::internal::load_descriptor_ephemeris_block(*found, storage)) {
        std::cerr << "FAIL: load OPM2 COB descriptor target=" << target_id << "\n";
        return false;
    }
    if (!taiyin::internal::get_compiled_block_from_storage(storage, target_id, block)) {
        std::cerr << "FAIL: get compiled OPM2 COB block target=" << target_id << "\n";
        taiyin::internal::destroy_storage_ephemeris_block(storage);
        return false;
    }
    *descriptor = *found;
    return true;
}

struct CobOracleCase {
    const char* label;
    int target_id;
    int center_id;
    const char* explicit_spk_path;
    const char* relative_spk_path;
};

void check_cob_case(const CobOracleCase& test_case, int* failures, int* checked) {
    const std::string path = spk_path(
        test_case.explicit_spk_path,
        test_case.relative_spk_path);
    if (!file_exists(path)) {
        std::cout << "SKIP: missing " << test_case.label
                  << " SPK; set TAIYIN_NASA_BSP_ROOT or the body-specific SPK env var\n";
        return;
    }

    taiyin::internal::StorageEphemerisBlock opm2_storage;
    taiyin::internal::CompiledEphemerisBlock opm2_block;
    taiyin::internal::EphemerisBlockDescriptor opm2_descriptor;
    if (!load_opm2_cob_block(
            test_case.target_id,
            test_case.center_id,
            &opm2_storage,
            &opm2_block,
            &opm2_descriptor)) {
        ++(*failures);
        return;
    }

    taiyin::internal::SpkEphemerisData* spk_data = 0;
    if (!taiyin::internal::compile_spk_ephemeris_data_from_file(
            path,
            test_case.target_id,
            test_case.center_id,
            opm2_descriptor.jd_tdb_start,
            opm2_descriptor.jd_tdb_end,
            &spk_data)) {
        std::cerr << "FAIL: compile SPK COB data for " << test_case.label << "\n";
        taiyin::internal::destroy_storage_ephemeris_block(&opm2_storage);
        ++(*failures);
        return;
    }

    const double jds[] = { 2460408.5, 2461000.5, 2462000.5 };
    for (size_t i = 0; i < sizeof(jds) / sizeof(jds[0]); ++i) {
        const double jd_value = jds[i];
        if (jd_value < opm2_descriptor.jd_tdb_start || jd_value >= opm2_descriptor.jd_tdb_end) {
            continue;
        }
        taiyin::SplitJulianDate jd;
        if (!taiyin::split_julian_date_from_double(jd_value, &jd)) {
            std::cerr << "FAIL: split COB oracle epoch for " << test_case.label << "\n";
            ++(*failures);
            continue;
        }
        taiyin::CartesianState opm2_state;
        taiyin::CartesianState spk_state;
        if (!taiyin::internal::eval_compiled_ephemeris_block(jd, &opm2_block, &opm2_state)) {
            std::cerr << "FAIL: eval OPM2 COB for " << test_case.label << " jd=" << jd_value << "\n";
            ++(*failures);
            continue;
        }
        if (!taiyin::internal::calc_spk_state(jd, spk_data, &spk_state)) {
            std::cerr << "FAIL: eval SPK COB for " << test_case.label << " jd=" << jd_value << "\n";
            ++(*failures);
            continue;
        }

        const std::string label = std::string(test_case.label) + " jd=" + std::to_string(jd_value);
        expect_less(position_norm_au(opm2_state), 1.0e-3, label + " OPM2 COB magnitude", failures);
        expect_less(max_position_diff_au(opm2_state, spk_state), 2.0e-9, label + " position diff au", failures);
        expect_less(max_velocity_diff_au_per_day(opm2_state, spk_state), 5.0e-8, label + " velocity diff au/day", failures);
        expect_less(max_acceleration_diff_au_per_day2(opm2_state, spk_state), 2.0e-6, label + " acceleration diff au/day^2", failures);
        ++(*checked);
    }

    taiyin::internal::spk_ephemeris_data_destroy(spk_data);
    taiyin::internal::destroy_storage_ephemeris_block(&opm2_storage);
}

}  // namespace

int main() {
    const CobOracleCase cases[] = {
        {
            "Jupiter COB",
            taiyin::TAIYIN_BODY_JUPITER,
            taiyin::TAIYIN_BODY_JUPITER_BARYCENTER,
            kJupiterSatellitesSpkPath,
            "satellites/jup365.bsp"
        },
        {
            "Saturn COB",
            taiyin::TAIYIN_BODY_SATURN,
            taiyin::TAIYIN_BODY_SATURN_BARYCENTER,
            kSaturnSatellitesSpkPath,
            "satellites/sat441.bsp"
        },
        {
            "Neptune COB",
            taiyin::TAIYIN_BODY_NEPTUNE,
            taiyin::TAIYIN_BODY_NEPTUNE_BARYCENTER,
            kNeptuneSatellitesSpkPath,
            "satellites/nep097.bsp"
        },
        {
            "Pluto COB",
            taiyin::TAIYIN_BODY_PLUTO,
            taiyin::TAIYIN_BODY_PLUTO_BARYCENTER,
            kPlutoSatellitesSpkPath,
            "satellites/plu060.bsp"
        },
    };

    int failures = 0;
    int checked = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        check_cob_case(cases[i], &failures, &checked);
    }

    if (failures == 0) {
        if (checked == 0) {
            std::cout << "test_opm2_spk_cob_oracles: SKIPPED external SPK data absent\n";
        } else {
            std::cout << "test_opm2_spk_cob_oracles: ALL TESTS PASSED (" << checked << " oracle points)\n";
        }
        return 0;
    }
    std::cerr << failures << " test_opm2_spk_cob_oracles failure(s)\n";
    return 1;
}
