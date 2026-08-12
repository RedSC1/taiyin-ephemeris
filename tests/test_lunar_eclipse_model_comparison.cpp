#include "runtime/eclipse/eclipse_time.h"
#include "legacy/sxwnl/eclipse/lunar_eclipse_sxwnl.h"
#include "runtime/eclipse/lunar_shadow_geometry.h"

#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/runtime/eclipse_search.h"
#include "taiyin/runtime/lunar_limb.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <string>

namespace {

namespace legacy = taiyin::runtime::sxwnl::lunar;

constexpr size_t kContactCount = taiyin::runtime::TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT;

const char* const kContactNames[kContactCount] = {
    "P1", "U1", "U2", "Greatest", "U3", "U4", "P4",
};

const double kPmoJdUt[kContactCount] = {
    2460926.143680556,
    2460926.185277778,
    2460926.229444444,
    2460926.258194444,
    2460926.286944444,
    2460926.331180556,
    2460926.372638889,
};

std::string repository_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    return root && root[0] != '\0' ? root : "..";
}

taiyin::SplitJulianDate split_jd(double value) {
    taiyin::SplitJulianDate result;
    taiyin::split_julian_date_from_double(value, &result);
    return result;
}

taiyin::SplitJulianDate invalid_jd() {
    return taiyin::SplitJulianDate(0, std::numeric_limits<double>::quiet_NaN());
}

taiyin::runtime::NativeCalcContext make_context() {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context, taiyin::TAIYIN_BODY_EARTH, taiyin::TAIYIN_BODY_EARTH);
    taiyin::runtime::native_context_use_solar_deflector(&context);
    context.apparent_options.flags = taiyin::TAIYIN_APPARENT_SPHERICAL
        | taiyin::TAIYIN_APPARENT_LIGHT_TIME
        | taiyin::TAIYIN_APPARENT_ABERRATION
        | taiyin::TAIYIN_APPARENT_DEFLECTION;
    context.apparent_options.output_frame_id =
        taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    context.eclipse_shadow_model_id = static_cast<uint8_t>(
        taiyin::dispatch::ECLIPSE_SHADOW_CHAUVENET);
    return context;
}

double degnorm(double value) {
    value = std::fmod(value, 360.0);
    return value < 0.0 ? value + 360.0 : value;
}

double meeus_f_normalized(double k) {
    const double k_half = k + 0.5;
    const double t = k_half / 1236.85;
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double t4 = t3 * t;
    double f = degnorm(160.7108 + 390.67050274 * k_half
                       - 0.0016341 * t2
                       - 0.00000227 * t3
                       + 0.000000011 * t4);
    if (f > 180.0) f -= 180.0;
    return f;
}

taiyin::SplitJulianDate meeus_max_jd(double k) {
    const double k_half = k + 0.5;
    const double t = k_half / 1236.85;
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double t4 = t3 * t;
    const double m = degnorm(2.5534 + 29.10535669 * k_half
                             - 0.0000218 * t2 - 0.00000011 * t3);
    const double m_prime = degnorm(201.5643 + 385.81693528 * k_half
                                   + 0.1017438 * t2
                                   + 0.00001239 * t3
                                   + 0.000000058 * t4);
    const double omega = degnorm(124.7746 - 1.56375580 * k_half
                                 + 0.0020691 * t2 + 0.00000215 * t3);
    const double e = 1.0 - 0.002516 * t - 0.0000074 * t2;
    const double a1 = degnorm(299.77 + 0.107408 * k_half - 0.009173 * t2);
    taiyin::SplitJulianDate result(
        2451550,
        0.09765 + 29.530588853 * k_half
            + 0.0001337 * t2
            - 0.000000150 * t3
            + 0.00000000073 * t4);
    const double f1 = (meeus_f_normalized(k)
                       - 0.02665 * std::sin(omega * M_PI / 180.0))
        * M_PI / 180.0;
    const double mr = m * M_PI / 180.0;
    const double mpr = m_prime * M_PI / 180.0;
    const double a1r = a1 * M_PI / 180.0;
    const double orad = omega * M_PI / 180.0;
    result += (-0.4065 * std::sin(mpr)
               + 0.1727 * e * std::sin(mr)
               + 0.0161 * std::sin(2.0 * mpr)
               - 0.0097 * std::sin(2.0 * f1)
               + 0.0073 * e * std::sin(mpr - mr)
               - 0.0050 * e * std::sin(mpr + mr)
               - 0.0023 * std::sin(mpr - 2.0 * f1)
               + 0.0021 * e * std::sin(2.0 * mr)
               + 0.0012 * std::sin(mpr + 2.0 * f1)
               + 0.0006 * e * std::sin(2.0 * mpr + mr)
               - 0.0004 * std::sin(3.0 * mpr)
               - 0.0003 * e * std::sin(mr + 2.0 * f1)
               + 0.0003 * std::sin(a1r)
               - 0.0002 * e * std::sin(mr - 2.0 * f1)
               - 0.0002 * e * std::sin(2.0 * mpr - mr)
               - 0.0002 * std::sin(orad));
    return result;
}

int meeus_k_for_jd(taiyin::SplitJulianDate jd_tt) {
    return static_cast<int>(std::floor(
        (jd_tt - taiyin::SplitJulianDate(2451545, 0.0))
        / 365.2425 * 12.3685));
}

enum GreatestMetric {
    GREATEST_SXWNL_ANGULAR,
    GREATEST_VECTOR_ANGULAR,
    GREATEST_VECTOR_PHYSICAL,
};

taiyin::Status evaluate_greatest_metric(
    const taiyin::runtime::NativeCalcContext* context,
    taiyin::SplitJulianDate jd_tt,
    GreatestMetric metric,
    double* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    if (metric == GREATEST_SXWNL_ANGULAR) {
        legacy::LecGeometry geometry;
        const taiyin::Status status = legacy::lecXY(
            context, jd_tt, 0, &geometry, diagnostic);
        if (status != taiyin::TAIYIN_STATUS_OK) return status;
        *out = geometry.rmin_rad * geometry.rmin_rad;
        return taiyin::TAIYIN_STATUS_OK;
    }

    taiyin::runtime::LunarShadowGeometry geometry;
    const taiyin::Status status = taiyin::runtime::evaluate_lunar_shadow_geometry(
        context, jd_tt, 0, &geometry, diagnostic);
    if (status != taiyin::TAIYIN_STATUS_OK) return status;
    if (metric == GREATEST_VECTOR_ANGULAR) {
        const double angle = std::atan2(
            geometry.axis_distance_km, geometry.axial_distance_km);
        *out = angle * angle;
    } else {
        *out = geometry.axis_distance_km * geometry.axis_distance_km;
    }
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin::Status refine_greatest_metric(
    const taiyin::runtime::NativeCalcContext* context,
    taiyin::SplitJulianDate seed,
    GreatestMetric metric,
    taiyin::SplitJulianDate* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    constexpr double step = 60.0 / 86400.0;
    constexpr double max_step = 0.25;
    taiyin::SplitJulianDate center = seed;
    for (int iteration = 0; iteration < 4; ++iteration) {
        double minus = std::nan("");
        double current = std::nan("");
        double plus = std::nan("");
        taiyin::Status status = evaluate_greatest_metric(
            context, center - step, metric, &minus, diagnostic);
        if (status != taiyin::TAIYIN_STATUS_OK) return status;
        status = evaluate_greatest_metric(
            context, center, metric, &current, diagnostic);
        if (status != taiyin::TAIYIN_STATUS_OK) return status;
        status = evaluate_greatest_metric(
            context, center + step, metric, &plus, diagnostic);
        if (status != taiyin::TAIYIN_STATUS_OK) return status;
        const double curvature = minus - 2.0 * current + plus;
        if (!(curvature > 0.0) || !std::isfinite(curvature)) {
            return taiyin::TAIYIN_ERROR_UNSUPPORTED;
        }
        double correction = 0.5 * step * (minus - plus) / curvature;
        correction = std::max(-max_step, std::min(max_step, correction));
        center += correction;
        if (std::fabs(correction) < 0.001 / 86400.0) break;
    }
    *out = center;
    return taiyin::TAIYIN_STATUS_OK;
}

enum ContactBoundary {
    PENUMBRAL_OUTER,
    UMBRAL_OUTER,
    UMBRAL_INNER,
};

taiyin::Status eval_legacy_contact_scalar(
    const taiyin::runtime::NativeCalcContext* context,
    taiyin::SplitJulianDate jd_tt,
    uint64_t flags,
    ContactBoundary boundary,
    double* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    legacy::LecGeometry geometry;
    taiyin::Status status = legacy::lecXY(context, jd_tt, flags, &geometry, diagnostic);
    if (status != taiyin::TAIYIN_STATUS_OK) return status;
    double radius = std::nan("");
    if (boundary == PENUMBRAL_OUTER) {
        radius = geometry.penumbra_radius_rad + geometry.moon_radius_toward_shadow_rad;
    } else if (boundary == UMBRAL_OUTER) {
        radius = geometry.umbra_radius_rad + geometry.moon_radius_toward_shadow_rad;
    } else {
        radius = geometry.umbra_radius_rad - geometry.moon_radius_away_from_shadow_rad;
    }
    *out = geometry.rmin_rad - radius;
    return std::isfinite(*out)
        ? taiyin::TAIYIN_STATUS_OK
        : taiyin::TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

taiyin::Status solve_legacy_contact(
    const taiyin::runtime::NativeCalcContext* context,
    taiyin::SplitJulianDate jd_max,
    double x,
    double y,
    double vx,
    double vy,
    double radius,
    int later,
    ContactBoundary boundary,
    uint64_t flags,
    bool force_geometry_refinement,
    taiyin::SplitJulianDate* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    const double first_dt = legacy::lineT(x, y, vx, vy, radius, later);
    if (!std::isfinite(first_dt)) return taiyin::TAIYIN_ERROR_UNSUPPORTED;
    const taiyin::SplitJulianDate first = jd_max + first_dt;
    legacy::LecMaxResult local;
    taiyin::Status status = legacy::lecMax(context, first, flags, &local, diagnostic);
    if (status != taiyin::TAIYIN_STATUS_OK) return status;
    const double second_dt = legacy::lineT(
        local.geometry.x_rad,
        local.geometry.y_rad,
        local.vx_rad_per_day,
        local.vy_rad_per_day,
        radius,
        later);
    taiyin::SplitJulianDate result = std::isfinite(second_dt) ? first + second_dt : first;
    if (!force_geometry_refinement
        && (flags & taiyin::runtime::TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION) == 0u) {
        *out = result;
        return taiyin::TAIYIN_STATUS_OK;
    }

    constexpr double derivative_step = 0.5 / 86400.0;
    constexpr double max_correction = 30.0 / 86400.0;
    for (int iteration = 0; iteration < 4; ++iteration) {
        double f0 = std::nan("");
        double fm = std::nan("");
        double fp = std::nan("");
        status = eval_legacy_contact_scalar(
            context, result, flags, boundary, &f0, diagnostic);
        if (status != taiyin::TAIYIN_STATUS_OK) return status;
        status = eval_legacy_contact_scalar(
            context, result - derivative_step, flags, boundary, &fm, diagnostic);
        if (status != taiyin::TAIYIN_STATUS_OK) return status;
        status = eval_legacy_contact_scalar(
            context, result + derivative_step, flags, boundary, &fp, diagnostic);
        if (status != taiyin::TAIYIN_STATUS_OK) return status;
        const double slope = (fp - fm) / (2.0 * derivative_step);
        if (!std::isfinite(slope) || std::fabs(slope) < 1.0e-12) {
            return taiyin::TAIYIN_ERROR_UNSUPPORTED;
        }
        double correction = -f0 / slope;
        correction = std::max(-max_correction, std::min(max_correction, correction));
        result += correction;
        if (std::fabs(correction) < 0.01 / 86400.0) break;
    }
    *out = result;
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin::Status solve_legacy_contacts_tt(
    const taiyin::runtime::NativeCalcContext* context,
    taiyin::SplitJulianDate jd_tt,
    uint64_t flags,
    bool force_geometry_refinement,
    taiyin::SplitJulianDate* contacts,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    for (size_t index = 0; index < kContactCount; ++index) contacts[index] = invalid_jd();
    const taiyin::SplitJulianDate seed = meeus_max_jd(meeus_k_for_jd(jd_tt));
    legacy::LecMaxResult first;
    taiyin::Status status = legacy::lecMax(context, seed, flags, &first, diagnostic);
    if (status != taiyin::TAIYIN_STATUS_OK) return status;
    const taiyin::SplitJulianDate intermediate = seed + first.dt_days;
    legacy::LecMaxResult second;
    status = legacy::lecMax(context, intermediate, flags, &second, diagnostic);
    if (status != taiyin::TAIYIN_STATUS_OK) return status;
    const taiyin::SplitJulianDate greatest = intermediate + second.dt_days;
    legacy::LecGeometry maximum_geometry;
    status = legacy::lecXY(context, greatest, flags, &maximum_geometry, diagnostic);
    if (status != taiyin::TAIYIN_STATUS_OK) return status;
    legacy::LecMaxResult maximum_motion;
    status = legacy::lecMax(context, greatest, flags, &maximum_motion, diagnostic);
    if (status != taiyin::TAIYIN_STATUS_OK) return status;

    const double x = maximum_motion.geometry.x_rad;
    const double y = maximum_motion.geometry.y_rad;
    const double vx = maximum_motion.vx_rad_per_day;
    const double vy = maximum_motion.vy_rad_per_day;
    const double moon = maximum_geometry.moon_radius_rad;
    const double umbra = maximum_geometry.umbra_radius_rad;
    const double penumbra = maximum_geometry.penumbra_radius_rad;
    contacts[taiyin::runtime::TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST] = greatest;

    const struct ContactSpec {
        size_t index;
        double radius;
        int later;
        ContactBoundary boundary;
    } specs[] = {
        {taiyin::runtime::TAIYIN_LUNAR_ECLIPSE_CONTACT_P1,
         moon + penumbra, 0, PENUMBRAL_OUTER},
        {taiyin::runtime::TAIYIN_LUNAR_ECLIPSE_CONTACT_U1,
         moon + umbra, 0, UMBRAL_OUTER},
        {taiyin::runtime::TAIYIN_LUNAR_ECLIPSE_CONTACT_U2,
         umbra - moon, 0, UMBRAL_INNER},
        {taiyin::runtime::TAIYIN_LUNAR_ECLIPSE_CONTACT_U3,
         umbra - moon, 1, UMBRAL_INNER},
        {taiyin::runtime::TAIYIN_LUNAR_ECLIPSE_CONTACT_U4,
         moon + umbra, 1, UMBRAL_OUTER},
        {taiyin::runtime::TAIYIN_LUNAR_ECLIPSE_CONTACT_P4,
         moon + penumbra, 1, PENUMBRAL_OUTER},
    };
    for (const ContactSpec& spec : specs) {
        status = solve_legacy_contact(
            context, greatest, x, y, vx, vy, spec.radius, spec.later,
            spec.boundary, flags, force_geometry_refinement,
            &contacts[spec.index], diagnostic);
        if (status != taiyin::TAIYIN_STATUS_OK) return status;
    }
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin::Status legacy_contacts_ut(
    const taiyin::runtime::NativeCalcContext& context,
    taiyin::SplitJulianDate jd_ut,
    uint64_t flags,
    bool force_geometry_refinement,
    taiyin::SplitJulianDate* contacts_ut,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    taiyin::SplitJulianDate jd_tt;
    taiyin::Status status = taiyin::runtime::eclipse_ut_to_tt(
        context, jd_ut, &jd_tt, nullptr, diagnostic);
    if (status != taiyin::TAIYIN_STATUS_OK) return status;
    taiyin::SplitJulianDate contacts_tt[kContactCount];
    status = solve_legacy_contacts_tt(
        &context, jd_tt, flags, force_geometry_refinement, contacts_tt, diagnostic);
    if (status != taiyin::TAIYIN_STATUS_OK) return status;
    for (size_t index = 0; index < kContactCount; ++index) {
        status = taiyin::runtime::eclipse_tt_to_ut(
            context, contacts_tt[index], &contacts_ut[index], nullptr, diagnostic);
        if (status != taiyin::TAIYIN_STATUS_OK) return status;
    }
    return taiyin::TAIYIN_STATUS_OK;
}

struct ErrorSummary {
    double mae;
    double rmse;
    double maximum;
};

ErrorSummary summarize_errors(const double* errors) {
    double absolute_sum = 0.0;
    double square_sum = 0.0;
    double maximum = 0.0;
    for (size_t index = 0; index < kContactCount; ++index) {
        absolute_sum += std::fabs(errors[index]);
        square_sum += errors[index] * errors[index];
        maximum = std::max(maximum, std::fabs(errors[index]));
    }
    return {absolute_sum / static_cast<double>(kContactCount),
            std::sqrt(square_sum / static_cast<double>(kContactCount)),
            maximum};
}

void compute_errors(const taiyin::SplitJulianDate* contacts, double* errors) {
    for (size_t index = 0; index < kContactCount; ++index) {
        errors[index] = (contacts[index] - split_jd(kPmoJdUt[index])) * 86400.0;
    }
}

}  // namespace

int main() {
    using namespace taiyin;
    using namespace taiyin::runtime;

    const std::string root = repository_root();
    const std::string ephemeris_root = root + "/data/ephemerides/opm2/major-bodies/600y";
    const char* sources[] = {ephemeris_root.c_str()};
    EphemerisRuntimeConfig config;
    config.source_paths = sources;
    config.source_path_count = 1;
    config.load_packaged_data = false;
    if (!initialize_global_ephemeris_runtime(config)) {
        std::printf("FAIL: initialize ephemeris runtime\n");
        return 1;
    }
    const std::string limb_path = root + "/data/lunar-limb/kaguya_lalt_16ppd.tll1";
    Status status = load_global_lunar_limb_model(limb_path.c_str());
    if (status != TAIYIN_STATUS_OK) {
        std::printf("FAIL: load TLL1 status=%d\n", static_cast<int>(status));
        return 1;
    }

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    const uint64_t circular_flags = TAIYIN_ECLIPSE_INCLUDE_CONTACTS;
    const uint64_t tll1_flags = circular_flags | TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION;
    const SplitJulianDate event_jd = split_jd(2460926.25);

    SplitJulianDate legacy_circular[kContactCount];
    SplitJulianDate legacy_moving_r_circular[kContactCount];
    SplitJulianDate legacy_tll1[kContactCount];
    status = legacy_contacts_ut(
        context, event_jd, circular_flags, false, legacy_circular, &diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        std::printf("FAIL: legacy circular status=%d\n", static_cast<int>(status));
        return 1;
    }
    status = legacy_contacts_ut(
        context, event_jd, circular_flags, true,
        legacy_moving_r_circular, &diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        std::printf("FAIL: legacy moving-R circular status=%d\n", static_cast<int>(status));
        return 1;
    }
    status = legacy_contacts_ut(
        context, event_jd, tll1_flags, false, legacy_tll1, &diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        std::printf("FAIL: legacy TLL1 status=%d\n", static_cast<int>(status));
        return 1;
    }

    LunarEclipseResultUt vector_circular_result;
    LunarEclipseResultUt vector_tll1_result;
    status = solve_lunar_eclipse_at_ut(
        &context, event_jd, circular_flags, &vector_circular_result, &diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        std::printf("FAIL: vector circular status=%d\n", static_cast<int>(status));
        return 1;
    }
    status = solve_lunar_eclipse_at_ut(
        &context, event_jd, tll1_flags, &vector_tll1_result, &diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        std::printf("FAIL: vector TLL1 status=%d\n", static_cast<int>(status));
        return 1;
    }

    double errors[5][kContactCount];
    compute_errors(legacy_circular, errors[0]);
    compute_errors(legacy_moving_r_circular, errors[1]);
    compute_errors(legacy_tll1, errors[2]);
    compute_errors(vector_circular_result.contact_jd_ut, errors[3]);
    compute_errors(vector_tll1_result.contact_jd_ut, errors[4]);
    const char* const labels[5] = {
        "SXWNL frozen-R circular",
        "SXWNL moving-R circular",
        "SXWNL moving-R TLL1",
        "3D moving-R circular",
        "3D moving-R TLL1",
    };

    std::printf("2025-09-07 lunar eclipse error versus PMO (seconds)\n");
    std::printf("event");
    for (const char* label : labels) std::printf(" | %s", label);
    std::printf("\n");
    for (size_t contact = 0; contact < kContactCount; ++contact) {
        std::printf("%s", kContactNames[contact]);
        for (size_t model = 0; model < 5; ++model) {
            std::printf(" | %+.6f", errors[model][contact]);
        }
        std::printf("\n");
    }
    for (size_t model = 0; model < 5; ++model) {
        const ErrorSummary summary = summarize_errors(errors[model]);
        std::printf("summary | %s | MAE=%.6f RMSE=%.6f MAX=%.6f\n",
                    labels[model], summary.mae, summary.rmse, summary.maximum);
        if (!std::isfinite(summary.mae) || summary.maximum > 20.0) {
            std::printf("FAIL: %s comparison is outside sanity bound\n", labels[model]);
            return 1;
        }
    }

    SplitJulianDate event_tt;
    status = eclipse_ut_to_tt(context, event_jd, &event_tt, nullptr, &diagnostic);
    if (status != TAIYIN_STATUS_OK) return 1;
    const SplitJulianDate greatest_seed = meeus_max_jd(meeus_k_for_jd(event_tt));
    const GreatestMetric metrics[3] = {
        GREATEST_SXWNL_ANGULAR,
        GREATEST_VECTOR_ANGULAR,
        GREATEST_VECTOR_PHYSICAL,
    };
    const char* const metric_labels[3] = {
        "SXWNL angular, iterative",
        "exact vector angular",
        "vector physical transverse",
    };
    std::printf("greatest metric isolation versus PMO (seconds)\n");
    for (size_t index = 0; index < 3; ++index) {
        SplitJulianDate greatest_tt;
        status = refine_greatest_metric(
            &context, greatest_seed, metrics[index], &greatest_tt, &diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            std::printf("FAIL: greatest metric %s status=%d\n",
                        metric_labels[index], static_cast<int>(status));
            return 1;
        }
        SplitJulianDate greatest_ut;
        status = eclipse_tt_to_ut(
            context, greatest_tt, &greatest_ut, nullptr, &diagnostic);
        if (status != TAIYIN_STATUS_OK) return 1;
        const double error = (greatest_ut
            - split_jd(kPmoJdUt[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST])) * 86400.0;
        std::printf("greatest | %s | error=%+.6f\n", metric_labels[index], error);
    }

    const char* run_bench = std::getenv("TAIYIN_RUN_LUNAR_BENCH");
    if (run_bench && run_bench[0] != '\0' && run_bench[0] != '0') {
        constexpr int iterations = 500;
        typedef std::chrono::steady_clock BenchClock;
        const auto benchmark = [&](const char* label,
                                   const std::function<Status(double*)>& function) {
            double sink = 0.0;
            const BenchClock::time_point start = BenchClock::now();
            for (int iteration = 0; iteration < iterations; ++iteration) {
                if (function(&sink) != TAIYIN_STATUS_OK) {
                    std::printf("FAIL: benchmark %s\n", label);
                    return false;
                }
            }
            const double total_us = std::chrono::duration<double, std::micro>(
                BenchClock::now() - start).count();
            std::printf("benchmark | %s | iterations=%d | us/event=%.6f | sink=%.12g\n",
                        label, iterations, total_us / static_cast<double>(iterations), sink);
            return true;
        };
        if (!benchmark("SXWNL frozen-R circular", [&](double* sink) {
                SplitJulianDate contacts[kContactCount];
                const Status result = legacy_contacts_ut(
                    context, event_jd, circular_flags, false, contacts, &diagnostic);
                if (result == TAIYIN_STATUS_OK) {
                    *sink += contacts[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST].day_number;
                    *sink += contacts[TAIYIN_LUNAR_ECLIPSE_CONTACT_P1].day_fraction;
                }
                return result;
            })
            || !benchmark("SXWNL moving-R circular", [&](double* sink) {
                SplitJulianDate contacts[kContactCount];
                const Status result = legacy_contacts_ut(
                    context, event_jd, circular_flags, true, contacts, &diagnostic);
                if (result == TAIYIN_STATUS_OK) {
                    *sink += contacts[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST].day_number;
                    *sink += contacts[TAIYIN_LUNAR_ECLIPSE_CONTACT_P1].day_fraction;
                }
                return result;
            })
            || !benchmark("SXWNL moving-R TLL1", [&](double* sink) {
                SplitJulianDate contacts[kContactCount];
                const Status result = legacy_contacts_ut(
                    context, event_jd, tll1_flags, false, contacts, &diagnostic);
                if (result == TAIYIN_STATUS_OK) {
                    *sink += contacts[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST].day_number;
                    *sink += contacts[TAIYIN_LUNAR_ECLIPSE_CONTACT_P1].day_fraction;
                }
                return result;
            })
            || !benchmark("3D cubic-q/R circular", [&](double* sink) {
                LunarEclipseResultUt result;
                const Status solve_status = solve_lunar_eclipse_at_ut(
                    &context, event_jd, circular_flags, &result, &diagnostic);
                if (solve_status == TAIYIN_STATUS_OK) {
                    *sink += result.maximum_jd_ut.day_number;
                    *sink += result.contact_jd_ut[TAIYIN_LUNAR_ECLIPSE_CONTACT_P1].day_fraction;
                }
                return solve_status;
            })
            || !benchmark("3D cubic-q/R TLL1+Newton1", [&](double* sink) {
                LunarEclipseResultUt result;
                const Status solve_status = solve_lunar_eclipse_at_ut(
                    &context, event_jd, tll1_flags, &result, &diagnostic);
                if (solve_status == TAIYIN_STATUS_OK) {
                    *sink += result.maximum_jd_ut.day_number;
                    *sink += result.contact_jd_ut[TAIYIN_LUNAR_ECLIPSE_CONTACT_P1].day_fraction;
                }
                return solve_status;
            })) return 1;
    }

    std::printf("lunar eclipse model comparison passed\n");
    return 0;
}
