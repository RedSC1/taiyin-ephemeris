#include "taiyin/body_id.h"
#include "taiyin/coordinates.h"
#include "taiyin/dispatch.h"
#include "taiyin/runtime/major_body_apparent.h"
#include "taiyin/vector3.h"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

using namespace taiyin;
using namespace taiyin::internal;
using namespace taiyin::runtime;

struct TestEphemerisData {
    double base;
};

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_status(TaiyinStatus actual, TaiyinStatus expected, const char* label, int* failures) {
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
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

bool test_position(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!data || !out) {
        return false;
    }
    const TestEphemerisData* test_data = static_cast<const TestEphemerisData*>(data);
    out->x = test_data->base + jd_tdb;
    out->y = test_data->base + 2.0;
    out->z = test_data->base + 3.0;
    return true;
}

bool test_velocity(double, const void* data, Vector3* out) noexcept {
    if (!data || !out) {
        return false;
    }
    const TestEphemerisData* test_data = static_cast<const TestEphemerisData*>(data);
    out->x = test_data->base + 4.0;
    out->y = test_data->base + 5.0;
    out->z = test_data->base + 6.0;
    return true;
}

bool test_acceleration(double, const void* data, Vector3* out) noexcept {
    if (!data || !out) {
        return false;
    }
    const TestEphemerisData* test_data = static_cast<const TestEphemerisData*>(data);
    out->x = test_data->base + 7.0;
    out->y = test_data->base + 8.0;
    out->z = test_data->base + 9.0;
    return true;
}

void destroy_test_data(void* data) noexcept {
    delete static_cast<TestEphemerisData*>(data);
}

StorageEphemerisBlock make_storage(double base) {
    StorageEphemerisBlock storage;
    storage.format = EphemerisBlockFormat::Kepler;
    storage.position = &test_position;
    storage.velocity = &test_velocity;
    storage.acceleration = &test_acceleration;
    storage.destroy_element = &destroy_test_data;
    TestEphemerisData* data = new TestEphemerisData();
    data->base = base;
    storage.data_vector.push_back(data);
    storage.total_bytes = sizeof(TestEphemerisData);
    return storage;
}

EphemerisBlockDescriptor make_descriptor(
    int target_id,
    int center_id,
    int method_id,
    int bucket_id,
    uint64_t source_id
) {
    EphemerisBlockDescriptor descriptor;
    descriptor.route_key = EphemerisRouteKey(target_id, center_id, method_id, bucket_id);
    descriptor.source_key = EphemerisBlockKey(source_id, static_cast<uint64_t>(bucket_id + 1), 1, 0);
    descriptor.target_id = target_id;
    descriptor.center_id = center_id;
    descriptor.method_id = method_id;
    descriptor.frame = EphemerisFrame::IcrfJ2000Equatorial;
    descriptor.format = EphemerisBlockFormat::Kepler;
    descriptor.jd_tdb_start = 100.0;
    descriptor.jd_tdb_end = 200.0;
    descriptor.path = "test";
    return descriptor;
}

void add_cached_descriptor(
    EphemerisBlockCatalog* catalog,
    EphemerisBlockCache* cache,
    const EphemerisBlockDescriptor& descriptor,
    double base,
    const char* label,
    int* failures
) {
    expect_true(catalog->add(descriptor), label, failures);
    StorageEphemerisBlock storage = make_storage(base);
    expect_true(cache->insert(descriptor.route_key, descriptor.jd_tdb_start, descriptor.jd_tdb_end, &storage), label, failures);
}

MajorBodyApparentPosition* find_body(MajorBodyApparentBatchResult* result, int body_id) {
    if (!result) {
        return 0;
    }
    for (size_t i = 0; i < result->body_count; ++i) {
        if (result->bodies[i].body_id == body_id) {
            return &result->bodies[i];
        }
    }
    return 0;
}

void setup_earth_sun_service(
    EphemerisBlockCatalog* catalog,
    EphemerisBlockCache* cache,
    EphemerisService* service,
    int method_id,
    int* failures
) {
    add_cached_descriptor(
        catalog,
        cache,
        make_descriptor(TAIYIN_BODY_EARTH, TAIYIN_BODY_SUN, method_id, 0, 900 + static_cast<uint64_t>(method_id)),
        10.0,
        "add fallback Earth/Sun descriptor",
        failures);
    service->set_catalog(catalog);
    service->set_cache(cache);
}

void expect_spherical_from_icrf(
    const Vector3& icrf_position,
    double actual_lon,
    double actual_lat,
    double actual_distance,
    const char* label,
    int* failures
) {
    const Matrix3x3 ecliptic_matrix = icrf_to_j2000_ecliptic_matrix();
    const Vector3 ecliptic = transform_position_with_matrix(icrf_position, ecliptic_matrix);
    double expected_lon = 0.0;
    double expected_lat = 0.0;
    double expected_distance = 0.0;
    cartesian_to_spherical(ecliptic, &expected_lon, &expected_lat, &expected_distance);
    expect_near(actual_lon, expected_lon, 1.0e-12, label, failures);
    expect_near(actual_lat, expected_lat, 1.0e-12, label, failures);
    expect_near(actual_distance, expected_distance, 1.0e-12, label, failures);
}

void test_major_body_apparent_geometric_batch(int* failures) {
    EphemerisBlockCatalog catalog;
    EphemerisBlockCache cache(1024 * 1024);

    add_cached_descriptor(
        &catalog,
        &cache,
        make_descriptor(TAIYIN_BODY_EARTH, TAIYIN_BODY_SUN, 100, 0, 100),
        10.0,
        "add Earth/Sun descriptor",
        failures);
    add_cached_descriptor(
        &catalog,
        &cache,
        make_descriptor(TAIYIN_BODY_MERCURY_BARYCENTER, TAIYIN_BODY_SUN, 100, 1, 101),
        100.0,
        "add Mercury/Sun descriptor",
        failures);

    EphemerisService service;
    service.set_catalog(&catalog);
    service.set_cache(&cache);

    const int bodies[] = { TAIYIN_BODY_SUN, TAIYIN_BODY_MERCURY_BARYCENTER };
    TaiyinApparentOptions options;
    options.flags = 0;

    MajorBodyApparentBatchRequest request;
    request.jd_tdb = 150.0;
    request.body_ids = bodies;
    request.body_count = sizeof(bodies) / sizeof(bodies[0]);
    request.options = &options;

    MajorBodyApparentBatchResult result;
    EphemerisEvalDiagnostic diagnostic;
    const TaiyinStatus status = eval_major_body_apparent_batch(&service, request, &result, &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "major body geometric batch status", failures);
    expect_size(result.body_count, 2, "major body geometric result count", failures);

    MajorBodyApparentPosition* sun = find_body(&result, TAIYIN_BODY_SUN);
    MajorBodyApparentPosition* mercury = find_body(&result, TAIYIN_BODY_MERCURY_BARYCENTER);
    expect_true(sun != 0, "Sun result exists", failures);
    expect_true(mercury != 0, "Mercury result exists", failures);
    if (sun) {
        expect_status(sun->status, TAIYIN_STATUS_OK, "Sun status", failures);
        expect_near(sun->geometric_state.position_au.x, -160.0, 1.0e-12, "Sun geometric x", failures);
        expect_near(sun->geometric_state.position_au.y, -12.0, 1.0e-12, "Sun geometric y", failures);
        Vector3 expected;
        expected.x = -160.0;
        expected.y = -12.0;
        expected.z = -13.0;
        expect_spherical_from_icrf(expected, sun->longitude_rad, sun->latitude_rad, sun->distance_au, "Sun ecliptic spherical", failures);
    }
    if (mercury) {
        expect_status(mercury->status, TAIYIN_STATUS_OK, "Mercury status", failures);
        expect_near(mercury->geometric_state.position_au.x, 90.0, 1.0e-12, "Mercury geometric x", failures);
        expect_near(mercury->geometric_state.position_au.y, 90.0, 1.0e-12, "Mercury geometric y", failures);
        Vector3 expected;
        expected.x = 90.0;
        expected.y = 90.0;
        expected.z = 90.0;
        expect_spherical_from_icrf(expected, mercury->longitude_rad, mercury->latitude_rad, mercury->distance_au, "Mercury ecliptic spherical", failures);
    }
}

void test_major_body_apparent_light_time_and_composite_moon(int* failures) {
    EphemerisBlockCatalog catalog;
    EphemerisBlockCache cache(1024 * 1024);

    add_cached_descriptor(
        &catalog,
        &cache,
        make_descriptor(TAIYIN_BODY_EARTH, TAIYIN_BODY_SUN, 200, 0, 200),
        10.0,
        "add light-time Earth/Sun descriptor",
        failures);
    add_cached_descriptor(
        &catalog,
        &cache,
        make_descriptor(TAIYIN_BODY_EMB, TAIYIN_BODY_SUN, 200, 1, 201),
        20.0,
        "add light-time EMB/Sun descriptor",
        failures);
    add_cached_descriptor(
        &catalog,
        &cache,
        make_descriptor(TAIYIN_BODY_MOON, TAIYIN_BODY_EARTH, 200, 2, 202),
        30.0,
        "add light-time Moon/Earth descriptor",
        failures);

    EphemerisService service;
    service.set_catalog(&catalog);
    service.set_cache(&cache);

    const int bodies[] = { TAIYIN_BODY_MOON };
    TaiyinApparentOptions options;
    options.flags = TAIYIN_MAJOR_BODY_APPARENT_LIGHT_TIME;

    MajorBodyApparentBatchRequest request;
    request.jd_tdb = 150.0;
    request.body_ids = bodies;
    request.body_count = sizeof(bodies) / sizeof(bodies[0]);
    request.options = &options;

    MajorBodyApparentBatchResult result;
    const TaiyinStatus status = eval_major_body_apparent_batch(&service, request, &result, 0);
    expect_status(status, TAIYIN_STATUS_OK, "Moon composite light-time batch status", failures);
    expect_size(result.body_count, 1, "Moon composite light-time result count", failures);

    MajorBodyApparentPosition* moon = find_body(&result, TAIYIN_BODY_MOON);
    expect_true(moon != 0, "Moon result exists", failures);
    if (moon) {
        expect_status(moon->status, TAIYIN_STATUS_OK, "Moon status", failures);
        expect_true(moon->light_time_days > 0.0, "Moon light-time positive", failures);
        expect_true(moon->distance_au > 0.0, "Moon distance positive", failures);
    }
}

void test_major_body_apparent_rejects_unknown_flags(int* failures) {
    EphemerisService service;
    const int bodies[] = { TAIYIN_BODY_SUN };
    TaiyinApparentOptions options;
    options.flags = 1u << 31;

    MajorBodyApparentBatchRequest request;
    request.jd_tdb = 150.0;
    request.body_ids = bodies;
    request.body_count = sizeof(bodies) / sizeof(bodies[0]);
    request.options = &options;

    MajorBodyApparentBatchResult result;
    EphemerisEvalDiagnostic diagnostic;
    const TaiyinStatus status = eval_major_body_apparent_batch(&service, request, &result, &diagnostic);
    expect_status(status, TAIYIN_ERROR_UNSUPPORTED, "unknown flags status", failures);
    expect_status(result.status, TAIYIN_ERROR_UNSUPPORTED, "unknown flags result status", failures);
    expect_status(diagnostic.status, TAIYIN_ERROR_UNSUPPORTED, "unknown flags diagnostic status", failures);
}

void test_major_body_apparent_reports_per_body_failure(int* failures) {
    EphemerisBlockCatalog catalog;
    EphemerisBlockCache cache(1024 * 1024);
    add_cached_descriptor(
        &catalog,
        &cache,
        make_descriptor(TAIYIN_BODY_EARTH, TAIYIN_BODY_SUN, 300, 0, 300),
        10.0,
        "add failure Earth/Sun descriptor",
        failures);

    EphemerisService service;
    service.set_catalog(&catalog);
    service.set_cache(&cache);

    const int bodies[] = { TAIYIN_BODY_MERCURY_BARYCENTER };
    TaiyinApparentOptions options;
    options.flags = 0;

    MajorBodyApparentBatchRequest request;
    request.jd_tdb = 150.0;
    request.body_ids = bodies;
    request.body_count = sizeof(bodies) / sizeof(bodies[0]);
    request.options = &options;

    MajorBodyApparentBatchResult result;
    EphemerisEvalDiagnostic diagnostic;
    const TaiyinStatus status = eval_major_body_apparent_batch(&service, request, &result, &diagnostic);
    expect_status(status, TAIYIN_EPHEMERIS_ERROR_NO_ROUTE, "missing Mercury batch status", failures);
    expect_size(result.body_count, 1, "missing Mercury result count", failures);
    expect_status(result.bodies[0].status, TAIYIN_EPHEMERIS_ERROR_NO_ROUTE, "missing Mercury per-body status", failures);
    expect_true(result.failed_body_id == TAIYIN_BODY_MERCURY_BARYCENTER, "missing Mercury failed body id", failures);
    expect_true(diagnostic.target_id == TAIYIN_BODY_MERCURY_BARYCENTER, "missing Mercury diagnostic target", failures);
}

void test_global_apparent_options_are_used_when_request_options_null(int* failures) {
    reset_global_apparent_options();
    reset_global_astro_model_context();

    TaiyinApparentOptions global_options;
    global_options.flags = 1u << 31;
    expect_status(
        set_global_apparent_options(global_options),
        TAIYIN_STATUS_OK,
        "set unsupported global apparent options",
        failures);

    EphemerisService service;
    const int bodies[] = { TAIYIN_BODY_SUN };
    MajorBodyApparentBatchRequest request;
    request.jd_tdb = 150.0;
    request.body_ids = bodies;
    request.body_count = sizeof(bodies) / sizeof(bodies[0]);
    request.options = 0;

    MajorBodyApparentBatchResult result;
    EphemerisEvalDiagnostic diagnostic;
    const TaiyinStatus status = eval_major_body_apparent_batch(&service, request, &result, &diagnostic);
    expect_status(status, TAIYIN_ERROR_UNSUPPORTED, "request null options uses global flags", failures);
    expect_status(diagnostic.status, TAIYIN_ERROR_UNSUPPORTED, "global flags diagnostic", failures);

    reset_global_apparent_options();
    reset_global_astro_model_context();
}

void test_model_context_fallback_and_explicit_override(int* failures) {
    reset_global_apparent_options();
    reset_global_astro_model_context();

    EphemerisBlockCatalog catalog;
    EphemerisBlockCache cache(1024 * 1024);
    EphemerisService service;
    setup_earth_sun_service(&catalog, &cache, &service, 400, failures);

    const int bodies[] = { TAIYIN_BODY_SUN };
    TaiyinApparentOptions options;
    options.flags = 0;
    options.model_context = 0;

    MajorBodyApparentBatchRequest request;
    request.jd_tdb = 150.0;
    request.body_ids = bodies;
    request.body_count = sizeof(bodies) / sizeof(bodies[0]);
    request.options = &options;

    TaiyinAstroModelContext bad_global_models;
    bad_global_models.precession_model_id = 999999;
    expect_status(
        set_global_astro_model_context(bad_global_models),
        TAIYIN_STATUS_OK,
        "set bad global astro context",
        failures);

    MajorBodyApparentBatchResult result;
    EphemerisEvalDiagnostic diagnostic;
    TaiyinStatus status = eval_major_body_apparent_batch(&service, request, &result, &diagnostic);
    expect_status(status, TAIYIN_ERROR_UNSUPPORTED, "options null model_context falls back to bad global context", failures);

    TaiyinAstroModelContext explicit_models;
    explicit_models.precession_model_id = dispatch::MODEL_SELECTION_DEFAULT;
    explicit_models.nutation_model_id = dispatch::MODEL_SELECTION_DEFAULT;
    options.model_context = &explicit_models;
    status = eval_major_body_apparent_batch(&service, request, &result, &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "explicit model context overrides bad global context", failures);

    reset_global_apparent_options();
    reset_global_astro_model_context();
}

void test_global_apparent_options_clear_model_context_pointer(int* failures) {
    reset_global_apparent_options();
    reset_global_astro_model_context();

    EphemerisBlockCatalog catalog;
    EphemerisBlockCache cache(1024 * 1024);
    EphemerisService service;
    setup_earth_sun_service(&catalog, &cache, &service, 500, failures);

    TaiyinAstroModelContext bad_models;
    bad_models.precession_model_id = 999999;
    TaiyinApparentOptions global_options;
    global_options.flags = 0;
    global_options.model_context = &bad_models;
    expect_status(
        set_global_apparent_options(global_options),
        TAIYIN_STATUS_OK,
        "set global apparent options with pointer",
        failures);

    TaiyinApparentOptions stored_options = get_global_apparent_options();
    expect_true(stored_options.model_context == 0, "global apparent options clear model_context pointer", failures);

    const int bodies[] = { TAIYIN_BODY_SUN };
    MajorBodyApparentBatchRequest request;
    request.jd_tdb = 150.0;
    request.body_ids = bodies;
    request.body_count = sizeof(bodies) / sizeof(bodies[0]);
    request.options = 0;

    MajorBodyApparentBatchResult result;
    EphemerisEvalDiagnostic diagnostic;
    const TaiyinStatus status = eval_major_body_apparent_batch(&service, request, &result, &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "global apparent options do not keep caller model pointer", failures);

    reset_global_apparent_options();
    reset_global_astro_model_context();
}

}  // namespace

int main() {
    int failures = 0;
    test_major_body_apparent_geometric_batch(&failures);
    test_major_body_apparent_light_time_and_composite_moon(&failures);
    test_major_body_apparent_rejects_unknown_flags(&failures);
    test_major_body_apparent_reports_per_body_failure(&failures);
    test_global_apparent_options_are_used_when_request_options_null(&failures);
    test_model_context_fallback_and_explicit_override(&failures);
    test_global_apparent_options_clear_model_context_pointer(&failures);

    if (failures != 0) {
        std::cerr << failures << " major body apparent test(s) failed\n";
        return 1;
    }
    std::cout << "test_major_body_apparent: ALL TESTS PASSED\n";
    return 0;
}
