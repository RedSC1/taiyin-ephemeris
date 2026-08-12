#include "runtime/eclipse/solar_eclipse_direct_solver.h"
#include "runtime/eclipse/solar_eclipse_besselian_solver.h"

#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

std::string data_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/major-bodies/600y";
    }
    return "../data/ephemerides/opm2/major-bodies/600y";
}

taiyin::runtime::NativeCalcContext make_context() {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context, taiyin::TAIYIN_BODY_EARTH, taiyin::TAIYIN_BODY_EARTH);
    taiyin::runtime::native_context_use_solar_deflector(&context);
    context.apparent_options.flags =
        taiyin::TAIYIN_APPARENT_SPHERICAL
        | taiyin::TAIYIN_APPARENT_LIGHT_TIME
        | taiyin::TAIYIN_APPARENT_ABERRATION
        | taiyin::TAIYIN_APPARENT_DEFLECTION;
    context.apparent_options.output_frame_id =
        taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    context.eclipse_shadow_model_id = static_cast<uint8_t>(
        taiyin::dispatch::ECLIPSE_SHADOW_CHAUVENET);
    return context;
}

double seconds_between(
    const taiyin::SplitJulianDate& a,
    const taiyin::SplitJulianDate& b
) {
    return std::fabs(a - b) * 86400.0;
}

}  // namespace

int main() {
    const std::string root = data_root();
    taiyin::runtime::EphemerisRuntimeConfig config;
    config.data_root = root.c_str();
    config.load_packaged_data = true;
    config.segment_cache_max_entries = 4096;
    if (!taiyin::runtime::initialize_global_ephemeris_runtime(config)) {
        std::printf("FAIL: initialize runtime from %s\n", root.c_str());
        return 1;
    }

    const taiyin::runtime::NativeCalcContext context = make_context();
    const uint64_t flags = taiyin::runtime::TAIYIN_ECLIPSE_INCLUDE_CONTACTS;
    int failures = 0;
    int event_count = 0;
    double max_greatest_seconds = 0.0;
    double max_contact_seconds = 0.0;

    // Roughly 1895-2105. The archived solver remains a differential oracle
    // while global search migrates from a Besselian polynomial to direct
    // instantaneous shadow geometry.
    for (int k = -1300; k <= 1300; ++k) {
        taiyin::runtime::SolarEclipseResult direct;
        taiyin::runtime::SolarEclipseResult legacy;
        taiyin::runtime::EphemerisEvalDiagnostic diagnostic{};
        const taiyin::Status direct_status =
            taiyin::runtime::solve_solar_eclipse_direct_for_meeus_k(
                &context, k, flags, 0, true, true, &direct, &diagnostic);
        const taiyin::Status legacy_status =
            taiyin::runtime::solve_solar_eclipse_besselian_for_meeus_k(
                &context, k, flags, true, true, &legacy, &diagnostic);
        if (direct_status != legacy_status) {
            std::printf(
                "FAIL: k=%d status direct=%d legacy=%d\n",
                k, direct_status, legacy_status);
            ++failures;
            continue;
        }
        if (direct_status != taiyin::TAIYIN_STATUS_OK) continue;
        if (direct.kind != legacy.kind) {
            // The 1935-01-05 eclipse is the smallest partial eclipse in the
            // NASA 1901-2000 catalog. The legacy scaled-radius test misses it,
            // while the exact cone/ellipsoid discriminant correctly retains
            // this grazing event.
            if (k == -804
                && direct.kind
                    == (taiyin::runtime::TAIYIN_ECLIPSE_PARTIAL
                        | taiyin::runtime::TAIYIN_ECLIPSE_NONCENTRAL)
                && legacy.kind == taiyin::runtime::TAIYIN_ECLIPSE_NONE
                && std::fabs(
                    taiyin::split_julian_date_to_double(direct.maximum_jd_tt)
                        - 2427807.733171296)
                    < 60.0 / 86400.0) {
                if (!(direct.penumbral_margin_km <= 0.0)) {
                    std::printf(
                        "FAIL: k=%d grazing eclipse has positive penumbral margin %.6f km\n",
                        k,
                        direct.penumbral_margin_km);
                    ++failures;
                }
                ++event_count;
                continue;
            }
            std::printf(
                "FAIL: k=%d kind direct=%u legacy=%u jd=%.9f pen_margin_km=%.6f axis_km=%.6f\n",
                k,
                direct.kind,
                legacy.kind,
                taiyin::split_julian_date_to_double(direct.maximum_jd_tt),
                direct.penumbral_margin_km,
                direct.axis_distance_km);
            ++failures;
            continue;
        }
        if (direct.kind == taiyin::runtime::TAIYIN_ECLIPSE_NONE) continue;
        ++event_count;
        const double greatest_seconds = seconds_between(
            direct.maximum_jd_tt, legacy.maximum_jd_tt);
        max_greatest_seconds = std::max(max_greatest_seconds, greatest_seconds);
        if (greatest_seconds > 0.1) {
            std::printf(
                "FAIL: k=%d greatest difference=%.6f seconds\n",
                k, greatest_seconds);
            ++failures;
        }
        for (size_t index = 0;
             index < taiyin::runtime::TAIYIN_SOLAR_ECLIPSE_CONTACT_COUNT;
             ++index) {
            const bool direct_finite = taiyin::split_julian_date_is_finite(
                direct.contact_jd_tt[index]);
            const bool legacy_finite = taiyin::split_julian_date_is_finite(
                legacy.contact_jd_tt[index]);
            if (direct_finite != legacy_finite) {
                std::printf(
                    "FAIL: k=%d contact=%zu finite direct=%d legacy=%d\n",
                    k, index, direct_finite ? 1 : 0, legacy_finite ? 1 : 0);
                ++failures;
                continue;
            }
            if (!direct_finite) continue;
            const double contact_seconds = seconds_between(
                direct.contact_jd_tt[index], legacy.contact_jd_tt[index]);
            max_contact_seconds = std::max(max_contact_seconds, contact_seconds);
            if (contact_seconds > 1.0) {
                std::printf(
                    "FAIL: k=%d contact=%zu difference=%.6f seconds\n",
                    k, index, contact_seconds);
                ++failures;
            }
        }
    }

    std::printf(
        "solar_direct_solver events=%d max_greatest_seconds=%.6f max_contact_seconds=%.6f\n",
        event_count, max_greatest_seconds, max_contact_seconds);
    return failures == 0 ? 0 : 1;
}
