#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/runtime/eclipse_search.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {

struct Result {
    const char* name;
    int iterations;
    double total_ms;
    double checksum;
};

template <typename Fn>
Result time_case(const char* name, int iterations, Fn fn) {
    volatile double sink = 0.0;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        sink += fn(i);
    }
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

    Result result;
    result.name = name;
    result.iterations = iterations;
    result.total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    result.checksum = sink;
    return result;
}

void print_result(const Result& result) {
    std::cout << std::left << std::setw(34) << result.name
              << " n=" << std::right << std::setw(8) << result.iterations
              << " total_ms=" << std::setw(10) << std::fixed << std::setprecision(3) << result.total_ms
              << " us/op=" << std::setw(10) << std::fixed << std::setprecision(3)
              << (result.total_ms * 1000.0 / static_cast<double>(result.iterations))
              << " checksum=" << std::setprecision(9) << result.checksum << "\n";
}

taiyin::SplitJulianDate jd(int year, int month, int day) {
    taiyin::SplitJulianDate result;
    taiyin::julian_day_split({year, month, day, 0, 0, 0.0}, &result);
    return result;
}

bool initialize_runtime(const char* data_root) {
    taiyin::runtime::EphemerisRuntimeConfig config;
    config.data_root = data_root;
    config.load_packaged_data = true;
    config.segment_cache_max_entries = 4096;
    return taiyin::runtime::initialize_global_ephemeris_runtime(config);
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
    context.eclipse_shadow_model_id = static_cast<uint8_t>(taiyin::dispatch::ECLIPSE_SHADOW_CHAUVENET);
    context.eclipse_moon_radius_model_id = static_cast<uint8_t>(taiyin::dispatch::ECLIPSE_MOON_ALMANAC);
    return context;
}

std::vector<taiyin::SplitJulianDate> make_start_jds(
    int count,
    const taiyin::SplitJulianDate& start_jd,
    double step_days
) {
    std::vector<taiyin::SplitJulianDate> starts;
    starts.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        starts.push_back(start_jd + static_cast<double>(i) * step_days);
    }
    return starts;
}

double finite_or_zero(double value) {
    return std::isfinite(value) ? value : 0.0;
}

double finite_or_zero(const taiyin::SplitJulianDate& value) {
    return taiyin::split_julian_date_is_finite(value)
        ? taiyin::split_julian_date_to_double(value)
        : 0.0;
}

double solar_checksum(const taiyin::runtime::SolarEclipseResultUt& result) {
    return finite_or_zero(result.maximum_jd_ut)
        + static_cast<double>(result.kind) * 1.0e-3
        + finite_or_zero(result.maximum_latitude_deg) * 1.0e-5
        + finite_or_zero(result.maximum_longitude_deg) * 1.0e-6
        + finite_or_zero(result.contact_jd_ut[taiyin::runtime::TAIYIN_SOLAR_ECLIPSE_CONTACT_P1]) * 1.0e-7
        + finite_or_zero(result.contact_jd_ut[taiyin::runtime::TAIYIN_SOLAR_ECLIPSE_CONTACT_P4]) * 1.0e-8;
}

double lunar_checksum(const taiyin::runtime::LunarEclipseResultUt& result) {
    return finite_or_zero(result.maximum_jd_ut)
        + static_cast<double>(result.kind) * 1.0e-3
        + finite_or_zero(result.umbral_magnitude) * 1.0e-5
        + finite_or_zero(result.penumbral_magnitude) * 1.0e-6
        + finite_or_zero(result.contact_jd_ut[taiyin::runtime::TAIYIN_LUNAR_ECLIPSE_CONTACT_P1]) * 1.0e-7
        + finite_or_zero(result.contact_jd_ut[taiyin::runtime::TAIYIN_LUNAR_ECLIPSE_CONTACT_P4]) * 1.0e-8;
}

double local_solar_checksum(const taiyin::runtime::LocalSolarEclipseResultUt& result) {
    return finite_or_zero(result.maximum_jd_ut)
        + static_cast<double>(result.kind) * 1.0e-3
        + finite_or_zero(result.magnitude) * 1.0e-5
        + finite_or_zero(result.obscuration) * 1.0e-6
        + finite_or_zero(result.sun_altitude_deg) * 1.0e-7
        + finite_or_zero(result.duration_seconds) * 1.0e-8;
}

}  // namespace

int main(int argc, char** argv) {
    const char* data_root = argc > 1 ? argv[1] : "data";
    const int iterations = argc > 2 ? std::atoi(argv[2]) : 1000;
    const char* mode = argc > 3 ? argv[3] : "all";
    const bool solar_global_only = std::strcmp(mode, "solar-global") == 0;
    const bool solar_next_all_only = std::strcmp(mode, "solar-next-all") == 0;
    const bool solar_next_central_only = std::strcmp(mode, "solar-next-central") == 0;
    const bool solar_previous_all_only = std::strcmp(mode, "solar-previous-all") == 0;
    const bool solar_previous_central_only = std::strcmp(mode, "solar-previous-central") == 0;
    const bool single_solar_case = solar_next_all_only
        || solar_next_central_only
        || solar_previous_all_only
        || solar_previous_central_only;

    if (iterations <= 0) {
        std::cerr << "iterations must be positive\n";
        return 1;
    }
    if (!initialize_runtime(data_root)) {
        std::cerr << "failed to initialize Taiyin runtime from " << data_root << "\n";
        return 1;
    }

    taiyin::runtime::NativeCalcContext context = make_context();
    taiyin::runtime::NativeCalcContext dallas_context = make_context();
    taiyin::runtime::native_context_set_observer_location(
        &dallas_context,
        taiyin::runtime::native_observer_location_degrees(-96.7970, 32.7767, 131.0));
    taiyin::runtime::NativeCalcContext new_york_context = make_context();
    taiyin::runtime::native_context_set_observer_location(
        &new_york_context,
        taiyin::runtime::native_observer_location_degrees(-74.0, 40.7, 0.0));
    const uint64_t flags = taiyin::runtime::TAIYIN_ECLIPSE_INCLUDE_CONTACTS;
    const std::vector<taiyin::SplitJulianDate> starts =
        make_start_jds(iterations, jd(1900, 1, 1), 37.0);
    const std::vector<taiyin::SplitJulianDate> reverse_starts =
        make_start_jds(iterations, jd(2100, 1, 1), -37.0);

    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    taiyin::runtime::SolarEclipseResultUt solar_warm;
    taiyin::runtime::LunarEclipseResultUt lunar_warm;
    taiyin::runtime::LocalSolarEclipseResultUt local_warm;
    taiyin::runtime::search_next_solar_eclipse_ut(&context, jd(2024, 1, 1), 0, flags, &solar_warm, &diagnostic);
    taiyin::runtime::search_next_lunar_eclipse_ut(&context, jd(2024, 1, 1), 0, flags, &lunar_warm, &diagnostic);
    taiyin::runtime::search_next_local_solar_eclipse_ut(
        &dallas_context, jd(2024, 1, 1), 0, flags, &local_warm, &diagnostic);

    std::cout << "catalog_size=" << taiyin::runtime::global_ephemeris_catalog_size()
              << " cache_entries=" << taiyin::runtime::global_ephemeris_cache_entry_count() << "\n";

    if (!single_solar_case || solar_next_all_only) {
        print_result(time_case("solar next all", iterations, [&](int i) {
            taiyin::runtime::SolarEclipseResultUt result;
            taiyin::runtime::EphemerisEvalDiagnostic diag;
            const taiyin::Status status = taiyin::runtime::search_next_solar_eclipse_ut(
                &context, starts[static_cast<size_t>(i)], 0, flags, &result, &diag);
            return status == taiyin::TAIYIN_STATUS_OK ? solar_checksum(result) : -1000.0;
        }));
    }

    if (!single_solar_case || solar_next_central_only) {
        print_result(time_case("solar next central", iterations, [&](int i) {
            taiyin::runtime::SolarEclipseResultUt result;
            taiyin::runtime::EphemerisEvalDiagnostic diag;
            const taiyin::Status status = taiyin::runtime::search_next_solar_eclipse_ut(
                &context,
                starts[static_cast<size_t>(i)],
                taiyin::runtime::TAIYIN_ECLIPSE_TOTAL
                    | taiyin::runtime::TAIYIN_ECLIPSE_ANNULAR
                    | taiyin::runtime::TAIYIN_ECLIPSE_HYBRID,
                flags,
                &result,
                &diag);
            return status == taiyin::TAIYIN_STATUS_OK ? solar_checksum(result) : -1000.0;
        }));
    }

    if (!single_solar_case || solar_previous_all_only) {
        print_result(time_case("solar previous all", iterations, [&](int i) {
            taiyin::runtime::SolarEclipseResultUt result;
            taiyin::runtime::EphemerisEvalDiagnostic diag;
            const taiyin::Status status = taiyin::runtime::search_next_solar_eclipse_ut(
                &context,
                reverse_starts[static_cast<size_t>(i)],
                0,
                flags | taiyin::runtime::TAIYIN_ECLIPSE_BACKWARD,
                &result,
                &diag);
            return status == taiyin::TAIYIN_STATUS_OK ? solar_checksum(result) : -1000.0;
        }));
    }

    if (!single_solar_case || solar_previous_central_only) {
        print_result(time_case("solar previous central", iterations, [&](int i) {
            taiyin::runtime::SolarEclipseResultUt result;
            taiyin::runtime::EphemerisEvalDiagnostic diag;
            const taiyin::Status status = taiyin::runtime::search_next_solar_eclipse_ut(
                &context,
                reverse_starts[static_cast<size_t>(i)],
                taiyin::runtime::TAIYIN_ECLIPSE_TOTAL
                    | taiyin::runtime::TAIYIN_ECLIPSE_ANNULAR
                    | taiyin::runtime::TAIYIN_ECLIPSE_HYBRID,
                flags | taiyin::runtime::TAIYIN_ECLIPSE_BACKWARD,
                &result,
                &diag);
            return status == taiyin::TAIYIN_STATUS_OK ? solar_checksum(result) : -1000.0;
        }));
    }

    if (solar_global_only || single_solar_case) return 0;

    print_result(time_case("lunar next all", iterations, [&](int i) {
        taiyin::runtime::LunarEclipseResultUt result;
        taiyin::runtime::EphemerisEvalDiagnostic diag;
        const taiyin::Status status = taiyin::runtime::search_next_lunar_eclipse_ut(
            &context, starts[static_cast<size_t>(i)], 0, flags, &result, &diag);
        return status == taiyin::TAIYIN_STATUS_OK ? lunar_checksum(result) : -1000.0;
    }));

    print_result(time_case("lunar next non-penumbral", iterations, [&](int i) {
        taiyin::runtime::LunarEclipseResultUt result;
        taiyin::runtime::EphemerisEvalDiagnostic diag;
        const taiyin::Status status = taiyin::runtime::search_next_lunar_eclipse_ut(
            &context,
            starts[static_cast<size_t>(i)],
            taiyin::runtime::TAIYIN_ECLIPSE_PARTIAL | taiyin::runtime::TAIYIN_ECLIPSE_TOTAL,
            flags | taiyin::runtime::TAIYIN_ECLIPSE_EXCLUDE_PENUMBRAL,
            &result,
            &diag);
        return status == taiyin::TAIYIN_STATUS_OK ? lunar_checksum(result) : -1000.0;
    }));

    print_result(time_case("lunar previous all", iterations, [&](int i) {
        taiyin::runtime::LunarEclipseResultUt result;
        taiyin::runtime::EphemerisEvalDiagnostic diag;
        const taiyin::Status status = taiyin::runtime::search_next_lunar_eclipse_ut(
            &context,
            reverse_starts[static_cast<size_t>(i)],
            0,
            flags | taiyin::runtime::TAIYIN_ECLIPSE_BACKWARD,
            &result,
            &diag);
        return status == taiyin::TAIYIN_STATUS_OK ? lunar_checksum(result) : -1000.0;
    }));

    print_result(time_case("lunar previous non-penumbral", iterations, [&](int i) {
        taiyin::runtime::LunarEclipseResultUt result;
        taiyin::runtime::EphemerisEvalDiagnostic diag;
        const taiyin::Status status = taiyin::runtime::search_next_lunar_eclipse_ut(
            &context,
            reverse_starts[static_cast<size_t>(i)],
            taiyin::runtime::TAIYIN_ECLIPSE_PARTIAL | taiyin::runtime::TAIYIN_ECLIPSE_TOTAL,
            flags | taiyin::runtime::TAIYIN_ECLIPSE_EXCLUDE_PENUMBRAL | taiyin::runtime::TAIYIN_ECLIPSE_BACKWARD,
            &result,
            &diag);
        return status == taiyin::TAIYIN_STATUS_OK ? lunar_checksum(result) : -1000.0;
    }));

    print_result(time_case("local solar next all", iterations, [&](int i) {
        taiyin::runtime::LocalSolarEclipseResultUt result;
        taiyin::runtime::EphemerisEvalDiagnostic diag;
        const taiyin::Status status = taiyin::runtime::search_next_local_solar_eclipse_ut(
            &dallas_context,
            starts[static_cast<size_t>(i)],
            0,
            flags,
            &result,
            &diag);
        return status == taiyin::TAIYIN_STATUS_OK ? local_solar_checksum(result) : -1000.0;
    }));

    print_result(time_case("local solar next total", iterations, [&](int i) {
        taiyin::runtime::LocalSolarEclipseResultUt result;
        taiyin::runtime::EphemerisEvalDiagnostic diag;
        const taiyin::Status status = taiyin::runtime::search_next_local_solar_eclipse_ut(
            &dallas_context,
            starts[static_cast<size_t>(i)],
            taiyin::runtime::TAIYIN_ECLIPSE_TOTAL,
            flags,
            &result,
            &diag);
        return status == taiyin::TAIYIN_STATUS_OK ? local_solar_checksum(result) : -1000.0;
    }));

    print_result(time_case("local solar next partial", iterations, [&](int i) {
        taiyin::runtime::LocalSolarEclipseResultUt result;
        taiyin::runtime::EphemerisEvalDiagnostic diag;
        const taiyin::Status status = taiyin::runtime::search_next_local_solar_eclipse_ut(
            &new_york_context,
            starts[static_cast<size_t>(i)],
            taiyin::runtime::TAIYIN_ECLIPSE_PARTIAL,
            flags,
            &result,
            &diag);
        return status == taiyin::TAIYIN_STATUS_OK ? local_solar_checksum(result) : -1000.0;
    }));

    print_result(time_case("local solar previous all", iterations, [&](int i) {
        taiyin::runtime::LocalSolarEclipseResultUt result;
        taiyin::runtime::EphemerisEvalDiagnostic diag;
        const taiyin::Status status = taiyin::runtime::search_next_local_solar_eclipse_ut(
            &dallas_context,
            reverse_starts[static_cast<size_t>(i)],
            0,
            flags | taiyin::runtime::TAIYIN_ECLIPSE_BACKWARD,
            &result,
            &diag);
        return status == taiyin::TAIYIN_STATUS_OK ? local_solar_checksum(result) : -1000.0;
    }));

    return 0;
}
