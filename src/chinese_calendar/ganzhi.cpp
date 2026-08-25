#include "taiyin/chinese_calendar/ganzhi.h"

#include "chinese_calendar/ganzhi_rules_internal.h"
#include "chinese_calendar/solar_term_internal.h"

#include <cmath>
#include <limits>

namespace taiyin {
namespace chinese_calendar {
namespace {

constexpr int32_t kJiaZiYear = 1984;
constexpr int64_t kJ2000DayNumber = 2451545;
constexpr uint8_t kLiChunIndexFromVernalEquinox = 21u;

bool valid_rat_hour_mode(int32_t mode) noexcept {
    return mode >= TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT
        && mode <= TAIYIN_GANZHI_RAT_HOUR_TOMORROW_GAN;
}

int32_t positive_mod(int32_t value, int32_t divisor) noexcept {
    const int32_t result = value % divisor;
    return result < 0 ? result + divisor : result;
}

bool valid_datetime(const CalendarDateTime& value) noexcept {
    SplitJulianDate jd;
    return std::isfinite(value.second) && julian_day_split(value, &jd);
}

Status status_from_rule_result(int32_t result) noexcept {
    return result == 0 ? TAIYIN_STATUS_OK : TAIYIN_ERROR_INVALID_ARGUMENT;
}

bool use_historical_pillar_terms(
    const ChineseCalendarContext& context
) noexcept {
    switch (context.config.pillar_historical_mode) {
    case TAIYIN_GANZHI_PILLAR_HISTORICAL_ON:
        return true;
    case TAIYIN_GANZHI_PILLAR_HISTORICAL_OFF:
        return false;
    default:
        return context.config.mode
            == TAIYIN_CHINESE_CALENDAR_CHINA_STANDARD_HISTORICAL;
    }
}

// The historical profile's civil days are proleptic fixed UTC+08, so the
// assigned day's boundary instant is D - 5/6 in JD(UT).
constexpr double kProfileDayOffsetDays = 480.0 / 1440.0;

int64_t profile_civil_day(const SplitJulianDate& jd_ut) noexcept {
    return (jd_ut + kProfileDayOffsetDays + 0.5).day_number;
}

// In historical pillar mode a term boundary is the assigned civil day's
// 00:00 (UTC+08), not the modern-ephemeris instant; a pseudo-precise
// ancient term time would be deceptive.  Returns false when the profile did
// not supply this term's day, leaving the precise instant in place.
bool historical_pillar_boundary(
    const ChineseCalendarContext& context,
    const SolarTermEvent& term,
    SplitJulianDate* out_boundary
) noexcept {
    int64_t assigned_day = 0;
    if (!use_historical_pillar_terms(context)
        || !internal::historical_profile_term_day(
            context, term, &assigned_day)) {
        return false;
    }
    runtime::set_operation_flag(
        &context.astronomy, kResultFlagHistoricalPillarTermsApplied);
    return normalize_split_julian_date(
        assigned_day - 1, 0.5 - kProfileDayOffsetDays, out_boundary);
}

Status calculate_year_pillar(
    const ChineseCalendarContext& context,
    const SplitJulianDate& instant_utc,
    const CalendarDateTime& virtual_time,
    uint8_t* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    SolarTermEvent li_chun;
    Status status = getSpecificJieQi(
        &context,
        virtual_time.year,
        kLiChunIndexFromVernalEquinox,
        &li_chun,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    SplitJulianDate boundary = li_chun.jd_ut;
    (void) historical_pillar_boundary(context, li_chun, &boundary);
    const double difference_days = instant_utc - boundary;
    if (!std::isfinite(difference_days)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    const int32_t pillar_year = virtual_time.year
        + (difference_days < -internal::kSolarTermRootEqualityToleranceDays ? -1 : 0);
    const int32_t index = positive_mod(pillar_year - kJiaZiYear, 60);
    return make_ganzhi(
        static_cast<uint8_t>(index % 10),
        static_cast<uint8_t>(index % 12),
        out);
}

Status calculate_month_pillar(
    const ChineseCalendarContext& context,
    const SplitJulianDate& instant_utc,
    uint8_t year_pillar,
    uint8_t* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    // Historical pillar mode judges by assigned-day granularity, so select
    // the candidate jie from one day ahead: a term whose precise instant is
    // later today still starts its pillar month at today's 00:00.
    SplitJulianDate query_instant = instant_utc;
    if (use_historical_pillar_terms(context)) {
        if (!add_days_to_split_jd(instant_utc, 1.0, &query_instant)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
    }
    SolarTermEvent previous_jie;
    Status status = getPrevJie(&context, query_instant, &previous_jie, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    SplitJulianDate boundary = previous_jie.jd_ut;
    const bool historical =
        historical_pillar_boundary(context, previous_jie, &boundary);
    // When the profile answered, the pillar month turns at the assigned
    // day's 00:00; otherwise the precise instant rules, and a candidate
    // found via the +1 day query shift may still lie in the future.
    const bool candidate_in_future = historical
        ? profile_civil_day(boundary) > profile_civil_day(instant_utc)
        : previous_jie.jd_ut - instant_utc
            > internal::kSolarTermRootEqualityToleranceDays;
    if (candidate_in_future) {
        SplitJulianDate step_back;
        if (!add_days_to_split_jd(previous_jie.jd_ut, -10.0, &step_back)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        status = getPrevJie(&context, step_back, &previous_jie, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        SplitJulianDate ignored;
        (void) historical_pillar_boundary(context, previous_jie, &ignored);
    }

    const uint8_t index = previous_jie.index_from_winter_solstice;
    if ((index & 1u) == 0u) return TAIYIN_ERROR_INTERNAL;
    // The rule uses 0=Yin, ..., 11=Chou.
    const uint8_t month_index = static_cast<uint8_t>(((index + 21u) / 2u) % 12u);
    return get_month_ganzhi(static_cast<uint8_t>(year_pillar >> 4), month_index, out);
}

Status calculate_day_and_hour_pillars(
    const CalendarDateTime& virtual_time,
    int32_t rat_hour_mode,
    uint8_t* out_day,
    uint8_t* out_hour
) noexcept {
    if (!out_day || !out_hour || !valid_rat_hour_mode(rat_hour_mode)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    CalendarDateTime day_anchor = virtual_time;
    const bool late_rat_hour = virtual_time.hour >= 23;
    if (late_rat_hour && rat_hour_mode == TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT) {
        SplitJulianDate midnight;
        CalendarDateTime next_day;
        day_anchor.hour = 0;
        day_anchor.minute = 0;
        day_anchor.second = 0.0;
        if (!julian_day_split(day_anchor, &midnight)
            || !add_days_to_split_jd(midnight, 1.0, &midnight)
            || !reverse_julian_day_split(midnight, &next_day)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        day_anchor = next_day;
    }
    Status status = calculate_day_pillar(day_anchor, out_day);
    if (status != TAIYIN_STATUS_OK) return status;

    const uint8_t hour_branch = static_cast<uint8_t>(((virtual_time.hour + 1) / 2) % 12);
    uint8_t hour_day_stem = static_cast<uint8_t>(*out_day >> 4);
    if (late_rat_hour && rat_hour_mode == TAIYIN_GANZHI_RAT_HOUR_TOMORROW_GAN) {
        uint8_t tomorrow;
        status = advance_ganzhi(*out_day, 1, &tomorrow);
        if (status != TAIYIN_STATUS_OK) return status;
        hour_day_stem = static_cast<uint8_t>(tomorrow >> 4);
    }
    return get_hour_ganzhi(hour_day_stem, hour_branch, out_hour);
}

}  // namespace

GanzhiFourPillars::GanzhiFourPillars() noexcept
    : year(kInvalidGanzhi),
      month(kInvalidGanzhi),
      day(kInvalidGanzhi),
      hour(kInvalidGanzhi) {}

Status normalize_chart_virtual_time(
    const CalendarDateTime& virtual_time,
    CalendarDateTime* out
) noexcept {
    if (!out || !valid_datetime(virtual_time)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (virtual_time.minute != 0 && virtual_time.minute != 59) {
        *out = virtual_time;
        return TAIYIN_STATUS_OK;
    }

    CalendarDateTime midnight = virtual_time;
    midnight.hour = 0;
    midnight.minute = 0;
    midnight.second = 0.0;
    SplitJulianDate source;
    SplitJulianDate midnight_jd;
    if (!julian_day_split(virtual_time, &source)
        || !julian_day_split(midnight, &midnight_jd)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    // Include the following midnight so 23:59:59.999... produced from an
    // exact next-day boundary is normalized into the next civil date.  Do not
    // use a broad tolerance: recognize only the exact binary spellings that
    // the split and scalar JD decoders produce for each civil-hour boundary.
    for (int hour = 0; hour <= 24; ++hour) {
        SplitJulianDate boundary;
        if (!add_days_to_split_jd(
                midnight_jd, static_cast<double>(hour) / 24.0,
                &boundary)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        CalendarDateTime canonical = midnight;
        if (hour < 24) {
            canonical.hour = hour;
        } else if (!reverse_julian_day_split(boundary, &canonical)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        canonical.minute = 0;
        canonical.second = 0.0;

        CalendarDateTime split_spelling;
        SplitJulianDate split_spelling_jd;
        if (!reverse_julian_day_split(boundary, &split_spelling)
            || !julian_day_split(split_spelling, &split_spelling_jd)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }

        bool scalar_matches = false;
        const double scalar_boundary = split_julian_date_to_double(boundary);
        if (std::isfinite(scalar_boundary)
            && scalar_boundary + 0.5
                >= static_cast<double>(std::numeric_limits<int>::min())
            && scalar_boundary + 0.5
                <= static_cast<double>(std::numeric_limits<int>::max())) {
            const CalendarDateTime scalar_spelling =
                reverse_julian_day(scalar_boundary);
            SplitJulianDate scalar_spelling_jd;
            if (!julian_day_split(scalar_spelling, &scalar_spelling_jd)) {
                return TAIYIN_ERROR_INVALID_ARGUMENT;
            }
            scalar_matches = source == scalar_spelling_jd;
        }

        if (source == boundary
            || source == split_spelling_jd
            || scalar_matches) {
            *out = canonical;
            return TAIYIN_STATUS_OK;
        }
    }

    *out = virtual_time;
    return TAIYIN_STATUS_OK;
}

Status make_ganzhi(uint8_t stem_id, uint8_t branch_id, uint8_t* out) noexcept {
    return status_from_rule_result(rules::make(stem_id, branch_id, out));
}

Status advance_ganzhi(uint8_t value, int32_t delta, uint8_t* out) noexcept {
    return status_from_rule_result(rules::advance(value, delta, out));
}

Status get_month_ganzhi(
    uint8_t year_stem_id,
    uint8_t month_index,
    uint8_t* out
) noexcept {
    return status_from_rule_result(
        rules::month(year_stem_id, month_index, out));
}

Status get_hour_ganzhi(
    uint8_t day_stem_id,
    uint8_t hour_index,
    uint8_t* out
) noexcept {
    return status_from_rule_result(
        rules::hour(day_stem_id, hour_index, out));
}

Status calculate_day_pillar(
    const CalendarDateTime& civil_date,
    uint8_t* out
) noexcept {
    if (!out || !valid_datetime(civil_date)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    CalendarDateTime noon_date = civil_date;
    noon_date.hour = 12;
    noon_date.minute = 0;
    noon_date.second = 0.0;
    SplitJulianDate noon;
    if (!julian_day_split(noon_date, &noon)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double days_from_j2000 = days_between_split_jd(
        SplitJulianDate(kJ2000DayNumber, 0.0), noon);
    if (!std::isfinite(days_from_j2000)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const int64_t day_offset = static_cast<int64_t>(std::floor(days_from_j2000));
    const int32_t day_index = positive_mod(
        static_cast<int32_t>(day_offset % 60) - 6, 60);
    return make_ganzhi(
        static_cast<uint8_t>(day_index % 10),
        static_cast<uint8_t>(day_index % 12),
        out);
}

Status get_nayin_element(uint8_t ganzhi, uint8_t* out_element_id) noexcept {
    return status_from_rule_result(
        rules::nayin_element(ganzhi, out_element_id));
}

Status get_nayin_id(uint8_t ganzhi, uint8_t* out_nayin_id) noexcept {
    return status_from_rule_result(rules::nayin_id(ganzhi, out_nayin_id));
}

Status calculate_four_pillars(
    const ChineseCalendarContext* context,
    const SplitJulianDate& instant_utc,
    const CalendarDateTime& virtual_time,
    int32_t rat_hour_mode,
    GanzhiFourPillars* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(instant_utc)
        || !valid_datetime(virtual_time) || !valid_rat_hour_mode(rat_hour_mode)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    CalendarDateTime normalized_virtual_time;
    Status status = normalize_chart_virtual_time(
        virtual_time, &normalized_virtual_time);
    if (status != TAIYIN_STATUS_OK) return status;
    *out = GanzhiFourPillars();
    status = calculate_year_pillar(
        *context, instant_utc, normalized_virtual_time,
        &out->year, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = calculate_month_pillar(
        *context, instant_utc, out->year, &out->month, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    return calculate_day_and_hour_pillars(
        normalized_virtual_time, rat_hour_mode, &out->day, &out->hour);
}

}  // namespace chinese_calendar
}  // namespace taiyin
