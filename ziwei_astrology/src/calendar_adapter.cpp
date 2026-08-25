#include "taiyin/ziwei/calendar_adapter.h"
#include "calendar_adapter_internal.h"

#include <cstdint>

namespace taiyin {
namespace ziwei {
namespace {

bool valid_options(const BirthResolutionOptions& options) noexcept {
    return options.rat_hour_mode
            >= chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT
        && options.rat_hour_mode
            <= chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TOMORROW_GAN
        && static_cast<uint8_t>(options.leap_month_strategy)
            <= static_cast<uint8_t>(LeapMonthStrategy::SplitAfterFifteenth)
        && static_cast<uint8_t>(
            options.anchor_options.rules.wu_hu_dun_year_boundary)
            <= static_cast<uint8_t>(PillarBoundary::Lunar)
        && static_cast<uint8_t>(
            options.anchor_options.rules.sihua_year_boundary)
            <= static_cast<uint8_t>(PillarBoundary::Lunar)
        && static_cast<uint8_t>(
            options.anchor_options.rules.body_master_year_boundary)
            <= static_cast<uint8_t>(PillarBoundary::Lunar)
        && static_cast<uint8_t>(options.anchor_options.chart_mode)
            <= static_cast<uint8_t>(ZiweiChartMode::RenPan);
}

int normalized(int64_t value, int modulus) noexcept {
    const int64_t result = value % modulus;
    return static_cast<int>(result < 0 ? result + modulus : result);
}

bool decode_ganzhi(uint8_t packed, Ganzhi* out) noexcept {
    if (out == NULL || packed == chinese_calendar::kInvalidGanzhi) return false;
    const Ganzhi value = {
        static_cast<Stem>((packed >> 4) & 0x0fu),
        static_cast<Branch>(packed & 0x0fu),
    };
    if (!is_valid(value)) return false;
    *out = value;
    return true;
}

bool decode_pillars(
    const chinese_calendar::GanzhiFourPillars& packed,
    Pillars* out
) noexcept {
    return out != NULL
        && decode_ganzhi(packed.year, &out->year)
        && decode_ganzhi(packed.month, &out->month)
        && decode_ganzhi(packed.day, &out->day)
        && decode_ganzhi(packed.hour, &out->hour);
}

bool make_lunar_pillars(
    int32_t effective_year,
    uint8_t effective_month,
    const Pillars& solar,
    Pillars* out
) noexcept {
    if (out == NULL || effective_month < 1u || effective_month > 12u) {
        return false;
    }
    const int year_stem = normalized(static_cast<int64_t>(effective_year) + 6, 10);
    const int year_branch = normalized(static_cast<int64_t>(effective_year) + 8, 12);
    const int month_stem = ((year_stem % 5) * 2 + 2
        + effective_month - 1u) % 10;
    const int month_branch = (effective_month + 1u) % 12u;
    Pillars result;
    result.year = Ganzhi{
        static_cast<Stem>(year_stem), static_cast<Branch>(year_branch),
    };
    result.month = Ganzhi{
        static_cast<Stem>(month_stem), static_cast<Branch>(month_branch),
    };
    result.day = solar.day;
    result.hour = solar.hour;
    if (!is_valid(result)) return false;
    *out = result;
    return true;
}

}  // namespace

BirthResolutionOptions default_birth_resolution_options() noexcept {
    BirthResolutionOptions result;
    result.rat_hour_mode =
        chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT;
    result.leap_month_strategy = LeapMonthStrategy::SplitAfterFifteenth;
    result.anchor_options = default_anchor_options();
    return result;
}

Status resolve_birth_from_calendar(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const SplitJulianDate& instant_utc,
    const CalendarDateTime& virtual_time,
    Gender gender,
    const BirthResolutionOptions& options,
    ResolvedBirth* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (calendar == NULL
        || out == NULL
        || !split_julian_date_is_finite(instant_utc)
        || !is_valid(gender)
        || !valid_options(options)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    CalendarDateTime normalized_virtual_time;
    Status status = chinese_calendar::normalize_chart_virtual_time(
        virtual_time, &normalized_virtual_time);
    if (status != TAIYIN_STATUS_OK) return status;

    ResolvedBirth result = {};
    result.leap_month_strategy = options.leap_month_strategy;
    result.facts.birth.instant_utc = instant_utc;
    result.facts.birth.virtual_time = normalized_virtual_time;
    result.facts.birth.gender = gender;

    // Only the Ziwei lunar date label follows the caller-selected virtual
    // clock. Solar-term boundaries and four-pillar astronomy below continue
    // to use the physical UTC instant.
    chinese_calendar::LunarDate lunar;
    status = detail::resolve_logical_lunar_date(
        calendar, normalized_virtual_time,
        options.rat_hour_mode, &lunar, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    result.facts.lunar_date.year = lunar.year;
    result.facts.lunar_date.historical_year = lunar.historical_year;
    result.facts.lunar_date.month = lunar.month;
    result.facts.lunar_date.day = lunar.day;
    result.facts.lunar_date.is_leap = lunar.is_leap;
    result.facts.lunar_date.month_name = lunar.month_name;

    LunarDateFacts effective_lunar_date = result.facts.lunar_date;
    effective_lunar_date.year = result.facts.lunar_date.historical_year;
    status = resolve_effective_lunar_month(
        effective_lunar_date,
        options.leap_month_strategy,
        &result.facts.effective_lunar_year,
        &result.facts.effective_lunar_month);
    if (status != TAIYIN_STATUS_OK) return status;

    chinese_calendar::GanzhiFourPillars packed;
    status = chinese_calendar::calculate_four_pillars(
        calendar,
        instant_utc,
        normalized_virtual_time,
        options.rat_hour_mode,
        &packed,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!decode_pillars(packed, &result.facts.solar_term_pillars)
        || !make_lunar_pillars(
            result.facts.effective_lunar_year,
            result.facts.effective_lunar_month,
            result.facts.solar_term_pillars,
            &result.facts.lunar_pillars)) {
        return TAIYIN_ERROR_INTERNAL;
    }

    status = detail::calculate_solar_day_from_previous_jie(
        calendar,
        instant_utc,
        normalized_virtual_time,
        options.rat_hour_mode,
        &result.facts.solar_day_from_previous_jie,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    status = compute_anchors(
        result.facts,
        options.anchor_options,
        &result.anchors,
        &result.body_palace);
    if (status != TAIYIN_STATUS_OK) return status;
    *out = result;
    return TAIYIN_STATUS_OK;
}

Status make_natal_chart_from_calendar(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const SplitJulianDate& instant_utc,
    const CalendarDateTime& virtual_time,
    Gender gender,
    const BirthResolutionOptions& options,
    const CompiledRules& rules,
    NatalChart* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out == NULL) return TAIYIN_ERROR_INVALID_ARGUMENT;
    ResolvedBirth resolved;
    const Status status = resolve_birth_from_calendar(
        calendar,
        instant_utc,
        virtual_time,
        gender,
        options,
        &resolved,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    return make_natal_chart(
        resolved.facts,
        resolved.anchors,
        resolved.body_palace,
        options.anchor_options.rules,
        rules,
        out);
}

}  // namespace ziwei
}  // namespace taiyin
