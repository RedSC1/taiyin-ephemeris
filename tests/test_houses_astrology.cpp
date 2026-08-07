#include "taiyin/angle.h"
#include "taiyin/astrology/houses.h"
#include "taiyin/dispatch.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/time.h"

#include "houses_internal.h"

#include <cmath>
#include <iostream>

namespace {

using namespace taiyin::astrology;

taiyin::SplitJulianDate split_jd(double jd) {
    taiyin::SplitJulianDate out;
    taiyin::split_julian_date_from_double(jd, &out);
    return out;
}

taiyin::Status calc_houses_ut(
    const taiyin::runtime::NativeCalcContext* context,
    double jd_ut,
    int system_id,
    HouseResult* out
) {
    return taiyin::astrology::calc_houses_ut(
        context, split_jd(jd_ut), system_id, out);
}

taiyin::Status calc_houses_tt(
    const taiyin::runtime::NativeCalcContext* context,
    double jd_tt,
    int system_id,
    HouseResult* out
) {
    return taiyin::astrology::calc_houses_tt(
        context, split_jd(jd_tt), system_id, out);
}

// Swiss and Taiyin use independently evaluated sidereal-time/model paths.
// The fixed oracle set differs by at most about 0.0054 arcseconds.
const double kSwissHouseToleranceArcseconds = 0.01;
// Swiss's public Placidus implementation terminates its fixed-point iteration
// at 0.01 arcseconds. Taiyin continues to 0.001 arcseconds, so an otherwise
// identical root can differ slightly from Swiss's last iterate.
const double kSwissPlacidusGeometryToleranceArcseconds = 0.01;
const double kSwissAdditionalHouseToleranceArcseconds = 0.01;

const int kCustomHouseSystem = TAIYIN_HOUSE_SYSTEM_CUSTOM_START + 17;
const int kFallbackHouseSystem = TAIYIN_HOUSE_SYSTEM_CUSTOM_START + 18;
const int kChainedFallbackHouseSystem = TAIYIN_HOUSE_SYSTEM_CUSTOM_START + 19;
const int kPartialHouseSystem = TAIYIN_HOUSE_SYSTEM_CUSTOM_START + 20;
const int kDirtyFallbackHouseSystem = TAIYIN_HOUSE_SYSTEM_CUSTOM_START + 21;

bool eval_custom_house_system(
    const taiyin::astrology::HouseSystemDispatchData* data,
    double out[12]
) {
    if (!data || !out) return false;
    for (int i = 0; i < 12; ++i) {
        out[i] = taiyin::normalize_radians(
            data->ascendant_rad + (i * 30.0 + 1.0) * taiyin::TAIYIN_DEG_TO_RAD);
    }
    return true;
}

bool eval_failing_house_system(
    const taiyin::astrology::HouseSystemDispatchData*,
    double[12]
) {
    return false;
}

bool eval_partial_house_system(
    const taiyin::astrology::HouseSystemDispatchData*,
    double out[12]
) {
    if (!out) return false;
    out[0] = 1.0;
    return true;
}

bool eval_dirty_failing_house_system(
    const taiyin::astrology::HouseSystemDispatchData*,
    double out[12]
) {
    if (!out) return false;
    for (int i = 0; i < 12; ++i) {
        out[i] = (i + 1.0) * taiyin::TAIYIN_DEG_TO_RAD;
    }
    return false;
}

double angular_difference(double left, double right) {
    return std::fabs(taiyin::normalize_signed_radians(left - right));
}

void expect_status(taiyin::Status actual, taiyin::Status expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << " actual=" << actual << " expected=" << expected << "\n";
        ++*failures;
    }
}

void expect_angle_close(
    double actual,
    double expected_degrees,
    double tolerance_arcseconds,
    const char* label,
    int* failures
) {
    const double difference = angular_difference(actual, expected_degrees * taiyin::TAIYIN_DEG_TO_RAD);
    if (difference > tolerance_arcseconds * taiyin::TAIYIN_ARCSEC_TO_RAD) {
        std::cerr << "FAIL: " << label << " actual=" << actual * taiyin::TAIYIN_RAD_TO_DEG
                  << " expected=" << expected_degrees << " diff_arcsec="
                  << difference * taiyin::TAIYIN_RAD_TO_ARCSEC << "\n";
        ++*failures;
    }
}

void expect_angle_near(
    double actual,
    double expected,
    double tolerance_arcseconds,
    const char* label,
    int* failures
) {
    const double difference = angular_difference(actual, expected);
    if (difference > tolerance_arcseconds * taiyin::TAIYIN_ARCSEC_TO_RAD) {
        std::cerr << "FAIL: " << label << " diff_arcsec="
                  << difference * taiyin::TAIYIN_RAD_TO_ARCSEC << "\n";
        ++*failures;
    }
}

void expect_true(bool condition, const char* label, int* failures) {
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        ++*failures;
    }
}

void expect_rate_close(
    double actual_rad_per_day,
    double expected_deg_per_day,
    double tolerance_deg_per_day,
    const char* label,
    int* failures
) {
    const double actual = actual_rad_per_day * taiyin::TAIYIN_RAD_TO_DEG;
    if (!std::isfinite(actual)
        || std::fabs(actual - expected_deg_per_day) > tolerance_deg_per_day) {
        std::cerr << "FAIL: " << label << " actual_deg_per_day=" << actual
                  << " expected_deg_per_day=" << expected_deg_per_day << "\n";
        ++*failures;
    }
}

struct SwissHouseCase {
    double jd_ut;
    double latitude_deg;
    double longitude_deg;
    double asc_deg;
    double mc_deg;
    double vertex_deg;
    double east_point_deg;
    double porphyry_deg[12];
};

const SwissHouseCase kSwissCases[] = {
    {
        2460311.0, 39.9167, 116.3833,
        137.955986373727, 39.424973002554, 275.514116285182, 124.685728170895,
        {137.955986373727, 165.112315250003, 192.268644126279, 219.424973002554,
         252.268644126279, 285.112315250003, 317.955986373727, 345.112315250003,
         12.268644126279, 39.424973002554, 72.268644126279, 105.112315250003},
    },
    {
        2460482.5, 51.5074, -0.1276,
        358.934770200524, 269.592132878527, 179.639693640752, 359.515476779856,
        {358.934770200524, 29.153891093192, 59.373011985859, 89.592132878527,
         119.373011985859, 149.153891093192, 178.934770200524, 209.153891093192,
         239.373011985859, 269.592132878527, 299.373011985859, 329.153891093192},
    },
    {
        2460311.0, 70.0, 0.0,
        315.981004600627, 279.783519001062, 190.009849336885, 11.576498989699,
        {315.981004600627, 3.915176067439, 51.849347534250, 99.783519001062,
         111.849347534250, 123.915176067439, 135.981004600627, 183.915176067439,
         231.849347534250, 279.783519001062, 291.849347534250, 303.915176067439},
    },
};

struct SwissPlacidusCase {
    double jd_ut;
    double latitude_deg;
    double longitude_deg;
    double cusp_deg[12];
};

const SwissPlacidusCase kSwissPlacidusCases[] = {
    {
        2460311.0, 39.9167, 116.3833,
        {137.955986373727, 159.905715838579, 186.829377344240, 219.424973002554,
         255.095673573944, 288.734504353569, 317.955986373727, 339.905715838579,
         6.829377344240, 39.424973002554, 75.095673573944, 108.734504353569},
    },
    {
        2460482.5, 51.5074, -0.1276,
        {358.934770200524, 47.009824871508, 71.360142892116, 89.592132878527,
         107.745308151828, 131.764100163102, 178.934770200524, 227.009824871508,
         251.360142892116, 269.592132878527, 287.745308151828, 311.764100163102},
    },
    {
        2460311.0, 65.0, 0.0,
        {75.230638668662, 86.334197484680, 93.082100179923, 99.783519001062,
         108.884006602794, 128.429626802114, 255.230638668662, 266.334197484680,
         273.082100179923, 279.783519001062, 288.884006602794, 308.429626802114},
    },
};

struct SwissPlacidusGeometryCase {
    double armc_deg;
    double latitude_deg;
    double obliquity_deg;
    double cusp_deg[12];
};

const SwissPlacidusGeometryCase kSwissPlacidusGeometryCases[] = {
    {
        123.456, 39.9167, 23.436,
        {206.656040425304212, 234.691492433991698, 266.680535157863687, 301.227197185053740,
         334.384202778317672, 3.033100066811357, 26.656040425304241, 54.691492433991698,
         86.680535157863687, 121.227197185053740, 154.384202778317700, 183.033100066811357},
    },
    {
        321.0, 65.0, 23.436,
        {109.520730098139978, 115.864458747021544, 124.643846626840386, 138.568579949071420,
         165.839339629716505, 235.249647239573562, 289.520730098140007, 295.864458747021558,
         304.643846626840400, 318.568579949071420, 345.839339629716505, 55.249647239573569},
    },
};

struct SwissAdditionalHouseCase {
    int system_id;
    double armc_deg;
    double latitude_deg;
    double obliquity_deg;
    double cusp_2_deg;
    double cusp_3_deg;
    double cusp_11_deg;
    double cusp_12_deg;
};

const SwissAdditionalHouseCase kSwissAdditionalHouseCases[] = {
    {TAIYIN_HOUSE_SYSTEM_KOCH, 123.456, 39.9167, 23.436,
     234.936776485515, 264.540999813118, 149.525868341525, 178.068623489599},
    {TAIYIN_HOUSE_SYSTEM_REGIOMONTANUS, 123.456, 39.9167, 23.436,
     232.029664046312, 263.651016632266, 155.643965346680, 182.866290863818},
    {TAIYIN_HOUSE_SYSTEM_CAMPANUS, 123.456, 39.9167, 23.436,
     238.684892427877, 271.036150663853, 149.252902070722, 177.029840838188},
    {TAIYIN_HOUSE_SYSTEM_ALCABITIUS, 123.456, 39.9167, 23.436,
     239.826121268091, 270.502069098862, 148.389454658680, 177.426355568296},
    {TAIYIN_HOUSE_SYSTEM_POLICH_PAGE, 123.456, 39.9167, 23.436,
     234.766436902177, 266.811479667173, 154.375721952386, 183.033117020216},
    {TAIYIN_HOUSE_SYSTEM_MORINUS, 123.456, 39.9167, 23.436,
     241.433663774475, 273.765879906050, 155.376538419478, 183.171505047157},

    {TAIYIN_HOUSE_SYSTEM_KOCH, 321.0, 65.0, 23.436,
     119.344913352104, 128.938230419202, 82.484343265263, 98.666577162776},
    {TAIYIN_HOUSE_SYSTEM_REGIOMONTANUS, 321.0, 65.0, 23.436,
     121.070718502448, 128.972829527681, 341.940060281711, 71.787536792854},
    {TAIYIN_HOUSE_SYSTEM_CAMPANUS, 321.0, 65.0, 23.436,
     127.358616102587, 133.614395141578, 325.751093387276, 353.257807002318},
    {TAIYIN_HOUSE_SYSTEM_ALCABITIUS, 321.0, 65.0, 23.436,
     118.948482059822, 128.616543530730, 12.007366882351, 63.123405808513},
    {TAIYIN_HOUSE_SYSTEM_POLICH_PAGE, 321.0, 65.0, 23.436,
     113.286298988012, 123.294179633497, 345.880618843970, 51.218050193380},
    {TAIYIN_HOUSE_SYSTEM_MORINUS, 321.0, 65.0, 23.436,
     80.205832041497, 112.703370405884, 351.731740566569, 19.402117765440},

    {TAIYIN_HOUSE_SYSTEM_KOCH, 15.0, -33.9, 23.44,
     116.728649839041, 153.121739616984, 40.045043024040, 63.608319511881},
    {TAIYIN_HOUSE_SYSTEM_REGIOMONTANUS, 15.0, -33.9, 23.44,
     120.544830423239, 161.021057649029, 42.106005676677, 64.103687531989},
    {TAIYIN_HOUSE_SYSTEM_CAMPANUS, 15.0, -33.9, 23.44,
     126.738193951344, 166.827750191371, 38.709659676534, 60.578289553659},
    {TAIYIN_HOUSE_SYSTEM_ALCABITIUS, 15.0, -33.9, 23.44,
     121.475414964379, 157.676223464243, 41.794358470855, 65.621785295747},
    {TAIYIN_HOUSE_SYSTEM_POLICH_PAGE, 15.0, -33.9, 23.44,
     123.642242736566, 162.011636746164, 43.780943172936, 66.716562561657},
    {TAIYIN_HOUSE_SYSTEM_MORINUS, 15.0, -33.9, 23.44,
     137.464329561902, 166.188453109242, 42.535670438098, 73.719556296644},

    {TAIYIN_HOUSE_SYSTEM_REGIOMONTANUS, 123.456, 70.0, 23.436,
     213.406994237568, 243.804884452810, 161.898952038066, 181.854120629394},
    {TAIYIN_HOUSE_SYSTEM_CAMPANUS, 123.456, 70.0, 23.436,
     242.863778890725, 278.376644416272, 140.266013102768, 162.462450221479},
    {TAIYIN_HOUSE_SYSTEM_ALCABITIUS, 123.456, 70.0, 23.436,
     233.685309199358, 267.595645572712, 145.101495164851, 170.535162958975},
    {TAIYIN_HOUSE_SYSTEM_POLICH_PAGE, 123.456, 70.0, 23.436,
     218.158672922025, 252.802696319263, 159.338245344840, 182.099554704336},
    {TAIYIN_HOUSE_SYSTEM_MORINUS, 123.456, 70.0, 23.436,
     241.433663774475, 273.765879906050, 155.376538419478, 183.171505047157},
};

}  // namespace

int main() {
    using namespace taiyin;
    using namespace taiyin::astrology;
    using namespace taiyin::runtime;

    int failures = 0;
    HouseResult default_result;
    for (int i = 0; i < 12; ++i) {
        expect_true(
            !std::isfinite(default_result.cusp_longitude_rad[i]),
            "default house cusp is NaN",
            &failures);
    }
    NativeCalcContext context;
    for (size_t case_index = 0;
         case_index < sizeof(kSwissPlacidusGeometryCases) / sizeof(kSwissPlacidusGeometryCases[0]);
         ++case_index) {
        const SwissPlacidusGeometryCase& oracle = kSwissPlacidusGeometryCases[case_index];
        double cusps[12] = {};
        expect_true(
            taiyin::astrology::internal::calc_placidus_cusps_from_armc(
                oracle.armc_deg * TAIYIN_DEG_TO_RAD,
                oracle.latitude_deg * TAIYIN_DEG_TO_RAD,
                oracle.obliquity_deg * TAIYIN_DEG_TO_RAD,
                cusps),
            "Placidus geometry converges",
            &failures);
        for (int i = 0; i < 12; ++i) {
            expect_angle_close(
                cusps[i], oracle.cusp_deg[i], kSwissPlacidusGeometryToleranceArcseconds,
                "Swiss Placidus ARMC geometry oracle", &failures);
        }
    }
    double polar_placidus_geometry[12] = {};
    expect_true(
        !taiyin::astrology::internal::calc_placidus_cusps_from_armc(
            123.456 * TAIYIN_DEG_TO_RAD,
            70.0 * TAIYIN_DEG_TO_RAD,
            23.436 * TAIYIN_DEG_TO_RAD,
            polar_placidus_geometry),
        "Placidus geometry rejects polar input",
        &failures);

    for (size_t case_index = 0;
         case_index < sizeof(kSwissAdditionalHouseCases) / sizeof(kSwissAdditionalHouseCases[0]);
         ++case_index) {
        const SwissAdditionalHouseCase& oracle = kSwissAdditionalHouseCases[case_index];
        HouseResult result;
        expect_status(
            calc_houses_from_armc(
                oracle.armc_deg * TAIYIN_DEG_TO_RAD,
                oracle.latitude_deg * TAIYIN_DEG_TO_RAD,
                oracle.obliquity_deg * TAIYIN_DEG_TO_RAD,
                oracle.system_id,
                &result),
            TAIYIN_STATUS_OK,
            "calculate additional house system from ARMC",
            &failures);
        expect_true(
            result.requested_system_id == oracle.system_id
                && result.resolved_system_id == oracle.system_id
                && result.flags == 0,
            "additional house system does not fallback",
            &failures);
        expect_angle_close(
            result.cusp_longitude_rad[1], oracle.cusp_2_deg,
            kSwissAdditionalHouseToleranceArcseconds,
            "Swiss additional house cusp 2 oracle", &failures);
        expect_angle_close(
            result.cusp_longitude_rad[2], oracle.cusp_3_deg,
            kSwissAdditionalHouseToleranceArcseconds,
            "Swiss additional house cusp 3 oracle", &failures);
        expect_angle_close(
            result.cusp_longitude_rad[10], oracle.cusp_11_deg,
            kSwissAdditionalHouseToleranceArcseconds,
            "Swiss additional house cusp 11 oracle", &failures);
        expect_angle_close(
            result.cusp_longitude_rad[11], oracle.cusp_12_deg,
            kSwissAdditionalHouseToleranceArcseconds,
            "Swiss additional house cusp 12 oracle", &failures);
    }

    for (int system_id = TAIYIN_HOUSE_SYSTEM_WHOLE_SIGN;
         system_id <= TAIYIN_HOUSE_SYSTEM_MORINUS;
         ++system_id) {
        HouseSystemModelEntry entry;
        expect_true(has_house_system_model(system_id), "built-in house model is registered", &failures);
        expect_true(
            find_house_system_model(system_id, &entry)
                && entry.model_id == system_id
                && entry.eval != nullptr,
            "find built-in house model",
            &failures);
    }
    expect_true(
        add_house_system_model(HouseSystemModelEntry(
            kCustomHouseSystem, &eval_custom_house_system)),
        "register custom house model",
        &failures);
    expect_true(
        !add_house_system_model(HouseSystemModelEntry(
            kCustomHouseSystem, &eval_custom_house_system)),
        "reject duplicate custom house model",
        &failures);
    expect_true(
        !add_house_system_model(HouseSystemModelEntry(
            TAIYIN_HOUSE_SYSTEM_EQUAL, &eval_custom_house_system)),
        "reject replacement of built-in house model",
        &failures);
    expect_true(
        !add_house_system_model(HouseSystemModelEntry(
            TAIYIN_HOUSE_SYSTEM_CUSTOM_START + 99,
            &eval_failing_house_system,
            TAIYIN_HOUSE_SYSTEM_CUSTOM_START + 98)),
        "reject unknown custom fallback model",
        &failures);
    expect_true(
        add_house_system_model(HouseSystemModelEntry(
            kFallbackHouseSystem,
            &eval_failing_house_system,
            TAIYIN_HOUSE_SYSTEM_EQUAL)),
        "register custom house model with fallback",
        &failures);
    expect_true(
        add_house_system_model(HouseSystemModelEntry(
            kChainedFallbackHouseSystem,
            &eval_failing_house_system,
            TAIYIN_HOUSE_SYSTEM_PLACIDUS)),
        "register custom house model with chained fallback",
        &failures);
    expect_true(
        add_house_system_model(HouseSystemModelEntry(
            kPartialHouseSystem, &eval_partial_house_system)),
        "register incomplete custom house model",
        &failures);
    expect_true(
        add_house_system_model(HouseSystemModelEntry(
            kDirtyFallbackHouseSystem,
            &eval_dirty_failing_house_system,
            kPartialHouseSystem)),
        "register dirty custom house fallback chain",
        &failures);

    HouseResult custom;
    expect_status(
        calc_houses_from_armc(
            123.456 * TAIYIN_DEG_TO_RAD,
            39.9167 * TAIYIN_DEG_TO_RAD,
            23.436 * TAIYIN_DEG_TO_RAD,
            kCustomHouseSystem,
            &custom),
        TAIYIN_STATUS_OK,
        "evaluate custom house model",
        &failures);
    for (int i = 0; i < 12; ++i) {
        expect_angle_near(
            custom.cusp_longitude_rad[i],
            normalize_radians(custom.ascendant_rad + (i * 30.0 + 1.0) * TAIYIN_DEG_TO_RAD),
            1.0e-8,
            "custom house model cusp",
            &failures);
    }
    expect_true(
        std::isnan(custom.ascendant_rate_rad_per_day)
            && std::isnan(custom.cusp_longitude_rate_rad_per_day[0]),
        "pure ARMC houses do not invent time derivatives",
        &failures);

    HouseResult custom_fallback;
    HouseResult equal_from_armc;
    expect_status(
        calc_houses_from_armc(
            123.456 * TAIYIN_DEG_TO_RAD,
            39.9167 * TAIYIN_DEG_TO_RAD,
            23.436 * TAIYIN_DEG_TO_RAD,
            kFallbackHouseSystem,
            &custom_fallback),
        TAIYIN_STATUS_OK,
        "evaluate custom house fallback",
        &failures);
    expect_status(
        calc_houses_from_armc(
            123.456 * TAIYIN_DEG_TO_RAD,
            39.9167 * TAIYIN_DEG_TO_RAD,
            23.436 * TAIYIN_DEG_TO_RAD,
            TAIYIN_HOUSE_SYSTEM_EQUAL,
            &equal_from_armc),
        TAIYIN_STATUS_OK,
        "evaluate Equal comparison from ARMC",
        &failures);
    expect_true(
        custom_fallback.requested_system_id == kFallbackHouseSystem
            && custom_fallback.resolved_system_id == TAIYIN_HOUSE_SYSTEM_EQUAL
            && (custom_fallback.flags & TAIYIN_HOUSE_RESULT_USED_FALLBACK) != 0u
            && (custom_fallback.flags & TAIYIN_HOUSE_RESULT_FALLBACK_PORPHYRY) == 0u,
        "custom house fallback identity",
        &failures);
    for (int i = 0; i < 12; ++i) {
        expect_angle_near(
            custom_fallback.cusp_longitude_rad[i],
            equal_from_armc.cusp_longitude_rad[i],
            1.0e-8,
            "custom house fallback cusps",
            &failures);
    }
    HouseResult dirty_fallback;
    expect_status(
        calc_houses_from_armc(
            123.456 * TAIYIN_DEG_TO_RAD,
            39.9167 * TAIYIN_DEG_TO_RAD,
            23.436 * TAIYIN_DEG_TO_RAD,
            kDirtyFallbackHouseSystem,
            &dirty_fallback),
        TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED,
        "reject mixed custom fallback output",
        &failures);
    expect_true(
        dirty_fallback.requested_system_id == -1
            && std::isnan(dirty_fallback.cusp_longitude_rad[0]),
        "clear rejected mixed custom fallback result",
        &failures);

    for (size_t case_index = 0; case_index < sizeof(kSwissCases) / sizeof(kSwissCases[0]); ++case_index) {
        const SwissHouseCase& oracle = kSwissCases[case_index];
        expect_status(
            native_context_set_observer_location(
                &context,
                native_observer_location_degrees(oracle.longitude_deg, oracle.latitude_deg, 0.0)),
            TAIYIN_STATUS_OK,
            "set house observer",
            &failures);
        HouseResult porphyry;
        expect_status(
            calc_houses_ut(&context, oracle.jd_ut, TAIYIN_HOUSE_SYSTEM_PORPHYRY, &porphyry),
            TAIYIN_STATUS_OK,
            "calculate Porphyry houses",
            &failures);
        expect_angle_close(porphyry.ascendant_rad, oracle.asc_deg, kSwissHouseToleranceArcseconds,
                           "Swiss ASC oracle", &failures);
        expect_angle_close(porphyry.midheaven_rad, oracle.mc_deg, kSwissHouseToleranceArcseconds,
                           "Swiss MC oracle", &failures);
        expect_angle_close(porphyry.vertex_rad, oracle.vertex_deg, kSwissHouseToleranceArcseconds,
                           "Swiss vertex oracle", &failures);
        expect_angle_close(porphyry.east_point_rad, oracle.east_point_deg, kSwissHouseToleranceArcseconds,
                           "Swiss East Point oracle", &failures);
        for (int i = 0; i < 12; ++i) {
            expect_angle_close(porphyry.cusp_longitude_rad[i], oracle.porphyry_deg[i],
                               kSwissHouseToleranceArcseconds,
                               "Swiss Porphyry cusp oracle", &failures);
        }
        expect_true(porphyry.flags == 0, "base house systems do not fallback", &failures);
        expect_true(porphyry.requested_system_id == TAIYIN_HOUSE_SYSTEM_PORPHYRY
                        && porphyry.resolved_system_id == TAIYIN_HOUSE_SYSTEM_PORPHYRY,
                    "Porphyry system identity", &failures);
        expect_true(
            (porphyry.flags & TAIYIN_HOUSE_RESULT_SPEED_UNAVAILABLE) == 0u
                && std::isfinite(porphyry.ascendant_rate_rad_per_day)
                && std::isfinite(porphyry.cusp_longitude_rate_rad_per_day[0]),
            "time-based houses include finite speeds",
            &failures);

        HousePositionResult cusp_position;
        expect_status(
            calc_house_position_from_longitude(
                &porphyry, porphyry.cusp_longitude_rad[4], &cusp_position),
            TAIYIN_STATUS_OK,
            "locate exact house cusp",
            &failures);
        expect_true(
            cusp_position.house_number == 5
                && cusp_position.fraction == 0.0
                && cusp_position.continuous_house_position == 5.0,
            "exact cusp belongs to the following house",
            &failures);
        const double first_span = normalize_radians(
            porphyry.cusp_longitude_rad[1] - porphyry.cusp_longitude_rad[0]);
        HousePositionResult midpoint_position;
        expect_status(
            calc_house_position_from_longitude(
                &porphyry,
                porphyry.cusp_longitude_rad[0] + 0.5 * first_span,
                &midpoint_position),
            TAIYIN_STATUS_OK,
            "locate house midpoint",
            &failures);
        expect_true(
            midpoint_position.house_number == 1
                && std::fabs(midpoint_position.fraction - 0.5) < 1.0e-12
                && std::fabs(midpoint_position.continuous_house_position - 1.5) < 1.0e-12,
            "house midpoint has continuous position 1.5",
            &failures);
        HouseResult invalid_partition = porphyry;
        invalid_partition.cusp_longitude_rad[2] =
            invalid_partition.cusp_longitude_rad[1];
        expect_status(
            calc_house_position_from_longitude(
                &invalid_partition,
                invalid_partition.cusp_longitude_rad[0],
                &midpoint_position),
            TAIYIN_ERROR_INVALID_ARGUMENT,
            "reject a degenerate cusp partition",
            &failures);

        HouseResult equal;
        expect_status(
            calc_houses_ut(&context, oracle.jd_ut, TAIYIN_HOUSE_SYSTEM_EQUAL, &equal),
            TAIYIN_STATUS_OK,
            "calculate Equal houses",
            &failures);
        for (int i = 0; i < 12; ++i) {
            expect_angle_close(
                equal.cusp_longitude_rad[i], oracle.asc_deg + i * 30.0,
                kSwissHouseToleranceArcseconds,
                "Equal cusp identity",
                &failures);
        }

        HouseResult whole_sign;
        expect_status(
            calc_houses_ut(&context, oracle.jd_ut, TAIYIN_HOUSE_SYSTEM_WHOLE_SIGN, &whole_sign),
            TAIYIN_STATUS_OK,
            "calculate Whole Sign houses",
            &failures);
        const double first_sign = std::floor(oracle.asc_deg / 30.0) * 30.0;
        for (int i = 0; i < 12; ++i) {
            expect_angle_close(
                whole_sign.cusp_longitude_rad[i], first_sign + i * 30.0,
                kSwissHouseToleranceArcseconds,
                "Whole Sign cusp identity",
                &failures);
        }

        HouseResult tt;
        const SplitJulianDate house_jd_ut = split_jd(oracle.jd_ut);
        const double delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
            context.delta_t_model_id, context.ephemeris_family_id,
            house_jd_ut, 0, 0);
        SplitJulianDate house_jd_tt;
        if (!ut1_to_tt_split_jd(house_jd_ut, delta_t, &house_jd_tt)) {
            std::cerr << "FAIL: convert house UT to split TT\n";
            ++failures;
            continue;
        }
        expect_status(
            taiyin::astrology::calc_houses_tt(
                &context, house_jd_tt, TAIYIN_HOUSE_SYSTEM_PORPHYRY, &tt),
            TAIYIN_STATUS_OK,
            "calculate TT houses",
            &failures);
        expect_angle_near(tt.ascendant_rad, porphyry.ascendant_rad, 1.0e-5,
                          "UT and TT ASC agree", &failures);
    }

    expect_status(
        native_context_set_observer_location(
            &context, native_observer_location_degrees(116.3833, 39.9167, 0.0)),
        TAIYIN_STATUS_OK,
        "set Swiss house-speed observer",
        &failures);
    HouseResult speed_oracle;
    expect_status(
        calc_houses_ut(
            &context, 2460311.0, TAIYIN_HOUSE_SYSTEM_PLACIDUS, &speed_oracle),
        TAIYIN_STATUS_OK,
        "calculate Placidus house speeds",
        &failures);
    // Centered one-second derivative of pyswisseph 2.10.03 swe_houses_ex().
    // Swiss's separately reported swe_houses_ex2() cusp speeds use additional
    // interpolation and are not exactly the derivative of its own cusps.
    const double swiss_cusp_speeds[12] = {
        283.9638389573338, 313.70404052036065, 350.4212823423586,
        368.34189699115996, 347.81521773966233, 309.98268942876166,
        283.9638389573338, 313.70404052158847, 350.4212823423586,
        368.34189699115996, 347.81521773966233, 309.982689426306,
    };
    for (int i = 0; i < 12; ++i) {
        expect_rate_close(
            speed_oracle.cusp_longitude_rate_rad_per_day[i],
            swiss_cusp_speeds[i],
            0.02,
            "Swiss Placidus cusp-speed oracle",
            &failures);
    }
    expect_rate_close(
        speed_oracle.ascendant_rate_rad_per_day,
        283.9638389573338, 0.02, "Swiss ASC-speed oracle", &failures);
    expect_rate_close(
        speed_oracle.midheaven_rate_rad_per_day,
        368.34189699115996, 0.02, "Swiss MC-speed oracle", &failures);
    expect_rate_close(
        speed_oracle.armc_rate_rad_per_day,
        360.98368459452104, 0.02, "Swiss ARMC-speed oracle", &failures);
    expect_rate_close(
        speed_oracle.vertex_rate_rad_per_day,
        354.18380292639995, 0.02, "Swiss Vertex-speed oracle", &failures);
    expect_rate_close(
        speed_oracle.east_point_rate_rad_per_day,
        351.35752404084997, 0.02, "Swiss East-Point-speed oracle", &failures);

    const double whole_sign_search_start_jd = 2460311.0;
    double whole_sign_lower_jd = whole_sign_search_start_jd;
    HouseResult whole_sign_lower;
    expect_status(
        calc_houses_ut(
            &context,
            whole_sign_lower_jd,
            TAIYIN_HOUSE_SYSTEM_WHOLE_SIGN,
            &whole_sign_lower),
        TAIYIN_STATUS_OK,
        "start Whole Sign ingress search",
        &failures);
    double whole_sign_upper_jd = NAN;
    for (int sample_index = 1; sample_index <= 288; ++sample_index) {
        const double sample_jd =
            whole_sign_search_start_jd + sample_index * (5.0 / 1440.0);
        HouseResult sample;
        if (calc_houses_ut(
                &context,
                sample_jd,
                TAIYIN_HOUSE_SYSTEM_WHOLE_SIGN,
                &sample) != TAIYIN_STATUS_OK) {
            continue;
        }
        if (angular_difference(
                sample.cusp_longitude_rad[0],
                whole_sign_lower.cusp_longitude_rad[0]) > 1.0 * TAIYIN_DEG_TO_RAD) {
            whole_sign_upper_jd = sample_jd;
            break;
        }
        whole_sign_lower_jd = sample_jd;
        whole_sign_lower = sample;
    }
    expect_true(
        std::isfinite(whole_sign_upper_jd),
        "find a Whole Sign cusp ingress",
        &failures);
    if (std::isfinite(whole_sign_upper_jd)) {
        for (int iteration = 0; iteration < 48; ++iteration) {
            const double mid_jd = 0.5 * (whole_sign_lower_jd + whole_sign_upper_jd);
            HouseResult mid;
            expect_status(
                calc_houses_ut(
                    &context,
                    mid_jd,
                    TAIYIN_HOUSE_SYSTEM_WHOLE_SIGN,
                    &mid),
                TAIYIN_STATUS_OK,
                "refine Whole Sign cusp ingress",
                &failures);
            if (angular_difference(
                    mid.cusp_longitude_rad[0],
                    whole_sign_lower.cusp_longitude_rad[0]) < 1.0e-12) {
                whole_sign_lower_jd = mid_jd;
                whole_sign_lower = mid;
            } else {
                whole_sign_upper_jd = mid_jd;
            }
        }
        HouseResult whole_sign_ingress;
        expect_status(
            calc_houses_ut(
                &context,
                0.5 * (whole_sign_lower_jd + whole_sign_upper_jd),
                TAIYIN_HOUSE_SYSTEM_WHOLE_SIGN,
                &whole_sign_ingress),
            TAIYIN_STATUS_OK,
            "calculate Whole Sign houses at cusp ingress",
            &failures);
        expect_true(
            (whole_sign_ingress.flags & TAIYIN_HOUSE_RESULT_SPEED_UNAVAILABLE) != 0u
                && std::isnan(whole_sign_ingress.ascendant_rate_rad_per_day)
                && std::isnan(whole_sign_ingress.cusp_longitude_rate_rad_per_day[0]),
            "Whole Sign cusp ingress does not publish a discontinuous speed",
            &failures);
    }

    for (size_t case_index = 0;
         case_index < sizeof(kSwissPlacidusCases) / sizeof(kSwissPlacidusCases[0]);
         ++case_index) {
        const SwissPlacidusCase& oracle = kSwissPlacidusCases[case_index];
        expect_status(
            native_context_set_observer_location(
                &context,
                native_observer_location_degrees(oracle.longitude_deg, oracle.latitude_deg, 0.0)),
            TAIYIN_STATUS_OK,
            "set Placidus observer",
            &failures);
        HouseResult placidus;
        expect_status(
            calc_houses_ut(&context, oracle.jd_ut, TAIYIN_HOUSE_SYSTEM_PLACIDUS, &placidus),
            TAIYIN_STATUS_OK,
            "calculate Placidus houses",
            &failures);
        expect_true(
            placidus.requested_system_id == TAIYIN_HOUSE_SYSTEM_PLACIDUS
                && placidus.resolved_system_id == TAIYIN_HOUSE_SYSTEM_PLACIDUS
                && placidus.flags == 0,
            "Placidus system identity",
            &failures);
        for (int i = 0; i < 12; ++i) {
            expect_angle_close(
                placidus.cusp_longitude_rad[i], oracle.cusp_deg[i], kSwissHouseToleranceArcseconds,
                "Swiss Placidus cusp oracle", &failures);
        }
    }

    expect_status(
        native_context_set_observer_location(
            &context, native_observer_location_degrees(0.0, 70.0, 0.0)),
        TAIYIN_STATUS_OK,
        "set polar Placidus observer",
        &failures);
    HouseResult polar_placidus;
    HouseResult polar_porphyry;
    expect_status(
        calc_houses_ut(&context, 2460311.0, TAIYIN_HOUSE_SYSTEM_PLACIDUS, &polar_placidus),
        TAIYIN_STATUS_OK,
        "fallback Placidus houses",
        &failures);
    expect_status(
        calc_houses_ut(&context, 2460311.0, TAIYIN_HOUSE_SYSTEM_PORPHYRY, &polar_porphyry),
        TAIYIN_STATUS_OK,
        "calculate fallback Porphyry houses",
        &failures);
    expect_true(
        polar_placidus.requested_system_id == TAIYIN_HOUSE_SYSTEM_PLACIDUS
            && polar_placidus.resolved_system_id == TAIYIN_HOUSE_SYSTEM_PORPHYRY
            && (polar_placidus.flags & TAIYIN_HOUSE_RESULT_USED_FALLBACK) != 0u
            && (polar_placidus.flags & TAIYIN_HOUSE_RESULT_FALLBACK_PORPHYRY) != 0u,
        "Placidus reports polar Porphyry fallback",
        &failures);
    for (int i = 0; i < 12; ++i) {
        expect_angle_near(
            polar_placidus.cusp_longitude_rad[i], polar_porphyry.cusp_longitude_rad[i], 0.001,
            "Placidus polar fallback matches Porphyry", &failures);
    }

    HouseResult polar_koch;
    expect_status(
        calc_houses_ut(&context, 2460311.0, TAIYIN_HOUSE_SYSTEM_KOCH, &polar_koch),
        TAIYIN_STATUS_OK,
        "fallback Koch houses",
        &failures);
    expect_true(
        polar_koch.requested_system_id == TAIYIN_HOUSE_SYSTEM_KOCH
            && polar_koch.resolved_system_id == TAIYIN_HOUSE_SYSTEM_PORPHYRY
            && (polar_koch.flags & TAIYIN_HOUSE_RESULT_USED_FALLBACK) != 0u
            && (polar_koch.flags & TAIYIN_HOUSE_RESULT_FALLBACK_PORPHYRY) != 0u,
        "Koch reports polar Porphyry fallback",
        &failures);
    for (int i = 0; i < 12; ++i) {
        expect_angle_near(
            polar_koch.cusp_longitude_rad[i], polar_porphyry.cusp_longitude_rad[i], 0.001,
            "Koch polar fallback matches Porphyry", &failures);
    }
    HouseResult chained_fallback;
    expect_status(
        calc_houses_from_armc(
            polar_porphyry.armc_rad,
            70.0 * TAIYIN_DEG_TO_RAD,
            23.436 * TAIYIN_DEG_TO_RAD,
            kChainedFallbackHouseSystem,
            &chained_fallback),
        TAIYIN_STATUS_OK,
        "evaluate chained polar house fallback",
        &failures);
    expect_true(
        chained_fallback.requested_system_id == kChainedFallbackHouseSystem
            && chained_fallback.resolved_system_id == TAIYIN_HOUSE_SYSTEM_PORPHYRY
            && (chained_fallback.flags & TAIYIN_HOUSE_RESULT_USED_FALLBACK) != 0u
            && (chained_fallback.flags & TAIYIN_HOUSE_RESULT_FALLBACK_PORPHYRY) != 0u,
        "custom fallback continues through Placidus to Porphyry",
        &failures);

    HouseResult invalid;
    NativeCalcContext no_observer;
    expect_status(
        calc_houses_ut(&no_observer, 2460311.0, TAIYIN_HOUSE_SYSTEM_EQUAL, &invalid),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject missing observer location",
        &failures);
    expect_status(
        calc_houses_ut(&context, 2460311.0, TAIYIN_HOUSE_SYSTEM_EQUAL, nullptr),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject missing UT house output",
        &failures);
    expect_status(
        calc_houses_tt(&context, 2460311.0, TAIYIN_HOUSE_SYSTEM_EQUAL, nullptr),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject missing TT house output",
        &failures);
    expect_status(
        calc_houses_ut(&context, 2460311.0, 99, &invalid),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject unknown house system",
        &failures);
    expect_status(
        calc_houses_from_armc(
            0.0, 91.0 * TAIYIN_DEG_TO_RAD, 23.436 * TAIYIN_DEG_TO_RAD,
            TAIYIN_HOUSE_SYSTEM_EQUAL, &invalid),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject invalid ARMC observer latitude",
        &failures);
    expect_status(
        calc_houses_from_armc(
            0.0, 90.0 * TAIYIN_DEG_TO_RAD, 23.436 * TAIYIN_DEG_TO_RAD,
            TAIYIN_HOUSE_SYSTEM_EQUAL, &invalid),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject north-pole ARMC observer latitude",
        &failures);
    expect_status(
        calc_houses_from_armc(
            0.0, -90.0 * TAIYIN_DEG_TO_RAD, 23.436 * TAIYIN_DEG_TO_RAD,
            TAIYIN_HOUSE_SYSTEM_EQUAL, &invalid),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject south-pole ARMC observer latitude",
        &failures);
    HouseSystemDispatchData polar_data;
    polar_data.armc_rad = 0.0;
    polar_data.observer_latitude_rad = 0.5 * TAIYIN_PI;
    polar_data.true_obliquity_rad = 23.436 * TAIYIN_DEG_TO_RAD;
    polar_data.ascendant_rad = 0.5 * TAIYIN_PI;
    polar_data.midheaven_rad = 0.0;
    double polar_cusps[12] = {};
    expect_true(
        !eval_house_system_model(
            TAIYIN_HOUSE_SYSTEM_REGIOMONTANUS, &polar_data, polar_cusps)
            && !eval_house_system_model(
                TAIYIN_HOUSE_SYSTEM_ALCABITIUS, &polar_data, polar_cusps)
            && !eval_house_system_model(
                TAIYIN_HOUSE_SYSTEM_POLICH_PAGE, &polar_data, polar_cusps),
        "reject undefined polar house geometries",
        &failures);
    polar_data.observer_latitude_rad = 80.0 * TAIYIN_DEG_TO_RAD;
    expect_true(
        !eval_house_system_model(
            TAIYIN_HOUSE_SYSTEM_ALCABITIUS, &polar_data, polar_cusps),
        "reject undefined Alcabitius circumpolar arc",
        &failures);
    expect_status(
        calc_houses_from_armc(
            0.0, 0.0, 0.0, TAIYIN_HOUSE_SYSTEM_EQUAL, &invalid),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject invalid ARMC obliquity",
        &failures);
    expect_status(
        calc_houses_from_armc(
            0.0, 0.0, 23.436 * TAIYIN_DEG_TO_RAD, 99, &invalid),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject unknown ARMC house system",
        &failures);
    expect_status(
        calc_houses_from_armc(
            0.0, 0.0, 23.436 * TAIYIN_DEG_TO_RAD,
            TAIYIN_HOUSE_SYSTEM_EQUAL, nullptr),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject missing ARMC house output",
        &failures);
    context.model_context.obliquity_model_id = 1;
    expect_status(
        calc_houses_ut(&context, 2460311.0, TAIYIN_HOUSE_SYSTEM_EQUAL, &invalid),
        TAIYIN_ERROR_UNSUPPORTED,
        "reject unsupported obliquity model",
        &failures);

    if (failures != 0) {
        std::cerr << failures << " house astrology test(s) failed\n";
        return 1;
    }
    std::cout << "house astrology tests passed\n";
    return 0;
}
