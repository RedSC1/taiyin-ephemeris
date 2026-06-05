#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/coordinates.h"
#include "taiyin/dispatch.h"
#include "taiyin/runtime/major_body_apparent.h"
#include "taiyin/runtime/taiyin_runtime.h"
#include "taiyin/vector3.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

using namespace taiyin;
using namespace taiyin::internal;
using namespace taiyin::runtime;

struct TestEphemerisData {
    double base;
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

std::vector<uint8_t> make_test_opm4_file_bytes(
    int target_id,
    int center_id,
    int method_id,
    double quant_unit_km
) {
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
    push_f64_le(&data, quant_unit_km);
    while (data.size() < 128) {
        push_u8(&data, 0);
    }
    data.insert(data.end(), payload.begin(), payload.end());
    return data;
}

bool write_test_opm4_file(
    const char* path,
    int target_id,
    int center_id,
    int method_id,
    double quant_unit_km
) {
    const std::vector<uint8_t> bytes = make_test_opm4_file_bytes(
        target_id,
        center_id,
        method_id,
        quant_unit_km);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

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

Matrix3x3 matrix_from_array(const double values[9]) {
    Matrix3x3 matrix = matrix3x3_identity();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            matrix.m[row][col] = values[row * 3 + col];
        }
    }
    return matrix;
}

void expect_spherical_from_apparent_matrix(
    const Vector3& icrf_position,
    const MajorBodyApparentBatchRequest& request,
    const ApparentOptions& options,
    const AstroModelContext& models,
    double actual_lon,
    double actual_lat,
    double actual_distance,
    const char* label,
    int* failures
) {
    dispatch::PrecessionModelEntry precession;
    dispatch::NutationModelEntry nutation;
    expect_true(dispatch::select_precession_model(models.precession_model_id, &precession), "select expected precession", failures);
    expect_true(dispatch::select_nutation_model(models.nutation_model_id, &nutation), "select expected nutation", failures);

    double output_matrix_values[9];
    const double jd_tt = std::isfinite(request.jd_tt) && request.jd_tt != 0.0
        ? request.jd_tt
        : request.jd_tdb;
    expect_true(
        taiyin_calc_apparent_matrices_flat(
            jd_tt,
            options.flags,
            options.output_frame_id,
            precession.model_id,
            nutation.model_id,
            models.obliquity_model_id,
            options.matrix_derivative_step_days,
            0,
            0,
            output_matrix_values,
            0,
            0,
            0,
            0,
            0,
            0),
        "calculate expected apparent output matrix",
        failures);
    const Vector3 output_position = transform_position_with_matrix(
        icrf_position,
        matrix_from_array(output_matrix_values));
    double expected_lon = 0.0;
    double expected_lat = 0.0;
    double expected_distance = 0.0;
    cartesian_to_spherical(output_position, &expected_lon, &expected_lat, &expected_distance);
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
    AstroModelContext models;
    ApparentOptions options;
    options.flags = 0;
    options.model_context = &models;

    MajorBodyApparentBatchRequest request;
    request.jd_tdb = 150.0;
    request.jd_tt = 150.25;
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
        expect_spherical_from_apparent_matrix(
            expected,
            request,
            options,
            models,
            sun->longitude_rad,
            sun->latitude_rad,
            sun->distance_au,
            "Sun apparent spherical",
            failures);
    }
    if (mercury) {
        expect_status(mercury->status, TAIYIN_STATUS_OK, "Mercury status", failures);
        expect_near(mercury->geometric_state.position_au.x, 90.0, 1.0e-12, "Mercury geometric x", failures);
        expect_near(mercury->geometric_state.position_au.y, 90.0, 1.0e-12, "Mercury geometric y", failures);
        Vector3 expected;
        expected.x = 90.0;
        expected.y = 90.0;
        expected.z = 90.0;
        expect_spherical_from_apparent_matrix(
            expected,
            request,
            options,
            models,
            mercury->longitude_rad,
            mercury->latitude_rad,
            mercury->distance_au,
            "Mercury apparent spherical",
            failures);
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
    ApparentOptions options;
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
    ApparentOptions options;
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
    ApparentOptions options;
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

    ApparentOptions global_options;
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
    ApparentOptions options;
    options.flags = 0;
    options.model_context = 0;

    MajorBodyApparentBatchRequest request;
    request.jd_tdb = 150.0;
    request.body_ids = bodies;
    request.body_count = sizeof(bodies) / sizeof(bodies[0]);
    request.options = &options;

    AstroModelContext bad_global_models;
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

    AstroModelContext explicit_models;
    explicit_models.precession_model_id = dispatch::MODEL_SELECTION_DEFAULT;
    explicit_models.nutation_model_id = dispatch::MODEL_SELECTION_DEFAULT;
    options.model_context = &explicit_models;
    status = eval_major_body_apparent_batch(&service, request, &result, &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "explicit model context overrides bad global context", failures);

    reset_global_apparent_options();
    reset_global_astro_model_context();
}

void test_output_frame_id_changes_spherical_output(int* failures) {
    reset_global_apparent_options();
    reset_global_astro_model_context();

    EphemerisBlockCatalog catalog;
    EphemerisBlockCache cache(1024 * 1024);
    EphemerisService service;
    setup_earth_sun_service(&catalog, &cache, &service, 450, failures);

    const int bodies[] = { TAIYIN_BODY_SUN };
    MajorBodyApparentBatchRequest request;
    request.jd_tdb = 150.0;
    request.jd_tt = 150.25;
    request.body_ids = bodies;
    request.body_count = sizeof(bodies) / sizeof(bodies[0]);

    ApparentOptions icrf_options;
    icrf_options.flags = 0;
    icrf_options.output_frame_id = TAIYIN_APPARENT_FRAME_ICRF;
    request.options = &icrf_options;

    MajorBodyApparentBatchResult icrf_result;
    TaiyinStatus status = eval_major_body_apparent_batch(&service, request, &icrf_result, 0);
    expect_status(status, TAIYIN_STATUS_OK, "ICRF output frame status", failures);
    expect_size(icrf_result.body_count, 1, "ICRF output frame count", failures);

    Vector3 expected_icrf;
    expected_icrf.x = -160.0;
    expected_icrf.y = -12.0;
    expected_icrf.z = -13.0;
    double expected_lon = 0.0;
    double expected_lat = 0.0;
    double expected_distance = 0.0;
    cartesian_to_spherical(expected_icrf, &expected_lon, &expected_lat, &expected_distance);
    expect_near(icrf_result.bodies[0].longitude_rad, expected_lon, 1.0e-12, "ICRF longitude", failures);
    expect_near(icrf_result.bodies[0].latitude_rad, expected_lat, 1.0e-12, "ICRF latitude", failures);
    expect_near(icrf_result.bodies[0].distance_au, expected_distance, 1.0e-12, "ICRF distance", failures);

    AstroModelContext models;
    ApparentOptions ecliptic_options;
    ecliptic_options.flags = 0;
    ecliptic_options.output_frame_id = TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    ecliptic_options.model_context = &models;
    request.options = &ecliptic_options;

    MajorBodyApparentBatchResult ecliptic_result;
    status = eval_major_body_apparent_batch(&service, request, &ecliptic_result, 0);
    expect_status(status, TAIYIN_STATUS_OK, "true ecliptic output frame status", failures);
    expect_spherical_from_apparent_matrix(
        expected_icrf,
        request,
        ecliptic_options,
        models,
        ecliptic_result.bodies[0].longitude_rad,
        ecliptic_result.bodies[0].latitude_rad,
        ecliptic_result.bodies[0].distance_au,
        "true ecliptic output frame spherical",
        failures);
    expect_true(
        std::fabs(ecliptic_result.bodies[0].latitude_rad - icrf_result.bodies[0].latitude_rad) > 1.0e-6,
        "output frame changes latitude",
        failures);

    reset_global_apparent_options();
    reset_global_astro_model_context();
}

void test_major_body_apparent_velocity_and_acceleration_flags(int* failures) {
    EphemerisBlockCatalog catalog;
    EphemerisBlockCache cache(1024 * 1024);
    EphemerisService service;
    setup_earth_sun_service(&catalog, &cache, &service, 425, failures);

    const int bodies[] = { TAIYIN_BODY_SUN };
    ApparentOptions options;
    options.flags = TAIYIN_APPARENT_VELOCITY | TAIYIN_APPARENT_ACCELERATION;
    options.output_frame_id = TAIYIN_APPARENT_FRAME_ICRF;

    MajorBodyApparentBatchRequest request;
    request.jd_tdb = 150.0;
    request.body_ids = bodies;
    request.body_count = sizeof(bodies) / sizeof(bodies[0]);
    request.options = &options;

    MajorBodyApparentBatchResult result;
    const TaiyinStatus status = eval_major_body_apparent_batch(&service, request, &result, 0);
    expect_status(status, TAIYIN_STATUS_OK, "velocity and acceleration flags status", failures);
    expect_size(result.body_count, 1, "velocity and acceleration result count", failures);
    expect_near(result.bodies[0].geometric_state.velocity_au_per_day.x, -14.0, 1.0e-12, "geometric velocity x", failures);
    expect_near(result.bodies[0].geometric_state.acceleration_au_per_day2.x, -17.0, 1.0e-12, "geometric acceleration x", failures);
    expect_near(result.bodies[0].apparent_state.velocity_au_per_day.x, -14.0, 1.0e-12, "apparent velocity x", failures);
    expect_near(result.bodies[0].apparent_state.acceleration_au_per_day2.x, -17.0, 1.0e-12, "apparent acceleration x", failures);
}

void test_major_body_apparent_aberration_and_deflection_flags(int* failures) {
    EphemerisBlockCatalog catalog;
    EphemerisBlockCache cache(1024 * 1024);

    add_cached_descriptor(
        &catalog,
        &cache,
        make_descriptor(TAIYIN_BODY_EARTH, TAIYIN_BODY_SUN, 475, 0, 475),
        10.0,
        "add aberration Earth/Sun descriptor",
        failures);
    add_cached_descriptor(
        &catalog,
        &cache,
        make_descriptor(TAIYIN_BODY_MERCURY_BARYCENTER, TAIYIN_BODY_SUN, 475, 1, 476),
        100.0,
        "add aberration Mercury/Sun descriptor",
        failures);

    EphemerisService service;
    service.set_catalog(&catalog);
    service.set_cache(&cache);

    ApparentDeflector deflectors[1];
    deflectors[0].body_id = TAIYIN_BODY_SUN;
    deflectors[0].schwarzschild_radius_au = 1.0e-8;
    deflectors[0].limit = 0.0;

    const int bodies[] = { TAIYIN_BODY_MERCURY_BARYCENTER };
    ApparentOptions options;
    options.flags = TAIYIN_APPARENT_ABERRATION | TAIYIN_APPARENT_DEFLECTION;
    options.output_frame_id = TAIYIN_APPARENT_FRAME_ICRF;
    options.deflectors = deflectors;
    options.deflector_count = 1;
    options.solar_deflector_index = 0;

    MajorBodyApparentBatchRequest request;
    request.jd_tdb = 150.0;
    request.body_ids = bodies;
    request.body_count = sizeof(bodies) / sizeof(bodies[0]);
    request.options = &options;

    MajorBodyApparentBatchResult result;
    const TaiyinStatus status = eval_major_body_apparent_batch(&service, request, &result, 0);
    expect_status(status, TAIYIN_STATUS_OK, "aberration and deflection flags status", failures);
    expect_size(result.body_count, 1, "aberration and deflection result count", failures);
    expect_true(result.bodies[0].distance_au > 0.0, "aberration and deflection distance positive", failures);
}

void test_major_body_apparent_requires_deflectors_for_aberration(int* failures) {
    EphemerisBlockCatalog catalog;
    EphemerisBlockCache cache(1024 * 1024);
    EphemerisService service;
    setup_earth_sun_service(&catalog, &cache, &service, 485, failures);

    const int bodies[] = { TAIYIN_BODY_SUN };
    ApparentOptions options;
    options.flags = TAIYIN_APPARENT_ABERRATION;
    options.output_frame_id = TAIYIN_APPARENT_FRAME_ICRF;

    MajorBodyApparentBatchRequest request;
    request.jd_tdb = 150.0;
    request.body_ids = bodies;
    request.body_count = sizeof(bodies) / sizeof(bodies[0]);
    request.options = &options;

    MajorBodyApparentBatchResult result;
    EphemerisEvalDiagnostic diagnostic;
    const TaiyinStatus status = eval_major_body_apparent_batch(&service, request, &result, &diagnostic);
    expect_status(status, TAIYIN_ERROR_INVALID_ARGUMENT, "aberration requires deflectors status", failures);
    expect_status(diagnostic.status, TAIYIN_ERROR_INVALID_ARGUMENT, "aberration requires deflectors diagnostic", failures);
}

void test_global_apparent_deflector_arrays(int* failures) {
    reset_global_apparent_options();
    reset_global_astro_model_context();
    reset_global_apparent_deflectors();

    ApparentDeflector deflectors[2];
    deflectors[0].body_id = TAIYIN_BODY_SUN;
    deflectors[0].schwarzschild_radius_au = 1.0e-8;
    deflectors[0].limit = 0.25;
    deflectors[1].body_id = TAIYIN_BODY_JUPITER_BARYCENTER;
    deflectors[1].schwarzschild_radius_au = 1.0e-11;
    deflectors[1].limit = 0.5;

    expect_status(
        set_global_apparent_deflectors(deflectors, 2, 0),
        TAIYIN_STATUS_OK,
        "set global apparent deflectors",
        failures);
    expect_size(get_global_apparent_deflector_count(), 2, "global apparent deflector count", failures);

    ApparentDeflector copied[2];
    int solar_index = -2;
    const size_t required = get_global_apparent_deflectors(copied, 2, &solar_index);
    expect_size(required, 2, "get global apparent deflectors required count", failures);
    expect_true(solar_index == 0, "global apparent deflector solar index", failures);
    expect_true(copied[0].body_id == TAIYIN_BODY_SUN, "copied Sun deflector id", failures);
    expect_near(copied[0].schwarzschild_radius_au, 1.0e-8, 0.0, "copied Sun deflector radius", failures);
    expect_near(copied[0].limit, 0.25, 0.0, "copied Sun deflector limit", failures);
    expect_true(copied[1].body_id == TAIYIN_BODY_JUPITER_BARYCENTER, "copied Jupiter deflector id", failures);

    expect_status(
        set_global_apparent_deflectors(0, 1, -1),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject null global deflector array with count",
        failures);
    expect_status(
        set_global_apparent_deflectors(deflectors, 2, 2),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject out-of-range solar deflector index",
        failures);

    reset_global_apparent_deflectors();
    expect_size(get_global_apparent_deflector_count(), 0, "reset global apparent deflectors", failures);
    reset_global_apparent_options();
    reset_global_astro_model_context();
}

void test_explicit_deflector_array_validation(int* failures) {
    reset_global_apparent_options();
    reset_global_astro_model_context();
    reset_global_apparent_deflectors();

    EphemerisService service;
    const int bodies[] = { TAIYIN_BODY_SUN };
    ApparentOptions options;
    options.flags = 0;
    options.deflectors = 0;
    options.deflector_count = 1;
    options.solar_deflector_index = -1;

    MajorBodyApparentBatchRequest request;
    request.jd_tdb = 150.0;
    request.body_ids = bodies;
    request.body_count = sizeof(bodies) / sizeof(bodies[0]);
    request.options = &options;

    MajorBodyApparentBatchResult result;
    EphemerisEvalDiagnostic diagnostic;
    const TaiyinStatus status = eval_major_body_apparent_batch(&service, request, &result, &diagnostic);
    expect_status(status, TAIYIN_ERROR_INVALID_ARGUMENT, "explicit deflector array requires pointer", failures);
    expect_status(diagnostic.status, TAIYIN_ERROR_INVALID_ARGUMENT, "explicit deflector array diagnostic", failures);

    reset_global_apparent_options();
    reset_global_astro_model_context();
    reset_global_apparent_deflectors();
}

void test_major_body_apparent_global_opm4_end_to_end(int* failures) {
    reset_global_apparent_options();
    reset_global_astro_model_context();
    reset_global_apparent_deflectors();

    const char* earth_path = "test_major_body_apparent_e2e_earth.opm4";
    const char* mars_path = "test_major_body_apparent_e2e_mars.opm4";
    std::remove(earth_path);
    std::remove(mars_path);

    expect_true(
        write_test_opm4_file(earth_path, TAIYIN_BODY_EARTH, TAIYIN_BODY_SUN, 1, 1000.0),
        "write end-to-end Earth OPM4 fixture",
        failures);
    expect_true(
        write_test_opm4_file(mars_path, TAIYIN_BODY_MARS_BARYCENTER, TAIYIN_BODY_SUN, 1, 2000.0),
        "write end-to-end Mars OPM4 fixture",
        failures);

    const char* paths[] = { earth_path, mars_path };
    EphemerisRuntimeConfig runtime_config;
    runtime_config.cache_max_bytes = 1024 * 1024;
    runtime_config.source_paths = paths;
    runtime_config.source_path_count = sizeof(paths) / sizeof(paths[0]);
    const bool runtime_ok = initialize_global_ephemeris_runtime(runtime_config);
    expect_true(runtime_ok, "initialize global runtime from end-to-end OPM4 fixtures", failures);
    expect_size(global_ephemeris_catalog_size(), 2, "end-to-end global catalog descriptor count", failures);

    const int bodies[] = { TAIYIN_BODY_MARS_BARYCENTER };
    ApparentOptions options;
    options.flags = TAIYIN_APPARENT_SPHERICAL | TAIYIN_APPARENT_VELOCITY | TAIYIN_APPARENT_ACCELERATION;
    options.output_frame_id = TAIYIN_APPARENT_FRAME_ICRF;

    MajorBodyApparentBatchRequest request;
    request.jd_tdb = 105.0;
    request.jd_tt = 105.0;
    request.observer_id = TAIYIN_BODY_EARTH;
    request.center_id = TAIYIN_BODY_SUN;
    request.body_ids = bodies;
    request.body_count = sizeof(bodies) / sizeof(bodies[0]);
    request.options = &options;

    MajorBodyApparentBatchResult result;
    EphemerisEvalDiagnostic diagnostic;
    const TaiyinStatus status = eval_global_major_body_apparent_batch(request, &result, &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "global OPM4 major body apparent status", failures);
    expect_size(result.body_count, 1, "global OPM4 major body apparent result count", failures);

    MajorBodyApparentPosition* mars = find_body(&result, TAIYIN_BODY_MARS_BARYCENTER);
    expect_true(mars != 0, "global OPM4 Mars result exists", failures);
    if (runtime_ok && status == TAIYIN_STATUS_OK && mars) {
        EphemerisRequest earth_request;
        earth_request.target_id = TAIYIN_BODY_EARTH;
        earth_request.center_id = TAIYIN_BODY_SUN;
        earth_request.frame = EphemerisFrame::IcrfJ2000Equatorial;
        earth_request.jd_tdb = request.jd_tdb;
        EphemerisRequest mars_request;
        mars_request.target_id = TAIYIN_BODY_MARS_BARYCENTER;
        mars_request.center_id = TAIYIN_BODY_SUN;
        mars_request.frame = EphemerisFrame::IcrfJ2000Equatorial;
        mars_request.jd_tdb = request.jd_tdb;

        EphemerisResult earth_state;
        EphemerisResult mars_state;
        expect_status(
            eval_global_ephemeris_state(earth_request, &earth_state, 0),
            TAIYIN_STATUS_OK,
            "global OPM4 direct Earth eval for expected state",
            failures);
        expect_status(
            eval_global_ephemeris_state(mars_request, &mars_state, 0),
            TAIYIN_STATUS_OK,
            "global OPM4 direct Mars eval for expected state",
            failures);

        const CartesianState expected = cartesian_state_subtract(mars_state.state, earth_state.state);
        expect_near(mars->geometric_state.position_au.x, expected.position_au.x, 1.0e-18, "global OPM4 geometric position x", failures);
        expect_near(mars->geometric_state.position_au.y, expected.position_au.y, 1.0e-18, "global OPM4 geometric position y", failures);
        expect_near(mars->geometric_state.position_au.z, expected.position_au.z, 1.0e-18, "global OPM4 geometric position z", failures);
        expect_near(mars->geometric_state.velocity_au_per_day.x, expected.velocity_au_per_day.x, 1.0e-18, "global OPM4 geometric velocity x", failures);
        expect_near(mars->geometric_state.acceleration_au_per_day2.x, expected.acceleration_au_per_day2.x, 1.0e-18, "global OPM4 geometric acceleration x", failures);
        expect_near(mars->apparent_state.position_au.x, expected.position_au.x, 1.0e-18, "global OPM4 apparent position x", failures);
        expect_near(mars->apparent_state.position_au.y, expected.position_au.y, 1.0e-18, "global OPM4 apparent position y", failures);
        expect_near(mars->apparent_state.position_au.z, expected.position_au.z, 1.0e-18, "global OPM4 apparent position z", failures);

        double expected_lon = 0.0;
        double expected_lat = 0.0;
        double expected_distance = 0.0;
        cartesian_to_spherical(expected.position_au, &expected_lon, &expected_lat, &expected_distance);
        expect_near(mars->longitude_rad, expected_lon, 1.0e-12, "global OPM4 longitude", failures);
        expect_near(mars->latitude_rad, expected_lat, 1.0e-12, "global OPM4 latitude", failures);
        expect_near(mars->distance_au, expected_distance, 1.0e-18, "global OPM4 distance", failures);
        expect_true(global_ephemeris_cache_entry_count() >= 2, "global OPM4 apparent path lazy-loaded cache", failures);
    }

    std::remove(earth_path);
    std::remove(mars_path);
    EphemerisRuntimeConfig empty_config;
    expect_true(initialize_global_ephemeris_runtime(empty_config), "reset global runtime after OPM4 end-to-end test", failures);
    reset_global_apparent_options();
    reset_global_astro_model_context();
    reset_global_apparent_deflectors();
}

void test_global_apparent_options_clear_model_context_pointer(int* failures) {
    reset_global_apparent_options();
    reset_global_astro_model_context();

    EphemerisBlockCatalog catalog;
    EphemerisBlockCache cache(1024 * 1024);
    EphemerisService service;
    setup_earth_sun_service(&catalog, &cache, &service, 500, failures);

    AstroModelContext bad_models;
    bad_models.precession_model_id = 999999;
    ApparentDeflector deflectors[1];
    deflectors[0].body_id = TAIYIN_BODY_SUN;
    deflectors[0].schwarzschild_radius_au = 1.0e-8;
    deflectors[0].limit = 0.1;

    ApparentOptions global_options;
    global_options.flags = 0;
    global_options.model_context = &bad_models;
    global_options.deflectors = deflectors;
    global_options.deflector_count = 1;
    global_options.solar_deflector_index = 0;
    expect_status(
        set_global_apparent_options(global_options),
        TAIYIN_STATUS_OK,
        "set global apparent options with pointer",
        failures);

    ApparentOptions stored_options = get_global_apparent_options();
    expect_true(stored_options.model_context == 0, "global apparent options clear model_context pointer", failures);
    expect_true(stored_options.deflectors == 0, "global apparent options clear deflectors pointer", failures);
    expect_size(stored_options.deflector_count, 0, "global apparent options clear deflector count", failures);
    expect_true(stored_options.solar_deflector_index == -1, "global apparent options clear solar deflector index", failures);

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
    test_output_frame_id_changes_spherical_output(&failures);
    test_major_body_apparent_velocity_and_acceleration_flags(&failures);
    test_major_body_apparent_aberration_and_deflection_flags(&failures);
    test_major_body_apparent_requires_deflectors_for_aberration(&failures);
    test_global_apparent_deflector_arrays(&failures);
    test_explicit_deflector_array_validation(&failures);
    test_major_body_apparent_global_opm4_end_to_end(&failures);
    test_global_apparent_options_clear_model_context_pointer(&failures);

    if (failures != 0) {
        std::cerr << failures << " major body apparent test(s) failed\n";
        return 1;
    }
    std::cout << "test_major_body_apparent: ALL TESTS PASSED\n";
    return 0;
}
