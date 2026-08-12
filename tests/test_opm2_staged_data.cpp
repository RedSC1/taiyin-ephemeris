#include "taiyin/body_id.h"
#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/ephemeris_catalog.h"
#include "taiyin/internal/ephemeris_source_identity.h"
#include "taiyin/internal/opm2.h"
#include "taiyin/internal/opm2_catalog_discovery.h"
#include "taiyin/runtime/ephemeris_engine.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/state.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

taiyin::SplitJulianDate split_jd(double jd) {
    taiyin::SplitJulianDate result;
    if (!taiyin::split_julian_date_from_double(jd, &result)) {
        result.day_fraction = NAN;
    }
    return result;
}

taiyin::SplitJulianDate offset_jd(const taiyin::SplitJulianDate& jd, double days) {
    taiyin::SplitJulianDate result;
    if (!taiyin::add_days_to_split_jd(jd, days, &result)) {
        result.day_fraction = NAN;
    }
    return result;
}

const double kMajorBodyProductStartJd = 2378496.5;
const double kMajorBodyProductEndJd = 2597641.5;
const double kAsteroidProductStartJd = 2386295.0;
const double kAsteroidProductEndJd = 2605445.0;

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_equal_int(int actual, int expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_equal_size(size_t actual, size_t expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    const double diff = std::fabs(actual - expected);
    if (!(diff <= tolerance)) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected
                  << " diff=" << diff
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

void expect_vector_near(
    const taiyin::Vector3& actual,
    const taiyin::Vector3& expected,
    double tolerance,
    const char* label,
    int* failures
) {
    expect_near(actual.x, expected.x, tolerance, label, failures);
    expect_near(actual.y, expected.y, tolerance, label, failures);
    expect_near(actual.z, expected.z, tolerance, label, failures);
}

void expect_state_near(
    const taiyin::CartesianState& actual,
    const taiyin::CartesianState& expected,
    double tolerance,
    const char* label,
    int* failures
) {
    expect_vector_near(actual.position_au, expected.position_au, tolerance, label, failures);
    expect_vector_near(actual.velocity_au_per_day, expected.velocity_au_per_day, tolerance, label, failures);
    expect_vector_near(actual.acceleration_au_per_day2, expected.acceleration_au_per_day2, tolerance, label, failures);
}

double vector_norm(const taiyin::Vector3& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

std::string repo_data_path(const std::string& suffix) {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/" + suffix;
    }
    return std::string("../data/ephemerides/opm2/") + suffix;
}

bool read_file_bytes(const std::string& path, std::vector<unsigned char>* out) {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) {
        return false;
    }
    out->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

const taiyin::internal::EphemerisBlockDescriptor* find_descriptor(
    const std::vector<taiyin::internal::EphemerisBlockDescriptor>& descriptors,
    int target_id
) {
    for (size_t i = 0; i < descriptors.size(); ++i) {
        if (descriptors[i].target_id == target_id) {
            return &descriptors[i];
        }
    }
    return 0;
}

void expect_descriptor_loads(
    const taiyin::internal::EphemerisBlockDescriptor& descriptor,
    const char* label,
    int* failures
) {
    taiyin::internal::StorageEphemerisBlock storage;
    if (!taiyin::internal::load_descriptor_ephemeris_block(descriptor, &storage)) {
        std::cerr << "FAIL: load descriptor: " << label << "\n";
        ++(*failures);
        return;
    }

    taiyin::internal::CompiledEphemerisBlock block;
    if (!taiyin::internal::get_compiled_block_from_storage(&storage, descriptor.target_id, &block)) {
        std::cerr << "FAIL: get compiled descriptor block: " << label << "\n";
        ++(*failures);
        taiyin::internal::destroy_storage_ephemeris_block(&storage);
        return;
    }

    const double jd_value = descriptor.jd_tdb_start
        + 0.373 * (descriptor.jd_tdb_end - descriptor.jd_tdb_start);
    const taiyin::SplitJulianDate jd = split_jd(jd_value);
    taiyin::CartesianState state;
    if (!taiyin::internal::eval_compiled_ephemeris_block(jd, &block, &state)
        || !std::isfinite(state.position_au.x)
        || !std::isfinite(state.position_au.y)
        || !std::isfinite(state.position_au.z)) {
        std::cerr << "FAIL: eval loaded descriptor: " << label << "\n";
        ++(*failures);
    }

    const taiyin::SplitJulianDate equivalent_jd{
        jd.day_number - 1, jd.day_fraction + 1.0};
    taiyin::CartesianState equivalent_state;
    if (!taiyin::internal::eval_compiled_ephemeris_block(
            equivalent_jd, &block, &equivalent_state)) {
        std::cerr << "FAIL: eval loaded descriptor equivalent split JD: " << label << "\n";
        ++(*failures);
    } else {
        expect_state_near(
            equivalent_state, state, 0.0,
            "OPM2 canonicalizes equivalent split JD", failures);
    }

    taiyin::Vector3 direct_position;
    if (!taiyin::internal::eval_compiled_ephemeris_block_position(jd, &block, &direct_position)) {
        std::cerr << "FAIL: eval loaded descriptor direct position: " << label << "\n";
        ++(*failures);
    } else {
        expect_vector_near(direct_position, state.position_au, 0.0, "OPM2 direct position matches state", failures);
    }

    taiyin::Vector3 direct_velocity;
    if (!taiyin::internal::eval_compiled_ephemeris_block_velocity(jd, &block, &direct_velocity)) {
        std::cerr << "FAIL: eval loaded descriptor direct velocity: " << label << "\n";
        ++(*failures);
    } else {
        expect_vector_near(direct_velocity, state.velocity_au_per_day, 0.0, "OPM2 direct velocity matches state", failures);
    }

    const double h = 1.0e-3;
    if (jd_value - h > descriptor.jd_tdb_start && jd_value + h < descriptor.jd_tdb_end) {
        taiyin::Vector3 prev_position;
        taiyin::Vector3 next_position;
        taiyin::Vector3 prev_velocity;
        taiyin::Vector3 next_velocity;
        if (!taiyin::internal::eval_compiled_ephemeris_block_position(offset_jd(jd, -h), &block, &prev_position)
            || !taiyin::internal::eval_compiled_ephemeris_block_position(offset_jd(jd, h), &block, &next_position)
            || !taiyin::internal::eval_compiled_ephemeris_block_velocity(jd, &block, &direct_velocity)) {
            std::cerr << "FAIL: eval loaded descriptor velocity sanity inputs: " << label << "\n";
            ++(*failures);
        } else {
            const taiyin::Vector3 finite_difference_velocity{
                (next_position.x - prev_position.x) / (2.0 * h),
                (next_position.y - prev_position.y) / (2.0 * h),
                (next_position.z - prev_position.z) / (2.0 * h)};
            expect_vector_near(
                direct_velocity,
                finite_difference_velocity,
                1.0e-7,
                "OPM2 analytic velocity finite-difference sanity",
                failures);
        }
        taiyin::Vector3 direct_acceleration;
        if (!taiyin::internal::eval_compiled_ephemeris_block_velocity(offset_jd(jd, -h), &block, &prev_velocity)
            || !taiyin::internal::eval_compiled_ephemeris_block_velocity(offset_jd(jd, h), &block, &next_velocity)
            || !taiyin::internal::eval_compiled_ephemeris_block_acceleration(jd, &block, &direct_acceleration)) {
            std::cerr << "FAIL: eval loaded descriptor acceleration sanity inputs: " << label << "\n";
            ++(*failures);
        } else {
            const taiyin::Vector3 finite_difference_acceleration{
                (next_velocity.x - prev_velocity.x) / (2.0 * h),
                (next_velocity.y - prev_velocity.y) / (2.0 * h),
                (next_velocity.z - prev_velocity.z) / (2.0 * h)};
            expect_vector_near(
                direct_acceleration,
                finite_difference_acceleration,
                1.0e-7,
                "OPM2 analytic acceleration finite-difference sanity",
                failures);
        }
    }
    taiyin::internal::destroy_storage_ephemeris_block(&storage);
}

void check_common_descriptor_fields(
    const taiyin::internal::EphemerisBlockDescriptor& descriptor,
    double expected_start_jd,
    double expected_end_jd,
    int* failures
) {
    expect_equal_int(descriptor.format, taiyin::internal::EphemerisBlockFormat::Opm2, "OPM2 descriptor format", failures);
    expect_equal_int(descriptor.method_id, static_cast<int>(taiyin::internal::OPM2_METHOD_ID), "OPM2 method id", failures);
    expect_equal_int(descriptor.frame, taiyin::internal::EphemerisFrame::IcrfJ2000Equatorial, "OPM2 frame", failures);
    expect_true(descriptor.jd_tdb_start <= expected_start_jd, "OPM2 coverage includes product start", failures);
    expect_true(descriptor.jd_tdb_end >= expected_end_jd, "OPM2 coverage includes product end", failures);
    expect_true(!descriptor.path.empty(), "OPM2 descriptor path", failures);
}

void test_major_body_staged_opm2(int* failures) {
    using namespace taiyin::internal;

    const std::string root = repo_data_path("major-bodies/600y");
    std::vector<EphemerisBlockDescriptor> descriptors;
    expect_true(collect_opm2_descriptors_from_directory(root, &descriptors), "collect staged major-body OPM2 descriptors", failures);
    expect_equal_size(descriptors.size(), 11u, "staged major-body OPM2 descriptor count", failures);

    const int expected_targets[] = {
        taiyin::TAIYIN_BODY_SUN,
        taiyin::TAIYIN_BODY_MERCURY_BARYCENTER,
        taiyin::TAIYIN_BODY_VENUS_BARYCENTER,
        taiyin::TAIYIN_BODY_EMB,
        taiyin::TAIYIN_BODY_MOON,
        taiyin::TAIYIN_BODY_MARS_BARYCENTER,
        taiyin::TAIYIN_BODY_JUPITER_BARYCENTER,
        taiyin::TAIYIN_BODY_SATURN_BARYCENTER,
        taiyin::TAIYIN_BODY_URANUS_BARYCENTER,
        taiyin::TAIYIN_BODY_NEPTUNE_BARYCENTER,
        taiyin::TAIYIN_BODY_PLUTO_BARYCENTER,
    };

    for (size_t i = 0; i < sizeof(expected_targets) / sizeof(expected_targets[0]); ++i) {
        const EphemerisBlockDescriptor* descriptor = find_descriptor(descriptors, expected_targets[i]);
        expect_true(descriptor != 0, "staged major-body OPM2 target discovered", failures);
        if (descriptor) {
            check_common_descriptor_fields(
                *descriptor,
                kMajorBodyProductStartJd,
                kMajorBodyProductEndJd,
                failures);
        }
    }

    const EphemerisBlockDescriptor* sun = find_descriptor(descriptors, taiyin::TAIYIN_BODY_SUN);
    const EphemerisBlockDescriptor* mercury = find_descriptor(descriptors, taiyin::TAIYIN_BODY_MERCURY_BARYCENTER);
    const EphemerisBlockDescriptor* mars = find_descriptor(descriptors, taiyin::TAIYIN_BODY_MARS_BARYCENTER);
    const EphemerisBlockDescriptor* jupiter = find_descriptor(descriptors, taiyin::TAIYIN_BODY_JUPITER_BARYCENTER);
    const EphemerisBlockDescriptor* moon = find_descriptor(descriptors, taiyin::TAIYIN_BODY_MOON);
    if (sun) {
        expect_equal_int(sun->center_id, taiyin::TAIYIN_BODY_SSB, "Sun OPM2 center", failures);
        expect_descriptor_loads(*sun, "sun", failures);
    }
    if (mercury) {
        expect_equal_int(mercury->center_id, taiyin::TAIYIN_BODY_SUN, "Mercury barycenter OPM2 center", failures);
        expect_descriptor_loads(*mercury, "mercury barycenter", failures);
    }
    if (mars) {
        expect_equal_int(mars->center_id, taiyin::TAIYIN_BODY_SSB, "Mars barycenter OPM2 center", failures);
        expect_descriptor_loads(*mars, "mars barycenter", failures);
    }
    if (jupiter) {
        expect_equal_int(jupiter->center_id, taiyin::TAIYIN_BODY_SSB, "Jupiter barycenter OPM2 center", failures);
        expect_descriptor_loads(*jupiter, "jupiter barycenter", failures);
    }
    if (moon) {
        expect_equal_int(moon->center_id, taiyin::TAIYIN_BODY_EARTH, "Moon OPM2 center", failures);
        expect_descriptor_loads(*moon, "moon", failures);
    }
}

void test_asteroid_staged_opm2(int* failures) {
    using namespace taiyin::internal;

    const std::string root = repo_data_path("asteroids/600y");
    std::vector<EphemerisBlockDescriptor> descriptors;
    expect_true(collect_opm2_descriptors_from_directory(root, &descriptors), "collect staged asteroid OPM2 descriptors", failures);
    expect_equal_size(descriptors.size(), 9u, "staged asteroid OPM2 descriptor count", failures);

    const int expected_targets[] = {
        taiyin::TAIYIN_BODY_CERES,
        taiyin::TAIYIN_BODY_PALLAS,
        taiyin::TAIYIN_BODY_JUNO,
        taiyin::TAIYIN_BODY_VESTA,
        taiyin::TAIYIN_BODY_EROS,
        taiyin::TAIYIN_BODY_CHIRON,
        taiyin::TAIYIN_BODY_PHOLUS,
        taiyin::TAIYIN_BODY_NESSUS,
        taiyin::TAIYIN_BODY_LILITH,
    };

    for (size_t i = 0; i < sizeof(expected_targets) / sizeof(expected_targets[0]); ++i) {
        const EphemerisBlockDescriptor* descriptor = find_descriptor(descriptors, expected_targets[i]);
        expect_true(descriptor != 0, "staged asteroid OPM2 target discovered", failures);
        if (descriptor) {
            check_common_descriptor_fields(
                *descriptor,
                kAsteroidProductStartJd,
                kAsteroidProductEndJd,
                failures);
            expect_equal_int(descriptor->center_id, taiyin::TAIYIN_BODY_SUN, "asteroid OPM2 center", failures);
        }
    }

    const EphemerisBlockDescriptor* ceres = find_descriptor(descriptors, taiyin::TAIYIN_BODY_CERES);
    const EphemerisBlockDescriptor* chiron = find_descriptor(descriptors, taiyin::TAIYIN_BODY_CHIRON);
    if (ceres) {
        expect_descriptor_loads(*ceres, "ceres", failures);
    }
    if (chiron) {
        expect_descriptor_loads(*chiron, "chiron", failures);
    }
}

void test_cob_staged_opm2(int* failures) {
    using namespace taiyin::internal;

    const std::string root = repo_data_path("cob/slices/uranus_1600_2200");
    std::vector<EphemerisBlockDescriptor> descriptors;
    expect_true(collect_opm2_descriptors_from_directory(root, &descriptors), "collect staged COB OPM2 descriptors", failures);
    expect_equal_size(descriptors.size(), 1u, "staged COB OPM2 descriptor count", failures);

    const EphemerisBlockDescriptor* uranus = find_descriptor(descriptors, taiyin::TAIYIN_BODY_URANUS);
    expect_true(uranus != 0, "staged Uranus COB OPM2 target discovered", failures);
    if (!uranus) {
        return;
    }

    expect_equal_int(uranus->format, EphemerisBlockFormat::Opm2, "Uranus COB OPM2 descriptor format", failures);
    expect_equal_int(uranus->method_id, static_cast<int>(OPM2_METHOD_ID), "Uranus COB OPM2 method id", failures);
    expect_equal_int(uranus->frame, EphemerisFrame::IcrfJ2000Equatorial, "Uranus COB OPM2 frame", failures);
    expect_equal_int(uranus->target_id, taiyin::TAIYIN_BODY_URANUS, "Uranus COB OPM2 target", failures);
    expect_equal_int(uranus->center_id, taiyin::TAIYIN_BODY_URANUS_BARYCENTER, "Uranus COB OPM2 center", failures);
    expect_near(uranus->jd_tdb_start, 2305447.5, 0.0, "Uranus COB OPM2 coverage start", failures);
    expect_near(uranus->jd_tdb_end, 2524582.5, 0.0, "Uranus COB OPM2 coverage end", failures);
    expect_true(!uranus->path.empty(), "Uranus COB OPM2 descriptor path", failures);
    expect_descriptor_loads(*uranus, "uranus COB", failures);
}

void test_cob_runtime_body_composite(int* failures) {
    using namespace taiyin;
    using namespace taiyin::internal;
    using namespace taiyin::runtime;

    const std::string major_root = repo_data_path("major-bodies/600y");
    const std::string cob_root = repo_data_path("cob/slices/uranus_1600_2200");
    const char* source_paths[] = { major_root.c_str(), cob_root.c_str() };

    EphemerisRuntimeConfig config;
    config.source_paths = source_paths;
    config.source_path_count = 2;
    config.load_packaged_data = false;
    config.segment_cache_max_entries = 32;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize staged major-body + COB runtime", failures);

    const double jd_value = 2460310.5;
    const SplitJulianDate jd = split_jd(jd_value);
    EphemerisRequest barycenter_request;
    barycenter_request.target_id = TAIYIN_BODY_URANUS_BARYCENTER;
    barycenter_request.center_id = TAIYIN_BODY_SSB;
    barycenter_request.frame = IcrfJ2000Equatorial;
    barycenter_request.jd_tdb = jd;
    barycenter_request.components = EPHEMERIS_BLOCK_COMPONENT_STATE;

    EphemerisResult barycenter_result;
    EphemerisEvalDiagnostic diagnostic;
    expect_equal_int(
        eval_global_ephemeris_state(barycenter_request, &barycenter_result, &diagnostic),
        TAIYIN_STATUS_OK,
        "eval Uranus barycenter wrt SSB",
        failures);

    EphemerisRequest offset_request;
    offset_request.target_id = TAIYIN_BODY_URANUS;
    offset_request.center_id = TAIYIN_BODY_URANUS_BARYCENTER;
    offset_request.frame = IcrfJ2000Equatorial;
    offset_request.jd_tdb = jd;
    offset_request.components = EPHEMERIS_BLOCK_COMPONENT_STATE;

    EphemerisResult offset_result;
    expect_equal_int(
        eval_global_ephemeris_state(offset_request, &offset_result, &diagnostic),
        TAIYIN_STATUS_OK,
        "eval Uranus COB offset",
        failures);
    expect_true(
        vector_norm(offset_result.state.position_au) < 1.0e-3,
        "Uranus COB offset stays sub-planet-system scale",
        failures);

    EphemerisRequest body_request;
    body_request.target_id = TAIYIN_BODY_URANUS;
    body_request.center_id = TAIYIN_BODY_SSB;
    body_request.frame = IcrfJ2000Equatorial;
    body_request.jd_tdb = jd;
    body_request.components = EPHEMERIS_BLOCK_COMPONENT_STATE;

    EphemerisResult body_result;
    expect_equal_int(
        eval_global_ephemeris_state(body_request, &body_result, &diagnostic),
        TAIYIN_STATUS_OK,
        "eval Uranus body wrt SSB through barycenter + COB",
        failures);

    const CartesianState expected_body = cartesian_state_add(barycenter_result.state, offset_result.state);
    expect_state_near(
        body_result.state,
        expected_body,
        1.0e-14,
        "Uranus body composite equals barycenter plus COB offset",
        failures);
    expect_equal_int(body_result.descriptor.target_id, TAIYIN_BODY_URANUS, "Uranus composite target", failures);
    expect_equal_int(body_result.descriptor.center_id, TAIYIN_BODY_SSB, "Uranus composite center", failures);
    expect_true(
        body_result.descriptor.jd_tdb_start <= jd_value
            && body_result.descriptor.jd_tdb_end > jd_value,
        "Uranus composite descriptor covers jd",
        failures);

    EphemerisRequest sun_center_request;
    sun_center_request.target_id = TAIYIN_BODY_URANUS;
    sun_center_request.center_id = TAIYIN_BODY_SUN;
    sun_center_request.frame = IcrfJ2000Equatorial;
    sun_center_request.jd_tdb = jd;
    sun_center_request.components = EPHEMERIS_BLOCK_COMPONENT_STATE;

    EphemerisResult sun_center_result;
    expect_equal_int(
        eval_global_ephemeris_state(sun_center_request, &sun_center_result, &diagnostic),
        TAIYIN_STATUS_OK,
        "eval Uranus body wrt Sun through SSB-centered barycenter + COB",
        failures);

    EphemerisRequest barycenter_sun_request;
    barycenter_sun_request.target_id = TAIYIN_BODY_URANUS_BARYCENTER;
    barycenter_sun_request.center_id = TAIYIN_BODY_SUN;
    barycenter_sun_request.frame = IcrfJ2000Equatorial;
    barycenter_sun_request.jd_tdb = jd;
    barycenter_sun_request.components = EPHEMERIS_BLOCK_COMPONENT_STATE;

    EphemerisResult barycenter_sun_result;
    expect_equal_int(
        eval_global_ephemeris_state(barycenter_sun_request, &barycenter_sun_result, &diagnostic),
        TAIYIN_STATUS_OK,
        "eval Uranus barycenter wrt Sun from SSB-centered data",
        failures);

    expect_state_near(
        sun_center_result.state,
        cartesian_state_add(barycenter_sun_result.state, offset_result.state),
        1.0e-14,
        "Uranus wrt Sun composite distinguishes SSB-centered barycenter data",
        failures);
}

void test_inner_planet_body_aliases(int* failures) {
    using namespace taiyin;
    using namespace taiyin::internal;
    using namespace taiyin::runtime;

    const std::string major_root = repo_data_path("major-bodies/600y");
    const char* source_paths[] = { major_root.c_str() };

    EphemerisRuntimeConfig config;
    config.source_paths = source_paths;
    config.source_path_count = 1;
    config.load_packaged_data = false;
    config.segment_cache_max_entries = 32;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize staged major-body runtime", failures);

    struct AliasCase {
        int body_id;
        int barycenter_id;
        const char* label;
    };

    const AliasCase cases[] = {
        { TAIYIN_BODY_MERCURY, TAIYIN_BODY_MERCURY_BARYCENTER, "Mercury" },
        { TAIYIN_BODY_VENUS, TAIYIN_BODY_VENUS_BARYCENTER, "Venus" },
    };

    const SplitJulianDate jd = split_jd(2460310.5);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        EphemerisRequest barycenter_request;
        barycenter_request.target_id = cases[i].barycenter_id;
        barycenter_request.center_id = TAIYIN_BODY_SUN;
        barycenter_request.frame = IcrfJ2000Equatorial;
        barycenter_request.jd_tdb = jd;
        barycenter_request.components = EPHEMERIS_BLOCK_COMPONENT_STATE;

        EphemerisResult barycenter_result;
        EphemerisEvalDiagnostic diagnostic;
        expect_equal_int(
            eval_global_ephemeris_state(barycenter_request, &barycenter_result, &diagnostic),
            TAIYIN_STATUS_OK,
            "eval inner planet barycenter",
            failures);

        EphemerisRequest body_request;
        body_request.target_id = cases[i].body_id;
        body_request.center_id = TAIYIN_BODY_SUN;
        body_request.frame = IcrfJ2000Equatorial;
        body_request.jd_tdb = jd;
        body_request.components = EPHEMERIS_BLOCK_COMPONENT_STATE;

        EphemerisResult body_result;
        expect_equal_int(
            eval_global_ephemeris_state(body_request, &body_result, &diagnostic),
            TAIYIN_STATUS_OK,
            "eval inner planet body alias",
            failures);

        expect_state_near(
            body_result.state,
            barycenter_result.state,
            0.0,
            cases[i].label,
            failures);
        expect_equal_int(body_result.descriptor.target_id, cases[i].body_id, "inner planet alias target", failures);
        expect_equal_int(body_result.descriptor.center_id, TAIYIN_BODY_SUN, "inner planet alias center", failures);

        EphemerisRequest ssb_body_request;
        ssb_body_request.target_id = cases[i].body_id;
        ssb_body_request.center_id = TAIYIN_BODY_SSB;
        ssb_body_request.frame = IcrfJ2000Equatorial;
        ssb_body_request.jd_tdb = jd;
        ssb_body_request.components = EPHEMERIS_BLOCK_COMPONENT_STATE;

        EphemerisResult ssb_body_result;
        expect_equal_int(
            eval_global_ephemeris_state(ssb_body_request, &ssb_body_result, &diagnostic),
            TAIYIN_STATUS_OK,
            "eval inner planet body alias wrt SSB from Sun-centered data",
            failures);

        EphemerisRequest ssb_barycenter_request;
        ssb_barycenter_request.target_id = cases[i].barycenter_id;
        ssb_barycenter_request.center_id = TAIYIN_BODY_SSB;
        ssb_barycenter_request.frame = IcrfJ2000Equatorial;
        ssb_barycenter_request.jd_tdb = jd;
        ssb_barycenter_request.components = EPHEMERIS_BLOCK_COMPONENT_STATE;

        EphemerisResult ssb_barycenter_result;
        expect_equal_int(
            eval_global_ephemeris_state(ssb_barycenter_request, &ssb_barycenter_result, &diagnostic),
            TAIYIN_STATUS_OK,
            "eval inner planet barycenter wrt SSB from Sun-centered data",
            failures);

        expect_state_near(
            ssb_body_result.state,
            ssb_barycenter_result.state,
            0.0,
            "inner planet alias wrt SSB distinguishes Sun-centered data",
            failures);
        expect_equal_int(ssb_body_result.descriptor.target_id, cases[i].body_id, "inner planet SSB alias target", failures);
        expect_equal_int(ssb_body_result.descriptor.center_id, TAIYIN_BODY_SSB, "inner planet SSB alias center", failures);
    }
}

void test_opm2_range_slice_matches_full_frame(int* failures) {
    const struct Case {
        const char* path;
        double jd_tdb;
    } cases[] = {
        {"major-bodies/600y/venus.opm2", 2460310.5},
        {"major-bodies/600y/emb.opm2", 2460310.5},
        {"asteroids/600y/ceres.opm2", 2460310.5},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        std::vector<unsigned char> bytes;
        const std::string path = repo_data_path(cases[i].path);
        if (!read_file_bytes(path, &bytes)) {
            std::cerr << "FAIL: read OPM2 slice regression fixture: " << path << "\n";
            ++(*failures);
            continue;
        }

        taiyin::internal::Opm2EphemerisData* full = 0;
        taiyin::internal::Opm2EphemerisData* sliced = 0;
        if (!taiyin::internal::compile_opm2_ephemeris_data(bytes.data(), bytes.size(), &full)) {
            std::cerr << "FAIL: compile full OPM2 slice regression fixture: " << path << "\n";
            ++(*failures);
            continue;
        }
        if (!taiyin::internal::compile_opm2_ephemeris_data_for_range(
                bytes.data(),
                bytes.size(),
                cases[i].jd_tdb,
                cases[i].jd_tdb + 1.0e-6,
                &sliced)) {
            std::cerr << "FAIL: compile sliced OPM2 slice regression fixture: " << path << "\n";
            ++(*failures);
            taiyin::internal::opm2_ephemeris_data_destroy(full);
            continue;
        }

        taiyin::Vector3 full_position;
        taiyin::Vector3 sliced_position;
        expect_true(
            taiyin::internal::calc_opm2_position(split_jd(cases[i].jd_tdb), full, &full_position),
            "eval full OPM2 slice regression fixture",
            failures);
        expect_true(
            taiyin::internal::calc_opm2_position(split_jd(cases[i].jd_tdb), sliced, &sliced_position),
            "eval sliced OPM2 slice regression fixture",
            failures);
        expect_vector_near(
            sliced_position,
            full_position,
            1.0e-15,
            "OPM2 range slice matches full frame",
            failures);

        taiyin::internal::opm2_ephemeris_data_destroy(full);
        taiyin::internal::opm2_ephemeris_data_destroy(sliced);
    }
}

void test_opm2_header_source_identity(int* failures) {
    const std::string path = repo_data_path("major-bodies/600y/emb.opm2");
    std::vector<unsigned char> bytes;
    if (!read_file_bytes(path, &bytes)) {
        std::cerr << "FAIL: read OPM2 source-id fixture: " << path << "\n";
        ++(*failures);
        return;
    }

    taiyin::internal::Opm2EpheSection ephe;
    taiyin::internal::Opm2GridSection grid;
    uint32_t source_id = taiyin::internal::OPM2_SOURCE_UNDEFINED;
    expect_true(
        taiyin::internal::parse_opm2_summary(
            bytes.data(), bytes.size(), &ephe, &grid, &source_id),
        "parse packaged OPM2 source id",
        failures);
    expect_equal_int(
        static_cast<int>(source_id),
        static_cast<int>(taiyin::internal::OPM2_SOURCE_TAIYIN_PRERELEASE),
        "packaged OPM2 header source id",
        failures);

    std::vector<taiyin::internal::EphemerisBlockDescriptor> descriptors;
    expect_equal_int(
        static_cast<int>(taiyin::internal::discover_opm2_file(
            path, taiyin::internal::EphemerisDiscoveryOptions(), &descriptors)),
        static_cast<int>(taiyin::internal::DiscoveryOk),
        "discover packaged OPM2 source-id fixture",
        failures);
    expect_equal_size(descriptors.size(), 1, "one packaged OPM2 descriptor", failures);
    if (descriptors.size() == 1) {
        expect_equal_int(
            static_cast<int>(descriptors[0].source_key.source_id),
            static_cast<int>(taiyin::internal::OPM2_SOURCE_TAIYIN_PRERELEASE),
            "descriptor uses OPM2 header source id",
            failures);
    }
}

void test_de442_package_auto_route_preference(int* failures) {
    using namespace taiyin;
    using namespace taiyin::internal;
    using namespace taiyin::runtime;

    const std::string prerelease_root = repo_data_path("major-bodies/600y");
    const std::string de442_root = repo_data_path("major-bodies/de442-full");
    const char* source_paths[] = {prerelease_root.c_str(), de442_root.c_str()};
    EphemerisRuntimeConfig config;
    config.source_paths = source_paths;
    config.source_path_count = 2;
    config.load_packaged_data = false;
    config.segment_cache_max_entries = 32;

    Runtime runtime;
    expect_true(runtime.initialize_ephemeris(config), "initialize combined prerelease + DE442 OPM2 runtime", failures);

    EphemerisRequest request;
    request.target_id = TAIYIN_BODY_JUPITER_BARYCENTER;
    request.center_id = TAIYIN_BODY_SSB;
    request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = split_jd(2460310.5);
    EphemerisBlockDescriptor selected;
    expect_true(
        runtime.ephemeris_engine().find_descriptor(request, &selected),
        "AUTO finds a combined-package Jupiter route", failures);
    expect_equal_int(
        static_cast<int>(selected.source_key.source_id),
        static_cast<int>(OPM2_SOURCE_TAIYIN_DE442_REBUILT),
        "AUTO prefers the DE442 OPM2 source over prerelease OPM2", failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_major_body_staged_opm2(&failures);
    test_asteroid_staged_opm2(&failures);
    test_cob_staged_opm2(&failures);
    test_cob_runtime_body_composite(&failures);
    test_inner_planet_body_aliases(&failures);
    test_opm2_range_slice_matches_full_frame(&failures);
    test_opm2_header_source_identity(&failures);
    test_de442_package_auto_route_preference(&failures);

    if (failures == 0) {
        std::cout << "test_opm2_staged_data: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_opm2_staged_data failure(s)\n";
    return 1;
}
