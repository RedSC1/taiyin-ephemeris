#ifndef TAIYIN_ZIWEI_TYPES_H
#define TAIYIN_ZIWEI_TYPES_H

#include "taiyin/time.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace taiyin {
namespace ziwei {

enum class Stem : uint8_t {
    Jia = 0,
    Yi = 1,
    Bing = 2,
    Ding = 3,
    Wu = 4,
    Ji = 5,
    Geng = 6,
    Xin = 7,
    Ren = 8,
    Gui = 9,
};

enum class Branch : uint8_t {
    Zi = 0,
    Chou = 1,
    Yin = 2,
    Mao = 3,
    Chen = 4,
    Si = 5,
    Wu = 6,
    Wei = 7,
    Shen = 8,
    You = 9,
    Xu = 10,
    Hai = 11,
};

// Ziwei keeps its own stable public encoding. Do not reinterpret this as the
// BaZi extension's gender enum, whose historical ABI ordering differs.
enum class Gender : uint8_t {
    Male = 0,
    Female = 1,
};

enum class Bureau : uint8_t {
    Water2 = 0,
    Wood3 = 1,
    Metal4 = 2,
    Earth5 = 3,
    Fire6 = 4,
};

enum class PalaceId : uint8_t {
    Life = 0,
    Siblings = 1,
    Spouse = 2,
    Children = 3,
    Wealth = 4,
    Health = 5,
    Travel = 6,
    Friends = 7,
    Career = 8,
    Property = 9,
    Fortune = 10,
    Parents = 11,
};

enum class FlowLevel : uint8_t {
    Decade = 0,
    Year = 1,
    Month = 2,
    Day = 3,
    Hour = 4,
};

// A Zi branch alone cannot distinguish 00:xx from 23:xx when a school splits
// the Rat hour. Keep that identity explicit for navigation and bindings.
enum class RatHourSegment : uint8_t {
    None = 0,
    Unified = 1,
    Early = 2,
    Late = 3,
};

typedef uint16_t StarId;
constexpr StarId kInvalidStarId = std::numeric_limits<StarId>::max();
constexpr std::size_t kStemCount = 10u;
constexpr std::size_t kBranchCount = 12u;
constexpr std::size_t kPalaceCount = 12u;
constexpr std::size_t kFlowLevelCount = 5u;

constexpr uint8_t to_index(Stem value) noexcept {
    return static_cast<uint8_t>(value);
}

constexpr uint8_t to_index(Branch value) noexcept {
    return static_cast<uint8_t>(value);
}

constexpr uint8_t to_index(Gender value) noexcept {
    return static_cast<uint8_t>(value);
}

constexpr uint8_t to_index(Bureau value) noexcept {
    return static_cast<uint8_t>(value);
}

constexpr uint8_t to_index(PalaceId value) noexcept {
    return static_cast<uint8_t>(value);
}

constexpr uint8_t to_index(FlowLevel value) noexcept {
    return static_cast<uint8_t>(value);
}

constexpr uint8_t to_index(RatHourSegment value) noexcept {
    return static_cast<uint8_t>(value);
}

constexpr bool is_valid(Stem value) noexcept {
    return to_index(value) < kStemCount;
}

constexpr bool is_valid(Branch value) noexcept {
    return to_index(value) < kBranchCount;
}

constexpr bool is_valid(Gender value) noexcept {
    return to_index(value) <= to_index(Gender::Female);
}

constexpr bool is_valid(Bureau value) noexcept {
    return to_index(value) <= to_index(Bureau::Fire6);
}

constexpr bool is_valid(PalaceId value) noexcept {
    return to_index(value) < kPalaceCount;
}

constexpr bool is_valid(FlowLevel value) noexcept {
    return to_index(value) < kFlowLevelCount;
}

constexpr bool is_valid(RatHourSegment value) noexcept {
    return to_index(value) <= to_index(RatHourSegment::Late);
}

constexpr uint8_t bureau_number(Bureau value) noexcept {
    return value == Bureau::Water2 ? 2u
        : value == Bureau::Wood3 ? 3u
        : value == Bureau::Metal4 ? 4u
        : value == Bureau::Earth5 ? 5u
        : value == Bureau::Fire6 ? 6u
        : 0u;
}

constexpr bool is_forward(Stem year_stem, Gender gender) noexcept {
    return (to_index(year_stem) & 1u) == to_index(gender);
}

constexpr Branch advance_branch(Branch value, int offset) noexcept {
    return static_cast<Branch>(
        (static_cast<int>(to_index(value)) + (offset % 12) + 12) % 12);
}

struct Ganzhi {
    Stem stem;
    Branch branch;
};

// Flow coordinates deliberately do not use Ganzhi. A flow layer combines the
// stem that drives Si-Hua with the physical branch occupied by that layer's
// life palace. Those two components need not form a valid sexagenary pair.
struct FlowCoordinate {
    Stem stem;
    Branch branch;
};

struct Pillars {
    Ganzhi year;
    Ganzhi month;
    Ganzhi day;
    Ganzhi hour;
};

// BirthInput contains one physical instant and one already-resolved clock for
// day/hour rules. Legal time-zone lookup and true/mean-solar-time conversion
// belong above this layer.
struct BirthInput {
    SplitJulianDate instant_utc;
    CalendarDateTime virtual_time;
    Gender gender;
};

struct LunarDateFacts {
    int32_t year;
    int32_t historical_year;
    uint8_t month;
    uint8_t day;
    uint8_t is_leap;
    uint8_t month_name;
};

// CalendarFacts is the boundary between Taiyin's calendar calculations and
// the finite Ziwei rule engine. Special rules may consume these bounded facts
// without turning their intermediate values into public anchors.
struct CalendarFacts {
    BirthInput birth;
    LunarDateFacts lunar_date;
    Pillars solar_term_pillars;
    Pillars lunar_pillars;
    int32_t effective_lunar_year;
    uint8_t effective_lunar_month;
    uint16_t solar_day_from_previous_jie;
};

constexpr bool is_valid(const Ganzhi& value) noexcept {
    return is_valid(value.stem)
        && is_valid(value.branch)
        && ((to_index(value.stem) & 1u) == (to_index(value.branch) & 1u));
}

constexpr bool is_valid(const FlowCoordinate& value) noexcept {
    return is_valid(value.stem) && is_valid(value.branch);
}

constexpr bool is_valid(const Pillars& value) noexcept {
    return is_valid(value.year)
        && is_valid(value.month)
        && is_valid(value.day)
        && is_valid(value.hour);
}

}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_TYPES_H
