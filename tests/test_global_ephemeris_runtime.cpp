#include "taiyin/body_id.h"
#include "taiyin/internal/custom_ephemeris_method.h"
#include "taiyin/internal/eop.h"
#include "taiyin/internal/ephemeris_file_loader.h"
#include "taiyin/internal/ephemeris_segment_cache.h"
#include "taiyin/internal/kepler_file.h"
#include "taiyin/internal/opm2.h"
#include "taiyin/internal/semi_analytic.h"
#include "taiyin/lunar_limb_tll1.h"
#include "taiyin/physical_constants.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cstdlib>
#include <cstdio>
#include <atomic>
#include <cmath>
#include <fstream>
#include <limits>
#include <new>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

const double JD0 = taiyin::JD_J2000;
const int ROUTE_TARGET = 920101;
const int EVICT_TARGET = 920102;
const int METHOD_LOW = 88201;
const int METHOD_HIGH = 88202;
const int METHOD_FILE = 88203;

struct VectorData {
    double x;
    double y;
    double z;
};

int g_file_load_count = 0;
int g_file_destroy_count = 0;

void expect_true(bool value, const char* label) {
    if (!value) {
        std::fprintf(stderr, "expected true: %s\n", label);
        std::abort();
    }
}

void expect_false(bool value, const char* label) {
    if (value) {
        std::fprintf(stderr, "expected false: %s\n", label);
        std::abort();
    }
}

void expect_size(size_t actual, size_t expected, const char* label) {
    if (actual != expected) {
        std::fprintf(stderr, "expected size %zu got %zu: %s\n", expected, actual, label);
        std::abort();
    }
}

void expect_int(int actual, int expected, const char* label) {
    if (actual != expected) {
        std::fprintf(stderr, "expected int %d got %d: %s\n", expected, actual, label);
        std::abort();
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label) {
    const double delta = actual > expected ? actual - expected : expected - actual;
    if (delta > tolerance) {
        std::fprintf(
            stderr,
            "expected near %.17g got %.17g tolerance %.17g: %s\n",
            expected,
            actual,
            tolerance,
            label);
        std::abort();
    }
}

bool vector_position(const taiyin::SplitJulianDate&, const void* data, taiyin::Vector3* out) {
    const VectorData* vector = static_cast<const VectorData*>(data);
    if (!vector || !out) {
        return false;
    }
    out->x = vector->x;
    out->y = vector->y;
    out->z = vector->z;
    return true;
}

bool vector_velocity(const taiyin::SplitJulianDate&, const void*, taiyin::Vector3* out) {
    if (!out) {
        return false;
    }
    out->x = 0.0;
    out->y = 0.0;
    out->z = 0.0;
    return true;
}

bool vector_acceleration(const taiyin::SplitJulianDate&, const void*, taiyin::Vector3* out) {
    if (!out) {
        return false;
    }
    out->x = 0.0;
    out->y = 0.0;
    out->z = 0.0;
    return true;
}

std::string make_temp_file_path() {
    char templ[] = "/tmp/taiyin-global-runtime-XXXXXX";
    const int fd = mkstemp(templ);
    if (fd >= 0) {
        close(fd);
    }
    return std::string(templ);
}

std::string make_temp_kepler_path() {
    char templ[] = "/tmp/taiyin-kepler-XXXXXX";
    const int fd = mkstemp(templ);
    if (fd >= 0) {
        close(fd);
        std::remove(templ);
    }
    return std::string(templ) + ".tke1";
}

std::string make_temp_missing_path() {
    char templ[] = "/tmp/taiyin-missing-XXXXXX";
    const int fd = mkstemp(templ);
    if (fd >= 0) {
        close(fd);
        std::remove(templ);
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

    VectorData* data = new (std::nothrow) VectorData();
    if (!data) {
        return false;
    }
    if (!(file >> data->x >> data->y >> data->z)) {
        delete data;
        return false;
    }

    ++g_file_load_count;
    *out_data = data;
    *out_bytes = sizeof(VectorData);
    return true;
}

void destroy_file_vector(void* data) {
    delete static_cast<VectorData*>(data);
    ++g_file_destroy_count;
}

taiyin::internal::CustomEphemerisMethodDefinition make_memory_definition(
    int method_id,
    const VectorData* data
) {
    taiyin::internal::CustomEphemerisMethodDefinition definition;
    definition.target_id = ROUTE_TARGET;
    definition.center_id = taiyin::TAIYIN_BODY_SUN;
    definition.method_id = method_id;
    definition.frame = taiyin::internal::IcrfJ2000Equatorial;
    definition.jd_tdb_start = JD0 - 1.0;
    definition.jd_tdb_end = JD0 + 1.0;
    definition.data = data;
    definition.bytes = sizeof(VectorData);
    definition.position = vector_position;
    definition.velocity = vector_velocity;
    definition.acceleration = vector_acceleration;
    definition.description = "runtime memory method";
    return definition;
}

taiyin::internal::CustomEphemerisFileMethodDefinition make_file_definition(
    const std::string& path
) {
    taiyin::internal::CustomEphemerisFileMethodDefinition definition;
    definition.target_id = EVICT_TARGET;
    definition.center_id = taiyin::TAIYIN_BODY_SUN;
    definition.method_id = METHOD_FILE;
    definition.frame = taiyin::internal::IcrfJ2000Equatorial;
    definition.jd_tdb_start = JD0 - 1.0;
    definition.jd_tdb_end = JD0 + 1.0;
    definition.path = path.c_str();
    definition.load = load_vector_file;
    definition.position = vector_position;
    definition.velocity = vector_velocity;
    definition.acceleration = vector_acceleration;
    definition.destroy = destroy_file_vector;
    definition.description = "runtime file method";
    return definition;
}

taiyin::runtime::EphemerisRequest make_request(int target_id) {
    taiyin::runtime::EphemerisRequest request;
    request.target_id = target_id;
    request.center_id = taiyin::TAIYIN_BODY_SUN;
    request.frame = taiyin::internal::IcrfJ2000Equatorial;
    taiyin::split_julian_date_from_double(JD0, &request.jd_tdb);
    return request;
}

taiyin::internal::EphemerisSegmentCacheKey make_cache_key(
    const taiyin::internal::EphemerisBlockDescriptor& descriptor
) {
    return taiyin::internal::EphemerisSegmentCacheKey(
        static_cast<uint32_t>(descriptor.format),
        descriptor.target_id,
        descriptor.center_id,
        descriptor.method_id,
        descriptor.frame,
        descriptor.source_key,
        static_cast<int64_t>(descriptor.route_key.bucket_id));
}

std::string repo_opm2_major_body_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/major-bodies/600y";
    }
    return "../data/ephemerides/opm2/major-bodies/600y";
}

void test_opm2_file_reload_after_eviction() {
    using taiyin::runtime::EphemerisEvalDiagnostic;
    using taiyin::runtime::EphemerisResult;

    const std::string source_root = repo_opm2_major_body_root();
    const char* source_paths[] = { source_root.c_str() };
    taiyin::runtime::EphemerisRuntimeConfig config;
    config.segment_cache_max_entries = 1;
    config.source_paths = source_paths;
    config.source_path_count = 1;
    config.load_packaged_data = false;
    expect_true(taiyin::runtime::initialize_global_ephemeris_runtime(config), "initialize OPM2 runtime");

    EphemerisEvalDiagnostic diagnostic;
    EphemerisResult mercury_first;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            make_request(taiyin::TAIYIN_BODY_MERCURY_BARYCENTER),
            &mercury_first,
            &diagnostic)),
        "eval OPM2 Mercury first");
    expect_false(mercury_first.cache_hit, "OPM2 Mercury first load misses cache");
    expect_true(!mercury_first.descriptor.path.empty(), "OPM2 Mercury descriptor keeps path");
    expect_size(taiyin::runtime::global_ephemeris_cache_entry_count(), 1, "OPM2 Mercury fills single cache slot");
    taiyin::internal::EphemerisSourceIndex retained_source;
    expect_true(
        taiyin::runtime::default_runtime().ephemeris_catalog().find_source_index(
            mercury_first.descriptor.source_key,
            &retained_source),
        "OPM2 runtime retains a mapped source view");
    const taiyin::internal::EphemerisFileView* retained_view =
        static_cast<const taiyin::internal::EphemerisFileView*>(retained_source.payload.get());
    expect_true(retained_view && retained_view->is_mapped(), "OPM2 source index is mmap-backed");
    expect_false(retained_view && retained_view->is_decompressed(), "OPM2 source index is not an owned fallback");
    retained_source = taiyin::internal::EphemerisSourceIndex();
    retained_view = 0;

    EphemerisResult venus;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            make_request(taiyin::TAIYIN_BODY_VENUS_BARYCENTER),
            &venus,
            &diagnostic)),
        "eval OPM2 Venus");
    expect_false(venus.cache_hit, "OPM2 Venus first load misses cache");
    expect_size(taiyin::runtime::global_ephemeris_cache_entry_count(), 1, "single OPM2 cache entry after eviction");
    expect_true(
        taiyin::runtime::default_runtime().ephemeris_catalog().find_source_index(
            mercury_first.descriptor.source_key,
            &retained_source),
        "OPM2 source metadata survives segment eviction");
    expect_false(
        static_cast<bool>(retained_source.payload),
        "OPM2 source mapping expires when its final segment is evicted");

    EphemerisResult mercury_reloaded;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            make_request(taiyin::TAIYIN_BODY_MERCURY_BARYCENTER),
            &mercury_reloaded,
            &diagnostic)),
        "reload OPM2 Mercury after eviction");
    expect_false(mercury_reloaded.cache_hit, "OPM2 Mercury reload misses cache after eviction");
    expect_near(
        mercury_reloaded.state.position_au.x,
        mercury_first.state.position_au.x,
        1.0e-15,
        "OPM2 Mercury reload x matches");
    expect_near(
        mercury_reloaded.state.position_au.y,
        mercury_first.state.position_au.y,
        1.0e-15,
        "OPM2 Mercury reload y matches");
    expect_near(
        mercury_reloaded.state.position_au.z,
        mercury_first.state.position_au.z,
        1.0e-15,
        "OPM2 Mercury reload z matches");
    expect_true(
        taiyin::runtime::default_runtime().ephemeris_catalog().find_source_index(
            mercury_reloaded.descriptor.source_key,
            &retained_source),
        "OPM2 reload restores source mapping");

    // Drop the test's copied shared_ptr before verifying that the runtime releases
    // its mapped source view together with the compiled segment cache.
    retained_source = taiyin::internal::EphemerisSourceIndex();
    taiyin::runtime::clear_global_ephemeris_cache();
    expect_size(taiyin::runtime::global_ephemeris_cache_entry_count(), 0, "OPM2 clear removes compiled segments");
    expect_true(
        taiyin::runtime::default_runtime().ephemeris_catalog().find_source_index(
            mercury_first.descriptor.source_key,
            &retained_source),
        "OPM2 clear preserves source identity metadata");
    expect_false(
        static_cast<bool>(retained_source.payload),
        "OPM2 clear releases mapped source view");

    EphemerisResult mercury_after_clear;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            make_request(taiyin::TAIYIN_BODY_MERCURY_BARYCENTER),
            &mercury_after_clear,
            &diagnostic)),
        "reload OPM2 Mercury after explicit clear");
    expect_false(mercury_after_clear.cache_hit, "OPM2 Mercury remap misses segment cache");
    expect_near(
        mercury_after_clear.state.position_au.x,
        mercury_first.state.position_au.x,
        1.0e-15,
        "OPM2 Mercury remap x matches");
    expect_true(
        taiyin::runtime::default_runtime().ephemeris_catalog().find_source_index(
            mercury_after_clear.descriptor.source_key,
            &retained_source),
        "OPM2 evaluation lazily restores mapped source view");
    retained_view = static_cast<const taiyin::internal::EphemerisFileView*>(
        retained_source.payload.get());
    expect_true(retained_view && retained_view->is_mapped(), "OPM2 restored source index is mmap-backed");
    expect_false(retained_view && retained_view->is_decompressed(), "OPM2 restored source index is not an owned fallback");
}

void test_builtin_semi_analytic_runtime_route() {
    using taiyin::runtime::EphemerisEvalDiagnostic;
    using taiyin::runtime::EphemerisRequest;
    using taiyin::runtime::EphemerisResult;

    taiyin::runtime::EphemerisRuntimeConfig config;
    config.segment_cache_max_entries = 8;
    config.load_packaged_data = true;
    expect_true(taiyin::runtime::initialize_global_ephemeris_runtime(config), "initialize runtime with builtin semi-analytical ephemeris");

    const taiyin::internal::EphemerisRouteRuleTable* semi_analytic_rules =
        taiyin::runtime::global_ephemeris_route_rule(
            taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC);
    expect_true(semi_analytic_rules != 0, "semi-analytical route id is registered");
    expect_size(
        semi_analytic_rules ? semi_analytic_rules->method_count() : 0,
        1,
        "semi-analytical route has one method");
    if (semi_analytic_rules && !semi_analytic_rules->rules().empty()) {
        expect_int(
            static_cast<int>(semi_analytic_rules->rules()[0].source_id),
            static_cast<int>(taiyin::internal::SEMI_ANALYTIC_SOURCE_ID),
            "semi-analytical route uses semi-analytical source");
        expect_int(
            semi_analytic_rules->rules()[0].method_id,
            taiyin::internal::SEMI_ANALYTIC_METHOD_ID,
            "semi-analytical route uses semi-analytical method");
    }

    EphemerisRequest mars;
    mars.target_id = taiyin::TAIYIN_BODY_MARS_BARYCENTER;
    mars.center_id = taiyin::TAIYIN_BODY_SUN;
    mars.frame = taiyin::internal::IcrfJ2000Equatorial;
    taiyin::split_julian_date_from_double(JD0, &mars.jd_tdb);
    mars.route_rule_id = taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC;

    EphemerisResult mars_result;
    EphemerisEvalDiagnostic diagnostic;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            mars,
            &mars_result,
            &diagnostic)),
        "eval Mars barycenter through semi-analytical route");
    expect_int(mars_result.descriptor.method_id, taiyin::internal::SEMI_ANALYTIC_METHOD_ID, "Mars route uses semi-analytical method");
    expect_int(
        static_cast<int>(mars_result.descriptor.source_key.source_id),
        static_cast<int>(taiyin::internal::SEMI_ANALYTIC_SOURCE_ID),
        "Mars route uses semi-analytical source");
    expect_int(mars_result.descriptor.target_id, taiyin::TAIYIN_BODY_MARS_BARYCENTER, "Mars semi-analytical target");
    expect_int(mars_result.descriptor.center_id, taiyin::TAIYIN_BODY_SUN, "Mars semi-analytical center");

    taiyin::internal::EphemerisBlockDescriptor found_mars;
    expect_true(
        taiyin::runtime::find_global_ephemeris_descriptor(mars, &found_mars),
        "find Mars descriptor through semi-analytical route");
    expect_int(
        found_mars.method_id,
        taiyin::internal::SEMI_ANALYTIC_METHOD_ID,
        "semi-analytical route resolves semi-analytical descriptor");

    EphemerisRequest earth;
    earth.target_id = taiyin::TAIYIN_BODY_EARTH;
    earth.center_id = taiyin::TAIYIN_BODY_SUN;
    earth.frame = taiyin::internal::IcrfJ2000Equatorial;
    taiyin::split_julian_date_from_double(JD0, &earth.jd_tdb);
    earth.route_rule_id = taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC;

    EphemerisResult earth_result;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            earth,
            &earth_result,
            &diagnostic)),
        "eval Earth body through semi-analytical route");
    expect_int(earth_result.descriptor.method_id, taiyin::internal::SEMI_ANALYTIC_METHOD_ID, "Earth route uses semi-analytical method");
    expect_int(earth_result.descriptor.target_id, taiyin::TAIYIN_BODY_EARTH, "Earth semi-analytical target");
    expect_int(earth_result.descriptor.center_id, taiyin::TAIYIN_BODY_SUN, "Earth semi-analytical center");

    EphemerisRequest mars_body;
    mars_body.target_id = taiyin::TAIYIN_BODY_MARS;
    mars_body.center_id = taiyin::TAIYIN_BODY_SUN;
    mars_body.frame = taiyin::internal::IcrfJ2000Equatorial;
    taiyin::split_julian_date_from_double(JD0, &mars_body.jd_tdb);
    mars_body.route_rule_id = taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC;

    EphemerisResult mars_body_result;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            mars_body,
            &mars_body_result,
            &diagnostic)),
        "semi-analytical route composes Mars body from barycenter and satellite offset");
    expect_int(
        mars_body_result.descriptor.method_id,
        taiyin::internal::SEMI_ANALYTIC_METHOD_ID,
        "Mars body composite uses semi-analytical components");

    struct SatelliteFixture {
        int satellite_id;
        int primary_id;
        const char* name;
    };
    static const SatelliteFixture satellites[] = {
        {taiyin::TAIYIN_BODY_PHOBOS, taiyin::TAIYIN_BODY_MARS, "Phobos"},
        {taiyin::TAIYIN_BODY_DEIMOS, taiyin::TAIYIN_BODY_MARS, "Deimos"},
        {taiyin::TAIYIN_BODY_IO, taiyin::TAIYIN_BODY_JUPITER, "Io"},
        {taiyin::TAIYIN_BODY_EUROPA, taiyin::TAIYIN_BODY_JUPITER, "Europa"},
        {taiyin::TAIYIN_BODY_GANYMEDE, taiyin::TAIYIN_BODY_JUPITER, "Ganymede"},
        {taiyin::TAIYIN_BODY_CALLISTO, taiyin::TAIYIN_BODY_JUPITER, "Callisto"},
        {taiyin::TAIYIN_BODY_TRITON, taiyin::TAIYIN_BODY_NEPTUNE, "Triton"},
        {taiyin::TAIYIN_BODY_CHARON, taiyin::TAIYIN_BODY_PLUTO, "Charon"},
        {taiyin::TAIYIN_BODY_NIX, taiyin::TAIYIN_BODY_PLUTO, "Nix"},
        {taiyin::TAIYIN_BODY_HYDRA, taiyin::TAIYIN_BODY_PLUTO, "Hydra"},
        {taiyin::TAIYIN_BODY_KERBEROS, taiyin::TAIYIN_BODY_PLUTO, "Kerberos"},
        {taiyin::TAIYIN_BODY_STYX, taiyin::TAIYIN_BODY_PLUTO, "Styx"},
    };
    const int centers[] = {
        0, taiyin::TAIYIN_BODY_SUN, taiyin::TAIYIN_BODY_EARTH,
    };
    for (size_t satellite_index = 0;
         satellite_index < sizeof(satellites) / sizeof(satellites[0]);
         ++satellite_index) {
        for (size_t center_index = 0;
             center_index < sizeof(centers) / sizeof(centers[0]);
             ++center_index) {
            EphemerisRequest request;
            request.target_id = satellites[satellite_index].satellite_id;
            request.center_id = center_index == 0
                ? satellites[satellite_index].primary_id
                : centers[center_index];
            request.frame = taiyin::internal::IcrfJ2000Equatorial;
            taiyin::split_julian_date_from_double(JD0, &request.jd_tdb);
            request.route_rule_id =
                taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC;

            EphemerisResult result;
            char label[192];
            std::snprintf(
                label,
                sizeof(label),
                "semi-analytical route evaluates %s relative to %s",
                satellites[satellite_index].name,
                center_index == 0 ? "its physical primary"
                    : (center_index == 1 ? "the Sun" : "Earth"));
            expect_true(
                taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
                    request,
                    &result,
                    &diagnostic)),
                label);
        }
    }

    double phobos_start = 0.0;
    double phobos_end = 0.0;
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(
            taiyin::TAIYIN_BODY_PHOBOS,
            taiyin::TAIYIN_BODY_MARS,
            &phobos_start,
            &phobos_end),
        "read Phobos inclusive semi-analytical coverage");
    EphemerisRequest endpoint_request;
    endpoint_request.target_id = taiyin::TAIYIN_BODY_PHOBOS;
    endpoint_request.center_id = taiyin::TAIYIN_BODY_MARS;
    endpoint_request.frame = taiyin::internal::IcrfJ2000Equatorial;
    taiyin::split_julian_date_from_double(phobos_end, &endpoint_request.jd_tdb);
    endpoint_request.route_rule_id =
        taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC;
    EphemerisResult endpoint_result;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            endpoint_request, &endpoint_result, &diagnostic)),
        "inclusive Phobos coverage endpoint remains routable");

    taiyin::split_julian_date_from_double(
        std::nextafter(phobos_end, std::numeric_limits<double>::infinity()),
        &endpoint_request.jd_tdb);
    expect_true(
        !taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            endpoint_request, &endpoint_result, &diagnostic)),
        "epoch after Phobos coverage endpoint remains rejected");
}

void test_auto_route_prefers_high_priority_composite_over_low_priority_direct() {
    using taiyin::runtime::EphemerisEvalDiagnostic;
    using taiyin::runtime::EphemerisRequest;
    using taiyin::runtime::EphemerisResult;

    taiyin::runtime::EphemerisRuntimeConfig config;
    config.segment_cache_max_entries = 64;
    config.load_packaged_data = true;
    const char* repo_root = std::getenv("TAIYIN_REPO_ROOT");
    expect_true(repo_root && repo_root[0] != '\0', "route priority test repo root");
    config.data_root = repo_root;
    expect_true(
        taiyin::runtime::initialize_global_ephemeris_runtime(config),
        "initialize runtime with packaged data for route priority regression");

    EphemerisEvalDiagnostic diagnostic;

    EphemerisRequest mars_sun;
    mars_sun.target_id = taiyin::TAIYIN_BODY_MARS_BARYCENTER;
    mars_sun.center_id = taiyin::TAIYIN_BODY_SUN;
    mars_sun.frame = taiyin::internal::IcrfJ2000Equatorial;
    taiyin::split_julian_date_from_double(JD0, &mars_sun.jd_tdb);

    EphemerisResult mars_sun_result;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            mars_sun,
            &mars_sun_result,
            &diagnostic)),
        "auto route evaluates Mars/Sun composite");
    expect_int(
        mars_sun_result.descriptor.method_id,
        static_cast<int>(taiyin::internal::OPM2_METHOD_ID),
        "auto Mars/Sun prefers OPM2 composite over semi-analytical direct");
    expect_int(
        static_cast<int>(mars_sun_result.descriptor.source_key.source_id),
        static_cast<int>(taiyin::internal::OPM2_SOURCE_TAIYIN_DE442_REBUILT),
        "auto Mars/Sun source is DE442 OPM2");

    EphemerisRequest physical_mars_sun = mars_sun;
    physical_mars_sun.target_id = taiyin::TAIYIN_BODY_MARS;
    EphemerisResult physical_mars_sun_result;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            physical_mars_sun,
            &physical_mars_sun_result,
            &diagnostic)),
        "auto route evaluates physical Mars/Sun composite with DE442 anchor");
    expect_int(
        physical_mars_sun_result.descriptor.method_id,
        static_cast<int>(taiyin::internal::OPM2_METHOD_ID),
        "physical Mars retains the OPM2 primary route instead of falling back entirely");
    expect_int(
        static_cast<int>(physical_mars_sun_result.descriptor.source_key.source_id),
        static_cast<int>(taiyin::internal::OPM2_SOURCE_TAIYIN_DE442_REBUILT),
        "physical Mars uses the DE442 OPM2 anchor with its allowed relative-body correction");

    EphemerisRequest earth_ssb;
    earth_ssb.target_id = taiyin::TAIYIN_BODY_EARTH;
    earth_ssb.center_id = taiyin::TAIYIN_BODY_SSB;
    earth_ssb.frame = taiyin::internal::IcrfJ2000Equatorial;
    taiyin::split_julian_date_from_double(JD0, &earth_ssb.jd_tdb);

    EphemerisResult earth_ssb_result;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            earth_ssb,
            &earth_ssb_result,
            &diagnostic)),
        "auto route evaluates Earth/SSB composite");
    expect_int(
        earth_ssb_result.descriptor.method_id,
        static_cast<int>(taiyin::internal::OPM2_METHOD_ID),
        "auto Earth/SSB prefers OPM2 EMB/Moon composite over semi-analytical direct");
    expect_int(
        static_cast<int>(earth_ssb_result.descriptor.source_key.source_id),
        static_cast<int>(taiyin::internal::OPM2_SOURCE_TAIYIN_DE442_REBUILT),
        "auto Earth/SSB source is DE442 OPM2");
}

void test_global_auxiliary_data_ownership() {
    taiyin::internal::EarthOrientationSample sample = {};
    sample.jd_utc = taiyin::JD_J2000;
    sample.dut1_seconds = 0.125;
    taiyin::internal::EarthOrientationTable table = { &sample, 1 };
    expect_true(
        taiyin::runtime::set_global_earth_orientation_table(&table),
        "install copied global EOP table");
    const taiyin::internal::EarthOrientationTable* installed =
        taiyin::runtime::global_earth_orientation_table();
    expect_true(installed != 0, "global EOP table is available");
    expect_true(installed != &table, "runtime owns copied EOP table object");
    expect_true(installed && installed->samples != table.samples, "runtime owns copied EOP samples");
    expect_near(
        installed && installed->samples ? installed->samples[0].dut1_seconds : 0.0,
        sample.dut1_seconds,
        0.0,
        "copied global EOP value");
    const taiyin::internal::EarthOrientationTable* copied_snapshot = installed;

    taiyin::runtime::EphemerisRuntimeConfig builtin_config;
    builtin_config.load_packaged_data = false;
    builtin_config.load_builtin_eop = true;
    expect_true(
        taiyin::runtime::initialize_global_ephemeris_runtime(builtin_config),
        "initialize runtime with built-in EOP");
    installed = taiyin::runtime::global_earth_orientation_table();
    expect_true(installed && installed->count > 0, "built-in global EOP is available");
    expect_near(
        copied_snapshot->samples[0].dut1_seconds,
        sample.dut1_seconds,
        0.0,
        "replaced global EOP snapshot remains valid");
    const taiyin::internal::EarthOrientationTable* builtin_snapshot = installed;

    taiyin::runtime::EphemerisRuntimeConfig empty_config;
    empty_config.load_packaged_data = false;
    expect_true(
        taiyin::runtime::initialize_global_ephemeris_runtime(empty_config),
        "reinitialize runtime without EOP");
    expect_true(
        taiyin::runtime::global_earth_orientation_table() == 0,
        "runtime reinitialization clears global EOP");
    expect_true(
        builtin_snapshot->samples && builtin_snapshot->count > 0,
        "cleared global EOP snapshot remains valid");
}

void test_kepler_source_key_is_file_aware() {
    using taiyin::runtime::EphemerisRequest;
    using taiyin::runtime::EphemerisEvalDiagnostic;
    using taiyin::runtime::EphemerisResult;

    taiyin::internal::KeplerElements elements[1];
    taiyin::internal::make_elliptic_kepler_elements(
        taiyin::TAIYIN_BODY_EROS,
        taiyin::TAIYIN_BODY_SSB,
        JD0 - 100.0,
        JD0 + 100.0,
        JD0,
        taiyin::TAIYIN_SOLAR_MU_AU3_DAY2,
        1.8,
        0.22,
        0.1,
        0.2,
        0.3,
        0.4,
        &elements[0]);
    const std::string first_path = make_temp_kepler_path();
    const std::string second_path = make_temp_kepler_path();
    std::remove(first_path.c_str());
    std::remove(second_path.c_str());
    expect_true(
        taiyin::internal::save_kepler_file(
            first_path.c_str(),
            elements,
            1,
            taiyin::internal::TAIYIN_KEPLER_FILE_METHOD_ID,
            taiyin::internal::IcrfJ2000Equatorial,
            JD0 - 100.0,
            JD0 + 100.0),
        "save first kepler file");
    expect_true(
        taiyin::internal::save_kepler_file(
            second_path.c_str(),
            elements,
            1,
            taiyin::internal::TAIYIN_KEPLER_FILE_METHOD_ID,
            taiyin::internal::IcrfJ2000Equatorial,
            JD0 - 100.0,
            JD0 + 100.0),
        "save second kepler file");

    taiyin::runtime::EphemerisRuntimeConfig config;
    config.segment_cache_max_entries = 64;
    config.load_packaged_data = false;
    const char* root_paths[2];
    root_paths[0] = first_path.c_str();
    root_paths[1] = second_path.c_str();
    config.source_paths = root_paths;
    config.source_path_count = 2;
    config.strict_discovery = true;
    expect_true(
        taiyin::runtime::initialize_global_ephemeris_runtime(config),
        "initialize runtime with two kepler files");

    EphemerisRequest eros;
    eros.target_id = taiyin::TAIYIN_BODY_EROS;
    eros.center_id = taiyin::TAIYIN_BODY_SSB;
    eros.frame = taiyin::internal::IcrfJ2000Equatorial;
    taiyin::split_julian_date_from_double(JD0, &eros.jd_tdb);

    EphemerisResult result;
    EphemerisEvalDiagnostic diagnostic;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            eros,
            &result,
            &diagnostic)),
        "eval Eros through kepler file source");
    expect_int(
        static_cast<int>(result.descriptor.source_key.source_id),
        static_cast<int>(taiyin::internal::TAIYIN_KEPLER_FILE_SOURCE_ID),
        "Eros source is kepler file");

    std::remove(first_path.c_str());
    std::remove(second_path.c_str());
    const taiyin::internal::EphemerisBlockCatalog& catalog =
        taiyin::runtime::default_runtime().ephemeris_catalog();
    expect_size(catalog.size(), 2, "two kepler descriptors in global catalog");
    uint64_t first_block_id = 0;
    uint64_t second_block_id = 0;
    for (size_t i = 0; i < catalog.size(); ++i) {
        taiyin::internal::EphemerisBlockDescriptor descriptor;
        expect_true(catalog.get(i, &descriptor), "get kepler descriptor from catalog");
        expect_int(
            static_cast<int>(descriptor.format),
            static_cast<int>(taiyin::internal::EphemerisBlockFormat::Kepler),
            "catalog descriptor is kepler format");
        if (i == 0) {
            first_block_id = descriptor.source_key.block_id;
        } else {
            second_block_id = descriptor.source_key.block_id;
        }
    }
    if (first_block_id == 0 || second_block_id == 0 || first_block_id == second_block_id) {
        std::fprintf(stderr, "expected distinct source block ids for two kepler files\n");
        std::abort();
    }
}

void test_initialization_failure_preserves_runtime() {
    using taiyin::runtime::EphemerisRequest;
    using taiyin::runtime::EphemerisEvalDiagnostic;
    using taiyin::runtime::EphemerisResult;

    taiyin::internal::clear_custom_ephemeris_methods();
    taiyin::runtime::EphemerisRuntimeConfig good_config;
    good_config.segment_cache_max_entries = 16;
    good_config.load_packaged_data = false;
    expect_true(
        taiyin::runtime::initialize_global_ephemeris_runtime(good_config),
        "initialize a usable empty runtime");

    VectorData data = { 11.0, 12.0, 13.0 };
    taiyin::internal::EphemerisBlockDescriptor descriptor;
    expect_true(
        taiyin::runtime::add_global_custom_ephemeris_method(
            make_memory_definition(ROUTE_TARGET, &data),
            100,
            "runtime marker for transaction test",
            &descriptor),
        "add marker descriptor to runtime");
    expect_size(
        taiyin::runtime::global_ephemeris_catalog_size(),
        1,
        "marker descriptor present before failed init");

    taiyin::runtime::EphemerisRuntimeConfig bad_config;
    bad_config.segment_cache_max_entries = 16;
    bad_config.load_packaged_data = false;
    const std::string bad_path = make_temp_missing_path();
    const char* bad_paths[1];
    bad_paths[0] = bad_path.c_str();
    bad_config.source_paths = bad_paths;
    bad_config.source_path_count = 1;
    expect_false(
        taiyin::runtime::initialize_global_ephemeris_runtime(bad_config),
        "failed initialization returns false");

    expect_size(
        taiyin::runtime::global_ephemeris_catalog_size(),
        1,
        "failed init preserves prior catalog");
    EphemerisRequest request = make_request(ROUTE_TARGET);
    EphemerisResult result;
    EphemerisEvalDiagnostic diagnostic;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            request,
            &result,
            &diagnostic)),
        "prior runtime still evaluates after failed init");
    expect_near(result.state.position_au.x, 11.0, 0.0, "prior runtime marker x preserved");

    taiyin::internal::clear_custom_ephemeris_methods();
}

void test_source_path_failure_does_not_pollute_catalog() {
    using taiyin::runtime::EphemerisRequest;
    using taiyin::runtime::EphemerisEvalDiagnostic;
    using taiyin::runtime::EphemerisResult;

    taiyin::runtime::EphemerisRuntimeConfig config;
    config.segment_cache_max_entries = 16;
    config.load_packaged_data = false;
    expect_true(
        taiyin::runtime::initialize_global_ephemeris_runtime(config),
        "initialize empty runtime for source-path transaction");

    taiyin::internal::KeplerElements elements[1];
    taiyin::internal::make_elliptic_kepler_elements(
        taiyin::TAIYIN_BODY_EROS,
        taiyin::TAIYIN_BODY_SSB,
        JD0 - 100.0,
        JD0 + 100.0,
        JD0,
        taiyin::TAIYIN_SOLAR_MU_AU3_DAY2,
        1.8,
        0.22,
        0.1,
        0.2,
        0.3,
        0.4,
        &elements[0]);
    const std::string good_path = make_temp_kepler_path();
    const std::string missing_path = make_temp_missing_path();
    std::remove(good_path.c_str());
    expect_true(
        taiyin::internal::save_kepler_file(
            good_path.c_str(),
            elements,
            1,
            taiyin::internal::TAIYIN_KEPLER_FILE_METHOD_ID,
            taiyin::internal::IcrfJ2000Equatorial,
            JD0 - 100.0,
            JD0 + 100.0),
        "save good kepler file");

    expect_true(
        taiyin::runtime::add_global_ephemeris_source_path(good_path.c_str()),
        "add good kepler source path");
    expect_size(
        taiyin::runtime::global_ephemeris_catalog_size(),
        1,
        "good source path adds one descriptor");

    expect_false(
        taiyin::runtime::add_global_ephemeris_source_path(missing_path.c_str()),
        "add nonexistent source path fails");
    expect_size(
        taiyin::runtime::global_ephemeris_catalog_size(),
        1,
        "failed source path does not pollute catalog");

    EphemerisRequest eros;
    eros.target_id = taiyin::TAIYIN_BODY_EROS;
    eros.center_id = taiyin::TAIYIN_BODY_SSB;
    eros.frame = taiyin::internal::IcrfJ2000Equatorial;
    taiyin::split_julian_date_from_double(JD0, &eros.jd_tdb);
    EphemerisResult result;
    EphemerisEvalDiagnostic diagnostic;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            eros,
            &result,
            &diagnostic)),
        "good source remains usable after failed add");

    std::remove(good_path.c_str());
}

void test_lunar_limb_model_concurrent_load_read() {
    const char* repo_root = std::getenv("TAIYIN_REPO_ROOT");
    if (!repo_root || repo_root[0] == '\0') {
        return;
    }
    const std::string limb_path = std::string(repo_root) + "/data/lunar-limb/kaguya_lalt_16ppd.tll1";
    if (!std::ifstream(limb_path.c_str())) {
        return;
    }

    const int thread_count = 4;
    const int iterations = 50;
    std::atomic<bool> stop(false);
    std::thread loader([&]() {
        for (int i = 0; i < iterations && !stop.load(); ++i) {
            taiyin::runtime::load_global_lunar_limb_model(limb_path.c_str());
        }
    });
    std::thread reader([&]() {
        for (int i = 0; i < iterations; ++i) {
            const taiyin::Tll1LunarLimbModel* model =
                taiyin::runtime::global_lunar_limb_model();
            (void)model;
        }
    });
    loader.join();
    stop.store(true);
    reader.join();

    std::vector<taiyin::runtime::RegisteredDataSource> sources;
    expect_true(
        taiyin::runtime::get_global_registered_data_sources(&sources),
        "list registered lunar-limb data");
    bool found_lunar_limb = false;
    for (size_t i = 0; i < sources.size(); ++i) {
        if (sources[i].kind
                == taiyin::runtime::TAIYIN_RUNTIME_DATA_SOURCE_LUNAR_LIMB
            && sources[i].source == limb_path) {
            found_lunar_limb = true;
            expect_true(
                sources[i].item_count > 0u,
                "registered lunar-limb source reports samples");
        }
    }
    expect_true(found_lunar_limb, "registered lunar-limb path is reported");
}

}  // namespace

int main() {
    using taiyin::internal::EphemerisBlockDescriptor;
    using taiyin::runtime::EphemerisBodyRouteEntry;
    using taiyin::runtime::EphemerisEvalDiagnostic;
    using taiyin::runtime::EphemerisResult;

    taiyin::internal::clear_custom_ephemeris_methods();
    taiyin::runtime::EphemerisRuntimeConfig config;
    config.segment_cache_max_entries = 1;
    config.load_packaged_data = false;
    expect_true(taiyin::runtime::initialize_global_ephemeris_runtime(config), "initialize empty runtime");
    expect_size(taiyin::runtime::global_ephemeris_catalog_size(), 0, "empty global catalog");
    expect_size(taiyin::runtime::global_ephemeris_cache_entry_count(), 0, "empty global cache");
    expect_true(
        taiyin::runtime::default_runtime().ephemeris_engine().catalog()
            == &taiyin::runtime::default_runtime().ephemeris_catalog(),
        "engine is bound to runtime catalog");
    expect_true(
        taiyin::runtime::default_runtime().ephemeris_engine().segment_cache()
            == taiyin::runtime::default_runtime().ephemeris_segment_cache(),
        "engine is bound to runtime segment cache");

    VectorData low_data = { 1.0, 2.0, 3.0 };
    VectorData high_data = { 4.0, 5.0, 6.0 };

    EphemerisBlockDescriptor low_descriptor;
    EphemerisBlockDescriptor high_descriptor;
    expect_true(
        taiyin::runtime::add_global_custom_ephemeris_method(
            make_memory_definition(METHOD_LOW, &low_data),
            100,
            "low-priority runtime method",
            &low_descriptor),
        "add low-priority descriptor");
    expect_true(
        taiyin::runtime::add_global_custom_ephemeris_method(
            make_memory_definition(METHOD_HIGH, &high_data),
            500,
            "high-priority runtime method",
            &high_descriptor),
        "add high-priority descriptor");
    expect_size(taiyin::runtime::global_ephemeris_catalog_size(), 2, "two descriptors in global catalog");
    std::vector<taiyin::runtime::RegisteredDataSource> registered_sources;
    expect_true(
        taiyin::runtime::get_global_registered_data_sources(
            &registered_sources),
        "list registered in-memory ephemeris sources");
    expect_size(
        registered_sources.size(), 1,
        "two in-memory descriptors aggregate into one source");
    expect_int(
        static_cast<int>(registered_sources[0].format),
        static_cast<int>(taiyin::runtime::TAIYIN_RUNTIME_DATA_FORMAT_CUSTOM),
        "registered in-memory source format");
    expect_true(
        (registered_sources[0].flags
            & taiyin::runtime::TAIYIN_RUNTIME_DATA_SOURCE_MEMORY) != 0u,
        "registered in-memory source flag");
    expect_size(
        registered_sources[0].item_count, 2,
        "registered in-memory source descriptor count");

    EphemerisBodyRouteEntry route_entry;
    expect_true(
        taiyin::runtime::default_runtime().ephemeris_body_registry().find(ROUTE_TARGET, &route_entry),
        "body registry has direct custom target");
    expect_true(route_entry.has_direct, "custom target is marked direct");

    EphemerisBlockDescriptor found;
    expect_true(
        taiyin::runtime::find_global_ephemeris_descriptor(make_request(ROUTE_TARGET), &found),
        "find descriptor through global runtime");
    expect_int(found.method_id, METHOD_HIGH, "find descriptor follows route rule priority");

    EphemerisResult result;
    EphemerisEvalDiagnostic diagnostic;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            make_request(ROUTE_TARGET),
            &result,
            &diagnostic)),
        "eval high-priority custom route");
    expect_false(result.cache_hit, "first global eval loads source segment");
    expect_int(result.descriptor.method_id, METHOD_HIGH, "eval uses high-priority method");
    expect_near(result.state.position_au.x, 4.0, 0.0, "high method x");
    expect_near(result.state.position_au.y, 5.0, 0.0, "high method y");
    expect_near(result.state.position_au.z, 6.0, 0.0, "high method z");

    const taiyin::internal::EphemerisSegmentCacheKey high_key = make_cache_key(result.descriptor);
    expect_true(taiyin::runtime::global_ephemeris_cache_contains(high_key), "high route is cached");

    EphemerisResult cached_result;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            make_request(ROUTE_TARGET),
            &cached_result,
            &diagnostic)),
        "eval high-priority route with reused source segment");
    expect_true(cached_result.cache_hit, "second global eval reuses source segment");

    const std::string file_path = make_temp_file_path();
    write_vector_file(file_path, -7.0, 8.0, -9.0);

    EphemerisBlockDescriptor file_descriptor;
    expect_true(
        taiyin::runtime::add_global_custom_ephemeris_file_method(
            make_file_definition(file_path),
            700,
            "file-backed runtime method",
            &file_descriptor),
        "add file-backed descriptor");
    expect_size(taiyin::runtime::global_ephemeris_catalog_size(), 3, "file descriptor added to global catalog");
    registered_sources.clear();
    expect_true(
        taiyin::runtime::get_global_registered_data_sources(
            &registered_sources),
        "list registered file-backed source");
    expect_size(
        registered_sources.size(), 2,
        "file-backed source is listed separately");
    bool found_registered_file = false;
    for (size_t i = 0; i < registered_sources.size(); ++i) {
        if (registered_sources[i].source == file_path) {
            found_registered_file = true;
            expect_size(
                registered_sources[i].item_count, 1,
                "file-backed source descriptor count");
        }
    }
    expect_true(found_registered_file, "registered file path is reported");

    EphemerisBlockDescriptor found_file;
    expect_true(
        taiyin::runtime::find_global_ephemeris_descriptor(make_request(EVICT_TARGET), &found_file),
        "find file-backed descriptor through global runtime");
    expect_true(found_file.path == file_path, "file-backed descriptor keeps reload path");

    EphemerisResult file_result;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            make_request(EVICT_TARGET),
            &file_result,
            &diagnostic)),
        "eval file-backed custom route");
    expect_false(file_result.cache_hit, "file-backed first eval loads cache");
    expect_int(g_file_load_count, 1, "file-backed route loaded from disk");
    expect_near(file_result.state.position_au.x, -7.0, 0.0, "file method x");
    expect_false(taiyin::runtime::global_ephemeris_cache_contains(high_key), "single-entry cache evicted high route");

    EphemerisResult reloaded_result;
    expect_true(
        taiyin::status_ok(taiyin::runtime::eval_global_ephemeris_state(
            make_request(ROUTE_TARGET),
            &reloaded_result,
            &diagnostic)),
        "reload high-priority route from catalog after eviction");
    expect_false(reloaded_result.cache_hit, "evicted high route reloads through catalog");
    expect_int(reloaded_result.descriptor.method_id, METHOD_HIGH, "reloaded route still uses route rule priority");
    expect_near(reloaded_result.state.position_au.x, 4.0, 0.0, "reloaded high method x");

    taiyin::runtime::clear_global_ephemeris_cache();
    taiyin::internal::clear_custom_ephemeris_methods();
    std::remove(file_path.c_str());
    expect_true(g_file_destroy_count >= 1, "file-backed cached payload destroyed");
    test_auto_route_prefers_high_priority_composite_over_low_priority_direct();
    test_builtin_semi_analytic_runtime_route();
    test_opm2_file_reload_after_eviction();
    test_global_auxiliary_data_ownership();
    test_lunar_limb_model_concurrent_load_read();
    test_initialization_failure_preserves_runtime();
    test_source_path_failure_does_not_pollute_catalog();
    test_kepler_source_key_is_file_aware();
    return 0;
}
