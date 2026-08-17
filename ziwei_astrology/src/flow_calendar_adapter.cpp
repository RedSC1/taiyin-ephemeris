#include "taiyin/ziwei/flow_calendar_adapter.h"
#include "calendar_adapter_internal.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace taiyin {
namespace ziwei {
namespace {

int normalized(int64_t value, int modulus) noexcept {
    const int64_t result = value % modulus;
    return static_cast<int>(result < 0 ? result + modulus : result);
}

Ganzhi year_ganzhi(int32_t year) noexcept {
    return Ganzhi{
        static_cast<Stem>(normalized(static_cast<int64_t>(year) + 6, 10)),
        static_cast<Branch>(normalized(static_cast<int64_t>(year) + 8, 12)),
    };
}

bool same_ganzhi(const Ganzhi& lhs, const Ganzhi& rhs) noexcept {
    return lhs.stem == rhs.stem && lhs.branch == rhs.branch;
}

bool same_virtual_time(
    const CalendarDateTime& lhs,
    const CalendarDateTime& rhs
) noexcept {
    return lhs.year == rhs.year && lhs.month == rhs.month && lhs.day == rhs.day
        && lhs.hour == rhs.hour && lhs.minute == rhs.minute
        && lhs.second == rhs.second;
}

bool same_pillars(const Pillars& lhs, const Pillars& rhs) noexcept {
    return same_ganzhi(lhs.year, rhs.year)
        && same_ganzhi(lhs.month, rhs.month)
        && same_ganzhi(lhs.day, rhs.day)
        && same_ganzhi(lhs.hour, rhs.hour);
}

bool same_calendar_facts(
    const CalendarFacts& lhs,
    const CalendarFacts& rhs
) noexcept {
    return lhs.birth.instant_utc == rhs.birth.instant_utc
        && same_virtual_time(lhs.birth.virtual_time, rhs.birth.virtual_time)
        && lhs.birth.gender == rhs.birth.gender
        && lhs.lunar_date.year == rhs.lunar_date.year
        && lhs.lunar_date.month == rhs.lunar_date.month
        && lhs.lunar_date.day == rhs.lunar_date.day
        && lhs.lunar_date.is_leap == rhs.lunar_date.is_leap
        && lhs.lunar_date.month_name == rhs.lunar_date.month_name
        && same_pillars(lhs.solar_term_pillars, rhs.solar_term_pillars)
        && same_pillars(lhs.lunar_pillars, rhs.lunar_pillars)
        && lhs.effective_lunar_year == rhs.effective_lunar_year
        && lhs.effective_lunar_month == rhs.effective_lunar_month
        && lhs.solar_day_from_previous_jie == rhs.solar_day_from_previous_jie;
}

bool same_birth_chart(
    const ResolvedBirth& birth,
    const NatalChart& natal
) noexcept {
    return same_calendar_facts(birth.facts, natal.birth_facts)
        && birth.facts.birth.gender == natal.gender
        && birth.body_palace == natal.body_palace
        && flatten_anchors(birth.anchors) == flatten_anchors(natal.anchors);
}

bool valid_options(const FlowResolutionOptions& options) noexcept {
    return is_valid(options.childhood_strategy)
        && (options.boundary == PillarBoundary::SolarTerm
            || options.boundary == PillarBoundary::Lunar)
        && options.rat_hour_mode
            >= chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT
        && options.rat_hour_mode
            <= chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TOMORROW_GAN;
}

bool valid_rat_hour_mode(int32_t mode) noexcept {
    return mode >= chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT
        && mode <= chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TOMORROW_GAN;
}

RatHourSegment rat_hour_segment(
    const CalendarDateTime& virtual_time,
    int32_t rat_hour_mode,
    Branch hour_branch
) noexcept {
    if (hour_branch != Branch::Zi) return RatHourSegment::None;
    if (rat_hour_mode == chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT) {
        return RatHourSegment::Unified;
    }
    return virtual_time.hour >= 23
        ? RatHourSegment::Late : RatHourSegment::Early;
}

bool encode_virtual_time(
    const CalendarDateTime& value,
    SplitJulianDate* out
) noexcept {
    return out != NULL && julian_day_split(value, out);
}

Status shift_target_by_local_days(
    const SplitJulianDate& current_instant_utc,
    const CalendarDateTime& current_virtual_time,
    double local_days,
    SplitJulianDate* out_instant_utc,
    CalendarDateTime* out_virtual_time
) noexcept {
    if (out_instant_utc == NULL || out_virtual_time == NULL
        || !split_julian_date_is_finite(current_instant_utc)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate current_local;
    SplitJulianDate next_local;
    SplitJulianDate next_instant;
    if (!encode_virtual_time(current_virtual_time, &current_local)
        || !add_days_to_split_jd(current_local, local_days, &next_local)
        || !add_days_to_split_jd(current_instant_utc, local_days, &next_instant)
        || !reverse_julian_day_split(next_local, out_virtual_time)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    // The reverse conversion is only used to advance the civil date.  Its
    // floating-point decomposition can otherwise turn 23:30:00 into
    // 23:29:59.999999..., even though stepping a flow day promises to retain
    // the caller's wall-clock fields exactly.
    out_virtual_time->hour = current_virtual_time.hour;
    out_virtual_time->minute = current_virtual_time.minute;
    out_virtual_time->second = current_virtual_time.second;
    *out_instant_utc = next_instant;
    return TAIYIN_STATUS_OK;
}

double hour_center(uint8_t slot, bool split_rat) noexcept {
    if (split_rat) {
        if (slot == 0u) return 0.5;
        if (slot == 12u) return 23.5;
    }
    return slot == 0u ? 0.5 : static_cast<double>(slot) * 2.0;
}

Status effective_solar_year(
    int32_t civil_year,
    const Ganzhi& solar_year,
    int32_t* out
) noexcept {
    if (out == NULL || !is_valid(solar_year)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (same_ganzhi(year_ganzhi(civil_year), solar_year)) {
        *out = civil_year;
        return TAIYIN_STATUS_OK;
    }
    if (civil_year != std::numeric_limits<int32_t>::min()
        && same_ganzhi(year_ganzhi(civil_year - 1), solar_year)) {
        *out = civil_year - 1;
        return TAIYIN_STATUS_OK;
    }
    return TAIYIN_ERROR_INTERNAL;
}

uint8_t solar_month_from_branch(Branch branch) noexcept {
    return static_cast<uint8_t>(
        normalized(static_cast<int>(to_index(branch))
            - static_cast<int>(to_index(Branch::Yin)), 12) + 1);
}

struct MonthIdentity {
    int64_t first_day;
};

bool earlier_month(
    const MonthIdentity& lhs,
    const MonthIdentity& rhs
) noexcept {
    return lhs.first_day < rhs.first_day;
}

Status resolve_lunar_month_sequence(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const SplitJulianDate& target_instant_utc,
    const chinese_calendar::LunarDate& lunar,
    uint8_t* out,
    bool* out_collapsed_to_leap_twelve,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (calendar == NULL
        || out == NULL
        || out_collapsed_to_leap_twelve == NULL) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_collapsed_to_leap_twelve = false;
    try {
        chinese_calendar::LunarDate first_lunar = lunar;
        first_lunar.day = 1u;
        chinese_calendar::SolarDate first_solar;
        Status status = chinese_calendar::fromLunar(
            calendar, &first_lunar, &first_solar, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        const CalendarDateTime first_clock = {
            first_solar.year,
            first_solar.month,
            first_solar.day,
            12,
            0,
            0.0,
        };
        SplitJulianDate first_jd;
        if (!julian_day_split(first_clock, &first_jd)) {
            return TAIYIN_ERROR_INTERNAL;
        }
        const int64_t target_first_day = (first_jd + 0.5).day_number;

        std::vector<MonthIdentity> months;
        const double offsets[3] = {-220.0, 0.0, 220.0};
        for (std::size_t probe = 0u; probe < 3u; ++probe) {
            chinese_calendar::ChineseCalendarYear year;
            status = chinese_calendar::calcY(
                calendar, target_instant_utc + offsets[probe],
                &year, diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
            for (std::size_t i = 0u; i < year.month_count; ++i) {
                const chinese_calendar::ChineseCalendarMonth& month =
                    year.months[i];
                if (month.lunar_year != lunar.year) continue;
                MonthIdentity identity = {
                    month.first_civil_day_number,
                };
                bool duplicate = false;
                for (std::size_t j = 0u; j < months.size(); ++j) {
                    if (months[j].first_day == identity.first_day) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) months.push_back(identity);
            }
        }
        std::sort(months.begin(), months.end(), earlier_month);
        for (std::size_t i = 0u; i < months.size(); ++i) {
            if (months[i].first_day == target_first_day) {
                // Ziwei's flow-month model is bounded to one optional leap
                // month. Historical reform windows can expose a fourteenth
                // (or later) physical month in one labelled lunar year. The
                // established compatibility rule keeps those dates
                // chartable by collapsing the overflow occurrence onto the
                // thirteenth slot and treating it as leap month twelve. This
                // is a Ziwei-domain normalization only; the calendar layer
                // retains the exact historical month identity for roundtrip.
                if (i >= 13u) {
                    *out = 13u;
                    *out_collapsed_to_leap_twelve = true;
                    return TAIYIN_STATUS_OK;
                }
                *out = static_cast<uint8_t>(i + 1u);
                return TAIYIN_STATUS_OK;
            }
        }
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return TAIYIN_ERROR_INTERNAL;
    }
    return TAIYIN_ERROR_INTERNAL;
}

Status decode_pillars(
    const chinese_calendar::GanzhiFourPillars& packed,
    Pillars* out
) noexcept {
    if (out == NULL) return TAIYIN_ERROR_INVALID_ARGUMENT;
    const uint8_t values[4] = {
        packed.year, packed.month, packed.day, packed.hour,
    };
    Ganzhi decoded[4];
    for (std::size_t i = 0u; i < 4u; ++i) {
        if (values[i] == chinese_calendar::kInvalidGanzhi) {
            return TAIYIN_ERROR_INTERNAL;
        }
        decoded[i] = Ganzhi{
            static_cast<Stem>((values[i] >> 4) & 0x0fu),
            static_cast<Branch>(values[i] & 0x0fu),
        };
        if (!is_valid(decoded[i])) return TAIYIN_ERROR_INTERNAL;
    }
    *out = Pillars{decoded[0], decoded[1], decoded[2], decoded[3]};
    return TAIYIN_STATUS_OK;
}

}  // namespace

FlowResolutionOptions default_flow_resolution_options() noexcept {
    FlowResolutionOptions result;
    result.boundary = PillarBoundary::Lunar;
    result.rat_hour_mode =
        chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT;
    result.childhood_strategy = ChildhoodStrategy::Skip;
    return result;
}

Status resolve_flow_from_calendar(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const ResolvedBirth& birth,
    const NatalChart& natal,
    const SplitJulianDate& target_instant_utc,
    const CalendarDateTime& target_virtual_time,
    const FlowResolutionOptions& options,
    ResolvedFlow* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (calendar == NULL
        || out == NULL
        || !split_julian_date_is_finite(target_instant_utc)
        || !valid_options(options)
        || !same_birth_chart(birth, natal)
        || target_instant_utc < birth.facts.birth.instant_utc) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    chinese_calendar::GanzhiFourPillars packed;
    Status status = chinese_calendar::calculate_four_pillars(
        calendar,
        target_instant_utc,
        target_virtual_time,
        options.rat_hour_mode,
        &packed,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    Pillars target_pillars;
    status = decode_pillars(packed, &target_pillars);
    if (status != TAIYIN_STATUS_OK) return status;

    ResolvedFlow result = {};
    if (options.boundary == PillarBoundary::Lunar) {
        // Flow years retain the physical lunar-year label on both sides of
        // the comparison. Natal leap-month policy may map a birth leap month
        // into the following logical year; mixing that with a raw target year
        // would reject the birth instant itself as "before birth".
        result.effective_birth_year = birth.facts.lunar_date.year;
        chinese_calendar::LunarDate lunar;
        status = chinese_calendar::fromInstant(
            calendar, target_instant_utc, &lunar, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        result.effective_target_year = lunar.year;
        result.target_month = lunar.month == 13u ? 12u : lunar.month;
        result.target_day = lunar.day;
        result.target_month_is_leap = lunar.is_leap != 0u;
        bool collapsed_to_leap_twelve = false;
        status = resolve_lunar_month_sequence(
            calendar,
            target_instant_utc,
            lunar,
            &result.target_month_sequence,
            &collapsed_to_leap_twelve,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        if (collapsed_to_leap_twelve) {
            result.target_month = 12u;
            result.target_month_is_leap = true;
        }
    } else {
        status = effective_solar_year(
            birth.facts.birth.virtual_time.year,
            birth.facts.solar_term_pillars.year,
            &result.effective_birth_year);
        if (status != TAIYIN_STATUS_OK) return status;
        status = effective_solar_year(
            target_virtual_time.year,
            target_pillars.year,
            &result.effective_target_year);
        if (status != TAIYIN_STATUS_OK) return status;
        result.target_month = solar_month_from_branch(
            target_pillars.month.branch);
        result.target_month_sequence = result.target_month;
        result.target_month_is_leap = false;
        uint16_t solar_day = 0u;
        status = detail::calculate_solar_day_from_previous_jie(
            calendar,
            target_instant_utc,
            target_virtual_time,
            options.rat_hour_mode,
            &solar_day,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        if (solar_day > kMaxFlowDayIndex) return TAIYIN_ERROR_INTERNAL;
        result.target_day = static_cast<uint8_t>(solar_day);
    }
    result.target_hour_index = to_index(target_pillars.hour.branch);
    result.target_rat_hour_segment = rat_hour_segment(
        target_virtual_time, options.rat_hour_mode, target_pillars.hour.branch);

    const int64_t virtual_age64 =
        static_cast<int64_t>(result.effective_target_year)
        - result.effective_birth_year + 1;
    if (virtual_age64 < 1
        || virtual_age64 > std::numeric_limits<int32_t>::max()) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    status = make_decade_for_year(
        natal,
        result.effective_birth_year,
        result.effective_target_year,
        options.childhood_strategy,
        &result.decade);
    if (status != TAIYIN_STATUS_OK) return status;
    status = make_small_limit(
        natal,
        birth.facts.solar_term_pillars.year.branch,
        static_cast<int32_t>(virtual_age64),
        &result.small_limit);
    if (status != TAIYIN_STATUS_OK) return status;
    status = make_flow_year(
        natal, result.effective_target_year, &result.year);
    if (status != TAIYIN_STATUS_OK) return status;
    status = make_flow_month(
        natal,
        result.effective_target_year,
        result.target_month,
        result.target_month_sequence,
        result.target_month_is_leap,
        birth.facts.effective_lunar_month,
        birth.facts.solar_term_pillars.hour.branch,
        &result.month);
    if (status != TAIYIN_STATUS_OK) return status;
    status = make_flow_day(
        natal,
        result.month,
        result.target_day,
        target_pillars.day.stem,
        &result.day);
    if (status != TAIYIN_STATUS_OK) return status;
    status = make_flow_hour_from_pillar(natal, result.day, target_pillars.hour,
        result.target_rat_hour_segment, &result.hour);
    if (status != TAIYIN_STATUS_OK) return status;
    *out = result;
    return TAIYIN_STATUS_OK;
}

Status set_flow_stack_from_calendar(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const ResolvedBirth& birth,
    const SplitJulianDate& target_instant_utc,
    const CalendarDateTime& target_virtual_time,
    const FlowResolutionOptions& options,
    const CompiledRules& rules,
    Chart* chart,
    ResolvedFlow* out_resolution,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return set_flow_stack_through_from_calendar(
        calendar,
        birth,
        target_instant_utc,
        target_virtual_time,
        options,
        FlowLevel::Hour,
        rules,
        chart,
        out_resolution,
        diagnostic);
}

Status set_flow_stack_through_from_calendar(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const ResolvedBirth& birth,
    const SplitJulianDate& target_instant_utc,
    const CalendarDateTime& target_virtual_time,
    const FlowResolutionOptions& options,
    FlowLevel deepest_level,
    const CompiledRules& rules,
    Chart* chart,
    ResolvedFlow* out_resolution,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (chart == NULL || !is_valid(deepest_level)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    ResolvedFlow resolved;
    Status status = resolve_flow_from_calendar(
        calendar,
        birth,
        chart->natal,
        target_instant_utc,
        target_virtual_time,
        options,
        &resolved,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    try {
        Chart candidate;
        candidate.natal = chart->natal;
        const LimitCoordinate* limits[kFlowLevelCount] = {
            &resolved.decade.limit,
            &resolved.year.limit,
            &resolved.month.limit,
            &resolved.day.limit,
            &resolved.hour.limit,
        };
        for (std::size_t level = 0u;
             level <= static_cast<std::size_t>(to_index(deepest_level));
             ++level) {
            status = push_limit_flow_layer(&candidate, *limits[level], rules);
            if (status != TAIYIN_STATUS_OK) return status;
        }
        chart->flow_stack = std::move(candidate.flow_stack);
        if (out_resolution != NULL) *out_resolution = resolved;
        return TAIYIN_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return TAIYIN_ERROR_INTERNAL;
    }
}

Status step_flow_hour_target(
    const SplitJulianDate& current_instant_utc,
    const CalendarDateTime& current_virtual_time,
    int32_t rat_hour_mode,
    int direction,
    SplitJulianDate* out_instant_utc,
    CalendarDateTime* out_virtual_time,
    RatHourSegment* out_rat_hour_segment
) noexcept {
    if (!valid_rat_hour_mode(rat_hour_mode)
        || (direction != -1 && direction != 1)
        || out_instant_utc == NULL
        || out_virtual_time == NULL
        || out_rat_hour_segment == NULL) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate current_local;
    if (!encode_virtual_time(current_virtual_time, &current_local)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const bool split_rat = rat_hour_mode
        != chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT;
    const uint8_t slot_count = split_rat ? 13u : 12u;
    uint8_t slot = 0u;
    int logical_day_shift = 0;
    if (split_rat) {
        slot = current_virtual_time.hour >= 23
            ? 12u
            : static_cast<uint8_t>(((current_virtual_time.hour + 1) / 2) % 12);
    } else {
        slot = static_cast<uint8_t>(((current_virtual_time.hour + 1) / 2) % 12);
        if (current_virtual_time.hour >= 23) logical_day_shift = 1;
    }

    int next_slot = static_cast<int>(slot) + direction;
    if (next_slot < 0) {
        next_slot += slot_count;
        --logical_day_shift;
    } else if (next_slot >= slot_count) {
        next_slot -= slot_count;
        ++logical_day_shift;
    }

    CalendarDateTime day_start = current_virtual_time;
    day_start.hour = 0;
    day_start.minute = 0;
    day_start.second = 0.0;
    SplitJulianDate day_start_jd;
    SplitJulianDate target_day_jd;
    SplitJulianDate target_local;
    CalendarDateTime target_clock;
    if (!julian_day_split(day_start, &day_start_jd)
        || !add_days_to_split_jd(day_start_jd,
            static_cast<double>(logical_day_shift), &target_day_jd)
        || !reverse_julian_day_split(target_day_jd, &target_clock)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double center = hour_center(
        static_cast<uint8_t>(next_slot), split_rat);
    target_clock.hour = static_cast<int32_t>(center);
    target_clock.minute = center - target_clock.hour >= 0.5 ? 30 : 0;
    target_clock.second = 0.0;
    if (!julian_day_split(target_clock, &target_local)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double delta_days = days_between_split_jd(current_local, target_local);
    SplitJulianDate target_instant;
    if (!add_days_to_split_jd(
            current_instant_utc, delta_days, &target_instant)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_instant_utc = target_instant;
    *out_virtual_time = target_clock;
    *out_rat_hour_segment = split_rat
        ? (next_slot == 0 ? RatHourSegment::Early
            : next_slot == 12 ? RatHourSegment::Late
            : RatHourSegment::None)
        : (next_slot == 0 ? RatHourSegment::Unified
            : RatHourSegment::None);
    return TAIYIN_STATUS_OK;
}

Status step_flow_day_target(
    const SplitJulianDate& current_instant_utc,
    const CalendarDateTime& current_virtual_time,
    int direction,
    SplitJulianDate* out_instant_utc,
    CalendarDateTime* out_virtual_time
) noexcept {
    if (direction != -1 && direction != 1) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    return shift_target_by_local_days(
        current_instant_utc,
        current_virtual_time,
        static_cast<double>(direction),
        out_instant_utc,
        out_virtual_time);
}

}  // namespace ziwei
}  // namespace taiyin
