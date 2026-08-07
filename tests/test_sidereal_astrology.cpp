#include "taiyin/angle.h"
#include "taiyin/astrology/sidereal.h"
#include "taiyin/astrology/targets.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/geometry.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/runtime/star_position.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

taiyin::SplitJulianDate split_jd(double jd) {
    taiyin::SplitJulianDate out;
    taiyin::split_julian_date_from_double(jd, &out);
    return out;
}

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << " actual=" << actual << " expected=" << expected << "\n";
        ++*failures;
    }
}

void expect_status(taiyin::Status actual, taiyin::Status expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << " actual=" << actual << " expected=" << expected << "\n";
        ++*failures;
    }
}

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: " << label << "\n";
        ++*failures;
    }
}

const int kCustomDeltaTModel = taiyin::dispatch::DELTA_T_CUSTOM_START + 911;
const int kCustomAyanamshaModel =
    taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_CUSTOM_START + 17;
const int kFailingAyanamshaModel =
    taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_CUSTOM_START + 18;
const int kNonFiniteAyanamshaModel =
    taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_CUSTOM_START + 19;
const int kOutputShapeSensitiveAyanamshaModel =
    taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_CUSTOM_START + 20;
const int kContextSensitiveAyanamshaModel =
    taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_CUSTOM_START + 21;

taiyin::SplitJulianDate captured_delta_t_jd(0, NAN);

double custom_delta_t(const taiyin::SplitJulianDate& jd, const void*) {
    captured_delta_t_jd = jd;
    return 5.0 * 86400.0;
}

struct CustomAyanamshaData {
    double reference_jd_tt;
    double offset_rad;
    double rate_rad_per_day;
};

taiyin::SplitJulianDate captured_custom_ayanamsha_jd(0, NAN);

// Keeps the test inputs compact while exercising the public, context-free
// sidereal API. Production code has no second sidereal context object.
struct TestSiderealContext {
    const taiyin::runtime::NativeCalcContext* native_context;
    int ayanamsha_id;
    uint64_t sidereal_flags;

    TestSiderealContext() noexcept
        : native_context(nullptr),
          ayanamsha_id(taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY),
          sidereal_flags(0) {}
};

taiyin::Status calc_ayanamsha_tt(
    const TestSiderealContext* context,
    double jd_tt,
    double* out_ayanamsha_rad
) {
    return taiyin::astrology::calc_ayanamsha_tt(
        context ? context->native_context : nullptr,
        context ? context->ayanamsha_id : -1,
        split_jd(jd_tt),
        context ? context->sidereal_flags : 0u,
        out_ayanamsha_rad);
}

taiyin::Status calc_sidereal_position_tt(
    const TestSiderealContext* context,
    int body_id,
    double jd_tt,
    uint64_t flags,
    taiyin::astrology::SiderealPosition* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic,
    double reference_epoch_jd = NAN
) {
    return taiyin::astrology::calc_sidereal_position_tt(
        context ? context->native_context : nullptr,
        context ? context->ayanamsha_id : -1,
        body_id, split_jd(jd_tt),
        flags | (context ? context->sidereal_flags : 0u), out, diagnostic,
        std::isfinite(reference_epoch_jd)
            ? split_jd(reference_epoch_jd)
            : taiyin::SplitJulianDate(0, NAN));
}

taiyin::Status calc_sidereal_position_ut(
    const TestSiderealContext* context,
    int body_id,
    double jd_ut,
    uint64_t flags,
    taiyin::astrology::SiderealPosition* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic,
    double reference_epoch_jd = NAN
) {
    return taiyin::astrology::calc_sidereal_position_ut(
        context ? context->native_context : nullptr,
        context ? context->ayanamsha_id : -1,
        body_id, split_jd(jd_ut),
        flags | (context ? context->sidereal_flags : 0u), out, diagnostic,
        std::isfinite(reference_epoch_jd)
            ? split_jd(reference_epoch_jd)
            : taiyin::SplitJulianDate(0, NAN));
}

taiyin::Status calc_sidereal_coordinates_tt(
    const TestSiderealContext* context,
    int body_id,
    double jd_tt,
    uint64_t flags,
    taiyin::astrology::SiderealCoordinates* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic,
    double reference_epoch_jd = NAN
) {
    return taiyin::astrology::calc_sidereal_coordinates_tt(
        context ? context->native_context : nullptr,
        context ? context->ayanamsha_id : -1,
        body_id, split_jd(jd_tt),
        flags | (context ? context->sidereal_flags : 0u), out, diagnostic,
        std::isfinite(reference_epoch_jd)
            ? split_jd(reference_epoch_jd)
            : taiyin::SplitJulianDate(0, NAN));
}

taiyin::Status calc_sidereal_coordinates_ut(
    const TestSiderealContext* context,
    int body_id,
    double jd_ut,
    uint64_t flags,
    taiyin::astrology::SiderealCoordinates* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic,
    double reference_epoch_jd = NAN
) {
    return taiyin::astrology::calc_sidereal_coordinates_ut(
        context ? context->native_context : nullptr,
        context ? context->ayanamsha_id : -1,
        body_id, split_jd(jd_ut),
        flags | (context ? context->sidereal_flags : 0u), out, diagnostic,
        std::isfinite(reference_epoch_jd)
            ? split_jd(reference_epoch_jd)
            : taiyin::SplitJulianDate(0, NAN));
}

taiyin::Status eval_custom_ayanamsha(
    const taiyin::astrology::AyanamshaDispatchData* data,
    double* out_ayanamsha_rad
) {
    const CustomAyanamshaData* model = data
        ? static_cast<const CustomAyanamshaData*>(data->model_data) : nullptr;
    if (!data || !data->native_context || !model || !out_ayanamsha_rad
        || !taiyin::split_julian_date_is_finite(data->jd_tt)) {
        return taiyin::TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (data->native_context->apparent_options.output_frame_id
        == taiyin::TAIYIN_APPARENT_FRAME_CUSTOM) {
        return taiyin::TAIYIN_ERROR_UNSUPPORTED;
    }
    captured_custom_ayanamsha_jd = data->jd_tt;
    *out_ayanamsha_rad = model->offset_rad
        + taiyin::days_between_split_jd(
            split_jd(model->reference_jd_tt), data->jd_tt)
            * model->rate_rad_per_day;
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin::Status eval_failing_ayanamsha(
    const taiyin::astrology::AyanamshaDispatchData*,
    double*
) {
    return taiyin::TAIYIN_ERROR_UNSUPPORTED;
}

taiyin::Status eval_non_finite_ayanamsha(
    const taiyin::astrology::AyanamshaDispatchData*,
    double* out_ayanamsha_rad
) {
    if (!out_ayanamsha_rad) return taiyin::TAIYIN_ERROR_INVALID_ARGUMENT;
    *out_ayanamsha_rad = NAN;
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin::Status eval_output_shape_sensitive_ayanamsha(
    const taiyin::astrology::AyanamshaDispatchData* data,
    double* out_ayanamsha_rad
) {
    if (!data || !out_ayanamsha_rad) {
        return taiyin::TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const uint64_t correction_flags =
        taiyin::runtime::TAIYIN_NATIVE_POSITION_TRUEPOS
        | taiyin::runtime::TAIYIN_NATIVE_POSITION_NO_ABERR
        | taiyin::runtime::TAIYIN_NATIVE_POSITION_NO_GDEFL
        | taiyin::runtime::TAIYIN_NATIVE_POSITION_ASTROMETRIC;
    if ((data->native_position_flags & ~correction_flags) != 0u
        || (data->sidereal_flags
            & (taiyin::astrology::TAIYIN_SIDEREAL_POSITION_FLAGS_MASK
                & ~correction_flags)) != 0u) {
        return taiyin::TAIYIN_ERROR_UNSUPPORTED;
    }
    *out_ayanamsha_rad = 0.25;
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin::Status eval_context_sensitive_ayanamsha(
    const taiyin::astrology::AyanamshaDispatchData* data,
    double* out_ayanamsha_rad
) {
    if (!data || !data->native_context || !out_ayanamsha_rad
        || !taiyin::split_julian_date_is_finite(data->jd_tt)) {
        return taiyin::TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double rate = static_cast<double>(
        data->native_context->model_context.precession_model_id + 1) * 1.0e-6;
    *out_ayanamsha_rad = 0.1
        + taiyin::days_between_split_jd(
            taiyin::SPLIT_JD_J2000, data->jd_tt) * rate;
    return taiyin::TAIYIN_STATUS_OK;
}

bool initialize_runtime(int* failures) {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (!root || root[0] == '\0') {
        std::cerr << "FAIL: TAIYIN_REPO_ROOT is required for star-anchored ayanamsha tests\n";
        ++*failures;
        return false;
    }
    const std::string opm2_root = std::string(root) + "/data/ephemerides/opm2/major-bodies/600y";
    const char* paths[] = { opm2_root.c_str() };
    taiyin::runtime::EphemerisRuntimeConfig config;
    config.source_paths = paths;
    config.source_path_count = 1;
    config.load_packaged_data = false;
    config.segment_cache_max_entries = 64;
    if (!taiyin::runtime::initialize_global_ephemeris_runtime(config)) {
        std::cerr << "FAIL: initialize OPM2 runtime for star-anchored ayanamsha tests\n";
        ++*failures;
        return false;
    }
    return true;
}

struct SwissSiderealPositionCase {
    int ayanamsha_id;
    int body_id;
    double jd_tt;
    double sidereal_longitude_deg;
    double tolerance_arcseconds;
    const char* label;
};

const SwissSiderealPositionCase kSwissSiderealPositionCases[] = {
    { taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY, taiyin::TAIYIN_BODY_SUN,
      2451545.0, 255.631735455534567, 0.1, "Fagan J2000 Sun" },
    { taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY, taiyin::TAIYIN_BODY_MOON,
      2451545.0, 198.578440444725345, 0.5, "Fagan J2000 Moon" },
    { taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY, taiyin::TAIYIN_BODY_MARS,
      2451545.0, 303.226299391789269, 0.1, "Fagan J2000 Mars" },
    { taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY, taiyin::TAIYIN_BODY_SUN,
      2460409.0, 354.061803991696138, 0.1, "Fagan 2024 Sun" },
    { taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY, taiyin::TAIYIN_BODY_MOON,
      2460409.0, 350.339976675580147, 0.5, "Fagan 2024 Moon" },
    { taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY, taiyin::TAIYIN_BODY_MARS,
      2460409.0, 317.767327060796276, 0.1, "Fagan 2024 Mars" },
    { taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_LAHIRI, taiyin::TAIYIN_BODY_SUN,
      2451545.0, 256.514943096260652, 0.1, "Lahiri J2000 Sun" },
    { taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_LAHIRI, taiyin::TAIYIN_BODY_MOON,
      2451545.0, 199.461648085451429, 0.5, "Lahiri J2000 Moon" },
    { taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_LAHIRI, taiyin::TAIYIN_BODY_MARS,
      2451545.0, 304.109507032515353, 0.1, "Lahiri J2000 Mars" },
    { taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_LAHIRI, taiyin::TAIYIN_BODY_SUN,
      2460409.0, 354.945011635697085, 0.1, "Lahiri 2024 Sun" },
    { taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_LAHIRI, taiyin::TAIYIN_BODY_MOON,
      2460409.0, 351.223184319581094, 0.5, "Lahiri 2024 Moon" },
    { taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_LAHIRI, taiyin::TAIYIN_BODY_MARS,
      2460409.0, 318.650534704797280, 0.1, "Lahiri 2024 Mars" },
};

}  // namespace

int main() {
    using namespace taiyin;
    using namespace taiyin::astrology;
    using namespace taiyin::runtime;
    int failures = 0;
    NativeCalcContext native;
    TestSiderealContext sidereal;
    sidereal.native_context = &native;
    sidereal.sidereal_flags = TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION
        | TAIYIN_NATIVE_POSITION_NONUT;

    const CustomAyanamshaData custom_ayanamsha_data = {
        JD_J2000,
        21.25 * TAIYIN_DEG_TO_RAD,
        0.00001 * TAIYIN_DEG_TO_RAD
    };
    expect_true(
        add_ayanamsha_model(AyanamshaModelEntry(
            kCustomAyanamshaModel,
            &eval_custom_ayanamsha,
            dispatch::PRECESSION_IAU2006,
            &custom_ayanamsha_data)),
        "register custom ayanamsha model",
        &failures);
    expect_true(
        has_ayanamsha_model(kCustomAyanamshaModel),
        "find registered custom ayanamsha model",
        &failures);
    AyanamshaModelEntry custom_entry;
    expect_true(
        find_ayanamsha_model(kCustomAyanamshaModel, &custom_entry),
        "read custom ayanamsha model metadata",
        &failures);
    expect_true(
        custom_entry.reference_precession_model_id == dispatch::PRECESSION_IAU2006
            && custom_entry.model_data == &custom_ayanamsha_data,
        "preserve custom ayanamsha model metadata",
        &failures);
    expect_true(
        !add_ayanamsha_model(AyanamshaModelEntry(
            kCustomAyanamshaModel, &eval_custom_ayanamsha)),
        "reject duplicate custom ayanamsha model",
        &failures);
    expect_true(
        !add_ayanamsha_model(AyanamshaModelEntry(
            TAIYIN_SIDEREAL_AYANAMSHA_LAHIRI, &eval_custom_ayanamsha)),
        "reject built-in ayanamsha replacement",
        &failures);
    expect_true(
        !add_ayanamsha_model(AyanamshaModelEntry(
            TAIYIN_SIDEREAL_AYANAMSHA_CUSTOM_START + 99,
            &eval_custom_ayanamsha,
            dispatch::PRECESSION_CUSTOM_START + 999)),
        "reject unknown reference precession model",
        &failures);
    expect_true(
        add_ayanamsha_model(AyanamshaModelEntry(
            kFailingAyanamshaModel, &eval_failing_ayanamsha)),
        "register failing custom ayanamsha model",
        &failures);
    expect_true(
        add_ayanamsha_model(AyanamshaModelEntry(
            kNonFiniteAyanamshaModel, &eval_non_finite_ayanamsha)),
        "register non-finite custom ayanamsha model",
        &failures);
    expect_true(
        add_ayanamsha_model(AyanamshaModelEntry(
            kOutputShapeSensitiveAyanamshaModel,
            &eval_output_shape_sensitive_ayanamsha)),
        "register output-shape-sensitive custom ayanamsha model",
        &failures);
    expect_true(
        add_ayanamsha_model(AyanamshaModelEntry(
            kContextSensitiveAyanamshaModel,
            &eval_context_sensitive_ayanamsha,
            dispatch::PRECESSION_IAU2006)),
        "register context-sensitive custom ayanamsha model",
        &failures);

    sidereal.ayanamsha_id = kCustomAyanamshaModel;
    double custom_ayanamsha = NAN;
    expect_status(
        calc_ayanamsha_tt(&sidereal, JD_J2000 + 10.0, &custom_ayanamsha),
        TAIYIN_STATUS_OK,
        "evaluate custom ayanamsha model",
        &failures);
    expect_near(
        custom_ayanamsha,
        custom_ayanamsha_data.offset_rad + 10.0 * custom_ayanamsha_data.rate_rad_per_day,
        1.0e-15,
        "custom ayanamsha model value",
        &failures);
    const SplitJulianDate precise_custom_epoch(2451545, 1.0e-10);
    captured_custom_ayanamsha_jd = SplitJulianDate(0, NAN);
    expect_status(
        astrology::calc_ayanamsha_tt(
            &native,
            kCustomAyanamshaModel,
            precise_custom_epoch,
            TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION
                | TAIYIN_NATIVE_POSITION_NONUT,
            &custom_ayanamsha),
        TAIYIN_STATUS_OK,
        "preserve split JD through custom ayanamsha dispatch",
        &failures);
    expect_true(
        captured_custom_ayanamsha_jd.day_number
                == precise_custom_epoch.day_number
            && captured_custom_ayanamsha_jd.day_fraction
                == precise_custom_epoch.day_fraction,
        "custom ayanamsha evaluator receives exact split JD",
        &failures);
    sidereal.ayanamsha_id = kFailingAyanamshaModel;
    expect_status(
        calc_ayanamsha_tt(&sidereal, JD_J2000, &custom_ayanamsha),
        TAIYIN_ERROR_UNSUPPORTED,
        "propagate custom ayanamsha status",
        &failures);
    expect_true(
        std::isnan(custom_ayanamsha),
        "clear custom ayanamsha output on evaluator failure",
        &failures);
    sidereal.ayanamsha_id = kNonFiniteAyanamshaModel;
    expect_status(
        calc_ayanamsha_tt(&sidereal, JD_J2000, &custom_ayanamsha),
        TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED,
        "reject non-finite custom ayanamsha output",
        &failures);
    sidereal.ayanamsha_id = TAIYIN_SIDEREAL_AYANAMSHA_CUSTOM_START + 999;
    expect_status(
        calc_ayanamsha_tt(&sidereal, JD_J2000, &custom_ayanamsha),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject unknown ayanamsha model",
        &failures);
    sidereal.ayanamsha_id = TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY;

    double fagan = NAN;
    expect_status(calc_ayanamsha_tt(&sidereal, 2433282.42346, &fagan), TAIYIN_STATUS_OK,
                  "evaluate Fagan-Bradley at its reference epoch", &failures);
    expect_near(fagan * TAIYIN_RAD_TO_DEG, 24.042044444444445, 1.0e-9,
                "Fagan-Bradley reference value", &failures);

    sidereal.ayanamsha_id = TAIYIN_SIDEREAL_AYANAMSHA_LAHIRI;
    double lahiri = NAN;
    expect_status(calc_ayanamsha_tt(&sidereal, 2435553.5, &lahiri), TAIYIN_STATUS_OK,
                  "evaluate Lahiri at its reference epoch", &failures);
    expect_near(lahiri * TAIYIN_RAD_TO_DEG, 23.245524742777778, 1.0e-9,
                "Lahiri reference value", &failures);

    sidereal.sidereal_flags = TAIYIN_SIDEREAL_RAW_REFERENCE_OFFSET;
    double raw = NAN;
    expect_status(calc_ayanamsha_tt(&sidereal, 2451545.0, &raw), TAIYIN_STATUS_OK,
                  "evaluate raw ayanamsha", &failures);
    sidereal.sidereal_flags = 0u;
    double compensated = NAN;
    expect_status(calc_ayanamsha_tt(&sidereal, 2451545.0, &compensated), TAIYIN_STATUS_OK,
                  "evaluate compensated ayanamsha", &failures);
    if (!std::isfinite(raw) || !std::isfinite(compensated)) {
        std::cerr << "FAIL: finite policy values\n";
        ++failures;
    }
    const int ids[] = {
        TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY,
        TAIYIN_SIDEREAL_AYANAMSHA_LAHIRI,
        TAIYIN_SIDEREAL_AYANAMSHA_RAMAN,
        TAIYIN_SIDEREAL_AYANAMSHA_KRISHNAMURTI,
    };
    const double swiss_j2000_deg[] = {
        24.736430097045,
        23.853222456319,
        22.406921142937,
        23.756370142937,
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        sidereal.ayanamsha_id = ids[i];
        double value = NAN;
        expect_status(calc_ayanamsha_tt(&sidereal, 2451545.0, &value), TAIYIN_STATUS_OK,
                      "evaluate default policy", &failures);
        expect_near(value * TAIYIN_RAD_TO_DEG, swiss_j2000_deg[i], 0.05 / 3600.0,
                    "Swiss J2000 ayanamsha_ex oracle", &failures);
    }
    sidereal.ayanamsha_id = TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY;
    sidereal.sidereal_flags = TAIYIN_NATIVE_POSITION_NONUT;
    double swiss_nonut = NAN;
    expect_status(
        calc_ayanamsha_tt(&sidereal, 2451545.0, &swiss_nonut),
        TAIYIN_STATUS_OK,
        "evaluate Swiss NONUT ayanamsha_ex policy",
        &failures);
    expect_near(
        swiss_nonut * TAIYIN_RAD_TO_DEG,
        24.740299966181,
        0.05 / 3600.0,
        "Swiss J2000 NONUT ayanamsha_ex oracle",
        &failures);
    if (initialize_runtime(&failures)) {
        expect_status(
            register_builtin_astrology_targets(),
            TAIYIN_STATUS_OK,
            "register built-in astrology targets for sidereal coordinates",
            &failures);
        {
            const double jd_tt = 2460409.0;
            const uint32_t position_flags =
                TAIYIN_NATIVE_POSITION_SPEED
                | TAIYIN_NATIVE_POSITION_RADIANS
                | TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX;
            AstrologyContext astrology;
            expect_status(
                configure_astrology_context(
                    &astrology,
                    &native,
                    TAIYIN_SIDEREAL_AYANAMSHA_LAHIRI,
                    TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION),
                TAIYIN_STATUS_OK,
                "configure native sidereal output frame",
                &failures);

            double native_values[6] = {};
            expect_status(
                runtime::calc_position_tt(
                    &astrology.native_context,
                    TAIYIN_BODY_MARS,
                    split_jd(jd_tt),
                    position_flags,
                    native_values,
                    nullptr),
                TAIYIN_STATUS_OK,
                "calculate directly through astrology native context",
                &failures);

            SiderealCoordinates wrapped;
            expect_status(
                astrology::calc_sidereal_coordinates_tt(
                    &native,
                    TAIYIN_SIDEREAL_AYANAMSHA_LAHIRI,
                    TAIYIN_BODY_MARS,
                    split_jd(jd_tt),
                    position_flags | TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION,
                    &wrapped,
                    nullptr),
                TAIYIN_STATUS_OK,
                "calculate through sidereal convenience wrapper",
                &failures);
            for (int index = 0; index < 6; ++index) {
                expect_near(
                    native_values[index],
                    wrapped.values[index],
                    2.0e-11,
                    "native custom frame matches sidereal wrapper",
                    &failures);
            }

            CartesianState native_state;
            expect_status(
                runtime::calc_state_tt(
                    &astrology.native_context,
                    TAIYIN_BODY_MARS,
                    split_jd(jd_tt),
                    TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX,
                    &native_state,
                    nullptr),
                TAIYIN_STATUS_OK,
                "calculate state through astrology native context",
                &failures);
            expect_true(
                std::isfinite(native_state.position_au.x)
                    && std::isfinite(native_state.position_au.y)
                    && std::isfinite(native_state.position_au.z)
                    && std::isfinite(native_state.velocity_au_per_day.x)
                    && std::isfinite(native_state.velocity_au_per_day.y)
                    && std::isfinite(native_state.velocity_au_per_day.z)
                    && std::isfinite(native_state.acceleration_au_per_day2.x)
                    && std::isfinite(native_state.acceleration_au_per_day2.y)
                    && std::isfinite(native_state.acceleration_au_per_day2.z),
                "custom frame transforms position, velocity, and acceleration",
                &failures);

            AstrologyContext copied = astrology;
            double copied_values[6] = {};
            expect_status(
                runtime::calc_position_tt(
                    &copied.native_context,
                    TAIYIN_BODY_MARS,
                    split_jd(jd_tt),
                    position_flags,
                    copied_values,
                    nullptr),
                TAIYIN_STATUS_OK,
                "copied astrology context repairs custom frame data",
                &failures);
            for (int index = 0; index < 6; ++index) {
                expect_near(
                    copied_values[index],
                    native_values[index],
                    1.0e-15,
                    "copied astrology context result",
                    &failures);
            }

            AstrologyContext custom_astrology;
            expect_status(
                configure_astrology_context(
                    &custom_astrology,
                    &native,
                    kCustomAyanamshaModel,
                    TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION),
                TAIYIN_STATUS_OK,
                "configure custom ayanamsha native output frame",
                &failures);
            double custom_native_values[6] = {};
            expect_status(
                runtime::calc_position_tt(
                    &custom_astrology.native_context,
                    TAIYIN_BODY_MARS,
                    split_jd(jd_tt),
                    position_flags,
                    custom_native_values,
                    nullptr),
                TAIYIN_STATUS_OK,
                "custom ayanamsha evaluator receives the non-recursive base context",
                &failures);
        }
        clear_global_star_catalogs();
        struct StarAnchoredCase {
            int id;
            double swiss_j2000_deg;
            double swiss_2024_deg;
            const char* label;
        };
        const StarAnchoredCase star_cases[] = {
            { TAIYIN_SIDEREAL_AYANAMSHA_TRUE_CHITRA,
                23.836148044591738, 24.184279842155330, "True Chitra" },
            { TAIYIN_SIDEREAL_AYANAMSHA_GALACTIC_CENTER_0_SAGITTARIUS,
                26.842177847649566, 27.191297937496284, "Galactic Center 0 Sagittarius" },
        };
        sidereal.sidereal_flags = 0u;
        for (size_t i = 0; i < sizeof(star_cases) / sizeof(star_cases[0]); ++i) {
            sidereal.ayanamsha_id = star_cases[i].id;
            double j2000 = NAN;
            double date_2024 = NAN;
            expect_status(calc_ayanamsha_tt(&sidereal, 2451545.0, &j2000), TAIYIN_STATUS_OK,
                          star_cases[i].label, &failures);
            expect_status(calc_ayanamsha_tt(&sidereal, 2460409.0, &date_2024), TAIYIN_STATUS_OK,
                          star_cases[i].label, &failures);
            expect_near(j2000 * TAIYIN_RAD_TO_DEG, star_cases[i].swiss_j2000_deg,
                        0.10 / 3600.0, "Swiss J2000 star-anchored oracle", &failures);
            expect_near(date_2024 * TAIYIN_RAD_TO_DEG, star_cases[i].swiss_2024_deg,
                        0.10 / 3600.0, "Swiss 2024 star-anchored oracle", &failures);
        }

        NativeCalcContext apparent_native;
        expect_status(
            native_context_use_solar_deflector(&apparent_native),
            TAIYIN_STATUS_OK,
            "configure solar deflector for Swiss apparent position oracle",
            &failures);
        apparent_native.apparent_options.flags |= TAIYIN_APPARENT_ABERRATION
            | TAIYIN_APPARENT_DEFLECTION;
        TestSiderealContext apparent_sidereal = sidereal;
        apparent_sidereal.native_context = &apparent_native;
        apparent_sidereal.sidereal_flags = 0u;

        apparent_sidereal.ayanamsha_id = kCustomAyanamshaModel;
        apparent_sidereal.sidereal_flags = TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION
            | TAIYIN_NATIVE_POSITION_NONUT;
        SiderealPosition custom_ayanamsha_position;
        expect_status(
            calc_sidereal_position_tt(
                &apparent_sidereal, TAIYIN_BODY_SUN, 2460409.0,
                0u, &custom_ayanamsha_position, nullptr),
            TAIYIN_STATUS_OK,
            "evaluate sidereal position with custom ayanamsha model",
            &failures);
        double position_ayanamsha = NAN;
        expect_status(
            calc_ayanamsha_tt(&apparent_sidereal, 2460409.0, &position_ayanamsha),
            TAIYIN_STATUS_OK,
            "evaluate custom ayanamsha for position identity",
            &failures);
        expect_near(
            normalize_signed_radians(
                custom_ayanamsha_position.tropical_longitude_rad
                    - custom_ayanamsha_position.sidereal_longitude_rad),
            normalize_signed_radians(position_ayanamsha),
            1.0e-14,
            "custom ayanamsha sidereal position identity",
            &failures);
        apparent_sidereal.sidereal_flags = 0u;

        expect_true(
            dispatch::add_delta_t_model(
                dispatch::DeltaTModelEntry(kCustomDeltaTModel, &custom_delta_t)),
            "register custom Delta-T model for UT sidereal regression",
            &failures);
        NativeCalcContext custom_delta_native = apparent_native;
        custom_delta_native.delta_t_model_id = kCustomDeltaTModel;
        TestSiderealContext custom_delta_sidereal = apparent_sidereal;
        custom_delta_sidereal.native_context = &custom_delta_native;
        const double custom_delta_jd_ut = 2460409.0;
        const double custom_delta_jd_tt = ut1_to_tt_jd(
            custom_delta_jd_ut,
            dispatch::eval_delta_t_with_ephemeris_correction(
                custom_delta_native.delta_t_model_id,
                custom_delta_native.ephemeris_family_id,
                split_jd(custom_delta_jd_ut),
                nullptr,
                nullptr));
        SiderealPosition custom_delta_ut;
        SiderealPosition custom_delta_tt;
        expect_status(
            calc_sidereal_position_ut(
                &custom_delta_sidereal, TAIYIN_BODY_SUN, custom_delta_jd_ut,
                0u, &custom_delta_ut, nullptr),
            TAIYIN_STATUS_OK,
            "evaluate UT sidereal position with custom Delta-T",
            &failures);
        expect_status(
            calc_sidereal_position_tt(
                &custom_delta_sidereal, TAIYIN_BODY_SUN, custom_delta_jd_tt,
                0u, &custom_delta_tt, nullptr),
            TAIYIN_STATUS_OK,
            "evaluate matching TT sidereal position with custom Delta-T",
            &failures);
        expect_near(
            custom_delta_ut.tropical_longitude_rad,
            custom_delta_tt.tropical_longitude_rad,
            1.0e-14,
            "custom Delta-T UT and TT tropical longitude agree",
            &failures);
        expect_near(
            custom_delta_ut.sidereal_longitude_rad,
            custom_delta_tt.sidereal_longitude_rad,
            1.0e-14,
            "custom Delta-T UT and TT sidereal longitude agree",
            &failures);

        for (size_t i = 0;
             i < sizeof(kSwissSiderealPositionCases) / sizeof(kSwissSiderealPositionCases[0]);
             ++i) {
            const SwissSiderealPositionCase& oracle = kSwissSiderealPositionCases[i];
            apparent_sidereal.ayanamsha_id = oracle.ayanamsha_id;
            SiderealPosition position;
            const uint32_t position_flags = oracle.body_id == TAIYIN_BODY_MARS
                ? TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX : 0u;
            expect_status(
                calc_sidereal_position_tt(
                    &apparent_sidereal, oracle.body_id, oracle.jd_tt,
                    position_flags, &position, nullptr),
                TAIYIN_STATUS_OK,
                oracle.label,
                &failures);
            const double difference = std::fabs(normalize_signed_radians(
                position.sidereal_longitude_rad - oracle.sidereal_longitude_deg * TAIYIN_DEG_TO_RAD));
            if (difference > oracle.tolerance_arcseconds * TAIYIN_ARCSEC_TO_RAD) {
                std::cerr << "FAIL: Swiss sidereal position oracle " << oracle.label
                          << " actual=" << position.sidereal_longitude_rad * TAIYIN_RAD_TO_DEG
                          << " expected=" << oracle.sidereal_longitude_deg
                          << " diff_arcsec=" << difference * TAIYIN_RAD_TO_ARCSEC << "\n";
                ++failures;
            }
            double ayanamsha = NAN;
            expect_status(
                calc_ayanamsha_tt(&apparent_sidereal, oracle.jd_tt, &ayanamsha),
                TAIYIN_STATUS_OK,
                "evaluate sidereal position ayanamsha", &failures);
            expect_near(
                normalize_signed_radians(
                    position.tropical_longitude_rad - position.sidereal_longitude_rad),
                normalize_signed_radians(ayanamsha),
                1.0e-14,
                "sidereal position longitude identity",
                &failures);
            double tropical_values[6] = {};
            expect_status(
                runtime::calc_position_tt(
                    &apparent_native,
                    oracle.body_id,
                    split_jd(oracle.jd_tt),
                    position_flags | TAIYIN_NATIVE_POSITION_RADIANS,
                    tropical_values,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate apparent tropical longitude oracle",
                &failures);
            expect_near(
                normalize_signed_radians(
                    position.tropical_longitude_rad - tropical_values[0]),
                0.0,
                2.0e-12,
                "sidereal position exposes apparent tropical longitude",
                &failures);
        }

        {
            const double jd_tt = 2460409.0;
            apparent_sidereal.ayanamsha_id =
                TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY;
            const uint32_t spherical_flags = TAIYIN_NATIVE_POSITION_SPEED
                | TAIYIN_NATIVE_POSITION_RADIANS;
            SiderealPosition structured;
            SiderealCoordinates ecliptic;
            expect_status(
                calc_sidereal_position_tt(
                    &apparent_sidereal,
                    TAIYIN_BODY_SUN,
                    jd_tt,
                    spherical_flags,
                    &structured,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate structured sidereal position for generic-coordinate regression",
                &failures);
            double tropical_with_speed[6] = {};
            expect_status(
                runtime::calc_position_tt(
                    &apparent_native,
                    TAIYIN_BODY_SUN,
                    split_jd(jd_tt),
                    spherical_flags,
                    tropical_with_speed,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate apparent tropical position and rate oracle",
                &failures);
            expect_near(
                structured.tropical_longitude_rad,
                tropical_with_speed[0],
                2.0e-12,
                "structured apparent tropical longitude",
                &failures);
            expect_near(
                structured.tropical_longitude_rate_rad_per_day,
                tropical_with_speed[3],
                2.0e-10,
                "structured apparent tropical longitude rate",
                &failures);
            expect_status(
                calc_sidereal_coordinates_tt(
                    &apparent_sidereal,
                    TAIYIN_BODY_SUN,
                    jd_tt,
                    spherical_flags,
                    &ecliptic,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate generic ecliptic sidereal coordinates",
                &failures);
            expect_true(
                ecliptic.coordinate_frame_id
                    == TAIYIN_SIDEREAL_FRAME_MEAN_ECLIPTIC_OF_DATE,
                "generic ecliptic sidereal coordinate frame",
                &failures);
            expect_true(
                ecliptic.position_flags == spherical_flags,
                "generic ecliptic sidereal preserves requested flags",
                &failures);
            expect_near(
                ecliptic.values[0], structured.sidereal_longitude_rad, 1.0e-15,
                "generic ecliptic sidereal longitude matches structured API",
                &failures);
            expect_near(
                ecliptic.values[1], structured.latitude_rad, 1.0e-15,
                "generic ecliptic sidereal latitude matches structured API",
                &failures);
            expect_near(
                ecliptic.values[2], structured.distance_au, 1.0e-15,
                "generic ecliptic sidereal distance matches structured API",
                &failures);
            expect_near(
                ecliptic.values[3], structured.sidereal_longitude_rate_rad_per_day,
                1.0e-15,
                "generic ecliptic sidereal longitude rate matches structured API",
                &failures);

            SiderealCoordinates ecliptic_xyz;
            expect_status(
                calc_sidereal_coordinates_tt(
                    &apparent_sidereal,
                    TAIYIN_BODY_SUN,
                    jd_tt,
                    spherical_flags | TAIYIN_NATIVE_POSITION_XYZ,
                    &ecliptic_xyz,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate Cartesian ecliptic sidereal coordinates",
                &failures);
            EclipticPositionVelocity ecliptic_from_xyz;
            expect_true(
                cartesian_position_velocity_to_ecliptic(
                    Vector3{
                        ecliptic_xyz.values[0],
                        ecliptic_xyz.values[1],
                        ecliptic_xyz.values[2],
                    },
                    Vector3{
                        ecliptic_xyz.values[3],
                        ecliptic_xyz.values[4],
                        ecliptic_xyz.values[5],
                    },
                    &ecliptic_from_xyz),
                "convert Cartesian ecliptic sidereal coordinates",
                &failures);
            expect_near(
                ecliptic_from_xyz.longitude_rad, ecliptic.values[0], 1.0e-14,
                "Cartesian ecliptic sidereal longitude",
                &failures);
            expect_near(
                ecliptic_from_xyz.latitude_rad, ecliptic.values[1], 1.0e-14,
                "Cartesian ecliptic sidereal latitude",
                &failures);
            expect_near(
                ecliptic_from_xyz.radius_au, ecliptic.values[2], 1.0e-14,
                "Cartesian ecliptic sidereal distance",
                &failures);
            expect_near(
                ecliptic_from_xyz.longitude_rate_rad_per_day,
                ecliptic.values[3],
                1.0e-14,
                "Cartesian ecliptic sidereal longitude rate",
                &failures);

            TestSiderealContext output_shape_sensitive_sidereal = apparent_sidereal;
            output_shape_sensitive_sidereal.ayanamsha_id =
                kOutputShapeSensitiveAyanamshaModel;
            SiderealCoordinates output_shape_sensitive_xyz;
            expect_status(
                calc_sidereal_coordinates_tt(
                    &output_shape_sensitive_sidereal,
                    TAIYIN_BODY_SUN,
                    jd_tt,
                    TAIYIN_NATIVE_POSITION_XYZ
                        | TAIYIN_NATIVE_POSITION_SPEED
                        | TAIYIN_NATIVE_POSITION_RADIANS,
                    &output_shape_sensitive_xyz,
                    nullptr),
                TAIYIN_STATUS_OK,
                "generic sidereal coordinates mask output flags for ayanamsha evaluators",
                &failures);

            NativeCalcContext context_sensitive_native = apparent_native;
            context_sensitive_native.model_context.precession_model_id =
                dispatch::PRECESSION_VONDRAK2011;
            context_sensitive_native.apparent_options.model_context =
                &context_sensitive_native.model_context;
            TestSiderealContext context_sensitive_sidereal = apparent_sidereal;
            context_sensitive_sidereal.native_context = &context_sensitive_native;
            context_sensitive_sidereal.ayanamsha_id =
                kContextSensitiveAyanamshaModel;
            context_sensitive_sidereal.sidereal_flags =
                TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION;
            SiderealPosition context_sensitive_position;
            expect_status(
                calc_sidereal_position_tt(
                    &context_sensitive_sidereal,
                    TAIYIN_BODY_SUN,
                    jd_tt,
                    TAIYIN_NATIVE_POSITION_SPEED
                        | TAIYIN_NATIVE_POSITION_RADIANS,
                    &context_sensitive_position,
                    nullptr),
                TAIYIN_STATUS_OK,
                "structured sidereal position uses the effective precession context",
                &failures);
            NativeCalcContext reference_native = context_sensitive_native;
            reference_native.model_context.precession_model_id =
                dispatch::PRECESSION_IAU2006;
            reference_native.apparent_options.model_context =
                &reference_native.model_context;
            reference_native.apparent_options.output_frame_id =
                TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
            double reference_values[6] = {};
            expect_status(
                runtime::calc_position_tt(
                    &reference_native,
                    TAIYIN_BODY_SUN,
                    split_jd(jd_tt),
                    TAIYIN_NATIVE_POSITION_SPEED
                        | TAIYIN_NATIVE_POSITION_RADIANS,
                    reference_values,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate effective-precession apparent tropical reference",
                &failures);
            expect_near(
                context_sensitive_position.tropical_longitude_rad,
                reference_values[0],
                2.0e-11,
                "structured tropical longitude matches effective context",
                &failures);
            expect_near(
                context_sensitive_position.tropical_longitude_rate_rad_per_day,
                reference_values[3],
                2.0e-10,
                "structured tropical rate matches effective context",
                &failures);
            double context_sensitive_offset = NAN;
            expect_status(
                astrology::calc_ayanamsha_tt(
                    &context_sensitive_native,
                    kContextSensitiveAyanamshaModel,
                    split_jd(jd_tt),
                    TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION
                        | TAIYIN_NATIVE_POSITION_NONUT,
                    &context_sensitive_offset),
                TAIYIN_STATUS_OK,
                "direct ayanamsha uses the effective precession context",
                &failures);
            expect_near(
                context_sensitive_offset,
                normalize_radians(
                    0.1 + (jd_tt - JD_J2000) * 2.0e-6),
                1.0e-15,
                "custom ayanamsha sees its reference precession model",
                &failures);

            SiderealCoordinates equatorial;
            SiderealCoordinates equatorial_xyz;
            SiderealCoordinates true_equatorial;
            const uint32_t mean_equatorial_flags = spherical_flags
                | TAIYIN_NATIVE_POSITION_EQUATORIAL
                | TAIYIN_NATIVE_POSITION_NONUT;
            expect_status(
                calc_sidereal_coordinates_tt(
                    &apparent_sidereal,
                    TAIYIN_BODY_SUN,
                    jd_tt,
                    mean_equatorial_flags,
                    &equatorial,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate equatorial sidereal coordinates",
                &failures);
            expect_status(
                calc_sidereal_coordinates_tt(
                    &apparent_sidereal,
                    TAIYIN_BODY_SUN,
                    jd_tt,
                    mean_equatorial_flags
                        | TAIYIN_NATIVE_POSITION_XYZ,
                    &equatorial_xyz,
                    nullptr),
                TAIYIN_STATUS_OK,
                    "evaluate Cartesian equatorial sidereal coordinates",
                    &failures);
            expect_status(
                calc_sidereal_coordinates_tt(
                    &apparent_sidereal,
                    TAIYIN_BODY_SUN,
                    jd_tt,
                    spherical_flags | TAIYIN_NATIVE_POSITION_EQUATORIAL,
                    &true_equatorial,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate true-equatorial sidereal coordinates",
                &failures);
            expect_true(
                equatorial.coordinate_frame_id
                    == TAIYIN_SIDEREAL_FRAME_MEAN_EQUATOR_OF_DATE
                    && equatorial_xyz.coordinate_frame_id
                        == TAIYIN_SIDEREAL_FRAME_MEAN_EQUATOR_OF_DATE,
                "generic mean-equatorial sidereal coordinate frame",
                &failures);
            expect_true(
                true_equatorial.coordinate_frame_id
                    == TAIYIN_SIDEREAL_FRAME_TRUE_EQUATOR_OF_DATE,
                "generic true-equatorial sidereal coordinate frame",
                &failures);
            double expected_mean_equatorial[6] = {};
            double expected_mean_equatorial_xyz[6] = {};
            double expected_true_equatorial[6] = {};
            expect_status(
                runtime::calc_position_tt(
                    &apparent_native,
                    TAIYIN_BODY_SUN,
                    split_jd(jd_tt),
                    mean_equatorial_flags,
                    expected_mean_equatorial,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate native mean-equatorial reference",
                &failures);
            expect_status(
                runtime::calc_position_tt(
                    &apparent_native,
                    TAIYIN_BODY_SUN,
                    split_jd(jd_tt),
                    mean_equatorial_flags | TAIYIN_NATIVE_POSITION_XYZ,
                    expected_mean_equatorial_xyz,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate native Cartesian mean-equatorial reference",
                &failures);
            expect_status(
                runtime::calc_position_tt(
                    &apparent_native,
                    TAIYIN_BODY_SUN,
                    split_jd(jd_tt),
                    spherical_flags | TAIYIN_NATIVE_POSITION_EQUATORIAL,
                    expected_true_equatorial,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate native true-equatorial reference",
                &failures);
            for (int index = 0; index < 6; ++index) {
                expect_near(
                    equatorial.values[index], expected_mean_equatorial[index], 1.0e-14,
                    "sidereal equatorial output matches native mean equator",
                    &failures);
                expect_near(
                    equatorial_xyz.values[index], expected_mean_equatorial_xyz[index], 1.0e-14,
                    "sidereal Cartesian equatorial output matches native mean equator",
                    &failures);
                expect_near(
                    true_equatorial.values[index], expected_true_equatorial[index], 1.0e-14,
                    "sidereal equatorial output matches native true equator",
                    &failures);
            }
            // Cartesian-to-spherical conversion preserves this equatorial
            // frame; its longitude and latitude are right ascension and
            // declination rather than ecliptic coordinates.
            EclipticPositionVelocity equatorial_spherical_from_xyz;
            expect_true(
                cartesian_position_velocity_to_ecliptic(
                    Vector3{
                        equatorial_xyz.values[0],
                        equatorial_xyz.values[1],
                        equatorial_xyz.values[2],
                    },
                    Vector3{
                        equatorial_xyz.values[3],
                        equatorial_xyz.values[4],
                        equatorial_xyz.values[5],
                    },
                    &equatorial_spherical_from_xyz),
                "convert Cartesian equatorial sidereal coordinates",
                &failures);
            expect_near(
                equatorial_spherical_from_xyz.longitude_rad, equatorial.values[0], 1.0e-14,
                "Cartesian equatorial sidereal right ascension",
                &failures);
            expect_near(
                equatorial_spherical_from_xyz.latitude_rad, equatorial.values[1], 1.0e-14,
                "Cartesian equatorial sidereal declination",
                &failures);
            expect_near(
                equatorial_spherical_from_xyz.longitude_rate_rad_per_day,
                equatorial.values[3],
                1.0e-14,
                "Cartesian equatorial sidereal right-ascension rate",
                &failures);

            const double step_days = 1.0e-3;
            SiderealCoordinates before;
            SiderealCoordinates after;
            expect_status(
                calc_sidereal_coordinates_tt(
                    &apparent_sidereal,
                    TAIYIN_BODY_SUN,
                    jd_tt - step_days,
                    TAIYIN_NATIVE_POSITION_EQUATORIAL
                        | TAIYIN_NATIVE_POSITION_XYZ
                        | TAIYIN_NATIVE_POSITION_NONUT,
                    &before,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate previous Cartesian equatorial sidereal coordinates",
                &failures);
            expect_status(
                calc_sidereal_coordinates_tt(
                    &apparent_sidereal,
                    TAIYIN_BODY_SUN,
                    jd_tt + step_days,
                    TAIYIN_NATIVE_POSITION_EQUATORIAL
                        | TAIYIN_NATIVE_POSITION_XYZ
                        | TAIYIN_NATIVE_POSITION_NONUT,
                    &after,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate next Cartesian equatorial sidereal coordinates",
                &failures);
            for (int index = 0; index < 3; ++index) {
                expect_near(
                    equatorial_xyz.values[index + 3],
                    (after.values[index] - before.values[index])
                        / (2.0 * step_days),
                    5.0e-9,
                    "equatorial sidereal Cartesian velocity finite difference",
                    &failures);
            }

            TestSiderealContext lahiri_sidereal = apparent_sidereal;
            lahiri_sidereal.ayanamsha_id = TAIYIN_SIDEREAL_AYANAMSHA_LAHIRI;
            SiderealCoordinates lahiri_equatorial;
            expect_status(
                calc_sidereal_coordinates_tt(
                    &lahiri_sidereal,
                    TAIYIN_BODY_SUN,
                    jd_tt,
                    mean_equatorial_flags,
                    &lahiri_equatorial,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate alternate-ayanamsha equatorial coordinates",
                &failures);
            for (int index = 0; index < 6; ++index) {
                expect_near(
                    lahiri_equatorial.values[index], equatorial.values[index], 1.0e-14,
                    "equatorial sidereal output is independent of ayanamsha",
                    &failures);
            }

            SiderealCoordinates direction_only_equatorial;
            expect_status(
                calc_sidereal_coordinates_tt(
                    &apparent_sidereal,
                    TAIYIN_ASTROLOGY_TARGET_MEAN_NODE,
                    jd_tt,
                    TAIYIN_NATIVE_POSITION_EQUATORIAL
                        | TAIYIN_NATIVE_POSITION_RADIANS,
                    &direction_only_equatorial,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate direction-only equatorial sidereal coordinates",
                &failures);
            expect_true(
                std::isfinite(direction_only_equatorial.values[0])
                    && std::isfinite(direction_only_equatorial.values[1])
                    && std::isnan(direction_only_equatorial.values[2]),
                "direction-only equatorial sidereal coordinates preserve angles",
                &failures);

            SiderealCoordinates degrees;
            SiderealCoordinates no_nutation;
            expect_status(
                calc_sidereal_coordinates_tt(
                    &apparent_sidereal,
                    TAIYIN_BODY_SUN,
                    jd_tt,
                    0u,
                    &degrees,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate degree sidereal coordinates",
                &failures);
            expect_status(
                calc_sidereal_coordinates_tt(
                    &apparent_sidereal,
                    TAIYIN_BODY_SUN,
                    jd_tt,
                    TAIYIN_NATIVE_POSITION_NONUT,
                    &no_nutation,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate explicit mean sidereal coordinates",
                &failures);
            expect_near(
                degrees.values[0], ecliptic.values[0] * TAIYIN_RAD_TO_DEG, 1.0e-12,
                "generic sidereal degree output",
                &failures);
            for (int index = 0; index < 3; ++index) {
                expect_near(
                    degrees.values[index], no_nutation.values[index], 1.0e-15,
                    "NONUT does not alter a mean sidereal frame",
                    &failures);
            }

            SiderealCoordinates no_nutation_with_speed;
            expect_status(
                calc_sidereal_coordinates_tt(
                    &apparent_sidereal,
                    TAIYIN_BODY_SUN,
                    jd_tt,
                    spherical_flags | TAIYIN_NATIVE_POSITION_NONUT,
                    &no_nutation_with_speed,
                    nullptr),
                TAIYIN_STATUS_OK,
                "evaluate explicit mean sidereal coordinates with speed",
                &failures);
            for (int index = 0; index < 6; ++index) {
                expect_near(
                    ecliptic.values[index], no_nutation_with_speed.values[index], 1.0e-15,
                    "NONUT does not alter mean sidereal position or rates",
                    &failures);
            }
        }

        {
            const double jd_tt = 2460409.0;
            const uint32_t flags = TAIYIN_NATIVE_POSITION_SPEED
                | TAIYIN_NATIVE_POSITION_RADIANS;
            TestSiderealContext j2000 = apparent_sidereal;
            j2000.ayanamsha_id = TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY;
            TestSiderealContext explicit_j2000 = j2000;
            const uint64_t j2000_flags = flags
                | TAIYIN_SIDEREAL_REFERENCE_J2000_ECLIPTIC;
            const uint64_t fixed_j2000_flags = flags
                | TAIYIN_SIDEREAL_REFERENCE_ECL_T0;

            SiderealPosition j2000_position;
            SiderealCoordinates j2000_coordinates;
            SiderealCoordinates explicit_j2000_coordinates;
            SiderealCoordinates j2000_nonut;
            expect_status(
                calc_sidereal_position_tt(
                    &j2000, TAIYIN_BODY_MARS, jd_tt,
                    j2000_flags | TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX,
                    &j2000_position, nullptr),
                TAIYIN_STATUS_OK,
                "evaluate J2000 sidereal position",
                &failures);
            expect_status(
                calc_sidereal_coordinates_tt(
                    &j2000, TAIYIN_BODY_MARS, jd_tt,
                    j2000_flags | TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX,
                    &j2000_coordinates, nullptr),
                TAIYIN_STATUS_OK,
                "evaluate J2000 sidereal coordinates",
                &failures);
            expect_status(
                calc_sidereal_coordinates_tt(
                    &explicit_j2000, TAIYIN_BODY_MARS, jd_tt,
                    fixed_j2000_flags | TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX,
                    &explicit_j2000_coordinates, nullptr, JD_J2000),
                TAIYIN_STATUS_OK,
                "evaluate explicit J2000 sidereal coordinates",
                &failures);
            expect_status(
                calc_sidereal_coordinates_tt(
                    &j2000, TAIYIN_BODY_MARS, jd_tt,
                    j2000_flags | TAIYIN_NATIVE_POSITION_NONUT
                        | TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX,
                    &j2000_nonut, nullptr),
                TAIYIN_STATUS_OK,
                "evaluate non-nutated J2000 sidereal coordinates",
                &failures);
            expect_true(
                j2000_position.coordinate_frame_id
                    == TAIYIN_SIDEREAL_FRAME_J2000_ECLIPTIC
                    && j2000_coordinates.coordinate_frame_id
                        == TAIYIN_SIDEREAL_FRAME_J2000_ECLIPTIC
                    && explicit_j2000_coordinates.coordinate_frame_id
                        == TAIYIN_SIDEREAL_FRAME_FIXED_MEAN_ECLIPTIC_AT_EPOCH,
                "fixed J2000 sidereal coordinate frames",
                &failures);
            expect_near(
                j2000_position.sidereal_longitude_rad, j2000_coordinates.values[0], 1.0e-14,
                "J2000 structured and generic sidereal longitude agree",
                &failures);
            for (int index = 0; index < 6; ++index) {
                expect_near(
                    j2000_coordinates.values[index], explicit_j2000_coordinates.values[index],
                    1.0e-14,
                    "J2000 reference-plane shortcut matches explicit epoch",
                    &failures);
                expect_near(
                    j2000_coordinates.values[index], j2000_nonut.values[index], 1.0e-14,
                    "NONUT does not alter the fixed J2000 ecliptic",
                    &failures);
            }

            struct FixedPlaneSwissOracle {
                int body_id;
                double jd_tt;
                double longitude_deg;
                double latitude_deg;
                double tolerance_arcseconds;
                const char* label;
            };
            // Generated with Swiss Ephemeris 2.10.03 and the local Swiss .se1 files:
            // SE_SIDM_FAGAN_BRADLEY | SE_SIDBIT_ECL_T0,
            // SEFLG_SWIEPH | SEFLG_SIDEREAL | SEFLG_NONUT.
            // t0 is Fagan/Bradley's B1950.0 reference epoch.
            const FixedPlaneSwissOracle fixed_plane_oracles[] = {
                { TAIYIN_BODY_SUN, jd_tt, 354.061803745170039, -0.003989802009938,
                  0.1, "Swiss ECL_T0 Fagan Sun" },
                { TAIYIN_BODY_MOON, 2415020.5, 249.068056498215, 1.101823400005,
                  0.02, "Swiss ECL_T0 Fagan Moon 1900" },
                { TAIYIN_BODY_MOON, JD_J2000, 198.578047375276, 5.175749314178,
                  0.02, "Swiss ECL_T0 Fagan Moon J2000" },
                { TAIYIN_BODY_MOON, jd_tt, 350.339973399251846, -0.022712332455778,
                  0.02, "Swiss ECL_T0 Fagan Moon 2024" },
                { TAIYIN_BODY_MOON, 2488070.5, 145.296793340149, -0.144427672050,
                  0.02, "Swiss ECL_T0 Fagan Moon 2100" },
                { TAIYIN_BODY_MARS, jd_tt, 317.767121633721104, -1.242002229359210,
                  0.1, "Swiss ECL_T0 Fagan Mars" },
            };
            TestSiderealContext fagan_fixed = j2000;
            for (size_t index = 0;
                 index < sizeof(fixed_plane_oracles) / sizeof(fixed_plane_oracles[0]);
                 ++index) {
                const FixedPlaneSwissOracle& oracle = fixed_plane_oracles[index];
                SiderealCoordinates result;
                const uint32_t body_flags = TAIYIN_NATIVE_POSITION_RADIANS
                    | (oracle.body_id == TAIYIN_BODY_MARS
                        ? TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX : 0u);
                expect_status(
                    calc_sidereal_coordinates_tt(
                        &fagan_fixed, oracle.body_id, oracle.jd_tt,
                        body_flags | TAIYIN_SIDEREAL_REFERENCE_ECL_T0,
                        &result, nullptr, 2433282.42346),
                    TAIYIN_STATUS_OK,
                    oracle.label,
                    &failures);
                const double longitude_difference = std::fabs(normalize_signed_radians(
                    result.values[0] - oracle.longitude_deg * TAIYIN_DEG_TO_RAD));
                if (longitude_difference > oracle.tolerance_arcseconds * TAIYIN_ARCSEC_TO_RAD) {
                    std::cerr << "FAIL: " << oracle.label << " longitude diff_arcsec="
                              << longitude_difference * TAIYIN_RAD_TO_ARCSEC << "\n";
                    ++failures;
                }
                expect_near(
                    result.values[1], oracle.latitude_deg * TAIYIN_DEG_TO_RAD,
                    oracle.tolerance_arcseconds * TAIYIN_ARCSEC_TO_RAD,
                    oracle.label,
                    &failures);
            }

            TestSiderealContext user_ut = j2000;
            const double user_ut_epoch_jd = 2460408.75;
            const double anchor_delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
                apparent_native.delta_t_model_id,
                apparent_native.ephemeris_family_id,
                split_jd(user_ut_epoch_jd),
                nullptr,
                nullptr);
            TestSiderealContext equivalent_tt = user_ut;
            const double equivalent_tt_epoch_jd = ut1_to_tt_jd(
                user_ut_epoch_jd, anchor_delta_t);
            SiderealCoordinates user_ut_coordinates;
            SiderealCoordinates equivalent_tt_coordinates;
            expect_status(
                calc_sidereal_coordinates_tt(
                    &user_ut, TAIYIN_BODY_SUN, jd_tt,
                    flags | TAIYIN_SIDEREAL_REFERENCE_ECL_T0
                        | TAIYIN_SIDEREAL_REFERENCE_EPOCH_UT1,
                    &user_ut_coordinates, nullptr, user_ut_epoch_jd),
                TAIYIN_STATUS_OK,
                "evaluate UT1 fixed-ecliptic reference epoch",
                &failures);
            expect_status(
                calc_sidereal_coordinates_tt(
                    &equivalent_tt, TAIYIN_BODY_SUN, jd_tt,
                    flags | TAIYIN_SIDEREAL_REFERENCE_ECL_T0,
                    &equivalent_tt_coordinates, nullptr, equivalent_tt_epoch_jd),
                TAIYIN_STATUS_OK,
                "evaluate equivalent TT fixed-ecliptic reference epoch",
                &failures);
            for (int index = 0; index < 6; ++index) {
                expect_near(
                    user_ut_coordinates.values[index], equivalent_tt_coordinates.values[index],
                    1.0e-14,
                    "UT1 and equivalent TT reference epochs agree",
                    &failures);
            }

            NativeCalcContext split_reference_native = apparent_native;
            split_reference_native.delta_t_model_id = kCustomDeltaTModel;
            const SplitJulianDate precise_reference_epoch(2451545, 1.0e-10);
            captured_delta_t_jd = SplitJulianDate(0, NAN);
            SiderealCoordinates precise_reference_coordinates;
            expect_status(
                astrology::calc_sidereal_coordinates_tt(
                    &split_reference_native,
                    TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY,
                    TAIYIN_BODY_SUN,
                    split_jd(jd_tt),
                    flags | TAIYIN_SIDEREAL_REFERENCE_ECL_T0
                        | TAIYIN_SIDEREAL_REFERENCE_EPOCH_UT1,
                    &precise_reference_coordinates,
                    nullptr,
                    precise_reference_epoch),
                TAIYIN_STATUS_OK,
                "preserve split UT1 sidereal reference epoch",
                &failures);
            expect_true(
                captured_delta_t_jd.day_number == precise_reference_epoch.day_number,
                "split sidereal reference epoch preserves day number",
                &failures);
            expect_near(
                captured_delta_t_jd.day_fraction,
                precise_reference_epoch.day_fraction,
                1.0e-20,
                "split sidereal reference epoch preserves sub-double fraction",
                &failures);

            TestSiderealContext invariable = j2000;
            SiderealCoordinates invariable_coordinates;
            expect_status(
                calc_sidereal_coordinates_tt(
                    &invariable, TAIYIN_BODY_MARS, jd_tt,
                    flags | TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX
                        | TAIYIN_SIDEREAL_REFERENCE_SSY_PLANE,
                    &invariable_coordinates, nullptr, JD_J2000),
                TAIYIN_STATUS_OK,
                "evaluate solar-system-invariable sidereal coordinates",
                &failures);
            expect_true(
                invariable_coordinates.coordinate_frame_id
                    == TAIYIN_SIDEREAL_FRAME_SOLAR_SYSTEM_INVARIABLE
                    && std::isfinite(invariable_coordinates.values[0])
                    && std::isfinite(invariable_coordinates.values[1])
                    && std::fabs(invariable_coordinates.values[1] - j2000_coordinates.values[1])
                        > 0.1 * TAIYIN_DEG_TO_RAD,
                "solar-system-invariable plane performs a three-dimensional transform",
                &failures);

            // Generated with Swiss Ephemeris 2.10.03 and the local Swiss .se1 files:
            // SE_SIDM_FAGAN_BRADLEY | SE_SIDBIT_SSY_PLANE,
            // SEFLG_SWIEPH | SEFLG_SIDEREAL | SEFLG_NONUT.
            const FixedPlaneSwissOracle invariable_oracles[] = {
                { TAIYIN_BODY_SUN, jd_tt, 354.068317682507029, 1.576989409159420,
                  0.1, "Swiss SSY Fagan Sun" },
                { TAIYIN_BODY_MOON, 2415020.5, 249.049807345865, 0.719139772374,
                  0.02, "Swiss SSY Fagan Moon 1900" },
                { TAIYIN_BODY_MOON, JD_J2000, 198.531074720142, 3.748436813810,
                  0.02, "Swiss SSY Fagan Moon J2000" },
                { TAIYIN_BODY_MOON, jd_tt, 350.345102904166026, 1.556725507227144,
                  0.02, "Swiss SSY Fagan Moon 2024" },
                { TAIYIN_BODY_MOON, 2488070.5, 145.292074350054, -1.543559462824,
                  0.02, "Swiss SSY Fagan Moon 2100" },
                { TAIYIN_BODY_MARS, jd_tt, 317.782769605490330, 0.048697329315726,
                  0.1, "Swiss SSY Fagan Mars" },
            };
            TestSiderealContext fagan_invariable = invariable;
            for (size_t index = 0;
                 index < sizeof(invariable_oracles) / sizeof(invariable_oracles[0]);
                 ++index) {
                const FixedPlaneSwissOracle& oracle = invariable_oracles[index];
                SiderealCoordinates result;
                const uint32_t body_flags = TAIYIN_NATIVE_POSITION_RADIANS
                    | (oracle.body_id == TAIYIN_BODY_MARS
                        ? TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX : 0u);
                expect_status(
                    calc_sidereal_coordinates_tt(
                        &fagan_invariable, oracle.body_id, oracle.jd_tt,
                        body_flags | TAIYIN_SIDEREAL_REFERENCE_SSY_PLANE,
                        &result, nullptr, 2433282.42346),
                    TAIYIN_STATUS_OK,
                    oracle.label,
                    &failures);
                const double longitude_difference = std::fabs(normalize_signed_radians(
                    result.values[0] - oracle.longitude_deg * TAIYIN_DEG_TO_RAD));
                if (longitude_difference > oracle.tolerance_arcseconds * TAIYIN_ARCSEC_TO_RAD) {
                    std::cerr << "FAIL: " << oracle.label << " longitude diff_arcsec="
                              << longitude_difference * TAIYIN_RAD_TO_ARCSEC << "\n";
                    ++failures;
                }
                expect_near(
                    result.values[1], oracle.latitude_deg * TAIYIN_DEG_TO_RAD,
                    oracle.tolerance_arcseconds * TAIYIN_ARCSEC_TO_RAD,
                    oracle.label,
                    &failures);
            }

            SiderealCoordinates fixed_mean_node;
            expect_status(
                calc_sidereal_coordinates_tt(
                    &j2000, TAIYIN_ASTROLOGY_TARGET_MEAN_NODE, jd_tt,
                    TAIYIN_NATIVE_POSITION_RADIANS
                        | TAIYIN_SIDEREAL_REFERENCE_J2000_ECLIPTIC,
                    &fixed_mean_node, nullptr),
                TAIYIN_STATUS_OK,
                "evaluate direction-only node on the fixed J2000 plane",
                &failures);
            expect_true(
                std::isfinite(fixed_mean_node.values[0])
                    && std::isfinite(fixed_mean_node.values[1])
                    && std::isnan(fixed_mean_node.values[2]),
                "fixed J2000 plane preserves direction-only node semantics",
                &failures);

            expect_status(
                calc_sidereal_coordinates_tt(
                    &explicit_j2000, TAIYIN_BODY_SUN, jd_tt,
                    flags | TAIYIN_SIDEREAL_REFERENCE_ECL_T0,
                    &equivalent_tt_coordinates, nullptr, NAN),
                TAIYIN_ERROR_INVALID_ARGUMENT,
                "reject fixed sidereal plane without an epoch",
                &failures);
            expect_status(
                calc_sidereal_coordinates_tt(
                    &explicit_j2000, TAIYIN_BODY_SUN, jd_tt,
                    flags | TAIYIN_SIDEREAL_REFERENCE_ECL_T0
                        | TAIYIN_SIDEREAL_REFERENCE_J2000_ECLIPTIC,
                    &equivalent_tt_coordinates, nullptr, JD_J2000),
                TAIYIN_ERROR_INVALID_ARGUMENT,
                "reject conflicting sidereal reference-plane flags",
                &failures);
            expect_status(
                calc_sidereal_coordinates_tt(
                    &explicit_j2000, TAIYIN_BODY_SUN, jd_tt,
                    flags | TAIYIN_SIDEREAL_RAW_REFERENCE_OFFSET
                        | TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION,
                    &equivalent_tt_coordinates, nullptr, NAN),
                TAIYIN_ERROR_INVALID_ARGUMENT,
                "reject conflicting sidereal precession-policy flags",
                &failures);
            expect_status(
                calc_sidereal_coordinates_tt(
                    &explicit_j2000, TAIYIN_BODY_SUN, jd_tt,
                    flags | TAIYIN_SIDEREAL_REFERENCE_EPOCH_UT1,
                    &equivalent_tt_coordinates, nullptr, NAN),
                TAIYIN_ERROR_INVALID_ARGUMENT,
                "reject sidereal UT1 epoch flag without a reference epoch",
                &failures);
            SiderealCoordinates equatorial_reference_plane;
            expect_status(
                calc_sidereal_coordinates_tt(
                    &explicit_j2000, TAIYIN_BODY_SUN, jd_tt,
                    flags | TAIYIN_NATIVE_POSITION_EQUATORIAL
                        | TAIYIN_SIDEREAL_REFERENCE_J2000_ECLIPTIC,
                    &equatorial_reference_plane, nullptr, NAN),
                TAIYIN_STATUS_OK,
                "equatorial output overrides the configured sidereal reference plane",
                &failures);
            expect_true(
                equatorial_reference_plane.coordinate_frame_id
                    == TAIYIN_SIDEREAL_FRAME_TRUE_EQUATOR_OF_DATE,
                "equatorial reference-plane override reports the native equatorial frame",
                &failures);
            SiderealCoordinates equatorial_precession_policy;
            expect_status(
                calc_sidereal_coordinates_tt(
                    &explicit_j2000, TAIYIN_BODY_SUN, jd_tt,
                    flags | TAIYIN_NATIVE_POSITION_EQUATORIAL
                        | TAIYIN_SIDEREAL_RAW_REFERENCE_OFFSET,
                    &equatorial_precession_policy, nullptr, NAN),
                TAIYIN_STATUS_OK,
                "equatorial output overrides the configured sidereal precession policy",
                &failures);
            SiderealCoordinates equatorial_reference_precession;
            expect_status(
                calc_sidereal_coordinates_tt(
                    &explicit_j2000, TAIYIN_BODY_SUN, jd_tt,
                    flags | TAIYIN_NATIVE_POSITION_EQUATORIAL
                        | TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION,
                    &equatorial_reference_precession, nullptr, NAN),
                TAIYIN_STATUS_OK,
                "equatorial output keeps the native precession model",
                &failures);
            for (int index = 0; index < 6; ++index) {
                expect_near(
                    equatorial_reference_plane.values[index],
                    equatorial_precession_policy.values[index],
                    1.0e-15,
                    "equatorial override is independent of sidereal configuration",
                    &failures);
                expect_near(
                    equatorial_reference_plane.values[index],
                    equatorial_reference_precession.values[index],
                    1.0e-15,
                    "equatorial override ignores reference-precession policy",
                    &failures);
            }
            SiderealPosition invalid_flags_position;
            expect_status(
                astrology::calc_sidereal_position_tt(
                    &apparent_native,
                    TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY,
                    TAIYIN_BODY_SUN,
                    split_jd(jd_tt),
                    TAIYIN_SIDEREAL_RAW_REFERENCE_OFFSET
                        | TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION,
                    &invalid_flags_position,
                    nullptr),
                TAIYIN_ERROR_INVALID_ARGUMENT,
                "reject invalid TT position flags before reading resolved state",
                &failures);
            expect_status(
                astrology::calc_sidereal_position_ut(
                    &apparent_native,
                    TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY,
                    TAIYIN_BODY_SUN,
                    split_jd(jd_tt),
                    TAIYIN_SIDEREAL_RAW_REFERENCE_OFFSET
                        | TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION,
                    &invalid_flags_position,
                    nullptr),
                TAIYIN_ERROR_INVALID_ARGUMENT,
                "reject invalid UT position flags before reading resolved state",
                &failures);
        }
    }
    return failures == 0 ? 0 : 1;
}
