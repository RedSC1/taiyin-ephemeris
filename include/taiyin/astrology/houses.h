#ifndef TAIYIN_ASTROLOGY_HOUSES_H
#define TAIYIN_ASTROLOGY_HOUSES_H

#include "taiyin/runtime/native_context.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <stdint.h>

namespace taiyin {
namespace astrology {

// House cusps are tropical longitudes on the true ecliptic of date. The
// observer location is read from NativeCalcContext.
enum HouseSystemId {
    TAIYIN_HOUSE_SYSTEM_WHOLE_SIGN = 0,
    TAIYIN_HOUSE_SYSTEM_EQUAL = 1,
    TAIYIN_HOUSE_SYSTEM_PORPHYRY = 2,
    TAIYIN_HOUSE_SYSTEM_PLACIDUS = 3,
    TAIYIN_HOUSE_SYSTEM_KOCH = 4,
    TAIYIN_HOUSE_SYSTEM_REGIOMONTANUS = 5,
    TAIYIN_HOUSE_SYSTEM_CAMPANUS = 6,
    TAIYIN_HOUSE_SYSTEM_ALCABITIUS = 7,
    TAIYIN_HOUSE_SYSTEM_POLICH_PAGE = 8,
    TAIYIN_HOUSE_SYSTEM_MORINUS = 9,
};

constexpr int TAIYIN_HOUSE_SYSTEM_CUSTOM_START = 10000;

// Set whenever the requested model resolves through any fallback.
constexpr uint32_t TAIYIN_HOUSE_RESULT_USED_FALLBACK = 1u << 0;
// Set in addition to USED_FALLBACK when the final resolved model is Porphyry.
constexpr uint32_t TAIYIN_HOUSE_RESULT_FALLBACK_PORPHYRY = 1u << 1;
// A valid position result was produced, but a stable centered speed sample
// was unavailable (for example at a model fallback boundary).
constexpr uint32_t TAIYIN_HOUSE_RESULT_SPEED_UNAVAILABLE = 1u << 2;

struct HouseSystemDispatchData {
    double armc_rad;
    double observer_latitude_rad;
    double true_obliquity_rad;
    double ascendant_rad;
    double midheaven_rad;
    // Filled by eval_house_system_model from the registered model entry.
    const void* model_data;

    HouseSystemDispatchData() noexcept;
};

// Evaluators may be called concurrently. Callback code and all state reachable
// through model_data must remain loaded, valid, and safe for concurrent access
// until the model is removed. Registration and removal are setup-time changes
// and must not overlap evaluation.
typedef bool (*HouseSystemFn)(
    const HouseSystemDispatchData* data,
    double out_cusp_longitude_rad[12]
);

struct HouseSystemModelEntry {
    int model_id;
    HouseSystemFn eval;
    // Negative means that an evaluation failure is returned to the caller.
    int fallback_model_id;
    // The registry does not own this pointer. It must remain valid until this
    // model is removed and may be read concurrently.
    const void* model_data;

    HouseSystemModelEntry() noexcept;
    HouseSystemModelEntry(
        int model_id_value,
        HouseSystemFn eval_value,
        int fallback_model_id_value = -1,
        const void* model_data_value = nullptr
    ) noexcept;
};

struct HouseResult {
    int requested_system_id;
    int resolved_system_id;
    uint32_t flags;
    double armc_rad;
    double ascendant_rad;
    double midheaven_rad;
    double vertex_rad;
    double east_point_rad;
    // Time derivatives are radians per UT/TT day. They are populated by the
    // time-based entry points and remain NAN for calc_houses_from_armc().
    double armc_rate_rad_per_day;
    double ascendant_rate_rad_per_day;
    double midheaven_rate_rad_per_day;
    double vertex_rate_rad_per_day;
    double east_point_rate_rad_per_day;
    // Zero-based: cusp_longitude_rad[0] is the first-house cusp.
    double cusp_longitude_rad[12];
    double cusp_longitude_rate_rad_per_day[12];

    HouseResult() noexcept;
};

struct HousePositionResult {
    // One-based house number in [1, 12].
    int house_number;
    // Fraction measured along the ecliptic-longitude arc from this cusp to
    // the next cusp, in [0, 1).
    double fraction;
    // house_number + fraction, in [1, 13).
    double continuous_house_position;

    HousePositionResult() noexcept;
};

// Custom model IDs must be at least TAIYIN_HOUSE_SYSTEM_CUSTOM_START. Built-in
// IDs and duplicate custom IDs cannot be replaced.
bool add_house_system_model(const HouseSystemModelEntry& entry) noexcept;
// Removes one custom model. Built-in IDs cannot be removed. Registration and
// removal are setup-time changes and must not overlap evaluation.
bool remove_house_system_model(int model_id) noexcept;
// Outcome of an ownership-aware custom-house removal attempt.
enum class HouseSystemModelRemovalResult {
    removed,
    not_found_or_mismatch,
    still_referenced,
};

// Removes a custom model only when its callback identity still matches. A
// model that is still selected as another registered model's fallback is kept
// registered, so a successful registration cannot later acquire a dangling
// fallback target.
HouseSystemModelRemovalResult remove_house_system_model_if_matches(
    int model_id,
    HouseSystemFn expected_eval,
    const void* expected_model_data
) noexcept;
bool find_house_system_model(int model_id, HouseSystemModelEntry* out) noexcept;
bool has_house_system_model(int model_id) noexcept;
bool eval_house_system_model(
    int model_id,
    const HouseSystemDispatchData* data,
    double out_cusp_longitude_rad[12]
) noexcept;

// Pure house geometry for callers that already have local ARMC and true
// obliquity. Angles are radians; observer latitude is geodetic.
Status calc_houses_from_armc(
    double armc_rad,
    double observer_latitude_rad,
    double true_obliquity_rad,
    int house_system_id,
    HouseResult* out
) noexcept;

// jd_ut is UT1. The supplied context must carry a valid observer location.
Status calc_houses_ut(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_ut,
    int house_system_id,
    HouseResult* out
) noexcept;

// jd_tt is TT. UT1 is obtained from the context's selected Delta-T model.
Status calc_houses_tt(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_tt,
    int house_system_id,
    HouseResult* out
) noexcept;

// Locates an ecliptic longitude in an already computed cusp partition.
// This is a longitude-only chart placement helper. It does not implement the
// latitude-sensitive Placidus/Gauquelin semi-arc definition.
Status calc_house_position_from_longitude(
    const HouseResult* houses,
    double ecliptic_longitude_rad,
    HousePositionResult* out
) noexcept;

}  // namespace astrology
}  // namespace taiyin

#endif  // TAIYIN_ASTROLOGY_HOUSES_H
