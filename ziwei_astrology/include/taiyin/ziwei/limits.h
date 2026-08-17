#ifndef TAIYIN_ZIWEI_LIMITS_H
#define TAIYIN_ZIWEI_LIMITS_H

#include "taiyin/status.h"
#include "taiyin/ziwei/flow.h"

#include <cstdint>

namespace taiyin {
namespace ziwei {

// Lunar flow days use 1..30. Jie-bounded solar months can span 31 or, after
// civil-day assignment at the endpoints, exceptionally 32 labeled days.
constexpr uint8_t kMaxFlowDayIndex = 32u;

enum class ChildhoodStrategy : uint8_t {
    Skip = 0,
    Sequential = 1,
};

constexpr bool is_valid(ChildhoodStrategy value) noexcept {
    return static_cast<uint8_t>(value)
        <= static_cast<uint8_t>(ChildhoodStrategy::Sequential);
}

// A formal layer coordinate plus the natal palace role occupying that branch.
// The coordinate stem drives Si-Hua; the branch is the layer's life palace.
struct LimitCoordinate {
    FlowLevel level;
    FlowCoordinate coordinate;
    PalaceId natal_role;
};

struct DecadeLimit {
    LimitCoordinate limit;
    uint16_t index;  // 0 is childhood; the first regular decade is 1.
    int32_t start_age;
    int32_t end_age;
    int32_t start_year;
    int32_t end_year;
    bool is_childhood;
};

// Small limit is a parallel annual reference, not a sixth formal FlowLayer.
struct SmallLimit {
    FlowCoordinate coordinate;
    PalaceId natal_role;
    int32_t virtual_age;
};

struct FlowYearLimit {
    LimitCoordinate limit;
    int32_t year;
};

struct FlowMonthLimit {
    LimitCoordinate limit;
    int32_t year;
    uint8_t month;
    uint8_t sequence;
    bool is_leap;
    Branch doujun;
};

struct FlowDayLimit {
    LimitCoordinate limit;
    uint8_t day;
};

struct FlowHourLimit {
    LimitCoordinate limit;
    uint8_t hour_index;
    RatHourSegment rat_hour_segment;
};

Status make_decade_by_index(
    const NatalChart& natal,
    int32_t effective_birth_year,
    uint16_t index,
    DecadeLimit* out
) noexcept;

Status make_childhood_decade(
    const NatalChart& natal,
    int32_t effective_birth_year,
    int32_t target_year,
    ChildhoodStrategy strategy,
    DecadeLimit* out
) noexcept;

Status make_decade_for_year(
    const NatalChart& natal,
    int32_t effective_birth_year,
    int32_t target_year,
    ChildhoodStrategy strategy,
    DecadeLimit* out
) noexcept;

Status make_small_limit(
    const NatalChart& natal,
    Branch birth_solar_year_branch,
    int32_t virtual_age,
    SmallLimit* out
) noexcept;

Status make_flow_year(
    const NatalChart& natal,
    int32_t physical_year,
    FlowYearLimit* out
) noexcept;

Status make_flow_month(
    const NatalChart& natal,
    int32_t physical_year,
    uint8_t logical_month,
    uint8_t sequence,
    bool is_leap,
    uint8_t birth_effective_month,
    Branch birth_hour,
    FlowMonthLimit* out
) noexcept;

Status make_flow_day(
    const NatalChart& natal,
    const FlowMonthLimit& month,
    uint8_t day_index,
    Stem physical_day_stem,
    FlowDayLimit* out
) noexcept;

Status make_flow_hour(
    const NatalChart& natal,
    const FlowDayLimit& day,
    uint8_t hour_index,
    FlowHourLimit* out
) noexcept;

// Calendar-backed flow resolution supplies the already-resolved physical
// hour stem so TOMORROW_GAN is not silently reconstructed as TODAY_GAN.
Status make_flow_hour_from_pillar(
    const NatalChart& natal,
    const FlowDayLimit& day,
    const Ganzhi& physical_hour,
    RatHourSegment rat_hour_segment,
    FlowHourLimit* out
) noexcept;

Status make_limit_flow_layer(
    const LimitCoordinate& limit,
    const NatalChart& natal,
    const CompiledRules& rules,
    FlowLayer* out
) noexcept;

Status push_limit_flow_layer(
    Chart* chart,
    const LimitCoordinate& limit,
    const CompiledRules& rules
) noexcept;

}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_LIMITS_H
