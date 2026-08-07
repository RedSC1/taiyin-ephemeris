#include "taiyin/astrology/houses.h"

#include "houses_internal.h"

#include "taiyin/angle.h"
#include "taiyin/coordinates.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace taiyin {
namespace astrology {
namespace {

constexpr double kSmall = 1.0e-14;
const double kPlacidusToleranceRad = 0.001 * TAIYIN_ARCSEC_TO_RAD;
constexpr int kMaxPlacidusIterations = 100;

double normalize_degrees(double value) noexcept {
    return normalize_radians(value) * TAIYIN_RAD_TO_DEG;
}

double ascendant_rad(double armc_rad, double obliquity_rad, double latitude_rad) noexcept {
    const double x = -(std::sin(obliquity_rad) * std::tan(latitude_rad)
        + std::cos(obliquity_rad) * std::sin(armc_rad));
    return normalize_radians(std::atan2(std::cos(armc_rad), x));
}

double midheaven_rad(double armc_rad, double obliquity_rad) noexcept {
    return normalize_radians(std::atan2(
        std::sin(armc_rad), std::cos(armc_rad) * std::cos(obliquity_rad)));
}

double great_circle_ecliptic_intersection_rad(
    double equatorial_angle_rad,
    double pole_latitude_rad,
    double obliquity_rad
) noexcept {
    return normalize_radians(std::atan2(
        std::sin(equatorial_angle_rad),
        std::cos(obliquity_rad) * std::cos(equatorial_angle_rad)
            - std::sin(obliquity_rad) * std::tan(pole_latitude_rad)));
}

double vertex_rad(double armc_rad, double obliquity_rad, double latitude_rad) noexcept {
    const double pole_latitude = latitude_rad >= 0.0
        ? 0.5 * TAIYIN_PI - latitude_rad
        : -0.5 * TAIYIN_PI - latitude_rad;
    double vertex = great_circle_ecliptic_intersection_rad(
        armc_rad - 0.5 * TAIYIN_PI, pole_latitude, obliquity_rad);
    if (std::fabs(latitude_rad) <= obliquity_rad
        && normalize_signed_radians(vertex - midheaven_rad(armc_rad, obliquity_rad)) > 0.0) {
        vertex = normalize_radians(vertex + TAIYIN_PI);
    }
    return vertex;
}

bool corrected_ascendant_midheaven(
    double armc_rad,
    double obliquity_rad,
    double latitude_rad,
    double* out_ascendant_rad,
    double* out_midheaven_rad
) noexcept {
    if (!out_ascendant_rad || !out_midheaven_rad) return false;
    double ascendant = ascendant_rad(armc_rad, obliquity_rad, latitude_rad);
    const double midheaven = midheaven_rad(armc_rad, obliquity_rad);
    if (!std::isfinite(ascendant) || !std::isfinite(midheaven)) return false;
    if (normalize_signed_radians(ascendant - midheaven) < 0.0) {
        ascendant = normalize_radians(ascendant + TAIYIN_PI);
    }
    *out_ascendant_rad = ascendant;
    *out_midheaven_rad = midheaven;
    return true;
}

void fill_equal_cusps(double first_cusp_rad, double out[12]) noexcept {
    for (int i = 0; i < 12; ++i) {
        out[i] = normalize_radians(first_cusp_rad + i * TAIYIN_PI / 6.0);
    }
}

void fill_quadrant_cusps(
    double ascendant,
    double midheaven,
    double cusp_2,
    double cusp_3,
    double cusp_11,
    double cusp_12,
    double out[12]
) noexcept {
    out[0] = ascendant;
    out[1] = cusp_2;
    out[2] = cusp_3;
    out[3] = normalize_radians(midheaven + TAIYIN_PI);
    out[4] = normalize_radians(cusp_11 + TAIYIN_PI);
    out[5] = normalize_radians(cusp_12 + TAIYIN_PI);
    out[6] = normalize_radians(ascendant + TAIYIN_PI);
    out[7] = normalize_radians(cusp_2 + TAIYIN_PI);
    out[8] = normalize_radians(cusp_3 + TAIYIN_PI);
    out[9] = midheaven;
    out[10] = cusp_11;
    out[11] = cusp_12;
}

void fill_porphyry_cusps(
    double ascendant,
    double midheaven,
    double out[12]
) noexcept {
    const double ic = normalize_radians(midheaven + TAIYIN_PI);
    const double mc_to_asc = normalize_radians(ascendant - midheaven);
    const double asc_to_ic = normalize_radians(ic - ascendant);
    fill_quadrant_cusps(
        ascendant,
        midheaven,
        normalize_radians(ascendant + asc_to_ic / 3.0),
        normalize_radians(ascendant + 2.0 * asc_to_ic / 3.0),
        normalize_radians(midheaven + mc_to_asc / 3.0),
        normalize_radians(midheaven + 2.0 * mc_to_asc / 3.0),
        out);
}

bool asin_checked(double value, double* out) noexcept {
    if (!out || !std::isfinite(value) || value < -1.0 || value > 1.0) return false;
    *out = std::asin(value);
    return true;
}

bool placidus_cusp_rad(
    double rectasc_rad,
    double initial_pole_latitude_rad,
    double latitude_rad,
    double obliquity_rad,
    double divisor,
    double* out
) noexcept {
    if (!out || !std::isfinite(rectasc_rad) || !std::isfinite(initial_pole_latitude_rad)
        || !std::isfinite(latitude_rad) || !std::isfinite(obliquity_rad)
        || !(divisor > 0.0)) {
        return false;
    }
    const double sine = std::sin(obliquity_rad);
    const double tangent_latitude = std::tan(latitude_rad);
    double cusp = great_circle_ecliptic_intersection_rad(
        rectasc_rad, initial_pole_latitude_rad, obliquity_rad);
    if (!std::isfinite(cusp)) return false;
    for (int iteration = 0; iteration < kMaxPlacidusIterations; ++iteration) {
        double declination = 0.0;
        if (!asin_checked(sine * std::sin(cusp), &declination)) return false;
        const double tangent_declination = std::tan(declination);
        if (!std::isfinite(tangent_declination)) return false;
        if (std::fabs(tangent_declination) < kSmall) {
            *out = normalize_radians(rectasc_rad);
            return true;
        }
        double pole_numerator = 0.0;
        if (!asin_checked(tangent_latitude * tangent_declination, &pole_numerator)) return false;
        const double pole_latitude = std::atan(
            std::sin(pole_numerator / divisor) / tangent_declination);
        if (!std::isfinite(pole_latitude)) return false;
        const double next = great_circle_ecliptic_intersection_rad(
            rectasc_rad, pole_latitude, obliquity_rad);
        if (!std::isfinite(next)) return false;
        if (iteration > 0
            && std::fabs(normalize_signed_radians(next - cusp)) < kPlacidusToleranceRad) {
            *out = next;
            return true;
        }
        cusp = next;
    }
    return false;
}

bool fill_placidus_cusps(const HouseSystemDispatchData& data, double out[12]) noexcept {
    if (std::fabs(data.observer_latitude_rad)
        >= 0.5 * TAIYIN_PI - data.true_obliquity_rad) {
        return false;
    }
    const double tangent_obliquity = std::tan(data.true_obliquity_rad);
    double a = 0.0;
    if (!std::isfinite(tangent_obliquity)
        || !asin_checked(std::tan(data.observer_latitude_rad) * tangent_obliquity, &a)) {
        return false;
    }
    const double pole_11_3 = std::atan(std::sin(a / 3.0) / tangent_obliquity);
    const double pole_12_2 = std::atan(std::sin(2.0 * a / 3.0) / tangent_obliquity);
    double cusp_2 = NAN;
    double cusp_3 = NAN;
    double cusp_11 = NAN;
    double cusp_12 = NAN;
    if (!placidus_cusp_rad(
            data.armc_rad + TAIYIN_PI / 6.0, pole_11_3,
            data.observer_latitude_rad, data.true_obliquity_rad, 3.0, &cusp_11)
        || !placidus_cusp_rad(
            data.armc_rad + TAIYIN_PI / 3.0, pole_12_2,
            data.observer_latitude_rad, data.true_obliquity_rad, 1.5, &cusp_12)
        || !placidus_cusp_rad(
            data.armc_rad + 2.0 * TAIYIN_PI / 3.0, pole_12_2,
            data.observer_latitude_rad, data.true_obliquity_rad, 1.5, &cusp_2)
        || !placidus_cusp_rad(
            data.armc_rad + 5.0 * TAIYIN_PI / 6.0, pole_11_3,
            data.observer_latitude_rad, data.true_obliquity_rad, 3.0, &cusp_3)) {
        return false;
    }
    fill_quadrant_cusps(
        data.ascendant_rad, data.midheaven_rad,
        cusp_2, cusp_3, cusp_11, cusp_12, out);
    return true;
}

bool eval_whole_sign(const HouseSystemDispatchData* data, double out[12]) {
    if (!data || !out) return false;
    const double sign_start_deg =
        std::floor(normalize_degrees(data->ascendant_rad) / 30.0) * 30.0;
    fill_equal_cusps(sign_start_deg * TAIYIN_DEG_TO_RAD, out);
    return true;
}

bool eval_equal(const HouseSystemDispatchData* data, double out[12]) {
    if (!data || !out) return false;
    fill_equal_cusps(data->ascendant_rad, out);
    return true;
}

bool eval_porphyry(const HouseSystemDispatchData* data, double out[12]) {
    if (!data || !out) return false;
    fill_porphyry_cusps(data->ascendant_rad, data->midheaven_rad, out);
    return true;
}

bool eval_placidus(const HouseSystemDispatchData* data, double out[12]) {
    return data && out && fill_placidus_cusps(*data, out);
}

bool eval_koch(const HouseSystemDispatchData* data, double out[12]) {
    if (!data || !out
        || std::fabs(data->observer_latitude_rad)
            >= 0.5 * TAIYIN_PI - data->true_obliquity_rad) {
        return false;
    }
    const double cos_latitude = std::cos(data->observer_latitude_rad);
    if (std::fabs(cos_latitude) < kSmall) return false;
    const double sine_a = std::max(-1.0, std::min(
        1.0,
        std::sin(data->midheaven_rad) * std::sin(data->true_obliquity_rad)
            / cos_latitude));
    const double cosine_a = std::sqrt(std::max(0.0, 1.0 - sine_a * sine_a));
    const double c = std::atan2(std::tan(data->observer_latitude_rad), cosine_a);
    const double third_ascensional_difference =
        std::asin(std::max(-1.0, std::min(1.0, std::sin(c) * sine_a))) / 3.0;
    const double cusp_11 = great_circle_ecliptic_intersection_rad(
        data->armc_rad + TAIYIN_PI / 6.0 - 2.0 * third_ascensional_difference,
        data->observer_latitude_rad, data->true_obliquity_rad);
    const double cusp_12 = great_circle_ecliptic_intersection_rad(
        data->armc_rad + TAIYIN_PI / 3.0 - third_ascensional_difference,
        data->observer_latitude_rad, data->true_obliquity_rad);
    const double cusp_2 = great_circle_ecliptic_intersection_rad(
        data->armc_rad + 2.0 * TAIYIN_PI / 3.0 + third_ascensional_difference,
        data->observer_latitude_rad, data->true_obliquity_rad);
    const double cusp_3 = great_circle_ecliptic_intersection_rad(
        data->armc_rad + 5.0 * TAIYIN_PI / 6.0 + 2.0 * third_ascensional_difference,
        data->observer_latitude_rad, data->true_obliquity_rad);
    fill_quadrant_cusps(
        data->ascendant_rad, data->midheaven_rad,
        cusp_2, cusp_3, cusp_11, cusp_12, out);
    return true;
}

bool fill_pole_division_cusps(
    const HouseSystemDispatchData* data,
    double pole_11,
    double pole_12,
    double out[12]
) noexcept {
    if (!data || !out || !std::isfinite(pole_11) || !std::isfinite(pole_12)) return false;
    fill_quadrant_cusps(
        data->ascendant_rad,
        data->midheaven_rad,
        great_circle_ecliptic_intersection_rad(
            data->armc_rad + 2.0 * TAIYIN_PI / 3.0,
            pole_12, data->true_obliquity_rad),
        great_circle_ecliptic_intersection_rad(
            data->armc_rad + 5.0 * TAIYIN_PI / 6.0,
            pole_11, data->true_obliquity_rad),
        great_circle_ecliptic_intersection_rad(
            data->armc_rad + TAIYIN_PI / 6.0,
            pole_11, data->true_obliquity_rad),
        great_circle_ecliptic_intersection_rad(
            data->armc_rad + TAIYIN_PI / 3.0,
            pole_12, data->true_obliquity_rad),
        out);
    return true;
}

bool eval_regiomontanus(const HouseSystemDispatchData* data, double out[12]) {
    if (!data || !out) return false;
    const double cos_latitude = std::cos(data->observer_latitude_rad);
    if (std::fabs(cos_latitude) < kSmall) return false;
    return fill_pole_division_cusps(
        data,
        std::atan(std::tan(data->observer_latitude_rad) * 0.5),
        std::atan(std::tan(data->observer_latitude_rad) * std::cos(TAIYIN_PI / 6.0)),
        out);
}

bool eval_campanus(const HouseSystemDispatchData* data, double out[12]) {
    if (!data || !out) return false;
    const double sin_latitude = std::sin(data->observer_latitude_rad);
    const double cos_latitude = std::cos(data->observer_latitude_rad);
    const double pole_11 = std::asin(std::max(-1.0, std::min(1.0, 0.5 * sin_latitude)));
    const double pole_12 = std::asin(std::max(
        -1.0, std::min(1.0, std::cos(TAIYIN_PI / 6.0) * sin_latitude)));
    const double equatorial_offset_11 = std::atan2(std::sqrt(3.0), cos_latitude);
    const double equatorial_offset_12 = std::atan2(1.0 / std::sqrt(3.0), cos_latitude);
    fill_quadrant_cusps(
        data->ascendant_rad,
        data->midheaven_rad,
        great_circle_ecliptic_intersection_rad(
            data->armc_rad + 0.5 * TAIYIN_PI + equatorial_offset_12,
            pole_12, data->true_obliquity_rad),
        great_circle_ecliptic_intersection_rad(
            data->armc_rad + 0.5 * TAIYIN_PI + equatorial_offset_11,
            pole_11, data->true_obliquity_rad),
        great_circle_ecliptic_intersection_rad(
            data->armc_rad + 0.5 * TAIYIN_PI - equatorial_offset_11,
            pole_11, data->true_obliquity_rad),
        great_circle_ecliptic_intersection_rad(
            data->armc_rad + 0.5 * TAIYIN_PI - equatorial_offset_12,
            pole_12, data->true_obliquity_rad),
        out);
    return true;
}

bool eval_alcabitius(const HouseSystemDispatchData* data, double out[12]) {
    if (!data || !out) return false;
    const double cos_latitude = std::cos(data->observer_latitude_rad);
    if (std::fabs(cos_latitude) < kSmall) return false;
    const double ascendant_declination = std::asin(std::max(
        -1.0,
        std::min(1.0, std::sin(data->ascendant_rad) * std::sin(data->true_obliquity_rad))));
    const double tan_latitude = std::tan(data->observer_latitude_rad);
    const double tan_declination = std::tan(ascendant_declination);
    if (!std::isfinite(tan_latitude) || !std::isfinite(tan_declination)) return false;
    const double cos_local_hour_angle = -tan_latitude * tan_declination;
    if (!std::isfinite(cos_local_hour_angle)
        || cos_local_hour_angle < -1.0 || cos_local_hour_angle > 1.0) {
        return false;
    }
    const double semidiurnal_arc = std::acos(cos_local_hour_angle);
    const double seminocturnal_arc = TAIYIN_PI - semidiurnal_arc;
    fill_quadrant_cusps(
        data->ascendant_rad,
        data->midheaven_rad,
        great_circle_ecliptic_intersection_rad(
            data->armc_rad + TAIYIN_PI - 2.0 * seminocturnal_arc / 3.0,
            0.0, data->true_obliquity_rad),
        great_circle_ecliptic_intersection_rad(
            data->armc_rad + TAIYIN_PI - seminocturnal_arc / 3.0,
            0.0, data->true_obliquity_rad),
        great_circle_ecliptic_intersection_rad(
            data->armc_rad + semidiurnal_arc / 3.0,
            0.0, data->true_obliquity_rad),
        great_circle_ecliptic_intersection_rad(
            data->armc_rad + 2.0 * semidiurnal_arc / 3.0,
            0.0, data->true_obliquity_rad),
        out);
    return true;
}

bool eval_polich_page(const HouseSystemDispatchData* data, double out[12]) {
    if (!data || !out) return false;
    const double cos_latitude = std::cos(data->observer_latitude_rad);
    if (std::fabs(cos_latitude) < kSmall) return false;
    return fill_pole_division_cusps(
        data,
        std::atan(std::tan(data->observer_latitude_rad) / 3.0),
        std::atan(2.0 * std::tan(data->observer_latitude_rad) / 3.0),
        out);
}

double equatorial_zero_latitude_to_ecliptic_longitude(
    double right_ascension_rad,
    double obliquity_rad
) noexcept {
    return normalize_radians(std::atan2(
        std::cos(obliquity_rad) * std::sin(right_ascension_rad),
        std::cos(right_ascension_rad)));
}

bool eval_morinus(const HouseSystemDispatchData* data, double out[12]) {
    if (!data || !out) return false;
    for (int sequence = 1; sequence <= 12; ++sequence) {
        const int cusp_index = (sequence + 9) % 12;
        out[cusp_index] = equatorial_zero_latitude_to_ecliptic_longitude(
            data->armc_rad + sequence * TAIYIN_PI / 6.0,
            data->true_obliquity_rad);
    }
    return true;
}

std::mutex& house_system_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<int, HouseSystemModelEntry>& house_system_models() {
    static std::unordered_map<int, HouseSystemModelEntry> models = [] {
        std::unordered_map<int, HouseSystemModelEntry> value;
        value[TAIYIN_HOUSE_SYSTEM_WHOLE_SIGN] =
            HouseSystemModelEntry(TAIYIN_HOUSE_SYSTEM_WHOLE_SIGN, &eval_whole_sign);
        value[TAIYIN_HOUSE_SYSTEM_EQUAL] =
            HouseSystemModelEntry(TAIYIN_HOUSE_SYSTEM_EQUAL, &eval_equal);
        value[TAIYIN_HOUSE_SYSTEM_PORPHYRY] =
            HouseSystemModelEntry(TAIYIN_HOUSE_SYSTEM_PORPHYRY, &eval_porphyry);
        value[TAIYIN_HOUSE_SYSTEM_PLACIDUS] = HouseSystemModelEntry(
            TAIYIN_HOUSE_SYSTEM_PLACIDUS, &eval_placidus, TAIYIN_HOUSE_SYSTEM_PORPHYRY);
        value[TAIYIN_HOUSE_SYSTEM_KOCH] = HouseSystemModelEntry(
            TAIYIN_HOUSE_SYSTEM_KOCH, &eval_koch, TAIYIN_HOUSE_SYSTEM_PORPHYRY);
        value[TAIYIN_HOUSE_SYSTEM_REGIOMONTANUS] =
            HouseSystemModelEntry(TAIYIN_HOUSE_SYSTEM_REGIOMONTANUS, &eval_regiomontanus);
        value[TAIYIN_HOUSE_SYSTEM_CAMPANUS] =
            HouseSystemModelEntry(TAIYIN_HOUSE_SYSTEM_CAMPANUS, &eval_campanus);
        value[TAIYIN_HOUSE_SYSTEM_ALCABITIUS] =
            HouseSystemModelEntry(TAIYIN_HOUSE_SYSTEM_ALCABITIUS, &eval_alcabitius);
        value[TAIYIN_HOUSE_SYSTEM_POLICH_PAGE] =
            HouseSystemModelEntry(TAIYIN_HOUSE_SYSTEM_POLICH_PAGE, &eval_polich_page);
        value[TAIYIN_HOUSE_SYSTEM_MORINUS] =
            HouseSystemModelEntry(TAIYIN_HOUSE_SYSTEM_MORINUS, &eval_morinus);
        return value;
    }();
    return models;
}

bool finite_cusps(const double cusps[12]) noexcept {
    if (!cusps) return false;
    for (int i = 0; i < 12; ++i) {
        if (!std::isfinite(cusps[i])) return false;
    }
    return true;
}

}  // namespace

namespace internal {

bool calc_placidus_cusps_from_armc(
    double armc_rad,
    double latitude_rad,
    double true_obliquity_rad,
    double out_cusp_longitude_rad[12]
) noexcept {
    HouseResult result;
    const Status status = calc_houses_from_armc(
        armc_rad,
        latitude_rad,
        true_obliquity_rad,
        TAIYIN_HOUSE_SYSTEM_PLACIDUS,
        &result);
    if (status != TAIYIN_STATUS_OK
        || result.resolved_system_id != TAIYIN_HOUSE_SYSTEM_PLACIDUS
        || !out_cusp_longitude_rad) {
        return false;
    }
    for (int i = 0; i < 12; ++i) {
        out_cusp_longitude_rad[i] = result.cusp_longitude_rad[i];
    }
    return true;
}

}  // namespace internal

HouseSystemDispatchData::HouseSystemDispatchData() noexcept
    : armc_rad(NAN),
      observer_latitude_rad(NAN),
      true_obliquity_rad(NAN),
      ascendant_rad(NAN),
      midheaven_rad(NAN),
      model_data(nullptr) {}

HouseSystemModelEntry::HouseSystemModelEntry() noexcept
    : model_id(-1), eval(nullptr), fallback_model_id(-1), model_data(nullptr) {}

HouseSystemModelEntry::HouseSystemModelEntry(
    int model_id_value,
    HouseSystemFn eval_value,
    int fallback_model_id_value,
    const void* model_data_value
) noexcept
    : model_id(model_id_value),
      eval(eval_value),
      fallback_model_id(fallback_model_id_value),
      model_data(model_data_value) {}

HouseResult::HouseResult() noexcept
    : requested_system_id(-1),
      resolved_system_id(-1),
      flags(0),
      armc_rad(NAN),
      ascendant_rad(NAN),
      midheaven_rad(NAN),
      vertex_rad(NAN),
      east_point_rad(NAN),
      armc_rate_rad_per_day(NAN),
      ascendant_rate_rad_per_day(NAN),
      midheaven_rate_rad_per_day(NAN),
      vertex_rate_rad_per_day(NAN),
      east_point_rate_rad_per_day(NAN),
      cusp_longitude_rad{NAN, NAN, NAN, NAN, NAN, NAN,
                         NAN, NAN, NAN, NAN, NAN, NAN},
      cusp_longitude_rate_rad_per_day{NAN, NAN, NAN, NAN, NAN, NAN,
                                      NAN, NAN, NAN, NAN, NAN, NAN} {}

HousePositionResult::HousePositionResult() noexcept
    : house_number(0), fraction(NAN), continuous_house_position(NAN) {}

bool add_house_system_model(const HouseSystemModelEntry& entry) noexcept {
    if (entry.model_id < TAIYIN_HOUSE_SYSTEM_CUSTOM_START || !entry.eval
        || entry.fallback_model_id == entry.model_id) {
        return false;
    }
    std::lock_guard<std::mutex> lock(house_system_mutex());
    std::unordered_map<int, HouseSystemModelEntry>& models = house_system_models();
    if (models.find(entry.model_id) != models.end()
        || (entry.fallback_model_id >= 0
            && models.find(entry.fallback_model_id) == models.end())) {
        return false;
    }
    try {
        models[entry.model_id] = entry;
    } catch (...) {
        return false;
    }
    return true;
}

bool remove_house_system_model(int model_id) noexcept {
    return remove_house_system_model_if_matches(model_id, nullptr, nullptr)
        == HouseSystemModelRemovalResult::removed;
}

HouseSystemModelRemovalResult remove_house_system_model_if_matches(
    int model_id,
    HouseSystemFn expected_eval,
    const void* expected_model_data
) noexcept {
    if (model_id < TAIYIN_HOUSE_SYSTEM_CUSTOM_START) {
        return HouseSystemModelRemovalResult::not_found_or_mismatch;
    }
    try {
        std::lock_guard<std::mutex> lock(house_system_mutex());
        std::unordered_map<int, HouseSystemModelEntry>& models =
            house_system_models();
        const std::unordered_map<int, HouseSystemModelEntry>::iterator it =
            models.find(model_id);
        if (it == models.end()
            || (expected_eval
                && (it->second.eval != expected_eval
                    || it->second.model_data != expected_model_data))) {
            return HouseSystemModelRemovalResult::not_found_or_mismatch;
        }
        for (std::unordered_map<int, HouseSystemModelEntry>::const_iterator
                 candidate = models.begin();
             candidate != models.end(); ++candidate) {
            if (candidate->second.fallback_model_id == model_id) {
                return HouseSystemModelRemovalResult::still_referenced;
            }
        }
        models.erase(it);
        return HouseSystemModelRemovalResult::removed;
    } catch (...) {
        return HouseSystemModelRemovalResult::not_found_or_mismatch;
    }
}

bool find_house_system_model(int model_id, HouseSystemModelEntry* out) noexcept {
    if (out) *out = HouseSystemModelEntry();
    if (!out) return false;
    std::lock_guard<std::mutex> lock(house_system_mutex());
    const std::unordered_map<int, HouseSystemModelEntry>& models = house_system_models();
    const std::unordered_map<int, HouseSystemModelEntry>::const_iterator it =
        models.find(model_id);
    if (it == models.end() || !it->second.eval) return false;
    *out = it->second;
    return true;
}

bool has_house_system_model(int model_id) noexcept {
    HouseSystemModelEntry entry;
    return find_house_system_model(model_id, &entry);
}

bool eval_house_system_model(
    int model_id,
    const HouseSystemDispatchData* data,
    double out_cusp_longitude_rad[12]
) noexcept {
    HouseSystemModelEntry entry;
    if (!data || !out_cusp_longitude_rad
        || !find_house_system_model(model_id, &entry) || !entry.eval) {
        return false;
    }
    try {
        double candidate[12];
        std::fill(candidate, candidate + 12, NAN);
        HouseSystemDispatchData dispatch = *data;
        dispatch.model_data = entry.model_data;
        if (!entry.eval(&dispatch, candidate) || !finite_cusps(candidate)) {
            return false;
        }
        std::copy(candidate, candidate + 12, out_cusp_longitude_rad);
        return true;
    } catch (...) {
        return false;
    }
}

Status calc_houses_from_armc(
    double armc_rad,
    double observer_latitude_rad,
    double true_obliquity_rad,
    int house_system_id,
    HouseResult* out
) noexcept {
    if (out) *out = HouseResult();
    HouseSystemModelEntry entry;
    if (!out || !std::isfinite(armc_rad) || !std::isfinite(observer_latitude_rad)
        || !std::isfinite(true_obliquity_rad)
        || std::fabs(observer_latitude_rad) >= 0.5 * TAIYIN_PI
        || !(true_obliquity_rad > 0.0) || !(true_obliquity_rad < 0.5 * TAIYIN_PI)
        || !find_house_system_model(house_system_id, &entry)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    HouseSystemDispatchData data;
    data.armc_rad = normalize_radians(armc_rad);
    data.observer_latitude_rad = observer_latitude_rad;
    data.true_obliquity_rad = true_obliquity_rad;
    if (!corrected_ascendant_midheaven(
            data.armc_rad,
            data.true_obliquity_rad,
            data.observer_latitude_rad,
            &data.ascendant_rad,
            &data.midheaven_rad)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    out->requested_system_id = house_system_id;
    out->resolved_system_id = house_system_id;
    out->armc_rad = data.armc_rad;
    out->ascendant_rad = data.ascendant_rad;
    out->midheaven_rad = data.midheaven_rad;
    out->vertex_rad = vertex_rad(
        data.armc_rad, data.true_obliquity_rad, data.observer_latitude_rad);
    out->east_point_rad = ascendant_rad(data.armc_rad, data.true_obliquity_rad, 0.0);
    if (!std::isfinite(out->vertex_rad) || !std::isfinite(out->east_point_rad)) {
        *out = HouseResult();
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    int resolved_system_id = house_system_id;
    for (int fallback_depth = 0; fallback_depth < 32; ++fallback_depth) {
        if (eval_house_system_model(
                resolved_system_id, &data, out->cusp_longitude_rad)) {
            out->resolved_system_id = resolved_system_id;
            if (resolved_system_id != house_system_id) {
                out->flags |= TAIYIN_HOUSE_RESULT_USED_FALLBACK;
                if (resolved_system_id == TAIYIN_HOUSE_SYSTEM_PORPHYRY) {
                    out->flags |= TAIYIN_HOUSE_RESULT_FALLBACK_PORPHYRY;
                }
            }
            return TAIYIN_STATUS_OK;
        }
        HouseSystemModelEntry failed_entry;
        if (!find_house_system_model(resolved_system_id, &failed_entry)
            || failed_entry.fallback_model_id < 0) {
            *out = HouseResult();
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }
        resolved_system_id = failed_entry.fallback_model_id;
    }
    *out = HouseResult();
    return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

Status calc_house_position_from_longitude(
    const HouseResult* houses,
    double ecliptic_longitude_rad,
    HousePositionResult* out
) noexcept {
    if (out) *out = HousePositionResult();
    if (!houses || !out || !std::isfinite(ecliptic_longitude_rad)
        || !finite_cusps(houses->cusp_longitude_rad)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    double total_span = 0.0;
    for (int i = 0; i < 12; ++i) {
        const double span = normalize_radians(
            houses->cusp_longitude_rad[(i + 1) % 12]
            - houses->cusp_longitude_rad[i]);
        if (!(span > kSmall) || !std::isfinite(span)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        total_span += span;
    }
    if (!std::isfinite(total_span)
        || std::fabs(total_span - TAIYIN_TWO_PI) > 1.0e-10) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double longitude = normalize_radians(ecliptic_longitude_rad);
    for (int i = 0; i < 12; ++i) {
        const double start = normalize_radians(houses->cusp_longitude_rad[i]);
        const double end = normalize_radians(houses->cusp_longitude_rad[(i + 1) % 12]);
        const double span = normalize_radians(end - start);
        if (!(span > kSmall) || !std::isfinite(span)) {
            continue;
        }
        const double offset = normalize_radians(longitude - start);
        if (offset < span || std::fabs(offset - span) <= kSmall) {
            if (std::fabs(offset - span) <= kSmall) {
                const int next = (i + 1) % 12;
                out->house_number = next + 1;
                out->fraction = 0.0;
                out->continuous_house_position = static_cast<double>(next + 1);
            } else {
                out->house_number = i + 1;
                out->fraction = offset / span;
                out->continuous_house_position =
                    static_cast<double>(out->house_number) + out->fraction;
            }
            return TAIYIN_STATUS_OK;
        }
    }
    *out = HousePositionResult();
    return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

}  // namespace astrology
}  // namespace taiyin
