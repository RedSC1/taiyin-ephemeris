#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/ephemeris_route_rule.h"
#include "taiyin/internal/opm2.h"
#include "taiyin/runtime/event_search.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/phenomena.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/runtime/star_position.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

const double JD_UT_NEAR_2024_EQUINOX = 2460380.5;
const double JD_UT_MARS_2003_OPPOSITION_SEDS = 2452880.249178241;
const int METHOD_EVENT_SEARCH_FIXED_ZERO = 970001;
const int METHOD_EVENT_SEARCH_TANGENT = 970002;
const int METHOD_EVENT_SEARCH_STATION = 970003;

struct SyntheticAspectBodyData {
    double station_jd_tdb;
    double station_longitude_rad;
    double curvature_rad_per_day2;
};

taiyin::SplitJulianDate split_jd(double jd) {
    taiyin::SplitJulianDate result;
    if (!taiyin::split_julian_date_from_double(jd, &result)) {
        result.day_fraction = NAN;
    }
    return result;
}

typedef taiyin::Status (*SingleLongitudeSearch)(
    const taiyin::runtime::NativeCalcContext*,
    double,
    taiyin::SplitJulianDate,
    uint64_t,
    taiyin::SplitJulianDate*,
    taiyin::runtime::EphemerisEvalDiagnostic*);

taiyin::Status search_single_longitude_for_test(
    SingleLongitudeSearch search,
    const taiyin::runtime::NativeCalcContext* context,
    double target_longitude_rad,
    double estimate_jd,
    uint64_t flags,
    double* out_jd,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    taiyin::SplitJulianDate result;
    const taiyin::Status status = search(
        context, target_longitude_rad, split_jd(estimate_jd), flags, &result, diagnostic);
    if (out_jd) {
        *out_jd = taiyin::split_julian_date_to_double(result);
    }
    return status;
}

taiyin::Status search_solar_longitude_ut(
    const taiyin::runtime::NativeCalcContext* context,
    double target_longitude_rad,
    double estimate_jd_ut,
    uint64_t flags,
    double* out_jd_ut,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    return search_single_longitude_for_test(
        taiyin::runtime::search_solar_longitude_ut,
        context, target_longitude_rad, estimate_jd_ut, flags, out_jd_ut, diagnostic);
}

taiyin::Status search_solar_longitude_tt(
    const taiyin::runtime::NativeCalcContext* context,
    double target_longitude_rad,
    double estimate_jd_tt,
    uint64_t flags,
    double* out_jd_tt,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    return search_single_longitude_for_test(
        taiyin::runtime::search_solar_longitude_tt,
        context, target_longitude_rad, estimate_jd_tt, flags, out_jd_tt, diagnostic);
}

taiyin::Status search_moon_longitude_ut(
    const taiyin::runtime::NativeCalcContext* context,
    double target_longitude_rad,
    double estimate_jd_ut,
    uint64_t flags,
    double* out_jd_ut,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    return search_single_longitude_for_test(
        taiyin::runtime::search_moon_longitude_ut,
        context, target_longitude_rad, estimate_jd_ut, flags, out_jd_ut, diagnostic);
}

taiyin::Status search_moon_longitude_tt(
    const taiyin::runtime::NativeCalcContext* context,
    double target_longitude_rad,
    double estimate_jd_tt,
    uint64_t flags,
    double* out_jd_tt,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    return search_single_longitude_for_test(
        taiyin::runtime::search_moon_longitude_tt,
        context, target_longitude_rad, estimate_jd_tt, flags, out_jd_tt, diagnostic);
}

taiyin::Status search_body_longitude_crossings_ut(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double target_longitude_rad,
    double start_jd_ut,
    double end_jd_ut,
    double max_step_days,
    uint64_t flags,
    double* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    std::vector<taiyin::SplitJulianDate> events(max_event_count);
    const taiyin::Status status = taiyin::runtime::search_body_longitude_crossings_ut(
        context,
        body_id,
        target_longitude_rad,
        split_jd(start_jd_ut),
        split_jd(end_jd_ut),
        max_step_days,
        flags,
        max_event_count == 0 ? nullptr : &events[0],
        max_event_count,
        out_event_count,
        diagnostic);
    if (out_jd_ut && out_event_count) {
        const size_t count = *out_event_count < max_event_count ? *out_event_count : max_event_count;
        for (size_t i = 0; i < count; ++i) {
            out_jd_ut[i] = taiyin::split_julian_date_to_double(events[i]);
        }
    }
    return status;
}

void copy_event_dates_for_test(
    const std::vector<taiyin::SplitJulianDate>& events,
    double* out_jd,
    size_t max_event_count,
    const size_t* event_count
) {
    if (!out_jd || !event_count) return;
    const size_t count = *event_count < max_event_count ? *event_count : max_event_count;
    for (size_t i = 0; i < count; ++i) {
        out_jd[i] = taiyin::split_julian_date_to_double(events[i]);
    }
}

taiyin::Status search_body_longitude_crossings_auto_step_ut(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double target_longitude_rad,
    double start_jd_ut,
    double end_jd_ut,
    uint64_t flags,
    double* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    std::vector<taiyin::SplitJulianDate> events(max_event_count);
    const taiyin::Status status = taiyin::runtime::search_body_longitude_crossings_auto_step_ut(
        context, body_id, target_longitude_rad, split_jd(start_jd_ut), split_jd(end_jd_ut),
        flags, max_event_count == 0 ? nullptr : &events[0], max_event_count,
        out_event_count, diagnostic);
    copy_event_dates_for_test(events, out_jd_ut, max_event_count, out_event_count);
    return status;
}

taiyin::Status search_body_aspect_crossings_ut(
    const taiyin::runtime::NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double aspect_rad,
    double start_jd_ut,
    double end_jd_ut,
    double max_step_days,
    uint64_t flags,
    double* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    std::vector<taiyin::SplitJulianDate> events(max_event_count);
    const taiyin::Status status = taiyin::runtime::search_body_aspect_crossings_ut(
        context, body_a_id, body_b_id, aspect_rad, split_jd(start_jd_ut), split_jd(end_jd_ut),
        max_step_days, flags, max_event_count == 0 ? nullptr : &events[0], max_event_count,
        out_event_count, diagnostic);
    copy_event_dates_for_test(events, out_jd_ut, max_event_count, out_event_count);
    return status;
}

taiyin::Status search_body_aspect_crossings_auto_step_ut(
    const taiyin::runtime::NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double aspect_rad,
    double start_jd_ut,
    double end_jd_ut,
    uint64_t flags,
    double* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    std::vector<taiyin::SplitJulianDate> events(max_event_count);
    const taiyin::Status status = taiyin::runtime::search_body_aspect_crossings_auto_step_ut(
        context, body_a_id, body_b_id, aspect_rad, split_jd(start_jd_ut), split_jd(end_jd_ut),
        flags, max_event_count == 0 ? nullptr : &events[0], max_event_count,
        out_event_count, diagnostic);
    copy_event_dates_for_test(events, out_jd_ut, max_event_count, out_event_count);
    return status;
}

taiyin::Status search_lunar_phase_crossings_ut(
    const taiyin::runtime::NativeCalcContext* context,
    double phase_rad,
    double start_jd_ut,
    double end_jd_ut,
    double max_step_days,
    uint64_t flags,
    double* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    std::vector<taiyin::SplitJulianDate> events(max_event_count);
    const taiyin::Status status = taiyin::runtime::search_lunar_phase_crossings_ut(
        context, phase_rad, split_jd(start_jd_ut), split_jd(end_jd_ut), max_step_days,
        flags, max_event_count == 0 ? nullptr : &events[0], max_event_count,
        out_event_count, diagnostic);
    copy_event_dates_for_test(events, out_jd_ut, max_event_count, out_event_count);
    return status;
}

taiyin::Status search_lunar_phase_crossings_default_step_ut(
    const taiyin::runtime::NativeCalcContext* context,
    double phase_rad,
    double start_jd_ut,
    double end_jd_ut,
    uint64_t flags,
    double* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    std::vector<taiyin::SplitJulianDate> events(max_event_count);
    const taiyin::Status status = taiyin::runtime::search_lunar_phase_crossings_default_step_ut(
        context, phase_rad, split_jd(start_jd_ut), split_jd(end_jd_ut), flags,
        max_event_count == 0 ? nullptr : &events[0], max_event_count,
        out_event_count, diagnostic);
    copy_event_dates_for_test(events, out_jd_ut, max_event_count, out_event_count);
    return status;
}

taiyin::Status search_lunar_phase_crossings_tt(
    const taiyin::runtime::NativeCalcContext* context,
    double phase_rad,
    double start_jd_tt,
    double end_jd_tt,
    double max_step_days,
    uint64_t flags,
    double* out_jd_tt,
    size_t max_event_count,
    size_t* out_event_count,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    std::vector<taiyin::SplitJulianDate> events(max_event_count);
    const taiyin::Status status = taiyin::runtime::search_lunar_phase_crossings_tt(
        context, phase_rad, split_jd(start_jd_tt), split_jd(end_jd_tt), max_step_days,
        flags, max_event_count == 0 ? nullptr : &events[0], max_event_count,
        out_event_count, diagnostic);
    copy_event_dates_for_test(events, out_jd_tt, max_event_count, out_event_count);
    return status;
}

taiyin::Status search_body_exact_aspects_ut(
    const taiyin::runtime::NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    const double* aspect_separations_rad,
    size_t aspect_count,
    double start_jd_ut,
    double end_jd_ut,
    double max_step_days,
    uint64_t flags,
    double* out_jd_ut,
    double* out_target_aspect_rad,
    size_t max_event_count,
    size_t* out_event_count,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    std::vector<taiyin::SplitJulianDate> events(max_event_count);
    const taiyin::Status status = taiyin::runtime::search_body_exact_aspects_ut(
        context, body_a_id, body_b_id, aspect_separations_rad, aspect_count,
        split_jd(start_jd_ut), split_jd(end_jd_ut), max_step_days, flags,
        max_event_count == 0 ? nullptr : &events[0], out_target_aspect_rad,
        max_event_count, out_event_count, diagnostic);
    copy_event_dates_for_test(events, out_jd_ut, max_event_count, out_event_count);
    return status;
}

taiyin::Status search_body_exact_aspects_auto_step_ut(
    const taiyin::runtime::NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    const double* aspect_separations_rad,
    size_t aspect_count,
    double start_jd_ut,
    double end_jd_ut,
    uint64_t flags,
    double* out_jd_ut,
    double* out_target_aspect_rad,
    size_t max_event_count,
    size_t* out_event_count,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    std::vector<taiyin::SplitJulianDate> events(max_event_count);
    const taiyin::Status status = taiyin::runtime::search_body_exact_aspects_auto_step_ut(
        context, body_a_id, body_b_id, aspect_separations_rad, aspect_count,
        split_jd(start_jd_ut), split_jd(end_jd_ut), flags,
        max_event_count == 0 ? nullptr : &events[0], out_target_aspect_rad,
        max_event_count, out_event_count, diagnostic);
    copy_event_dates_for_test(events, out_jd_ut, max_event_count, out_event_count);
    return status;
}

taiyin::Status search_body_longitude_stations_ut(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double start_jd_ut,
    double end_jd_ut,
    double max_step_days,
    uint64_t flags,
    double* out_jd_ut,
    double* out_longitude_rad,
    size_t max_event_count,
    size_t* out_event_count,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    std::vector<taiyin::SplitJulianDate> events(max_event_count);
    const taiyin::Status status = taiyin::runtime::search_body_longitude_stations_ut(
        context, body_id, split_jd(start_jd_ut), split_jd(end_jd_ut), max_step_days, flags,
        max_event_count == 0 ? nullptr : &events[0], out_longitude_rad,
        max_event_count, out_event_count, diagnostic);
    copy_event_dates_for_test(events, out_jd_ut, max_event_count, out_event_count);
    return status;
}

taiyin::Status search_body_longitude_stations_auto_step_ut(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double start_jd_ut,
    double end_jd_ut,
    uint64_t flags,
    double* out_jd_ut,
    double* out_longitude_rad,
    size_t max_event_count,
    size_t* out_event_count,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    std::vector<taiyin::SplitJulianDate> events(max_event_count);
    const taiyin::Status status = taiyin::runtime::search_body_longitude_stations_auto_step_ut(
        context, body_id, split_jd(start_jd_ut), split_jd(end_jd_ut), flags,
        max_event_count == 0 ? nullptr : &events[0], out_longitude_rad,
        max_event_count, out_event_count, diagnostic);
    copy_event_dates_for_test(events, out_jd_ut, max_event_count, out_event_count);
    return status;
}

taiyin::Status search_body_longitude_stations_tt(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double start_jd_tt,
    double end_jd_tt,
    double max_step_days,
    uint64_t flags,
    double* out_jd_tt,
    double* out_longitude_rad,
    size_t max_event_count,
    size_t* out_event_count,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    std::vector<taiyin::SplitJulianDate> events(max_event_count);
    const taiyin::Status status = taiyin::runtime::search_body_longitude_stations_tt(
        context, body_id, split_jd(start_jd_tt), split_jd(end_jd_tt), max_step_days, flags,
        max_event_count == 0 ? nullptr : &events[0], out_longitude_rad,
        max_event_count, out_event_count, diagnostic);
    copy_event_dates_for_test(events, out_jd_tt, max_event_count, out_event_count);
    return status;
}

taiyin::Status search_greatest_elongation_ut(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double start_jd_ut,
    double end_jd_ut,
    uint64_t flags,
    taiyin::runtime::GreatestElongationSearchResult* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    return taiyin::runtime::search_greatest_elongation_ut(
        context, body_id, split_jd(start_jd_ut), split_jd(end_jd_ut), flags, out, diagnostic);
}

taiyin::Status search_minimum_angular_separation_ut(
    const taiyin::runtime::NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double start_jd_ut,
    double end_jd_ut,
    double max_step_days,
    uint64_t flags,
    taiyin::runtime::AngularSeparationSearchResult* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    return taiyin::runtime::search_minimum_angular_separation_ut(
        context, body_a_id, body_b_id, split_jd(start_jd_ut), split_jd(end_jd_ut),
        max_step_days, flags, out, diagnostic);
}

taiyin::Status search_next_solar_transit_ut(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double jd_start_ut,
    uint64_t flags,
    taiyin::runtime::SolarTransitSearchResult* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    return taiyin::runtime::search_next_solar_transit_ut(
        context, body_id, split_jd(jd_start_ut), flags, out, diagnostic);
}

taiyin::Status search_next_local_solar_transit_ut(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double jd_start_ut,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    taiyin::runtime::LocalSolarTransitSearchResult* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    return taiyin::runtime::search_next_local_solar_transit_ut(
        context, body_id, split_jd(jd_start_ut), longitude_deg, latitude_deg,
        height_m, flags, out, diagnostic);
}

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_status(taiyin::Status actual, taiyin::Status expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << "\n";
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

void expect_near(
    const taiyin::SplitJulianDate& actual,
    double expected,
    double tolerance,
    const char* label,
    int* failures
) {
    expect_near(
        taiyin::split_julian_date_to_double(actual), expected, tolerance, label, failures);
}

void expect_near(
    const taiyin::SplitJulianDate& actual,
    const taiyin::SplitJulianDate& expected,
    double tolerance,
    const char* label,
    int* failures
) {
    expect_near(
        taiyin::split_julian_date_to_double(actual),
        taiyin::split_julian_date_to_double(expected),
        tolerance,
        label,
        failures);
}

bool operator==(const taiyin::SplitJulianDate& actual, double expected) {
    return taiyin::split_julian_date_to_double(actual) == expected;
}

bool operator<(const taiyin::SplitJulianDate& actual, double expected) {
    return taiyin::split_julian_date_to_double(actual) < expected;
}

bool operator>(const taiyin::SplitJulianDate& actual, double expected) {
    return taiyin::split_julian_date_to_double(actual) > expected;
}

std::ostream& operator<<(std::ostream& stream, const taiyin::SplitJulianDate& jd) {
    return stream << taiyin::split_julian_date_to_double(jd);
}

double tangent_longitude_rad(
    const taiyin::SplitJulianDate& jd_tdb,
    const SyntheticAspectBodyData* data
) {
    const double dt = -taiyin::days_between_split_jd_and_double(jd_tdb, data->station_jd_tdb);
    return data->station_longitude_rad - data->curvature_rad_per_day2 * dt * dt;
}

double tangent_longitude_rate_rad_per_day(
    const taiyin::SplitJulianDate& jd_tdb,
    const SyntheticAspectBodyData* data
) {
    const double dt = -taiyin::days_between_split_jd_and_double(jd_tdb, data->station_jd_tdb);
    return -2.0 * data->curvature_rad_per_day2 * dt;
}

bool synthetic_aspect_position(
    const taiyin::SplitJulianDate& jd_tdb,
    const void* raw,
    taiyin::Vector3* out
) {
    if (!raw || !out) {
        return false;
    }
    const SyntheticAspectBodyData* data = static_cast<const SyntheticAspectBodyData*>(raw);
    const double longitude = tangent_longitude_rad(jd_tdb, data);
    out->x = std::cos(longitude);
    out->y = std::sin(longitude);
    out->z = 0.0;
    return true;
}

bool synthetic_aspect_velocity(
    const taiyin::SplitJulianDate& jd_tdb,
    const void* raw,
    taiyin::Vector3* out
) {
    if (!raw || !out) {
        return false;
    }
    const SyntheticAspectBodyData* data = static_cast<const SyntheticAspectBodyData*>(raw);
    const double longitude = tangent_longitude_rad(jd_tdb, data);
    const double longitude_rate = tangent_longitude_rate_rad_per_day(jd_tdb, data);
    out->x = -std::sin(longitude) * longitude_rate;
    out->y = std::cos(longitude) * longitude_rate;
    out->z = 0.0;
    return true;
}

bool synthetic_aspect_acceleration(
    const taiyin::SplitJulianDate& jd_tdb,
    const void* raw,
    taiyin::Vector3* out
) {
    if (!raw || !out) {
        return false;
    }
    const SyntheticAspectBodyData* data = static_cast<const SyntheticAspectBodyData*>(raw);
    const double longitude = tangent_longitude_rad(jd_tdb, data);
    const double longitude_rate = tangent_longitude_rate_rad_per_day(jd_tdb, data);
    const double longitude_acceleration = -2.0 * data->curvature_rad_per_day2;
    out->x = -std::cos(longitude) * longitude_rate * longitude_rate
        - std::sin(longitude) * longitude_acceleration;
    out->y = -std::sin(longitude) * longitude_rate * longitude_rate
        + std::cos(longitude) * longitude_acceleration;
    out->z = 0.0;
    return true;
}

taiyin::internal::CustomEphemerisMethodDefinition make_synthetic_aspect_definition(
    int target_id,
    int method_id,
    const SyntheticAspectBodyData* data
) {
    taiyin::internal::CustomEphemerisMethodDefinition definition;
    definition.target_id = target_id;
    definition.center_id = taiyin::TAIYIN_BODY_EARTH;
    definition.method_id = method_id;
    definition.frame = taiyin::internal::IcrfJ2000Equatorial;
    definition.jd_tdb_start = data->station_jd_tdb - 10.0;
    definition.jd_tdb_end = data->station_jd_tdb + 10.0;
    definition.data = data;
    definition.bytes = sizeof(*data);
    definition.position = synthetic_aspect_position;
    definition.velocity = synthetic_aspect_velocity;
    definition.acceleration = synthetic_aspect_acceleration;
    definition.description = "synthetic tangent aspect body";
    return definition;
}

std::string repo_opm2_major_body_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/major-bodies/600y";
    }
    return "../data/ephemerides/opm2/major-bodies/600y";
}

std::string repo_fixed_star_catalog_path() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/stars/catalogs/stars-fixed-traditional.tsc1";
    }
    return "../data/stars/catalogs/stars-fixed-traditional.tsc1";
}

std::string external_de441_path() {
    const char* explicit_path = std::getenv("TAIYIN_DE441_PATH");
    if (explicit_path && explicit_path[0] != '\0') {
        return std::string(explicit_path);
    }
    const char* nasa_root = std::getenv("TAIYIN_NASA_BSP_ROOT");
    if (nasa_root && nasa_root[0] != '\0') {
        std::string path = nasa_root;
        if (!path.empty() && path[path.size() - 1] != '/') path += "/";
        path += "planetary/de441.bsp";
        return path;
    }
    return std::string();
}

bool file_exists(const std::string& path) {
    if (path.empty()) return false;
    std::ifstream file(path.c_str(), std::ios::binary);
    return static_cast<bool>(file);
}

bool initialize_packaged_runtime(int* failures) {
    taiyin::runtime::EphemerisRuntimeConfig config;
    const std::string data_root = repo_opm2_major_body_root();
    const char* source_paths[] = { data_root.c_str() };
    config.source_paths = source_paths;
    config.source_path_count = 1;
    config.load_packaged_data = true;
    config.segment_cache_max_entries = 256;
    const bool ok = taiyin::runtime::initialize_global_ephemeris_runtime(config);
    expect_true(ok, "initialize OPM2 runtime", failures);
    return ok;
}

bool initialize_de441_runtime(const std::string& de441_path, int* failures) {
    taiyin::runtime::EphemerisRuntimeConfig config;
    const char* source_paths[] = { de441_path.c_str() };
    config.source_paths = source_paths;
    config.source_path_count = 1;
    config.load_packaged_data = false;
    config.segment_cache_max_entries = 512;
    const bool ok = taiyin::runtime::initialize_global_ephemeris_runtime(config);
    expect_true(ok, "initialize DE441 runtime", failures);
    return ok;
}

taiyin::runtime::NativeCalcContext make_context() {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context,
        taiyin::TAIYIN_BODY_EARTH,
        taiyin::TAIYIN_BODY_EARTH);
    taiyin::runtime::native_context_use_solar_deflector(&context);
    context.apparent_options.flags =
        taiyin::TAIYIN_APPARENT_SPHERICAL
        | taiyin::TAIYIN_APPARENT_LIGHT_TIME
        | taiyin::TAIYIN_APPARENT_ABERRATION
        | taiyin::TAIYIN_APPARENT_DEFLECTION;
    context.apparent_options.output_frame_id = taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    return context;
}

double calc_body_longitude(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double jd_ut,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic,
    int* failures
) {
    double position[6] = {};
    expect_status(
        taiyin::runtime::calc_position_ut(
            context,
            body_id,
            split_jd(jd_ut),
            taiyin::runtime::TAIYIN_NATIVE_POSITION_SPEED | taiyin::runtime::TAIYIN_NATIVE_POSITION_RADIANS,
            position,
            diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "verify searched body longitude",
        failures);
    return position[0];
}

double calc_body_longitude_tt(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double jd_tt,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic,
    int* failures
) {
    const double tdb_minus_tt_seconds = taiyin::dispatch::eval_tdb(
        context->model_context.tdb_model_id,
        split_jd(jd_tt),
        0);
    const double jd_tdb = taiyin::add_seconds_to_jd(jd_tt, tdb_minus_tt_seconds);
    double position[6] = {};
    expect_status(
        taiyin::runtime::calc_position_tdb(
            context,
            body_id,
            split_jd(jd_tdb),
            split_jd(jd_tt),
            taiyin::runtime::TAIYIN_NATIVE_POSITION_SPEED | taiyin::runtime::TAIYIN_NATIVE_POSITION_RADIANS,
            position,
            diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "verify searched TT body longitude",
        failures);
    return position[0];
}

void expect_longitude_at(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double jd_ut,
    double target_rad,
    const char* label,
    int* failures
) {
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    const double longitude = calc_body_longitude(context, body_id, jd_ut, &diagnostic, failures);
    expect_true(
        std::fabs(taiyin::angular_difference_radians(longitude, target_rad)) <= 1.0e-9,
        label,
        failures);
}

void expect_longitude_at_tt(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double jd_tt,
    double target_rad,
    const char* label,
    int* failures
) {
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    const double longitude = calc_body_longitude_tt(context, body_id, jd_tt, &diagnostic, failures);
    expect_true(
        std::fabs(taiyin::angular_difference_radians(longitude, target_rad)) <= 1.0e-9,
        label,
        failures);
}

double calc_body_aspect(
    const taiyin::runtime::NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double jd_ut,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic,
    int* failures
) {
    const double lon_a = calc_body_longitude(context, body_a_id, jd_ut, diagnostic, failures);
    const double lon_b = calc_body_longitude(context, body_b_id, jd_ut, diagnostic, failures);
    return taiyin::normalize_radians(lon_a - lon_b);
}

double calc_body_aspect_tt(
    const taiyin::runtime::NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double jd_tt,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic,
    int* failures
) {
    const double lon_a = calc_body_longitude_tt(context, body_a_id, jd_tt, diagnostic, failures);
    const double lon_b = calc_body_longitude_tt(context, body_b_id, jd_tt, diagnostic, failures);
    return taiyin::normalize_radians(lon_a - lon_b);
}

double jd_utc(int year, int month, int day, int hour, int minute) {
    return taiyin::julian_day({ year, month, day, hour, minute, 0.0 });
}

double jd_year_approx(double year) {
    return 2451545.0 + (year - 2000.0) * 365.2425;
}

struct SolarTransitCatalogOracle {
    const char* label;
    int body_id;
    double search_start_jd_ut;
    double search_end_jd_ut;
    double t1_jd_ut;
    double t2_jd_ut;
    double greatest_jd_ut;
    double t3_jd_ut;
    double t4_jd_ut;
    double minimum_separation_arcsec;
    double duration_min_days;
    double duration_max_days;
    bool run_jpl_vector_oracle;
};

double calc_angular_separation(
    const taiyin::runtime::NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double jd_ut,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic,
    int* failures
) {
    double position_a[6] = {};
    double position_b[6] = {};
    const uint32_t flags = taiyin::runtime::TAIYIN_NATIVE_POSITION_XYZ;
    expect_status(
        taiyin::runtime::calc_position_ut(
            context, body_a_id, split_jd(jd_ut), flags, position_a, diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "calc angular separation body A",
        failures);
    expect_status(
        taiyin::runtime::calc_position_ut(
            context, body_b_id, split_jd(jd_ut), flags, position_b, diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "calc angular separation body B",
        failures);

    const double radius_a = std::sqrt(
        position_a[0] * position_a[0]
        + position_a[1] * position_a[1]
        + position_a[2] * position_a[2]);
    const double radius_b = std::sqrt(
        position_b[0] * position_b[0]
        + position_b[1] * position_b[1]
        + position_b[2] * position_b[2]);
    if (!(radius_a > 0.0) || !(radius_b > 0.0)) {
        ++(*failures);
        return NAN;
    }
    double cos_angle =
        (position_a[0] * position_b[0]
            + position_a[1] * position_b[1]
            + position_a[2] * position_b[2])
        / (radius_a * radius_b);
    if (cos_angle > 1.0) {
        cos_angle = 1.0;
    } else if (cos_angle < -1.0) {
        cos_angle = -1.0;
    }
    return std::acos(cos_angle);
}

double calc_angular_separation(
    const taiyin::runtime::NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    const taiyin::SplitJulianDate& jd_ut,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic,
    int* failures
) {
    return calc_angular_separation(
        context,
        body_a_id,
        body_b_id,
        taiyin::split_julian_date_to_double(jd_ut),
        diagnostic,
        failures);
}

void expect_aspect_at(
    const taiyin::runtime::NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double jd_ut,
    double aspect_rad,
    const char* label,
    int* failures
) {
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    const double aspect = calc_body_aspect(context, body_a_id, body_b_id, jd_ut, &diagnostic, failures);
    expect_true(
        std::fabs(taiyin::angular_difference_radians(aspect, aspect_rad)) <= 1.0e-9,
        label,
        failures);
}

void expect_aspect_at_tt(
    const taiyin::runtime::NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double jd_tt,
    double aspect_rad,
    const char* label,
    int* failures
) {
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    const double aspect = calc_body_aspect_tt(context, body_a_id, body_b_id, jd_tt, &diagnostic, failures);
    expect_true(
        std::fabs(taiyin::angular_difference_radians(aspect, aspect_rad)) <= 1.0e-9,
        label,
        failures);
}

void test_solar_longitude_crossing(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    double jd_ut = 0.0;
    const Status status = search_solar_longitude_ut(
        &context,
        0.0,
        JD_UT_NEAR_2024_EQUINOX,
        0,
        &jd_ut,
        &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "search solar longitude", failures);
    if (status != TAIYIN_STATUS_OK) {
        return;
    }
    expect_true(jd_ut > JD_UT_NEAR_2024_EQUINOX, "event is after estimate", failures);
    expect_true(jd_ut < JD_UT_NEAR_2024_EQUINOX + 60.0, "event is near estimate", failures);
    expect_longitude_at(&context, TAIYIN_BODY_SUN, jd_ut, 0.0, "verified solar longitude crosses target", failures);
}

void test_position_tt_entry_matches_tdb(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    const double jd_tt = JD_UT_NEAR_2024_EQUINOX;
    const SplitJulianDate split_jd_tt = split_jd(jd_tt);
    SplitJulianDate split_jd_tdb;
    add_seconds_to_split_jd(
        split_jd_tt,
        dispatch::eval_tdb(context.model_context.tdb_model_id, split_jd_tt, 0),
        &split_jd_tdb);
    const uint32_t flags = TAIYIN_NATIVE_POSITION_SPEED | TAIYIN_NATIVE_POSITION_RADIANS;

    double tt_position[6] = {};
    double tdb_position[6] = {};
    EphemerisEvalDiagnostic tt_diagnostic;
    EphemerisEvalDiagnostic tdb_diagnostic;
    expect_status(
        calc_position_tt(
            &context, TAIYIN_BODY_SUN, split_jd_tt, flags, tt_position, &tt_diagnostic),
        TAIYIN_STATUS_OK,
        "calc_position_tt succeeds",
        failures);
    expect_status(
        calc_position_tdb(
            &context,
            TAIYIN_BODY_SUN,
            split_jd_tdb,
            split_jd_tt,
            flags,
            tdb_position,
            &tdb_diagnostic),
        TAIYIN_STATUS_OK,
        "calc_position_tdb succeeds",
        failures);
    for (int i = 0; i < 6; ++i) {
        expect_near(tt_position[i], tdb_position[i], 0.0, "calc_position_tt matches tdb", failures);
    }

    const int body_ids[] = { TAIYIN_BODY_SUN, TAIYIN_BODY_VENUS_BARYCENTER };
    double tt_batch[12] = {};
    double tdb_batch[12] = {};
    EphemerisEvalDiagnostic tt_batch_diagnostics[2];
    EphemerisEvalDiagnostic tdb_batch_diagnostics[2];
    expect_status(
        calc_positions_tt(
            &context, body_ids, 2, split_jd_tt, flags, tt_batch, tt_batch_diagnostics),
        TAIYIN_STATUS_OK,
        "calc_positions_tt succeeds",
        failures);
    expect_status(
        calc_positions_tdb(
            &context,
            body_ids,
            2,
            split_jd_tdb,
            split_jd_tt,
            flags,
            tdb_batch,
            tdb_batch_diagnostics),
        TAIYIN_STATUS_OK,
        "calc_positions_tdb succeeds",
        failures);
    for (int i = 0; i < 12; ++i) {
        expect_near(tt_batch[i], tdb_batch[i], 0.0, "calc_positions_tt matches tdb", failures);
    }
}

void test_solar_longitude_tt_crossing(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    const double estimate_jd_tt = JD_UT_NEAR_2024_EQUINOX;
    double jd_tt = 0.0;
    const Status status = search_solar_longitude_tt(
        &context,
        0.0,
        estimate_jd_tt,
        0,
        &jd_tt,
        &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "search TT solar longitude", failures);
    if (status != TAIYIN_STATUS_OK) {
        return;
    }
    expect_true(jd_tt > estimate_jd_tt, "TT event is after estimate", failures);
    expect_true(jd_tt < estimate_jd_tt + 60.0, "TT event is near estimate", failures);
    expect_longitude_at_tt(
        &context,
        TAIYIN_BODY_SUN,
        jd_tt,
        0.0,
        "verified TT solar longitude crosses target",
        failures);
}

void test_solar_longitude_reverse(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    double jd_ut = 0.0;
    const Status status = search_solar_longitude_ut(
        &context,
        0.0,
        2460395.0,
        TAIYIN_EVENT_SEARCH_REVERSE,
        &jd_ut,
        &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "reverse search solar longitude", failures);
    if (status != TAIYIN_STATUS_OK) {
        return;
    }
    expect_true(jd_ut < 2460395.0, "reverse event is before estimate", failures);
    expect_near(jd_ut, 2460389.6294463626, 5.0e-8, "reverse solar longitude jd_ut", failures);
    expect_longitude_at(&context, TAIYIN_BODY_SUN, jd_ut, 0.0, "verified reverse solar longitude", failures);
}

void test_solar_longitude_terms_sequence(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    double estimate_jd = JD_UT_NEAR_2024_EQUINOX - 10.0;
    for (int term = 0; term < 24; ++term) {
        const double target = static_cast<double>(term) * TAIYIN_TWO_PI / 24.0;
        EphemerisEvalDiagnostic diagnostic;
        double jd_ut = 0.0;
        const Status status = search_solar_longitude_ut(
            &context,
            target,
            estimate_jd,
            0,
            &jd_ut,
            &diagnostic);
        expect_status(status, TAIYIN_STATUS_OK, "search solar longitude target", failures);
        if (status != TAIYIN_STATUS_OK) {
            return;
        }
        expect_true(jd_ut > estimate_jd, "solar longitude target is after sequence estimate", failures);
        expect_true(jd_ut < estimate_jd + 25.0, "solar longitude target is in expected interval", failures);
        expect_longitude_at(&context, TAIYIN_BODY_SUN, jd_ut, target, "solar longitude target final error", failures);
        estimate_jd = jd_ut + 0.5;
    }
}

void test_solar_longitude_oracles(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    struct Oracle {
        double target_longitude_rad;
        double estimate_jd_ut;
        uint64_t search_flags;
        double expected_jd_ut;
    };

    const Oracle oracles[] = {
        { 0.0, JD_UT_NEAR_2024_EQUINOX, 0, 2460389.6294463626 },
        { TAIYIN_TWO_PI / 12.0, JD_UT_NEAR_2024_EQUINOX, 0, 2460420.0831652079 },
        { TAIYIN_TWO_PI / 6.0, JD_UT_NEAR_2024_EQUINOX, 0, 2460451.0413237908 },
        { TAIYIN_TWO_PI / 4.0, JD_UT_NEAR_2024_EQUINOX, 0, 2460482.3687479557 },
        { TAIYIN_PI, JD_UT_NEAR_2024_EQUINOX, 0, 2460576.0303197531 },
    };

    NativeCalcContext context = make_context();
    for (size_t i = 0; i < sizeof(oracles) / sizeof(oracles[0]); ++i) {
        EphemerisEvalDiagnostic diagnostic;
        double jd_ut = 0.0;
        const Status status = search_solar_longitude_ut(
            &context,
            oracles[i].target_longitude_rad,
            oracles[i].estimate_jd_ut,
            oracles[i].search_flags,
            &jd_ut,
            &diagnostic);
        expect_status(status, TAIYIN_STATUS_OK, "solar longitude oracle search", failures);
        if (status != TAIYIN_STATUS_OK) {
            continue;
        }
        expect_near(jd_ut, oracles[i].expected_jd_ut, 5.0e-8, "solar longitude oracle jd_ut", failures);
        expect_longitude_at(
            &context,
            TAIYIN_BODY_SUN,
            jd_ut,
            oracles[i].target_longitude_rad,
            "solar longitude oracle final error",
            failures);
    }
}

void test_moon_longitude_crossing(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    const double estimate_jd = 2460380.5;
    const double start_longitude = calc_body_longitude(
        &context,
        TAIYIN_BODY_MOON,
        estimate_jd,
        &diagnostic,
        failures);
    const double target = normalize_radians(start_longitude + TAIYIN_PI / 2.0);

    double jd_ut = 0.0;
    const Status status = search_moon_longitude_ut(
        &context,
        target,
        estimate_jd,
        0,
        &jd_ut,
        &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "search Moon longitude", failures);
    if (status != TAIYIN_STATUS_OK) {
        return;
    }
    expect_true(jd_ut > estimate_jd, "Moon longitude crossing is after estimate", failures);
    expect_true(jd_ut < estimate_jd + 10.0, "Moon longitude crossing uses lunar-scale bracket", failures);
    expect_longitude_at(
        &context,
        TAIYIN_BODY_MOON,
        jd_ut,
        target,
        "verified Moon longitude crossing",
        failures);
}

void test_moon_longitude_crossing_across_wrap(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    const double estimate_jd = 2460380.5;
    const double start_longitude = calc_body_longitude(
        &context, TAIYIN_BODY_MOON, estimate_jd, &diagnostic, failures);
    const double target = normalize_radians(start_longitude - TAIYIN_DEG_TO_RAD);

    double jd_ut = 0.0;
    const Status status = search_moon_longitude_ut(
        &context, target, estimate_jd, 0, &jd_ut, &diagnostic);
    expect_status(
        status, TAIYIN_STATUS_OK,
        "search Moon longitude across angular wrap", failures);
    if (status != TAIYIN_STATUS_OK) return;
    expect_true(
        jd_ut > estimate_jd + 20.0 && jd_ut < estimate_jd + 35.0,
        "wrapped Moon longitude crossing is in the next lunar cycle",
        failures);
    expect_longitude_at(
        &context, TAIYIN_BODY_MOON, jd_ut, target,
        "verified wrapped Moon longitude crossing", failures);
}

void test_moon_longitude_tt_crossing(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    const double estimate_jd_tt = 2460380.5;
    const double start_longitude = calc_body_longitude_tt(
        &context,
        TAIYIN_BODY_MOON,
        estimate_jd_tt,
        &diagnostic,
        failures);
    const double target = normalize_radians(start_longitude + TAIYIN_PI / 2.0);

    double jd_tt = 0.0;
    const Status status = search_moon_longitude_tt(
        &context,
        target,
        estimate_jd_tt,
        0,
        &jd_tt,
        &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "search TT Moon longitude", failures);
    if (status != TAIYIN_STATUS_OK) {
        return;
    }
    expect_true(jd_tt > estimate_jd_tt, "TT Moon longitude crossing is after estimate", failures);
    expect_true(jd_tt < estimate_jd_tt + 10.0, "TT Moon longitude crossing uses lunar-scale bracket", failures);
    expect_longitude_at_tt(
        &context,
        TAIYIN_BODY_MOON,
        jd_tt,
        target,
        "verified TT Moon longitude crossing",
        failures);
}

void test_bounded_solar_longitude_crossings(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    double events[2] = {};
    size_t event_count = 99;
    const Status status = search_body_longitude_crossings_ut(
        &context,
        TAIYIN_BODY_SUN,
        0.0,
        JD_UT_NEAR_2024_EQUINOX,
        2460395.0,
        2.0,
        0,
        events,
        2,
        &event_count,
        &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "bounded solar longitude crossings", failures);
    if (status != TAIYIN_STATUS_OK) {
        return;
    }
    expect_true(event_count == 1, "bounded solar crossing count", failures);
    expect_near(events[0], 2460389.6294463626, 5.0e-8, "bounded solar crossing jd_ut", failures);
    expect_longitude_at(&context, TAIYIN_BODY_SUN, events[0], 0.0, "bounded solar crossing longitude", failures);
}

void test_bounded_moon_longitude_crossings(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    const double start_jd = 2460380.5;
    const double start_longitude = calc_body_longitude(
        &context,
        TAIYIN_BODY_MOON,
        start_jd,
        &diagnostic,
        failures);
    const double target = normalize_radians(start_longitude + TAIYIN_PI / 2.0);

    double direct_jd = 0.0;
    expect_status(
        search_moon_longitude_ut(&context, target, start_jd, 0, &direct_jd, &diagnostic),
        TAIYIN_STATUS_OK,
        "direct Moon longitude crossing for bounded oracle",
        failures);

    double events[2] = {};
    size_t event_count = 0;
    const Status status = search_body_longitude_crossings_ut(
        &context,
        TAIYIN_BODY_MOON,
        target,
        start_jd,
        start_jd + 10.0,
        0.5,
        0,
        events,
        2,
        &event_count,
        &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "bounded Moon longitude crossings", failures);
    if (status != TAIYIN_STATUS_OK) {
        return;
    }
    expect_true(event_count == 1, "bounded Moon crossing count", failures);
    expect_near(events[0], direct_jd, 5.0e-8, "bounded Moon crossing matches direct search", failures);
    expect_longitude_at(&context, TAIYIN_BODY_MOON, events[0], target, "bounded Moon crossing longitude", failures);
}

void test_bounded_longitude_search_no_event_and_capacity(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    double events[1] = {};
    size_t event_count = 99;
    expect_status(
        search_body_longitude_crossings_ut(
            &context,
            TAIYIN_BODY_SUN,
            0.0,
            2460390.0,
            2460391.0,
            0.25,
            0,
            events,
            1,
            &event_count,
            &diagnostic),
        TAIYIN_EVENT_ERROR_NOT_FOUND,
        "bounded longitude search reports no event",
        failures);
    expect_true(event_count == 0, "bounded no-event count reset", failures);

    expect_status(
        search_body_longitude_crossings_ut(
            &context,
            TAIYIN_BODY_SUN,
            0.0,
            JD_UT_NEAR_2024_EQUINOX,
            2460395.0,
            2.0,
            0,
            0,
            0,
            &event_count,
            &diagnostic),
        TAIYIN_ERROR_OUT_OF_MEMORY,
        "bounded longitude search reports output capacity",
        failures);
}

void test_recommended_search_steps(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    expect_near(recommended_longitude_search_step_days(TAIYIN_BODY_MOON), 0.25, 0.0, "Moon longitude step", failures);
    expect_near(recommended_longitude_search_step_days(TAIYIN_BODY_MERCURY_BARYCENTER), 0.5, 0.0, "Mercury longitude step", failures);
    expect_near(recommended_longitude_search_step_days(TAIYIN_BODY_NEPTUNE_BARYCENTER), 3.0, 0.0, "Neptune longitude step", failures);
    expect_near(recommended_aspect_search_step_days(TAIYIN_BODY_MOON, TAIYIN_BODY_SUN), 0.25, 0.0, "Moon-Sun aspect step", failures);
    expect_near(recommended_aspect_search_step_days(TAIYIN_BODY_VENUS_BARYCENTER, TAIYIN_BODY_PLUTO_BARYCENTER), 1.0, 0.0, "Venus-Pluto aspect step", failures);
}

void test_auto_step_longitude_and_aspect_wrappers(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    double manual_events[4] = {};
    size_t manual_count = 0;
    const Status manual_status = search_body_longitude_crossings_ut(
        &context,
        TAIYIN_BODY_SUN,
        0.0,
        JD_UT_NEAR_2024_EQUINOX,
        2460395.0,
        recommended_longitude_search_step_days(TAIYIN_BODY_SUN),
        0,
        manual_events,
        4,
        &manual_count,
        &diagnostic);
    expect_status(manual_status, TAIYIN_STATUS_OK, "manual-step longitude wrapper oracle", failures);

    double auto_events[4] = {};
    size_t auto_count = 0;
    const Status auto_status = search_body_longitude_crossings_auto_step_ut(
        &context,
        TAIYIN_BODY_SUN,
        0.0,
        JD_UT_NEAR_2024_EQUINOX,
        2460395.0,
        0,
        auto_events,
        4,
        &auto_count,
        &diagnostic);
    expect_status(auto_status, TAIYIN_STATUS_OK, "auto-step longitude wrapper", failures);
    if (manual_status == TAIYIN_STATUS_OK && auto_status == TAIYIN_STATUS_OK) {
        expect_true(auto_count == manual_count, "auto-step longitude count", failures);
        expect_near(auto_events[0], manual_events[0], 0.0, "auto-step longitude event", failures);
    }

    double aspect_events[4] = {};
    size_t aspect_count = 0;
    const Status aspect_status = search_body_aspect_crossings_auto_step_ut(
        &context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        0.0,
        JD_UT_NEAR_2024_EQUINOX,
        JD_UT_NEAR_2024_EQUINOX + 40.0,
        0,
        aspect_events,
        4,
        &aspect_count,
        &diagnostic);
    expect_status(aspect_status, TAIYIN_STATUS_OK, "auto-step aspect wrapper", failures);
    if (aspect_status == TAIYIN_STATUS_OK) {
        expect_true(aspect_count == 1, "auto-step aspect count", failures);
        expect_aspect_at(&context, TAIYIN_BODY_MOON, TAIYIN_BODY_SUN, aspect_events[0], 0.0, "auto-step aspect event", failures);
    }
}

void test_lunar_phase_crossings(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    double lunar_events[4] = {};
    size_t lunar_count = 0;
    const Status lunar_status = search_lunar_phase_crossings_ut(
        &context,
        0.0,
        JD_UT_NEAR_2024_EQUINOX,
        JD_UT_NEAR_2024_EQUINOX + 40.0,
        1.0,
        0,
        lunar_events,
        4,
        &lunar_count,
        &diagnostic);
    expect_status(lunar_status, TAIYIN_STATUS_OK, "lunar phase crossing search", failures);
    if (lunar_status != TAIYIN_STATUS_OK) {
        return;
    }
    expect_true(lunar_count == 1, "lunar phase crossing count", failures);
    expect_aspect_at(
        &context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        lunar_events[0],
        0.0,
        "verified lunar phase crossing",
        failures);

    double generic_events[4] = {};
    size_t generic_count = 0;
    const Status generic_status = search_body_aspect_crossings_ut(
        &context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        0.0,
        JD_UT_NEAR_2024_EQUINOX,
        JD_UT_NEAR_2024_EQUINOX + 40.0,
        1.0,
        0,
        generic_events,
        4,
        &generic_count,
        &diagnostic);
    expect_status(generic_status, TAIYIN_STATUS_OK, "generic Moon-Sun aspect crossing search", failures);
    if (generic_status != TAIYIN_STATUS_OK) {
        return;
    }
    expect_true(generic_count == lunar_count, "generic aspect crossing count", failures);
    expect_near(generic_events[0], lunar_events[0], 5.0e-8, "generic aspect matches lunar phase wrapper", failures);

    double default_events[4] = {};
    size_t default_count = 0;
    const Status default_status = search_lunar_phase_crossings_default_step_ut(
        &context,
        0.0,
        JD_UT_NEAR_2024_EQUINOX,
        JD_UT_NEAR_2024_EQUINOX + 40.0,
        0,
        default_events,
        4,
        &default_count,
        &diagnostic);
    expect_status(default_status, TAIYIN_STATUS_OK, "default-step lunar phase crossing search", failures);
    if (default_status != TAIYIN_STATUS_OK) {
        return;
    }
    expect_true(default_count == lunar_count, "default-step lunar phase count", failures);
    expect_near(default_events[0], lunar_events[0], 0.0, "default-step lunar phase event", failures);
}

void test_lunar_phase_crossings_tt(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    double events[4] = {};
    size_t event_count = 0;
    const Status status = search_lunar_phase_crossings_tt(
        &context,
        TAIYIN_PI / 2.0,
        JD_UT_NEAR_2024_EQUINOX,
        JD_UT_NEAR_2024_EQUINOX + 15.0,
        0.5,
        0,
        events,
        4,
        &event_count,
        &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "TT lunar phase crossing search", failures);
    if (status != TAIYIN_STATUS_OK) {
        return;
    }
    expect_true(event_count == 1, "TT lunar phase crossing count", failures);
    expect_aspect_at_tt(
        &context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        events[0],
        TAIYIN_PI / 2.0,
        "verified TT lunar phase crossing",
        failures);
}

void test_exact_aspect_search_lunar_quarters(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    const double aspects[] = { TAIYIN_PI / 2.0 };
    double events[8] = {};
    double targets[8] = {};
    size_t event_count = 0;
    const Status status = search_body_exact_aspects_ut(
        &context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        aspects,
        1,
        JD_UT_NEAR_2024_EQUINOX,
        JD_UT_NEAR_2024_EQUINOX + 40.0,
        0.5,
        0,
        events,
        targets,
        8,
        &event_count,
        &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "exact lunar quarter aspect search", failures);
    if (status != TAIYIN_STATUS_OK) {
        return;
    }
    expect_true(event_count >= 2, "exact lunar quarter count includes both orientations", failures);
    bool saw_forward = false;
    bool saw_reverse = false;
    for (size_t i = 0; i < event_count; ++i) {
        if (i > 0) {
            expect_true(events[i] > events[i - 1], "exact aspects are sorted by time", failures);
        }
        if (std::fabs(angular_difference_radians(targets[i], TAIYIN_PI / 2.0)) <= 1.0e-9) {
            saw_forward = true;
        } else if (std::fabs(angular_difference_radians(targets[i], 3.0 * TAIYIN_PI / 2.0)) <= 1.0e-9) {
            saw_reverse = true;
        } else {
            expect_true(false, "exact lunar quarter target is 90 or 270", failures);
        }
        expect_aspect_at(&context, TAIYIN_BODY_MOON, TAIYIN_BODY_SUN, events[i], targets[i], "exact lunar quarter final error", failures);
    }
    expect_true(saw_forward, "exact lunar quarter forward orientation", failures);
    expect_true(saw_reverse, "exact lunar quarter reverse orientation", failures);
}

void test_exact_aspect_search_auto_step_and_dedup(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    const double duplicate_aspects[] = { 0.0, TAIYIN_TWO_PI };
    double events[4] = {};
    double targets[4] = {};
    size_t event_count = 0;
    const Status status = search_body_exact_aspects_auto_step_ut(
        &context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        duplicate_aspects,
        2,
        JD_UT_NEAR_2024_EQUINOX,
        JD_UT_NEAR_2024_EQUINOX + 40.0,
        0,
        events,
        targets,
        1,
        &event_count,
        &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "exact aspect auto-step duplicate aspect search", failures);
    if (status != TAIYIN_STATUS_OK) {
        return;
    }
    expect_true(event_count == 1, "duplicate conjunction aspect is deduplicated", failures);
    expect_near(targets[0], 0.0, 0.0, "duplicate conjunction target", failures);
    expect_aspect_at(&context, TAIYIN_BODY_MOON, TAIYIN_BODY_SUN, events[0], 0.0, "deduped conjunction final error", failures);
}

void test_exact_aspect_search_capacity_and_flags(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    const double aspects[] = { TAIYIN_PI / 2.0 };
    double event = 0.0;
    double target = 0.0;
    size_t event_count = 0;
    expect_status(
        search_body_exact_aspects_ut(
            &context,
            TAIYIN_BODY_MOON,
            TAIYIN_BODY_SUN,
            aspects,
            1,
            JD_UT_NEAR_2024_EQUINOX,
            JD_UT_NEAR_2024_EQUINOX + 40.0,
            0.5,
            0,
            &event,
            &target,
            1,
            &event_count,
            &diagnostic),
        TAIYIN_ERROR_OUT_OF_MEMORY,
        "exact aspect search reports output capacity",
        failures);

    expect_status(
        search_body_exact_aspects_ut(
            &context,
            TAIYIN_BODY_MOON,
            TAIYIN_BODY_SUN,
            aspects,
            1,
            JD_UT_NEAR_2024_EQUINOX,
            JD_UT_NEAR_2024_EQUINOX + 40.0,
            0.5,
            TAIYIN_EVENT_SEARCH_REVERSE,
            &event,
            &target,
            1,
            &event_count,
            &diagnostic),
        TAIYIN_ERROR_UNSUPPORTED,
        "exact aspect search rejects reverse flag",
        failures);
}

void test_exact_aspect_search_tangent_station_candidate(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context;
    native_context_set_geocentric_observer(&context, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH);
    context.apparent_options.flags = 0;
    context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_ICRF;

    const double station_jd_ut = JD_UT_NEAR_2024_EQUINOX + 2.0;
    const double delta_t_seconds = dispatch::eval_delta_t_with_ephemeris_correction(
        context.delta_t_model_id,
        context.ephemeris_family_id,
        split_jd(station_jd_ut),
        0,
        0);
    const double station_jd_tt = ut1_to_tt_jd(station_jd_ut, delta_t_seconds);
    const double station_jd_tdb = add_seconds_to_jd(
        station_jd_tt,
        dispatch::eval_tdb(context.model_context.tdb_model_id, split_jd(station_jd_tt), 0));

    const int zero_body_id = internal::register_celestial_body("event_search_synthetic_zero");
    const int tangent_body_id = internal::register_celestial_body("event_search_synthetic_tangent");
    expect_true(zero_body_id != 0, "register synthetic zero body", failures);
    expect_true(tangent_body_id != 0, "register synthetic tangent body", failures);

    const SyntheticAspectBodyData zero_data = {
        station_jd_tdb,
        0.0,
        0.0,
    };
    const SyntheticAspectBodyData tangent_data = {
        station_jd_tdb,
        2.0 * TAIYIN_PI / 3.0,
        0.01,
    };

    internal::EphemerisBlockDescriptor zero_descriptor;
    expect_true(
        add_global_custom_ephemeris_method(
            make_synthetic_aspect_definition(zero_body_id, METHOD_EVENT_SEARCH_FIXED_ZERO, &zero_data),
            2000,
            "event-search synthetic zero",
            &zero_descriptor),
        "install synthetic zero body",
        failures);

    internal::EphemerisBlockDescriptor tangent_descriptor;
    expect_true(
        add_global_custom_ephemeris_method(
            make_synthetic_aspect_definition(tangent_body_id, METHOD_EVENT_SEARCH_TANGENT, &tangent_data),
            2000,
            "event-search synthetic tangent",
            &tangent_descriptor),
        "install synthetic tangent body",
        failures);

    EphemerisEvalDiagnostic diagnostic;
    double crossing_event = 0.0;
    size_t crossing_count = 0;
    const double aspect = 2.0 * TAIYIN_PI / 3.0;
    expect_status(
        search_body_aspect_crossings_ut(
            &context,
            tangent_body_id,
            zero_body_id,
            aspect,
            station_jd_ut - 2.0,
            station_jd_ut + 2.0,
            4.0,
            0,
            &crossing_event,
            1,
            &crossing_count,
            &diagnostic),
        TAIYIN_EVENT_ERROR_NOT_FOUND,
        "plain aspect crossing misses tangent station",
        failures);
    expect_true(crossing_count == 0, "plain crossing tangent count", failures);

    const double aspects[] = { aspect };
    double exact_events[2] = {};
    double exact_targets[2] = {};
    size_t exact_count = 0;
    const Status exact_status = search_body_exact_aspects_ut(
        &context,
        tangent_body_id,
        zero_body_id,
        aspects,
        1,
        station_jd_ut - 2.0,
        station_jd_ut + 2.0,
        4.0,
        0,
        exact_events,
        exact_targets,
        2,
        &exact_count,
        &diagnostic);
    expect_status(exact_status, TAIYIN_STATUS_OK, "exact aspect finds tangent station", failures);
    if (exact_status != TAIYIN_STATUS_OK) {
        return;
    }
    expect_true(exact_count == 1, "exact tangent station count", failures);
    expect_near(exact_events[0], station_jd_ut, 1.0e-8, "exact tangent station jd_ut", failures);
    expect_near(exact_targets[0], aspect, 1.0e-12, "exact tangent station target", failures);
    expect_aspect_at(
        &context,
        tangent_body_id,
        zero_body_id,
        exact_events[0],
        aspect,
        "exact tangent station final error",
        failures);
}

void test_body_longitude_station_search_synthetic(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context;
    native_context_set_geocentric_observer(&context, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH);
    context.apparent_options.flags = 0;
    context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_ICRF;

    const double station_jd_ut = JD_UT_NEAR_2024_EQUINOX + 3.0;
    const double delta_t_seconds = dispatch::eval_delta_t_with_ephemeris_correction(
        context.delta_t_model_id,
        context.ephemeris_family_id,
        split_jd(station_jd_ut),
        0,
        0);
    const double station_jd_tt = ut1_to_tt_jd(station_jd_ut, delta_t_seconds);
    const double station_jd_tdb = add_seconds_to_jd(
        station_jd_tt,
        dispatch::eval_tdb(context.model_context.tdb_model_id, split_jd(station_jd_tt), 0));

    const int station_body_id = internal::register_celestial_body("event_search_synthetic_station");
    expect_true(station_body_id != 0, "register synthetic station body", failures);

    const double station_longitude = 80.0 * TAIYIN_DEG_TO_RAD;
    const SyntheticAspectBodyData station_data = {
        station_jd_tdb,
        station_longitude,
        0.02,
    };

    internal::EphemerisBlockDescriptor descriptor;
    expect_true(
        add_global_custom_ephemeris_method(
            make_synthetic_aspect_definition(station_body_id, METHOD_EVENT_SEARCH_STATION, &station_data),
            2000,
            "event-search synthetic station",
            &descriptor),
        "install synthetic station body",
        failures);

    EphemerisEvalDiagnostic diagnostic;
    double events[2] = {};
    double longitudes[2] = {};
    size_t event_count = 0;
    const Status status = search_body_longitude_stations_ut(
        &context,
        station_body_id,
        station_jd_ut - 2.0,
        station_jd_ut + 2.0,
        4.0,
        0,
        events,
        longitudes,
        2,
        &event_count,
        &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "synthetic longitude station search", failures);
    if (status != TAIYIN_STATUS_OK) {
        return;
    }
    expect_true(event_count == 1, "synthetic station count", failures);
    expect_near(events[0], station_jd_ut, 1.0e-8, "synthetic station jd_ut", failures);
    expect_near(longitudes[0], station_longitude, 1.0e-9, "synthetic station longitude", failures);

    double position[6] = {};
    expect_status(
        calc_position_ut(
            &context,
            station_body_id,
            split_jd(events[0]),
            TAIYIN_NATIVE_POSITION_SPEED | TAIYIN_NATIVE_POSITION_RADIANS,
            position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "verify synthetic station position",
        failures);
    expect_near(position[3], 0.0, 1.0e-10, "synthetic station longitude speed", failures);

    double auto_events[2] = {};
    double auto_longitudes[2] = {};
    size_t auto_count = 0;
    expect_status(
        search_body_longitude_stations_auto_step_ut(
            &context,
            station_body_id,
            station_jd_ut - 2.0,
            station_jd_ut + 2.0,
            0,
            auto_events,
            auto_longitudes,
            2,
            &auto_count,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "auto-step synthetic longitude station search",
        failures);
    expect_true(auto_count == 1, "auto-step synthetic station count", failures);
    expect_near(auto_events[0], events[0], 1.0e-8, "auto-step synthetic station jd_ut", failures);

    double tt_events[2] = {};
    double tt_longitudes[2] = {};
    size_t tt_count = 0;
    expect_status(
        search_body_longitude_stations_tt(
            &context,
            station_body_id,
            station_jd_tt - 2.0,
            station_jd_tt + 2.0,
            4.0,
            0,
            tt_events,
            tt_longitudes,
            2,
            &tt_count,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "TT synthetic longitude station search",
        failures);
    expect_true(tt_count == 1, "TT synthetic station count", failures);
    expect_near(tt_events[0], station_jd_tt, 1.0e-8, "TT synthetic station jd_tt", failures);
    expect_near(tt_longitudes[0], station_longitude, 1.0e-9, "TT synthetic station longitude", failures);

    size_t capacity_count = 0;
    expect_status(
        search_body_longitude_stations_ut(
            &context,
            station_body_id,
            station_jd_ut - 2.0,
            station_jd_ut + 2.0,
            4.0,
            0,
            0,
            0,
            0,
            &capacity_count,
            &diagnostic),
        TAIYIN_ERROR_OUT_OF_MEMORY,
        "station search reports output capacity",
        failures);

    double rejected_event = 0.0;
    size_t rejected_count = 0;
    expect_status(
        search_body_longitude_stations_ut(
            &context,
            station_body_id,
            station_jd_ut - 2.0,
            station_jd_ut + 2.0,
            4.0,
            TAIYIN_EVENT_SEARCH_REVERSE,
            &rejected_event,
            0,
            1,
            &rejected_count,
            &diagnostic),
        TAIYIN_ERROR_UNSUPPORTED,
        "station search rejects reverse flag",
        failures);
}

void test_real_body_longitude_station_smoke(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    struct StationSmokeCase {
        const char* label;
        int body_id;
        double start_jd_ut;
        double end_jd_ut;
        double max_step_days;
        double expected_jd_ut;
        double expected_tolerance_days;
        bool check_retrograde_turn;
    };

    const StationSmokeCase cases[] = {
        {
            "Mercury 2003 August retrograde station",
            TAIYIN_BODY_MERCURY,
            2452878.5,
            2452882.5,
            0.25,
            2452880.070395550,
            2.0 / 86400.0,
            true,
        },
        {
            "Mercury 2024 April station",
            TAIYIN_BODY_MERCURY_BARYCENTER,
            2460418.0,
            2460432.0,
            0.5,
            NAN,
            NAN,
            false,
        },
        {
            "Mars 2024 December station",
            TAIYIN_BODY_MARS_BARYCENTER,
            2460638.0,
            2460665.0,
            1.0,
            NAN,
            NAN,
            false,
        },
    };

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        double events[4] = {};
        double longitudes[4] = {};
        size_t event_count = 0;
        const StationSmokeCase& station_case = cases[i];
        const Status status = search_body_longitude_stations_ut(
            &context,
            station_case.body_id,
            station_case.start_jd_ut,
            station_case.end_jd_ut,
            station_case.max_step_days,
            0,
            events,
            longitudes,
            4,
            &event_count,
            &diagnostic);
        expect_status(status, TAIYIN_STATUS_OK, station_case.label, failures);
        if (status != TAIYIN_STATUS_OK) {
            std::cerr << "  diagnostic target=" << diagnostic.target_id
                      << " status=" << diagnostic.status
                      << " jd=" << split_julian_date_to_double(diagnostic.jd_tdb) << "\n";
            continue;
        }
        expect_true(event_count == 1, "real station smoke count", failures);
        if (event_count == 0) {
            continue;
        }

        double position[6] = {};
        expect_status(
            calc_position_ut(
                &context,
                station_case.body_id,
                split_jd(events[0]),
                TAIYIN_NATIVE_POSITION_SPEED | TAIYIN_NATIVE_POSITION_RADIANS,
                position,
                &diagnostic),
            TAIYIN_STATUS_OK,
            "verify real station position",
            failures);
        expect_true(
            std::fabs(position[3]) <= 1.0e-7,
            "real station longitude speed is near zero",
            failures);
        expect_true(
            std::fabs(angular_difference_radians(position[0], longitudes[0])) <= 1.0e-9,
            "real station longitude output matches calc_position",
            failures);
        if (std::isfinite(station_case.expected_jd_ut)) {
            expect_near(
                events[0],
                station_case.expected_jd_ut,
                station_case.expected_tolerance_days,
                "real station oracle jd_ut",
                failures);
        }
        if (station_case.check_retrograde_turn) {
            double before[6] = {};
            double after[6] = {};
            expect_status(
                calc_position_ut(
                    &context,
                    station_case.body_id,
                    split_jd(events[0] - 0.25),
                    TAIYIN_NATIVE_POSITION_SPEED | TAIYIN_NATIVE_POSITION_RADIANS,
                    before,
                    &diagnostic),
                TAIYIN_STATUS_OK,
                "verify station speed before",
                failures);
            expect_status(
                calc_position_ut(
                    &context,
                    station_case.body_id,
                    split_jd(events[0] + 0.25),
                    TAIYIN_NATIVE_POSITION_SPEED | TAIYIN_NATIVE_POSITION_RADIANS,
                    after,
                    &diagnostic),
                TAIYIN_STATUS_OK,
                "verify station speed after",
                failures);
            expect_true(before[3] > 0.0, "Mercury 2003 speed is direct before station", failures);
            expect_true(after[3] < 0.0, "Mercury 2003 speed is retrograde after station", failures);
        }
    }
}

void test_greatest_elongation_opm2_semi_analytic_sanity(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    struct ElongationCase {
        const char* label;
        int body_id;
        double start_jd_ut;
        double end_jd_ut;
        uint32_t expected_kind;
        double min_elongation_deg;
        double max_elongation_deg;
        double jpl_jd_ut;
        double jpl_elongation_deg;
    };

    const ElongationCase cases[] = {
        {
            "Mercury eastern elongation 2024 Mar",
            TAIYIN_BODY_MERCURY,
            2460369.5,
            2460414.5,
            TAIYIN_GREATEST_ELONGATION_EASTERN,
            15.0,
            30.0,
            2460394.440334700048,
            18.701601185129,
        },
        {
            "Mercury western elongation 2024 May",
            TAIYIN_BODY_MERCURY,
            2460419.5,
            2460464.5,
            TAIYIN_GREATEST_ELONGATION_WESTERN,
            15.0,
            30.0,
            2460440.395385454409,
            26.365604783691,
        },
        {
            "Venus eastern elongation 2023 Jun",
            TAIYIN_BODY_VENUS,
            2460095.5,
            2460165.5,
            TAIYIN_GREATEST_ELONGATION_EASTERN,
            40.0,
            50.0,
            2460099.958895524964,
            45.399231306352,
        },
        {
            "Venus western elongation 2023 Oct",
            TAIYIN_BODY_VENUS,
            2460205.5,
            2460285.5,
            TAIYIN_GREATEST_ELONGATION_WESTERN,
            40.0,
            50.0,
            2460241.468374033459,
            46.413181096623,
        },
    };

    NativeCalcContext opm_context = make_context();
    expect_status(
        native_context_set_route_rule(&opm_context, TAIYIN_EPHEMERIS_ROUTE_OPM2),
        TAIYIN_STATUS_OK,
        "set OPM2 route for elongation sanity",
        failures);
    NativeCalcContext semi_analytic_context = make_context();
    expect_status(
        native_context_set_route_rule(&semi_analytic_context, TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC),
        TAIYIN_STATUS_OK,
        "set semi-analytical route for elongation sanity",
        failures);
    expect_true(opm_context.center_id == TAIYIN_BODY_EARTH, "elongation test enters from Earth-centered context", failures);
    expect_true(semi_analytic_context.center_id == TAIYIN_BODY_EARTH, "semi-analytical elongation test enters from Earth-centered context", failures);

    const uint64_t flags = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const ElongationCase& elongation_case = cases[i];
        EphemerisEvalDiagnostic opm_diagnostic;
        EphemerisEvalDiagnostic semi_analytic_diagnostic;
        GreatestElongationSearchResult opm_result;
        GreatestElongationSearchResult semi_analytic_result;
        const Status opm_status = search_greatest_elongation_ut(
            &opm_context,
            elongation_case.body_id,
            elongation_case.start_jd_ut,
            elongation_case.end_jd_ut,
            flags,
            &opm_result,
            &opm_diagnostic);
        expect_status(opm_status, TAIYIN_STATUS_OK, elongation_case.label, failures);
        if (opm_status != TAIYIN_STATUS_OK) {
            continue;
        }
        const Status semi_analytic_status = search_greatest_elongation_ut(
            &semi_analytic_context,
            elongation_case.body_id,
            elongation_case.start_jd_ut,
            elongation_case.end_jd_ut,
            flags,
            &semi_analytic_result,
            &semi_analytic_diagnostic);
        expect_status(semi_analytic_status, TAIYIN_STATUS_OK, "semi-analytical greatest elongation sanity", failures);
        if (semi_analytic_status != TAIYIN_STATUS_OK) {
            std::cerr << "  " << elongation_case.label
                      << " semi-analytical diagnostic status=" << semi_analytic_diagnostic.status
                      << " target=" << semi_analytic_diagnostic.target_id
                      << " center=" << semi_analytic_diagnostic.center_id
                      << " jd="
                      << split_julian_date_to_double(semi_analytic_diagnostic.jd_tdb) << "\n";
            continue;
        }

        const double opm_elongation_deg = opm_result.elongation_rad * TAIYIN_RAD_TO_DEG;
        expect_true(
            opm_result.kind == elongation_case.expected_kind,
            "OPM2 greatest elongation east/west kind",
            failures);
        expect_true(
            semi_analytic_result.kind == elongation_case.expected_kind,
            "semi-analytical greatest elongation east/west kind",
            failures);
        expect_true(
            opm_elongation_deg >= elongation_case.min_elongation_deg
                && opm_elongation_deg <= elongation_case.max_elongation_deg,
            "OPM2 greatest elongation magnitude range",
            failures);
        expect_near(
            opm_result.jd_ut,
            elongation_case.jpl_jd_ut,
            2.0 / 86400.0,
            "OPM2 greatest elongation tracks JPL Horizons time",
            failures);
        expect_near(
            opm_result.elongation_rad,
            elongation_case.jpl_elongation_deg * TAIYIN_DEG_TO_RAD,
            (0.1 / 3600.0) * TAIYIN_DEG_TO_RAD,
            "OPM2 greatest elongation tracks JPL Horizons angle",
            failures);
        expect_near(
            semi_analytic_result.jd_ut,
            opm_result.jd_ut,
            0.05,
            "semi-analytical greatest elongation time tracks OPM2",
            failures);
        expect_near(
            semi_analytic_result.elongation_rad,
            opm_result.elongation_rad,
            5.0e-5,
            "semi-analytical greatest elongation angle tracks OPM2",
            failures);
        expect_near(
            opm_result.phenomena.solar_elongation_rad,
            opm_result.elongation_rad,
            0.0,
            "phenomena elongation mirrors search elongation",
            failures);
        expect_true(
            opm_result.evaluation_count > 0 && semi_analytic_result.evaluation_count > 0,
            "greatest elongation reports evaluation counts",
            failures);
    }

    NativeCalcContext j2000_context = opm_context;
    j2000_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC;
    GreatestElongationSearchResult of_date_result;
    GreatestElongationSearchResult j2000_result;
    EphemerisEvalDiagnostic of_date_diagnostic;
    EphemerisEvalDiagnostic j2000_diagnostic;
    expect_status(
        search_greatest_elongation_ut(
            &opm_context,
            TAIYIN_BODY_VENUS,
            2460095.5,
            2460165.5,
            flags,
            &of_date_result,
            &of_date_diagnostic),
        TAIYIN_STATUS_OK,
        "greatest elongation of-date frame search",
        failures);
    expect_status(
        search_greatest_elongation_ut(
            &j2000_context,
            TAIYIN_BODY_VENUS,
            2460095.5,
            2460165.5,
            flags,
            &j2000_result,
            &j2000_diagnostic),
        TAIYIN_STATUS_OK,
        "greatest elongation J2000 frame search",
        failures);
    expect_near(
        j2000_result.jd_ut,
        of_date_result.jd_ut,
        5.0e-8,
        "greatest elongation time is frame-invariant",
        failures);
    expect_near(
        j2000_result.elongation_rad,
        of_date_result.elongation_rad,
        1.0e-12,
        "greatest elongation angle is frame-invariant",
        failures);
    expect_true(
        j2000_result.kind == of_date_result.kind,
        "greatest elongation direction remains consistent across ecliptic frames",
        failures);

    GreatestElongationSearchResult no_station_result;
    EphemerisEvalDiagnostic no_station_diagnostic;
    expect_status(
        search_greatest_elongation_ut(
            &opm_context,
            TAIYIN_BODY_MERCURY,
            2460369.5,
            2460370.5,
            flags,
            &no_station_result,
            &no_station_diagnostic),
        TAIYIN_EVENT_ERROR_NOT_FOUND,
        "greatest elongation returns not found when no stationary point is bracketed",
        failures);
    expect_true(
        no_station_result.body_id == 0 && no_station_result.jd_ut == 0.0,
        "greatest elongation not-found result is not polluted by interval maximum",
        failures);

    GreatestElongationSearchResult rejected_result;
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        search_greatest_elongation_ut(
            &opm_context,
            TAIYIN_BODY_MARS,
            2460369.5,
            2460414.5,
            flags,
            &rejected_result,
            &diagnostic),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "greatest elongation rejects non-inner planet body",
        failures);
}

void test_minimum_angular_separation_sanity(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext opm_context = make_context();
    expect_status(
        native_context_set_route_rule(&opm_context, TAIYIN_EPHEMERIS_ROUTE_OPM2),
        TAIYIN_STATUS_OK,
        "set OPM2 route for minimum angular separation",
        failures);
    NativeCalcContext semi_analytic_context = make_context();
    expect_status(
        native_context_set_route_rule(&semi_analytic_context, TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC),
        TAIYIN_STATUS_OK,
        "set semi-analytical route for minimum angular separation",
        failures);

    EphemerisEvalDiagnostic opm_diagnostic;
    EphemerisEvalDiagnostic semi_analytic_diagnostic;
    AngularSeparationSearchResult opm_result;
    AngularSeparationSearchResult semi_analytic_result;
    const Status opm_status = search_minimum_angular_separation_ut(
        &opm_context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        2460408.5,
        2460410.0,
        0.05,
        0,
        &opm_result,
        &opm_diagnostic);
    expect_status(opm_status, TAIYIN_STATUS_OK, "OPM2 minimum Sun-Moon angular separation", failures);
    if (opm_status != TAIYIN_STATUS_OK) {
        return;
    }
    const Status semi_analytic_status = search_minimum_angular_separation_ut(
        &semi_analytic_context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        2460408.5,
        2460410.0,
        0.05,
        0,
        &semi_analytic_result,
        &semi_analytic_diagnostic);
    expect_status(semi_analytic_status, TAIYIN_STATUS_OK, "semi-analytical minimum Sun-Moon angular separation", failures);
    if (semi_analytic_status != TAIYIN_STATUS_OK) {
        return;
    }

    expect_true(opm_result.body_a_id == TAIYIN_BODY_MOON, "minimum separation body A id", failures);
    expect_true(opm_result.body_b_id == TAIYIN_BODY_SUN, "minimum separation body B id", failures);
    expect_true(opm_result.separation_rad < 0.02, "minimum Sun-Moon separation is eclipse-scale", failures);
    // JPL Horizons vector oracle, target Moon(301) and Sun(10), center Earth
    // geocenter, TIME_TYPE=UT, VEC_CORR=LT+S, sampled at 10-second cadence
    // and quadratically fitted around the minimum.
    const double jpl_minimum_jd_ut = 2460409.262042756;
    const double jpl_minimum_separation_rad = 0.347680257505077 * TAIYIN_DEG_TO_RAD;
    expect_near(
        opm_result.jd,
        jpl_minimum_jd_ut,
        2.0 / 86400.0,
        "OPM2 minimum Sun-Moon separation tracks JPL Horizons time",
        failures);
    expect_near(
        opm_result.separation_rad,
        jpl_minimum_separation_rad,
        (0.1 / 3600.0) * TAIYIN_DEG_TO_RAD,
        "OPM2 minimum Sun-Moon separation tracks JPL Horizons angle",
        failures);
    if (std::fabs(opm_result.separation_rate_rad_per_day) > 1.0e-6) {
        std::cerr << "  minimum separation rate="
                  << opm_result.separation_rate_rad_per_day
                  << " jd=" << opm_result.jd
                  << " separation=" << opm_result.separation_rad << "\n";
    }
    expect_true(
        std::fabs(opm_result.separation_rate_rad_per_day) <= 1.0e-6,
        "minimum angular separation rate is near zero",
        failures);
    expect_near(
        semi_analytic_result.jd,
        opm_result.jd,
        0.02,
        "semi-analytical minimum angular separation time tracks OPM2",
        failures);
    expect_near(
        semi_analytic_result.separation_rad,
        opm_result.separation_rad,
        1.0e-4,
        "semi-analytical minimum angular separation tracks OPM2",
        failures);

    const double before = calc_angular_separation(
        &opm_context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        opm_result.jd - 0.05,
        &opm_diagnostic,
        failures);
    const double after = calc_angular_separation(
        &opm_context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        opm_result.jd + 0.05,
        &opm_diagnostic,
        failures);
    expect_true(before > opm_result.separation_rad, "minimum separation is below pre-sample", failures);
    expect_true(after > opm_result.separation_rad, "minimum separation is below post-sample", failures);

    AngularSeparationSearchResult rejected_result;
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        search_minimum_angular_separation_ut(
            &opm_context,
            TAIYIN_BODY_SUN,
            TAIYIN_BODY_SUN,
            2460408.5,
            2460410.0,
            0.05,
            0,
            &rejected_result,
            &diagnostic),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "minimum angular separation rejects identical bodies",
        failures);
    expect_status(
        search_minimum_angular_separation_ut(
            &opm_context,
            TAIYIN_BODY_MOON,
            TAIYIN_BODY_SUN,
            2460408.5,
            2460410.0,
            0.05,
            TAIYIN_NATIVE_POSITION_XYZ,
            &rejected_result,
            &diagnostic),
        TAIYIN_ERROR_UNSUPPORTED,
        "minimum angular separation rejects caller XYZ flag",
        failures);
}

void test_minimum_body_star_angular_separation_sanity(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    expect_status(
        add_global_tsc1_star_catalog(repo_fixed_star_catalog_path().c_str()),
        TAIYIN_STATUS_OK,
        "load traditional-star catalog for body-star separation",
        failures);

    NativeCalcContext context = make_context();
    expect_status(
        native_context_set_route_rule(&context, TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC),
        TAIYIN_STATUS_OK,
        "set semi-analytical route for body-star separation",
        failures);

    BodyStarAngularSeparationSearchResult result;
    EphemerisEvalDiagnostic diagnostic;
    const Status status = search_minimum_body_star_angular_separation_ut(
        &context,
        TAIYIN_BODY_SUN,
        "antares",
        split_jd(2460634.5),
        split_jd(2460654.5),
        1.0,
        TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX,
        &result,
        &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "minimum Sun-Antares separation", failures);
    if (status == TAIYIN_STATUS_OK) {
        expect_true(result.body_id == TAIYIN_BODY_SUN, "body-star result body id", failures);
        expect_true(
            result.jd > split_jd(2460634.5) && result.jd < split_jd(2460654.5),
            "Sun-Antares minimum lies inside search interval",
            failures);
        expect_true(
            result.separation_rad < 6.0 * TAIYIN_DEG_TO_RAD,
            "Sun-Antares conjunction reaches the expected ecliptic-latitude scale",
            failures);
        expect_true(
            std::fabs(result.separation_rate_rad_per_day) <= 1.0e-6,
            "body-star minimum separation rate is near zero",
            failures);
        expect_true(result.evaluation_count > 0, "body-star search evaluates targets", failures);
    }

    NativeCalcContext topocentric_context = context;
    const NativeObserverLocation changan =
        native_observer_location_degrees(108.94, 34.26, 400.0);
    expect_status(
        native_context_set_simple_topocentric_observer(
            &topocentric_context,
            changan,
            split_jd(2460644.5),
            split_jd(2460644.5)),
        TAIYIN_STATUS_OK,
        "set topocentric observer for body-star separation",
        failures);
    BodyStarAngularSeparationSearchResult topocentric_result;
    expect_status(
        search_minimum_body_star_angular_separation_ut(
            &topocentric_context,
            TAIYIN_BODY_SUN,
            "antares",
            split_jd(2460634.5),
            split_jd(2460654.5),
            1.0,
            TAIYIN_NATIVE_POSITION_TOPOCENTRIC
                | TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX,
            &topocentric_result,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "topocentric body-star separation applies one observer to both targets",
        failures);

    NativeCalcContext shifted_topocentric_context = context;
    expect_status(
        native_context_set_simple_topocentric_observer(
            &shifted_topocentric_context,
            changan,
            split_jd(2460644.75),
            split_jd(2460644.75)),
        TAIYIN_STATUS_OK,
        "set a differently seeded topocentric observer",
        failures);
    BodyStarAngularSeparationSearchResult moon_topocentric_result;
    BodyStarAngularSeparationSearchResult moon_shifted_topocentric_result;
    const uint64_t topocentric_flags =
        TAIYIN_NATIVE_POSITION_TOPOCENTRIC
        | TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX;
    expect_status(
        search_minimum_body_star_angular_separation_ut(
            &topocentric_context,
            TAIYIN_BODY_MOON,
            "antares",
            split_jd(2460634.5),
            split_jd(2460654.5),
            0.5,
            topocentric_flags,
            &moon_topocentric_result,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "topocentric Moon-star separation",
        failures);
    expect_status(
        search_minimum_body_star_angular_separation_ut(
            &shifted_topocentric_context,
            TAIYIN_BODY_MOON,
            "antares",
            split_jd(2460634.5),
            split_jd(2460654.5),
            0.5,
            topocentric_flags,
            &moon_shifted_topocentric_result,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "topocentric Moon-star separation ignores setter epoch",
        failures);
    expect_near(
        moon_shifted_topocentric_result.jd,
        moon_topocentric_result.jd,
        1.0e-12,
        "topocentric Moon-star minimum rebuilds observer at every sample",
        failures);
    expect_near(
        moon_shifted_topocentric_result.separation_rad,
        moon_topocentric_result.separation_rad,
        1.0e-14,
        "topocentric Moon-star separation ignores setter epoch",
        failures);

    NativeCalcContext explicit_offset_context = topocentric_context;
    const CartesianState explicit_offset =
        explicit_offset_context.apparent_options.observer_offset;
    expect_status(
        native_context_set_topocentric_observer_offset(
            &explicit_offset_context, explicit_offset),
        TAIYIN_STATUS_OK,
        "set explicit topocentric offset for angular-separation search",
        failures);
    expect_true(
        explicit_offset_context.topocentric_observer_model
            == TAIYIN_NATIVE_TOPOCENTRIC_OBSERVER_EXPLICIT_OFFSET,
        "explicit observer offset retains its setup mode",
        failures);
    expect_status(
        native_context_refresh_topocentric_observer(
            &explicit_offset_context, split_jd(2460645.0), split_jd(2460645.0)),
        TAIYIN_STATUS_OK,
        "explicit observer offset is not rebuilt for a search sample",
        failures);
    expect_near(
        explicit_offset_context.apparent_options.observer_offset.position_au.x,
        explicit_offset.position_au.x,
        0.0,
        "explicit observer offset x survives sample refresh",
        failures);
    expect_near(
        explicit_offset_context.apparent_options.observer_offset.position_au.y,
        explicit_offset.position_au.y,
        0.0,
        "explicit observer offset y survives sample refresh",
        failures);
    expect_near(
        explicit_offset_context.apparent_options.observer_offset.position_au.z,
        explicit_offset.position_au.z,
        0.0,
        "explicit observer offset z survives sample refresh",
        failures);

    BodyStarAngularSeparationSearchResult tt_result;
    expect_status(
        search_minimum_body_star_angular_separation_tt(
            &context,
            TAIYIN_BODY_SUN,
            "antares",
            split_jd(2460634.5),
            split_jd(2460654.5),
            1.0,
            0u,
            &tt_result,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "minimum Sun-Antares separation TT",
        failures);
    expect_near(
        tt_result.separation_rad,
        result.separation_rad,
        1.0e-8,
        "UT and TT body-star minima agree in angle",
        failures);

    SplitJulianDate historical_start;
    expect_true(
        julian_day_split({833, 6, 9, 0, 0, 0.0}, &historical_start),
        "construct Tang-era body-star search date",
        failures);
    double historical_star_position[6] = {};
    expect_status(
        calc_star_position_ut(
            &context,
            "antares",
            historical_start,
            TAIYIN_NATIVE_POSITION_XYZ | TAIYIN_NATIVE_POSITION_SPEED,
            historical_star_position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "heliocentric historical star position needs no approximation flag",
        failures);

    NativeCalcContext mars_observer = context;
    expect_status(
        native_context_set_geocentric_observer(
            &mars_observer,
            TAIYIN_BODY_MARS_BARYCENTER,
            TAIYIN_BODY_SUN),
        TAIYIN_STATUS_OK,
        "set Mars-barycenter observer",
        failures);
    double mars_true_star[6] = {};
    expect_status(
        calc_star_position_ut(
            &mars_observer,
            "antares",
            historical_start,
            TAIYIN_NATIVE_POSITION_XYZ | TAIYIN_NATIVE_POSITION_TRUEPOS,
            mars_true_star,
            &diagnostic),
        TAIYIN_ERROR_UNSUPPORTED,
        "fixed-star position rejects non-Earth observer",
        failures);

    ObservedPosition mars_observed;
    expect_status(
        calc_observed_star_ut(
            &mars_observer,
            "antares",
            historical_start,
            0u,
            &mars_observed,
            &diagnostic),
        TAIYIN_ERROR_UNSUPPORTED,
        "observed fixed star rejects non-Earth observer",
        failures);

    expect_status(
        calc_observed_star_ut(
            &mars_observer,
            "antares",
            historical_start,
            TAIYIN_OBSERVED_TOPOCENTRIC | TAIYIN_OBSERVED_HORIZONTAL,
            &mars_observed,
            &diagnostic),
        TAIYIN_ERROR_UNSUPPORTED,
        "topocentric fixed-star observation rejects non-Earth observer",
        failures);

    BodyStarAngularSeparationSearchResult non_earth_search;
    expect_status(
        search_minimum_body_star_angular_separation_ut(
            &mars_observer,
            TAIYIN_BODY_SUN,
            "antares",
            historical_start,
            historical_start + 2.0,
            0.25,
            0u,
            &non_earth_search,
            &diagnostic),
        TAIYIN_ERROR_UNSUPPORTED,
        "body-star search rejects non-Earth observer",
        failures);

    BodyStarAngularSeparationSearchResult historical_result;
    expect_status(
        search_minimum_body_star_angular_separation_ut(
            &context,
            TAIYIN_BODY_MARS,
            "antares",
            historical_start,
            historical_start + 2.0,
            0.25,
            TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX,
            &historical_result,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "Tang-era Mars-Antares minimum uses barycenter approximation",
        failures);
    expect_true(
        historical_result.separation_rad < 3.0 * TAIYIN_DEG_TO_RAD,
        "Tang-era Mars remains near Antares",
        failures);

    BodyStarAngularSeparationSearchResult rejected;
    expect_status(
        search_minimum_body_star_angular_separation_ut(
            &context,
            TAIYIN_BODY_SUN,
            "",
            split_jd(2460634.5),
            split_jd(2460654.5),
            1.0,
            0u,
            &rejected,
            &diagnostic),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "body-star search rejects empty star key",
        failures);
    expect_status(
        search_minimum_body_star_angular_separation_ut(
            &context,
            TAIYIN_BODY_SUN,
            "not-a-real-star",
            split_jd(2460634.5),
            split_jd(2460654.5),
            1.0,
            0u,
            &rejected,
            &diagnostic),
        TAIYIN_FILE_ERROR_NOT_FOUND,
        "body-star search reports missing star",
        failures);
}

void test_solar_transit_search_sanity(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    expect_status(
        native_context_set_route_rule(&context, TAIYIN_EPHEMERIS_ROUTE_OPM2),
        TAIYIN_STATUS_OK,
        "set OPM2 route for solar transit",
        failures);

    EphemerisEvalDiagnostic diagnostic;
    const SolarTransitCatalogOracle catalog_cases[] = {
        {
            "Mercury 2006",
            TAIYIN_BODY_MERCURY,
            jd_utc(2006, 11, 8, 18, 0),
            jd_utc(2006, 11, 9, 1, 0),
            jd_utc(2006, 11, 8, 19, 12),
            jd_utc(2006, 11, 8, 19, 14),
            jd_utc(2006, 11, 8, 21, 41),
            jd_utc(2006, 11, 9, 0, 8),
            jd_utc(2006, 11, 9, 0, 10),
            422.9,
            0.15,
            0.35,
            false,
        },
        {
            "Mercury 2016",
            TAIYIN_BODY_MERCURY,
            jd_utc(2016, 5, 9, 10, 0),
            jd_utc(2016, 5, 9, 20, 0),
            jd_utc(2016, 5, 9, 11, 12),
            jd_utc(2016, 5, 9, 11, 15),
            jd_utc(2016, 5, 9, 14, 57),
            jd_utc(2016, 5, 9, 18, 39),
            jd_utc(2016, 5, 9, 18, 42),
            318.5,
            0.25,
            0.40,
            false,
        },
        {
            "Mercury 2019",
            TAIYIN_BODY_MERCURY,
            jd_utc(2019, 11, 11, 12, 0),
            jd_utc(2019, 11, 11, 19, 0),
            jd_utc(2019, 11, 11, 12, 35),
            jd_utc(2019, 11, 11, 12, 37),
            jd_utc(2019, 11, 11, 15, 20),
            jd_utc(2019, 11, 11, 18, 2),
            jd_utc(2019, 11, 11, 18, 4),
            75.9,
            0.20,
            0.35,
            true,
        },
        {
            "Venus 2012",
            TAIYIN_BODY_VENUS,
            jd_utc(2012, 6, 5, 20, 0),
            jd_utc(2012, 6, 6, 6, 0),
            jd_utc(2012, 6, 5, 22, 9),
            jd_utc(2012, 6, 5, 22, 27),
            jd_utc(2012, 6, 6, 1, 29),
            jd_utc(2012, 6, 6, 4, 31),
            jd_utc(2012, 6, 6, 4, 49),
            554.4,
            0.20,
            0.35,
            false,
        },
        {
            "Venus 2004",
            TAIYIN_BODY_VENUS,
            jd_utc(2004, 6, 8, 4, 0),
            jd_utc(2004, 6, 8, 12, 30),
            jd_utc(2004, 6, 8, 5, 13),
            jd_utc(2004, 6, 8, 5, 33),
            jd_utc(2004, 6, 8, 8, 20),
            jd_utc(2004, 6, 8, 11, 7),
            jd_utc(2004, 6, 8, 11, 26),
            626.9,
            0.20,
            0.35,
            false,
        },
    };

    SolarTransitSearchResult mercury_2019;
    bool saw_mercury_2019 = false;
    SolarTransitSearchResult venus_2012;
    bool saw_venus_2012 = false;
    for (const SolarTransitCatalogOracle& c : catalog_cases) {
        SolarTransitSearchResult result;
        const Status status = search_next_solar_transit_ut(
            &context,
            c.body_id,
            c.search_start_jd_ut,
            0,
            &result,
            &diagnostic);
        expect_status(
            status,
            TAIYIN_STATUS_OK,
            (std::string(c.label) + " solar transit search").c_str(),
            failures);
        if (status != TAIYIN_STATUS_OK) {
            continue;
        }

        expect_true(
            result.body_id == c.body_id,
            (std::string(c.label) + " solar transit body id").c_str(),
            failures);
        expect_true(
            (result.kind & TAIYIN_SOLAR_TRANSIT_FULL_DISK) != 0u,
            (std::string(c.label) + " transit is full-disk").c_str(),
            failures);
        expect_true(
            result.t1_jd_ut < result.t2_jd_ut
                && result.t2_jd_ut < result.greatest_jd_ut
                && result.greatest_jd_ut < result.t3_jd_ut
                && result.t3_jd_ut < result.t4_jd_ut,
            (std::string(c.label) + " transit contacts are ordered").c_str(),
            failures);
        expect_true(
            result.minimum_separation_rad < result.sun_radius_rad - result.body_radius_rad,
            (std::string(c.label) + " minimum separation is inside interior contact radius").c_str(),
            failures);
        // NASA transit catalogs give geocentric UT contact times rounded to
        // the nearest minute and minimum separation rounded to 0.1 arcsec.
        const double nasa_contact_tolerance_days = 90.0 / 86400.0;
        expect_near(
            result.t1_jd_ut,
            c.t1_jd_ut,
            nasa_contact_tolerance_days,
            (std::string(c.label) + " transit T1 tracks NASA catalog").c_str(),
            failures);
        expect_near(
            result.t2_jd_ut,
            c.t2_jd_ut,
            nasa_contact_tolerance_days,
            (std::string(c.label) + " transit T2 tracks NASA catalog").c_str(),
            failures);
        expect_near(
            result.greatest_jd_ut,
            c.greatest_jd_ut,
            nasa_contact_tolerance_days,
            (std::string(c.label) + " greatest transit tracks NASA catalog").c_str(),
            failures);
        expect_near(
            result.t3_jd_ut,
            c.t3_jd_ut,
            nasa_contact_tolerance_days,
            (std::string(c.label) + " transit T3 tracks NASA catalog").c_str(),
            failures);
        expect_near(
            result.t4_jd_ut,
            c.t4_jd_ut,
            nasa_contact_tolerance_days,
            (std::string(c.label) + " transit T4 tracks NASA catalog").c_str(),
            failures);
        expect_near(
            result.minimum_separation_rad,
            c.minimum_separation_arcsec / 3600.0 * TAIYIN_DEG_TO_RAD,
            0.5 / 3600.0 * TAIYIN_DEG_TO_RAD,
            (std::string(c.label) + " transit minimum separation tracks NASA catalog").c_str(),
            failures);
        expect_true(
            (result.t4_jd_ut - result.t1_jd_ut) > c.duration_min_days
                && (result.t4_jd_ut - result.t1_jd_ut) < c.duration_max_days,
            (std::string(c.label) + " transit duration is plausible").c_str(),
            failures);
        expect_true(
            result.evaluation_count > 0,
            (std::string(c.label) + " transit reports evaluations").c_str(),
            failures);

        if (c.run_jpl_vector_oracle) {
            mercury_2019 = result;
            saw_mercury_2019 = true;
        }
        if (std::string(c.label) == "Venus 2012") {
            venus_2012 = result;
            saw_venus_2012 = true;
        }
    }

    expect_true(saw_mercury_2019, "Mercury 2019 JPL vector oracle case is exercised", failures);
    if (saw_mercury_2019) {
        // JPL Horizons vector-derived oracle:
        // Mercury(199) and Sun(10), center=Earth geocenter, EPHEM_TYPE=VECTORS,
        // TIME_TYPE=UT, VEC_CORR=LT+S, 2019-11-11 12:30..18:10 UT with
        // 2041 output steps (~10-second cadence). Contacts are roots of the
        // same apparent-radius convention as Taiyin; greatest transit is a
        // 7-point quadratic fit around minimum angular separation.
        expect_near(
            mercury_2019.t1_jd_ut,
            2458799.024617881048,
            1.0 / 86400.0,
            "Mercury 2019 T1 tracks JPL vector oracle",
            failures);
        expect_near(
            mercury_2019.t2_jd_ut,
            2458799.025791218039,
            1.0 / 86400.0,
            "Mercury 2019 T2 tracks JPL vector oracle",
            failures);
        expect_near(
            mercury_2019.greatest_jd_ut,
            2458799.138751322404,
            1.0 / 86400.0,
            "Mercury 2019 greatest transit tracks JPL vector oracle",
            failures);
        expect_near(
            mercury_2019.t3_jd_ut,
            2458799.251772006042,
            1.0 / 86400.0,
            "Mercury 2019 T3 tracks JPL vector oracle",
            failures);
        expect_near(
            mercury_2019.t4_jd_ut,
            2458799.252945519518,
            1.0 / 86400.0,
            "Mercury 2019 T4 tracks JPL vector oracle",
            failures);
        expect_near(
            mercury_2019.minimum_separation_rad,
            75.935175896432 / 3600.0 * TAIYIN_DEG_TO_RAD,
            0.01 / 3600.0 * TAIYIN_DEG_TO_RAD,
            "Mercury 2019 minimum separation tracks JPL vector oracle",
            failures);

        SolarTransitSearchResult previous;
        expect_status(
            search_next_solar_transit_ut(
                &context,
                TAIYIN_BODY_MERCURY,
                mercury_2019.t4_jd_ut + 30.0 / 86400.0,
                TAIYIN_EVENT_SEARCH_REVERSE,
                &previous,
                &diagnostic),
            TAIYIN_STATUS_OK,
            "solar transit reverse search finds previous event",
            failures);
        expect_near(
            previous.greatest_jd_ut,
            mercury_2019.greatest_jd_ut,
            1.0e-6,
            "solar transit reverse search returns Mercury 2019",
            failures);
    }
    expect_true(saw_venus_2012, "Venus 2012 catalog oracle case is exercised", failures);
    if (saw_venus_2012) {
        SolarTransitSearchResult previous_venus;
        expect_status(
            search_next_solar_transit_ut(
                &context,
                TAIYIN_BODY_VENUS,
                venus_2012.t4_jd_ut + 30.0 / 86400.0,
                TAIYIN_EVENT_SEARCH_REVERSE,
                &previous_venus,
                &diagnostic),
            TAIYIN_STATUS_OK,
            "solar transit reverse search finds previous Venus event",
            failures);
        expect_near(
            previous_venus.greatest_jd_ut,
            venus_2012.greatest_jd_ut,
            1.0e-6,
            "solar transit reverse search returns Venus 2012",
            failures);
    }

    NativeCalcContext icrf_output_context = context;
    icrf_output_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_ICRF;
    SolarTransitSearchResult icrf_output_result;
    expect_status(
        search_next_solar_transit_ut(
            &icrf_output_context,
            TAIYIN_BODY_MERCURY,
            jd_utc(2019, 11, 11, 12, 0),
            0,
            &icrf_output_result,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "solar transit candidate search ignores caller output frame",
        failures);
    if (saw_mercury_2019) {
        expect_near(
            icrf_output_result.greatest_jd_ut,
            mercury_2019.greatest_jd_ut,
            1.0e-6,
            "solar transit ICRF caller frame returns Mercury 2019",
            failures);
    }

    SolarTransitSearchResult none;
    expect_status(
        search_next_solar_transit_ut(
            &context,
            TAIYIN_BODY_MARS,
            2458798.0,
            0,
            &none,
            &diagnostic),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "solar transit rejects outer planet",
        failures);

    expect_status(
        search_next_solar_transit_ut(
            &context,
            TAIYIN_BODY_MERCURY,
            2458798.0,
            TAIYIN_NATIVE_POSITION_XYZ,
            &none,
            &diagnostic),
        TAIYIN_ERROR_UNSUPPORTED,
        "solar transit rejects caller XYZ flag",
        failures);

    expect_status(
        search_next_solar_transit_ut(
            &context,
            TAIYIN_BODY_MERCURY,
            2458798.0,
            TAIYIN_NATIVE_POSITION_TOPOCENTRIC,
            &none,
            &diagnostic),
        TAIYIN_ERROR_UNSUPPORTED,
        "solar transit rejects topocentric native flag",
        failures);

    NativeCalcContext topocentric_context = context;
    const NativeObserverLocation denver =
        native_observer_location_degrees(-104.9903, 39.7392, 1609.3);
    expect_status(
        native_context_set_simple_topocentric_observer(
            &topocentric_context,
            denver,
            split_jd(2458799.0),
            split_jd(2458799.0)),
        TAIYIN_STATUS_OK,
        "set topocentric context for solar transit rejection",
        failures);
    expect_status(
        search_next_solar_transit_ut(
            &topocentric_context,
            TAIYIN_BODY_MERCURY,
            2458798.0,
            0,
            &none,
            &diagnostic),
        TAIYIN_ERROR_UNSUPPORTED,
        "solar transit rejects topocentric context",
        failures);
}

taiyin::Status hard_scan_next_solar_transit_ut(
    const taiyin::runtime::NativeCalcContext& context,
    int body_id,
    double start_jd_ut,
    bool reverse,
    double max_distance_days,
    uint64_t position_flags,
    double* out_greatest_jd_ut,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
);

void expect_de441_hard_scan_solar_transit(
    const taiyin::runtime::NativeCalcContext& context,
    int body_id,
    double start_jd_ut,
    uint64_t flags,
    double max_distance_days,
    const char* label,
    int* failures
) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    EphemerisEvalDiagnostic diagnostic;
    SolarTransitSearchResult result;
    const Status status = search_next_solar_transit_ut(
        &context,
        body_id,
        start_jd_ut,
        flags,
        &result,
        &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, label, failures);
    if (status != TAIYIN_STATUS_OK) {
        return;
    }

    const bool reverse = (flags & TAIYIN_EVENT_SEARCH_REVERSE) != 0u;
    expect_true(
        reverse ? result.greatest_jd_ut < start_jd_ut : result.greatest_jd_ut > start_jd_ut,
        (std::string(label) + " respects search direction").c_str(),
        failures);
    expect_true(
        std::fabs(split_julian_date_to_double(result.greatest_jd_ut) - start_jd_ut)
            < max_distance_days,
        (std::string(label) + " does not jump across unrelated cycles").c_str(),
        failures);
    expect_true(
        result.t1_jd_ut < result.t2_jd_ut
            && result.t2_jd_ut < result.greatest_jd_ut
            && result.greatest_jd_ut < result.t3_jd_ut
            && result.t3_jd_ut < result.t4_jd_ut,
        (std::string(label) + " contacts are ordered").c_str(),
        failures);
    expect_true(
        result.minimum_separation_rad < result.sun_radius_rad + result.body_radius_rad,
        (std::string(label) + " is a disk-crossing candidate").c_str(),
        failures);

    double hard_greatest_jd_ut = NAN;
    const Status hard_status = hard_scan_next_solar_transit_ut(
        context,
        body_id,
        start_jd_ut,
        reverse,
        max_distance_days,
        flags & TAIYIN_EVENT_SEARCH_POSITION_FLAGS_MASK,
        &hard_greatest_jd_ut,
        &diagnostic);
    expect_status(
        hard_status,
        TAIYIN_STATUS_OK,
        (std::string(label) + " hard scan finds transit").c_str(),
        failures);
    if (hard_status != TAIYIN_STATUS_OK) {
        std::cerr << "DEBUG hard scan miss: " << label
                  << " start=" << start_jd_ut
                  << " k_greatest=" << result.greatest_jd_ut
                  << " delta_days=" << (result.greatest_jd_ut - start_jd_ut)
                  << " reverse=" << reverse << "\n";
    }
    if (hard_status == TAIYIN_STATUS_OK) {
        expect_near(
            result.greatest_jd_ut,
            hard_greatest_jd_ut,
            2.0 / 86400.0,
            (std::string(label) + " k search matches hard scan").c_str(),
            failures);
    }
}

int test_physical_inner_planet_body(int body_id) {
    switch (body_id) {
    case taiyin::TAIYIN_BODY_MERCURY_BARYCENTER:
        return taiyin::TAIYIN_BODY_MERCURY;
    case taiyin::TAIYIN_BODY_VENUS_BARYCENTER:
        return taiyin::TAIYIN_BODY_VENUS;
    default:
        return body_id;
    }
}

int test_ephemeris_inner_planet_body(int body_id) {
    switch (body_id) {
    case taiyin::TAIYIN_BODY_MERCURY:
        return taiyin::TAIYIN_BODY_MERCURY_BARYCENTER;
    case taiyin::TAIYIN_BODY_VENUS:
        return taiyin::TAIYIN_BODY_VENUS_BARYCENTER;
    default:
        return body_id;
    }
}

double hard_scan_minimum_step_days(int body_id) {
    switch (body_id) {
    case taiyin::TAIYIN_BODY_VENUS:
    case taiyin::TAIYIN_BODY_VENUS_BARYCENTER:
        return 20.0;
    default:
        return 5.0;
    }
}

double hard_scan_chunk_span_days(int body_id) {
    switch (body_id) {
    case taiyin::TAIYIN_BODY_VENUS:
    case taiyin::TAIYIN_BODY_VENUS_BARYCENTER:
        return 500.0;
    default:
        return 100.0;
    }
}

double hard_scan_xyz_distance_au(const double* xyz) {
    return std::sqrt(xyz[0] * xyz[0] + xyz[1] * xyz[1] + xyz[2] * xyz[2]);
}

taiyin::Status hard_scan_body_is_closer_than_sun(
    const taiyin::runtime::NativeCalcContext& context,
    int body_id,
    double jd_ut,
    uint64_t flags,
    bool* out_closer,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    if (!out_closer) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out_closer = false;
    double body_xyz[6] = {};
    Status status = calc_position_ut(
        &context,
        test_ephemeris_inner_planet_body(body_id),
        split_jd(jd_ut),
        static_cast<uint32_t>(flags) | TAIYIN_NATIVE_POSITION_XYZ,
        body_xyz,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    double sun_xyz[6] = {};
    status = calc_position_ut(
        &context,
        TAIYIN_BODY_SUN,
        split_jd(jd_ut),
        static_cast<uint32_t>(flags) | TAIYIN_NATIVE_POSITION_XYZ,
        sun_xyz,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    const double body_distance = hard_scan_xyz_distance_au(body_xyz);
    const double sun_distance = hard_scan_xyz_distance_au(sun_xyz);
    if (!std::isfinite(body_distance) || !std::isfinite(sun_distance)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    *out_closer = body_distance < sun_distance;
    return TAIYIN_STATUS_OK;
}

taiyin::Status hard_scan_apparent_radius_sum_rad(
    const taiyin::runtime::NativeCalcContext& context,
    int body_id,
    double jd_ut,
    uint64_t flags,
    double* out_radius_sum_rad,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    if (!out_radius_sum_rad) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out_radius_sum_rad = NAN;

    BodyPhenomena sun;
    Status status = calc_body_phenomena_ut(
        &context,
        TAIYIN_BODY_SUN,
        split_jd(jd_ut),
        flags,
        &sun,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    BodyPhenomena body;
    status = calc_body_phenomena_ut(
        &context,
        test_physical_inner_planet_body(body_id),
        split_jd(jd_ut),
        flags,
        &body,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!std::isfinite(sun.apparent_diameter_rad)
        || !std::isfinite(body.apparent_diameter_rad)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    *out_radius_sum_rad = 0.5 * (sun.apparent_diameter_rad + body.apparent_diameter_rad);
    return TAIYIN_STATUS_OK;
}

taiyin::Status hard_scan_next_solar_transit_ut(
    const taiyin::runtime::NativeCalcContext& context,
    int body_id,
    double start_jd_ut,
    bool reverse,
    double max_distance_days,
    uint64_t position_flags,
    double* out_greatest_jd_ut,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    if (!out_greatest_jd_ut || !std::isfinite(start_jd_ut) || !(max_distance_days > 0.0)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_greatest_jd_ut = NAN;

    const double global_start = reverse ? start_jd_ut - max_distance_days : start_jd_ut;
    const double global_end = reverse ? start_jd_ut : start_jd_ut + max_distance_days;
    const double span = hard_scan_chunk_span_days(body_id);
    const double stride = 0.5 * span;
    for (double cursor = start_jd_ut;
         reverse ? cursor > global_start : cursor < global_end;
         cursor += reverse ? -stride : stride) {
        const double chunk_start = reverse ? std::max(global_start, cursor - span) : cursor;
        const double chunk_end = reverse ? cursor : std::min(global_end, cursor + span);
        if (!(chunk_start < chunk_end)) {
            continue;
        }
        AngularSeparationSearchResult minimum;
        Status status = search_minimum_angular_separation_ut(
            &context,
            test_ephemeris_inner_planet_body(body_id),
            TAIYIN_BODY_SUN,
            chunk_start,
            chunk_end,
            hard_scan_minimum_step_days(body_id),
            position_flags,
            &minimum,
            diagnostic);
        if (status == TAIYIN_EVENT_ERROR_NOT_FOUND) {
            continue;
        }
        if (status != TAIYIN_STATUS_OK) {
            return status;
        }

        bool body_is_closer = false;
        status = hard_scan_body_is_closer_than_sun(
            context,
            body_id,
            split_julian_date_to_double(minimum.jd),
            position_flags,
            &body_is_closer,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        if (!body_is_closer) {
            continue;
        }

        double radius_sum = NAN;
        status = hard_scan_apparent_radius_sum_rad(
            context,
            body_id,
            split_julian_date_to_double(minimum.jd),
            position_flags,
            &radius_sum,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        if (minimum.separation_rad <= radius_sum) {
            *out_greatest_jd_ut = split_julian_date_to_double(minimum.jd);
            return TAIYIN_STATUS_OK;
        }
    }
    return TAIYIN_EVENT_ERROR_NOT_FOUND;
}

void test_solar_transit_de441_hard_scan_sanity(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    const std::string de441 = external_de441_path();
    if (!file_exists(de441)) {
        std::cout << "SKIP: set TAIYIN_DE441_PATH or TAIYIN_NASA_BSP_ROOT to run DE441 transit boundary test\n";
        return;
    }
    if (!initialize_de441_runtime(de441, failures)) {
        return;
    }

    NativeCalcContext context = make_context();
    expect_status(
        native_context_set_route_rule(&context, TAIYIN_EPHEMERIS_ROUTE_SPK),
        TAIYIN_STATUS_OK,
        "set SPK route for DE441 transit hard scan",
        failures);

    struct HardScanCase {
        const char* label;
        int body_id;
        double start_year;
        bool reverse;
        double max_years;
    };
    const HardScanCase cases[] = {
        { "DE441 ancient-edge Mercury forward", TAIYIN_BODY_MERCURY_BARYCENTER, -13150.0, false, 60.0 },
        { "DE441 ancient-mid Mercury forward", TAIYIN_BODY_MERCURY_BARYCENTER, -8000.0, false, 60.0 },
        { "DE441 near-modern Mercury forward", TAIYIN_BODY_MERCURY_BARYCENTER, 2000.0, false, 60.0 },
        { "DE441 near-modern Mercury reverse", TAIYIN_BODY_MERCURY_BARYCENTER, 2020.0, true, 60.0 },
        { "DE441 future-mid Mercury reverse", TAIYIN_BODY_MERCURY_BARYCENTER, 9000.0, true, 60.0 },
        { "DE441 future-edge Mercury reverse", TAIYIN_BODY_MERCURY_BARYCENTER, 17150.0, true, 60.0 },
        { "DE441 ancient-edge Venus forward", TAIYIN_BODY_VENUS_BARYCENTER, -13100.0, false, 300.0 },
        { "DE441 ancient-mid Venus forward", TAIYIN_BODY_VENUS_BARYCENTER, -8000.0, false, 300.0 },
        { "DE441 near-modern Venus forward", TAIYIN_BODY_VENUS_BARYCENTER, 2000.0, false, 300.0 },
        { "DE441 near-modern Venus reverse", TAIYIN_BODY_VENUS_BARYCENTER, 2013.0, true, 300.0 },
        { "DE441 future-mid Venus reverse", TAIYIN_BODY_VENUS_BARYCENTER, 9000.0, true, 300.0 },
        { "DE441 future-edge Venus reverse", TAIYIN_BODY_VENUS_BARYCENTER, 17100.0, true, 300.0 },
    };
    for (const HardScanCase& c : cases) {
        expect_de441_hard_scan_solar_transit(
            context,
            c.body_id,
            jd_year_approx(c.start_year),
            c.reverse ? TAIYIN_EVENT_SEARCH_REVERSE : 0u,
            c.max_years * 365.2425,
            c.label,
            failures);
    }
}

void test_local_solar_transit_search_sanity(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    expect_status(
        native_context_set_route_rule(&context, TAIYIN_EPHEMERIS_ROUTE_OPM2),
        TAIYIN_STATUS_OK,
        "set OPM2 route for local solar transit",
        failures);

    EphemerisEvalDiagnostic diagnostic;
    LocalSolarTransitSearchResult missing_atmosphere;
    expect_status(
        search_next_local_solar_transit_ut(
            &context,
            TAIYIN_BODY_MERCURY,
            jd_utc(2019, 11, 11, 12, 0),
            -74.0060,
            40.7128,
            10.0,
            0,
            &missing_atmosphere,
            &diagnostic),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "local solar transit default refraction requires atmosphere",
        failures);

    expect_status(
        native_context_set_atmosphere(&context, native_standard_atmosphere()),
        TAIYIN_STATUS_OK,
        "set standard atmosphere for local solar transit",
        failures);

    LocalSolarTransitSearchResult local;
    const Status status = search_next_local_solar_transit_ut(
        &context,
        TAIYIN_BODY_MERCURY,
        jd_utc(2019, 11, 11, 12, 0),
        -74.0060,
        40.7128,
        10.0,
        0,
        &local,
        &diagnostic);
    expect_status(
        status,
        TAIYIN_STATUS_OK,
        "local solar transit search succeeds with atmosphere",
        failures);
    if (status != TAIYIN_STATUS_OK) {
        return;
    }

    expect_true(
        local.global.body_id == TAIYIN_BODY_MERCURY
            && local.topocentric.body_id == TAIYIN_BODY_MERCURY,
        "local solar transit preserves body id",
        failures);
    expect_true(
        (local.topocentric.kind & TAIYIN_SOLAR_TRANSIT_FULL_DISK) != 0u,
        "local solar transit remains full-disk",
        failures);
    expect_true(
        local.topocentric.t1_jd_ut < local.topocentric.t2_jd_ut
            && local.topocentric.t2_jd_ut < local.topocentric.greatest_jd_ut
            && local.topocentric.greatest_jd_ut < local.topocentric.t3_jd_ut
            && local.topocentric.t3_jd_ut < local.topocentric.t4_jd_ut,
        "local topocentric transit contacts are ordered",
        failures);
    expect_true(
        std::fabs(local.topocentric.greatest_jd_ut - local.global.greatest_jd_ut) < 30.0 / 86400.0,
        "local topocentric greatest remains near geocentric greatest",
        failures);
    expect_true(
        std::fabs(local.topocentric.greatest_jd_ut - local.global.greatest_jd_ut) > 0.05 / 86400.0,
        "local topocentric greatest differs from geocentric greatest",
        failures);
    expect_true(
        (local.visibility_flags & TAIYIN_SOLAR_TRANSIT_VISIBLE_AT_OBSERVER) != 0u,
        "local solar transit is visible at New York",
        failures);
    expect_true(
        (local.visibility_flags & TAIYIN_SOLAR_TRANSIT_GREATEST_VISIBLE) != 0u,
        "local solar transit greatest is visible",
        failures);
    for (size_t i = 0; i < TAIYIN_SOLAR_TRANSIT_CONTACT_COUNT; ++i) {
        expect_true(
            std::isfinite(local.contact_sun_altitude_deg[i])
                && std::isfinite(local.contact_sun_azimuth_deg[i]),
            "local solar transit contact horizontal coordinates are finite",
            failures);
    }

    NativeCalcContext no_refraction_context = make_context();
    expect_status(
        native_context_set_route_rule(&no_refraction_context, TAIYIN_EPHEMERIS_ROUTE_OPM2),
        TAIYIN_STATUS_OK,
        "set OPM2 route for local no-refraction transit",
        failures);
    LocalSolarTransitSearchResult no_refraction;
    expect_status(
        search_next_local_solar_transit_ut(
            &no_refraction_context,
            TAIYIN_BODY_MERCURY,
            jd_utc(2019, 11, 11, 12, 0),
            -74.0060,
            40.7128,
            10.0,
            TAIYIN_EVENT_SEARCH_NO_REFRACTION,
            &no_refraction,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "local solar transit no-refraction path works without atmosphere",
        failures);
}

void test_mars_2003_opposition_seds_reference(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    double events[2] = {};
    size_t event_count = 0;

    // SEDS Mars 2003 reference: opposition on 2003-08-28 17:58:49 UT.
    // The packaged OPM2 major-body data currently routes Mars through the
    // system barycenter; COB data can tighten this later without changing the
    // event-search semantics.
    const Status status = search_body_aspect_crossings_ut(
        &context,
        TAIYIN_BODY_MARS_BARYCENTER,
        TAIYIN_BODY_SUN,
        TAIYIN_PI,
        JD_UT_MARS_2003_OPPOSITION_SEDS - 8.0,
        JD_UT_MARS_2003_OPPOSITION_SEDS + 8.0,
        0.25,
        0,
        events,
        2,
        &event_count,
        &diagnostic);
    expect_status(status, TAIYIN_STATUS_OK, "2003 Mars opposition search", failures);
    if (status != TAIYIN_STATUS_OK) {
        std::cerr << "  diagnostic target=" << diagnostic.target_id
                  << " status=" << diagnostic.status << "\n";
        return;
    }

    expect_true(event_count == 1, "2003 Mars opposition count", failures);
    if (event_count == 0) {
        return;
    }
    expect_near(
        events[0],
        JD_UT_MARS_2003_OPPOSITION_SEDS,
        10.0 / 86400.0,
        "2003 Mars opposition JD vs SEDS",
        failures);
    expect_aspect_at(
        &context,
        TAIYIN_BODY_MARS_BARYCENTER,
        TAIYIN_BODY_SUN,
        events[0],
        TAIYIN_PI,
        "verified 2003 Mars opposition",
        failures);

    const double major_aspects[] = {
        0.0,
        TAIYIN_PI / 3.0,
        TAIYIN_PI / 2.0,
        2.0 * TAIYIN_PI / 3.0,
        TAIYIN_PI,
    };
    double exact_events[4] = {};
    double exact_targets[4] = {};
    size_t exact_count = 0;
    const Status exact_status = search_body_exact_aspects_ut(
        &context,
        TAIYIN_BODY_MARS_BARYCENTER,
        TAIYIN_BODY_SUN,
        major_aspects,
        sizeof(major_aspects) / sizeof(major_aspects[0]),
        JD_UT_MARS_2003_OPPOSITION_SEDS - 8.0,
        JD_UT_MARS_2003_OPPOSITION_SEDS + 8.0,
        0.25,
        0,
        exact_events,
        exact_targets,
        4,
        &exact_count,
        &diagnostic);
    expect_status(exact_status, TAIYIN_STATUS_OK, "2003 Mars exact major-aspect search", failures);
    if (exact_status != TAIYIN_STATUS_OK) {
        return;
    }
    expect_true(exact_count == 1, "2003 Mars exact major-aspect count", failures);
    expect_near(exact_events[0], events[0], 5.0e-8, "exact major-aspect Mars opposition matches crossing", failures);
    expect_near(exact_targets[0], TAIYIN_PI, 1.0e-12, "exact major-aspect Mars opposition target", failures);
}

void test_route_rule_policy(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext opm_context = make_context();
    expect_status(
        native_context_set_route_rule(&opm_context, TAIYIN_EPHEMERIS_ROUTE_OPM2),
        TAIYIN_STATUS_OK,
        "set OPM2 route rule",
        failures);
    EphemerisEvalDiagnostic opm_diagnostic;
    double opm_jd = 0.0;
    expect_status(
        search_solar_longitude_ut(
            &opm_context,
            0.0,
            JD_UT_NEAR_2024_EQUINOX,
            0,
            &opm_jd,
            &opm_diagnostic),
        TAIYIN_STATUS_OK,
        "route rule OPM2 search",
        failures);

    taiyin::internal::EphemerisRouteRuleTable missing_table;
    expect_true(
        missing_table.upsert_source_method(
            999999,
            static_cast<int>(taiyin::internal::OPM2_METHOD_ID),
            100,
            "missing source only"),
        "build missing source route rule",
        failures);
    expect_true(
        register_global_ephemeris_route_rule(999999, missing_table),
        "register missing source route rule",
        failures);
    NativeCalcContext missing_context = make_context();
    expect_status(
        native_context_set_route_rule(&missing_context, 999999),
        TAIYIN_STATUS_OK,
        "set missing source route rule",
        failures);
    EphemerisEvalDiagnostic missing_diagnostic;
    double missing_jd = 0.0;
    expect_status(
        search_solar_longitude_ut(
            &missing_context,
            0.0,
            JD_UT_NEAR_2024_EQUINOX,
            0,
            &missing_jd,
            &missing_diagnostic),
        TAIYIN_EVENT_ERROR_NOT_FOUND,
        "missing source route rule does not fall back",
        failures);
}

void test_data_boundary_termination(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    expect_status(
        native_context_set_route_rule(&context, TAIYIN_EPHEMERIS_ROUTE_OPM2),
        TAIYIN_STATUS_OK,
        "set OPM2 route for data boundary",
        failures);
    EphemerisEvalDiagnostic diagnostic;
    double jd_ut = 0.0;
    const Status status = search_solar_longitude_ut(
        &context,
        0.0,
        3000000.0,
        0,
        &jd_ut,
        &diagnostic);
    expect_status(status, TAIYIN_EVENT_ERROR_NOT_FOUND, "search reports event not found at data boundary", failures);
    expect_true(
        diagnostic.status == TAIYIN_EPHEMERIS_ERROR_COMPOSITE_COVERAGE_GAP
            || diagnostic.status == TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP
            || diagnostic.status == TAIYIN_EPHEMERIS_ERROR_COMPOSITE_MISSING_COMPONENT
            || diagnostic.status == TAIYIN_EPHEMERIS_ERROR_NO_ROUTE,
        "data boundary diagnostic status",
        failures);
}

void test_component_boundary_during_search(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    expect_status(
        native_context_set_route_rule(&context, TAIYIN_EPHEMERIS_ROUTE_OPM2),
        TAIYIN_STATUS_OK,
        "set OPM2 route for component boundary",
        failures);
    const double jd_near_product_end = 2597690.0;

    EphemerisEvalDiagnostic diagnostic;
    const double longitude = calc_body_longitude(
        &context,
        TAIYIN_BODY_SUN,
        jd_near_product_end,
        &diagnostic,
        failures);

    EphemerisEvalDiagnostic search_diagnostic;
    double jd_ut = 0.0;
    const double target = normalize_radians(longitude - 0.1);
    const Status status = search_solar_longitude_ut(
        &context,
        target,
        jd_near_product_end,
        0,
        &jd_ut,
        &search_diagnostic);
    expect_status(status, TAIYIN_EVENT_ERROR_NOT_FOUND, "search hits component data boundary", failures);
    expect_true(
        search_diagnostic.status == TAIYIN_EPHEMERIS_ERROR_COMPOSITE_COVERAGE_GAP
            || search_diagnostic.status == TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP
            || search_diagnostic.status == TAIYIN_EPHEMERIS_ERROR_COMPOSITE_MISSING_COMPONENT
            || search_diagnostic.status == TAIYIN_EPHEMERIS_ERROR_NO_ROUTE,
        "component boundary diagnostic status",
        failures);
}

void test_opm2_route_rule_component_gap(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    expect_status(
        native_context_set_route_rule(&context, TAIYIN_EPHEMERIS_ROUTE_OPM2),
        TAIYIN_STATUS_OK,
        "set OPM2 route rule for component gap",
        failures);

    EphemerisEvalDiagnostic diagnostic;
    (void)calc_body_longitude(
        &context,
        TAIYIN_BODY_SUN,
        2597690.0,
        &diagnostic,
        failures);

    double position[6] = {};
    const Status status = calc_position_ut(
        &context,
        TAIYIN_BODY_SUN,
        split_jd(2597720.0),
        TAIYIN_NATIVE_POSITION_SPEED | TAIYIN_NATIVE_POSITION_RADIANS,
        position,
        &diagnostic);
    expect_true(status == TAIYIN_EPHEMERIS_ERROR_COMPOSITE_COVERAGE_GAP
            || status == TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP
            || status == TAIYIN_EPHEMERIS_ERROR_COMPOSITE_MISSING_COMPONENT
            || status == TAIYIN_EPHEMERIS_ERROR_NO_ROUTE,
        "OPM2 component gap reports coverage failure",
        failures);
}

void test_rejected_frames(int* failures) {
    taiyin::runtime::NativeCalcContext context = make_context();
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    taiyin::SplitJulianDate jd_ut;
    expect_status(
        taiyin::runtime::search_solar_longitude_ut(
            &context,
            0.0,
            split_jd(JD_UT_NEAR_2024_EQUINOX),
            taiyin::runtime::TAIYIN_NATIVE_POSITION_XYZ,
            &jd_ut,
            &diagnostic),
        taiyin::TAIYIN_ERROR_UNSUPPORTED,
        "body longitude search rejects XYZ",
        failures);
    expect_status(
        taiyin::runtime::search_solar_longitude_ut(
            &context,
            0.0,
            split_jd(JD_UT_NEAR_2024_EQUINOX),
            taiyin::runtime::TAIYIN_NATIVE_POSITION_EQUATORIAL,
            &jd_ut,
            &diagnostic),
        taiyin::TAIYIN_ERROR_UNSUPPORTED,
        "body longitude search rejects equatorial",
        failures);
    expect_status(
        taiyin::runtime::search_solar_longitude_ut(
            &context,
            0.0,
            split_jd(JD_UT_NEAR_2024_EQUINOX),
            1ull << 40,
            &jd_ut,
            &diagnostic),
        taiyin::TAIYIN_ERROR_UNSUPPORTED,
        "body longitude search rejects unknown search flags",
        failures);
    size_t event_count = 0;
    expect_status(
        taiyin::runtime::search_body_longitude_crossings_ut(
            &context,
            taiyin::TAIYIN_BODY_SUN,
            0.0,
            split_jd(JD_UT_NEAR_2024_EQUINOX),
            split_jd(JD_UT_NEAR_2024_EQUINOX + 30.0),
            1.0,
            taiyin::runtime::TAIYIN_EVENT_SEARCH_REVERSE,
            &jd_ut,
            1,
            &event_count,
            &diagnostic),
        taiyin::TAIYIN_ERROR_UNSUPPORTED,
        "bounded body longitude search rejects reverse flag",
        failures);
    expect_status(
        taiyin::runtime::search_body_aspect_crossings_ut(
            &context,
            taiyin::TAIYIN_BODY_MOON,
            taiyin::TAIYIN_BODY_SUN,
            0.0,
            split_jd(JD_UT_NEAR_2024_EQUINOX),
            split_jd(JD_UT_NEAR_2024_EQUINOX + 30.0),
            1.0,
            taiyin::runtime::TAIYIN_EVENT_SEARCH_REVERSE,
            &jd_ut,
            1,
            &event_count,
            &diagnostic),
        taiyin::TAIYIN_ERROR_UNSUPPORTED,
        "bounded aspect search rejects reverse flag",
        failures);
}

}  // namespace

int main() {
    int failures = 0;
    if (initialize_packaged_runtime(&failures)) {
        test_solar_longitude_crossing(&failures);
        test_position_tt_entry_matches_tdb(&failures);
        test_solar_longitude_tt_crossing(&failures);
        test_solar_longitude_reverse(&failures);
        test_solar_longitude_terms_sequence(&failures);
        test_solar_longitude_oracles(&failures);
        test_moon_longitude_crossing(&failures);
        test_moon_longitude_crossing_across_wrap(&failures);
        test_moon_longitude_tt_crossing(&failures);
        test_bounded_solar_longitude_crossings(&failures);
        test_bounded_moon_longitude_crossings(&failures);
        test_bounded_longitude_search_no_event_and_capacity(&failures);
        test_recommended_search_steps(&failures);
        test_auto_step_longitude_and_aspect_wrappers(&failures);
        test_lunar_phase_crossings(&failures);
        test_lunar_phase_crossings_tt(&failures);
        test_exact_aspect_search_lunar_quarters(&failures);
        test_exact_aspect_search_auto_step_and_dedup(&failures);
        test_exact_aspect_search_capacity_and_flags(&failures);
        test_exact_aspect_search_tangent_station_candidate(&failures);
        test_body_longitude_station_search_synthetic(&failures);
        test_real_body_longitude_station_smoke(&failures);
        test_greatest_elongation_opm2_semi_analytic_sanity(&failures);
        test_minimum_angular_separation_sanity(&failures);
        test_minimum_body_star_angular_separation_sanity(&failures);
        test_solar_transit_search_sanity(&failures);
        test_local_solar_transit_search_sanity(&failures);
        test_mars_2003_opposition_seds_reference(&failures);
        test_route_rule_policy(&failures);
        test_data_boundary_termination(&failures);
        test_component_boundary_during_search(&failures);
        test_opm2_route_rule_component_gap(&failures);
        test_rejected_frames(&failures);
    }
    test_solar_transit_de441_hard_scan_sanity(&failures);
    return failures == 0 ? 0 : 1;
}
