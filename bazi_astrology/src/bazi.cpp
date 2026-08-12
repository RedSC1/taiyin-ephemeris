#include "taiyin/bazi/bazi.h"

#include "bazi_rules_internal.h"
#include "chinese_calendar/solar_term_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace taiyin {
namespace bazi {
namespace {

bool valid_earth_palace_mode(int32_t mode) noexcept {
    return mode >= BaziEarthPalaceFireEarth && mode <= BaziEarthPalaceWaterEarth;
}

bool valid_qiyun_direction_mode(int32_t mode) noexcept {
    return mode == BaziQiYunDirectionYearStemGender;
}

bool valid_qiyun_time_model(int32_t model) noexcept {
    return model >= BaziQiYunTraditionalCalendar
        && model <= BaziQiYunTropicalYear;
}

bool valid_dayun_boundary_model(int32_t model) noexcept {
    return model >= BaziDaYunCivilYears
        && model <= BaziDaYunTropicalYears;
}

bool valid_siling_table_model(int32_t model) noexcept {
    return model >= BaziRenyuanSilingSanMingTongHui
        && model <= BaziRenyuanSilingCommon;
}

bool valid_siling_time_model(int32_t model) noexcept {
    return model >= BaziRenyuanSilingElapsed24Hours
        && model <= BaziRenyuanSilingLocalCivilDays;
}

double calendar_day_offset(
    const chinese_calendar::ChineseCalendarContext& context
) noexcept {
    if (context.config.day_boundary_mode
        == chinese_calendar::TAIYIN_CHINESE_CALENDAR_FIXED_UTC_OFFSET) {
        return static_cast<double>(context.config.utc_offset_minutes)
            / (24.0 * 60.0);
    }
    return context.config.calendar_meridian_deg / 360.0;
}

int64_t calendar_civil_day_number(
    const chinese_calendar::ChineseCalendarContext& context,
    const SplitJulianDate& jd_ut
) noexcept {
    return (jd_ut + calendar_day_offset(context) + 0.5).day_number;
}

bool valid_datetime(const CalendarDateTime& value) noexcept {
    SplitJulianDate ignored;
    return std::isfinite(value.second) && julian_day_split(value, &ignored);
}

SplitJulianDate invalid_split_jd() noexcept {
    return SplitJulianDate(0, std::numeric_limits<double>::quiet_NaN());
}

CalendarDateTime invalid_datetime() noexcept {
    return CalendarDateTime{
        0, 0, 0, 0, 0, std::numeric_limits<double>::quiet_NaN()};
}

bool add_calendar_components(
    const CalendarDateTime& origin,
    int64_t years,
    int64_t months,
    double remaining_days,
    CalendarDateTime* out,
    double* out_elapsed_days
) noexcept {
    if (!out || !out_elapsed_days || !valid_datetime(origin)
        || years < 0 || months < 0 || !std::isfinite(remaining_days)
        || remaining_days < 0.0) {
        return false;
    }
    const int64_t month_index = static_cast<int64_t>(origin.month) - 1 + months;
    const int64_t target_year = static_cast<int64_t>(origin.year)
        + years + month_index / 12;
    const int64_t target_month = month_index % 12 + 1;
    if (target_year < std::numeric_limits<int>::min()
        || target_year > std::numeric_limits<int>::max()) {
        return false;
    }
    const CalendarDateTime base = {
        static_cast<int>(target_year),
        static_cast<int>(target_month),
        1,
        origin.hour,
        origin.minute,
        origin.second,
    };
    SplitJulianDate origin_jd;
    SplitJulianDate result_jd;
    if (!julian_day_split(origin, &origin_jd)
        || !julian_day_split(base, &result_jd)
        || !add_days_to_split_jd(
            result_jd,
            static_cast<double>(origin.day - 1) + remaining_days,
            &result_jd)
        || !reverse_julian_day_split(result_jd, out)) {
        return false;
    }
    *out_elapsed_days = result_jd - origin_jd;
    return std::isfinite(*out_elapsed_days);
}

bool add_continuous_days(
    const SplitJulianDate& origin_jd_ut,
    const CalendarDateTime& origin_civil,
    double offset_days,
    SplitJulianDate* out_jd_ut,
    CalendarDateTime* out_civil
) noexcept {
    SplitJulianDate civil_jd;
    return out_jd_ut && out_civil && std::isfinite(offset_days)
        && split_julian_date_is_finite(origin_jd_ut)
        && julian_day_split(origin_civil, &civil_jd)
        && add_days_to_split_jd(origin_jd_ut, offset_days, out_jd_ut)
        && add_days_to_split_jd(civil_jd, offset_days, &civil_jd)
        && reverse_julian_day_split(civil_jd, out_civil);
}

bool add_civil_years(
    const SplitJulianDate& origin_jd_ut,
    const CalendarDateTime& origin_civil,
    int64_t years,
    SplitJulianDate* out_jd_ut,
    CalendarDateTime* out_civil
) noexcept {
    double elapsed_days = 0.0;
    return add_calendar_components(
            origin_civil, years, 0, 0.0, out_civil, &elapsed_days)
        && add_days_to_split_jd(origin_jd_ut, elapsed_days, out_jd_ut);
}

Status status_from_rule_result(int32_t result) noexcept {
    return result == 0 ? TAIYIN_STATUS_OK : TAIYIN_ERROR_INVALID_ARGUMENT;
}

Status status_from_collection_result(int32_t result) noexcept {
    if (result == 0) return TAIYIN_STATUS_OK;
    if (result == -1) return TAIYIN_ERROR_INVALID_ARGUMENT;
    if (result == -2) return TAIYIN_ERROR_OUT_OF_MEMORY;
    return TAIYIN_ERROR_INTERNAL;
}

Status fill_chart_rule_data(BaziChart* out, int32_t earth_palace_mode) noexcept {
    const uint8_t pillars[4] = {
        out->pillars.year,
        out->pillars.month,
        out->pillars.day,
        out->pillars.hour,
    };
    const uint8_t day_stem = static_cast<uint8_t>(out->pillars.day >> 4);
    for (size_t i = 0; i < 4; ++i) {
        Status status = status_from_rule_result(rules::ten_god(
            day_stem,
            static_cast<uint8_t>(pillars[i] >> 4),
            &out->visible_ten_gods[i]));
        if (status != TAIYIN_STATUS_OK) return status;
        status = status_from_rule_result(rules::hidden_stems(
            static_cast<uint8_t>(pillars[i] & 0x0fu),
            out->hidden_stems[i],
            &out->hidden_stem_count[i]));
        if (status != TAIYIN_STATUS_OK) return status;
        for (size_t j = 0; j < out->hidden_stem_count[i]; ++j) {
            status = status_from_rule_result(rules::ten_god(
                day_stem, out->hidden_stems[i][j], &out->hidden_ten_gods[i][j]));
            if (status != TAIYIN_STATUS_OK) return status;
        }
        status = get_life_stage(
            day_stem,
            static_cast<uint8_t>(pillars[i] & 0x0fu),
            earth_palace_mode,
            &out->life_stages[i]);
        if (status != TAIYIN_STATUS_OK) return status;
        status = chinese_calendar::get_nayin_id(pillars[i], &out->nayin_ids[i]);
        if (status != TAIYIN_STATUS_OK) return status;
    }
    return status_from_rule_result(rules::extra_pillars(
        out->pillars.year,
        out->pillars.month,
        out->pillars.day,
        out->pillars.hour,
        &out->extra.ming_gong,
        &out->extra.shen_gong,
        &out->extra.tai_yuan,
        &out->extra.tai_xi));
}

}  // namespace

BaziContextConfig::BaziContextConfig() noexcept
    : earth_palace_mode(BaziEarthPalaceFireEarth),
      qiyun_direction_mode(BaziQiYunDirectionYearStemGender),
      qiyun_time_model(BaziQiYunTraditionalCalendar),
      dayun_boundary_model(BaziDaYunCivilYears) {}

BaziContext::BaziContext() noexcept : config() {}

BaziExtraPillars::BaziExtraPillars() noexcept
    : ming_gong(kInvalidGanzhi),
      shen_gong(kInvalidGanzhi),
      tai_yuan(kInvalidGanzhi),
      tai_xi(kInvalidGanzhi) {}

BaziChart::BaziChart() noexcept
    : pillars(),
      extra(),
      hidden_stem_count{},
      hidden_stems{},
      visible_ten_gods{},
      hidden_ten_gods{},
      life_stages{},
      nayin_ids{} {
    std::memset(hidden_stems, kInvalidGanzhi, sizeof(hidden_stems));
    std::memset(hidden_ten_gods, kInvalidGanzhi, sizeof(hidden_ten_gods));
    std::memset(visible_ten_gods, kInvalidGanzhi, sizeof(visible_ten_gods));
    std::memset(life_stages, kInvalidGanzhi, sizeof(life_stages));
    std::memset(nayin_ids, kInvalidNaYin, sizeof(nayin_ids));
}

BaziQiYunResult::BaziQiYunResult() noexcept
    : direction(0),
      time_model(BaziQiYunTraditionalCalendar),
      reference_jie_index(0xffu),
      reserved{},
      jie_interval_days(std::numeric_limits<double>::quiet_NaN()),
      start_age_years(std::numeric_limits<double>::quiet_NaN()),
      offset_years(0),
      offset_months(0),
      offset_days(0),
      offset_hours(0),
      offset_minutes(0),
      offset_seconds(std::numeric_limits<double>::quiet_NaN()),
      reference_jie_jd_ut(invalid_split_jd()),
      start_jd_ut(invalid_split_jd()),
      start_civil_time(invalid_datetime()) {}

BaziDaYun::BaziDaYun() noexcept
    : index(0u),
      ganzhi(kInvalidGanzhi),
      reserved{},
      start_virtual_age(0),
      end_virtual_age(0),
      start_jd_ut(invalid_split_jd()),
      end_jd_ut(invalid_split_jd()),
      start_civil_time(invalid_datetime()),
      end_civil_time(invalid_datetime()) {}

BaziXiaoYun::BaziXiaoYun() noexcept
    : age(0), ganzhi(kInvalidGanzhi), reserved{0, 0, 0} {}

BaziRenyuanSilingSegment::BaziRenyuanSilingSegment() noexcept
    : stem_id(kInvalidGanzhi),
      origin_kind(BaziRenyuanSilingOriginStem),
      segment_index(0xffu),
      reserved(0u),
      start_day(std::numeric_limits<double>::quiet_NaN()),
      end_day(std::numeric_limits<double>::quiet_NaN()) {}

BaziRenyuanSilingResult::BaziRenyuanSilingResult() noexcept
    : table_model(BaziRenyuanSilingSanMingTongHui),
      time_model(BaziRenyuanSilingElapsed24Hours),
      month_branch_id(0xffu),
      stem_id(kInvalidGanzhi),
      origin_kind(BaziRenyuanSilingOriginStem),
      segment_index(0xffu),
      previous_jie_index(0xffu),
      reserved{},
      days_since_jie(std::numeric_limits<double>::quiet_NaN()),
      segment_start_day(std::numeric_limits<double>::quiet_NaN()),
      segment_end_day(std::numeric_limits<double>::quiet_NaN()),
      previous_jie_jd_ut(invalid_split_jd()) {}

BaziContextConfig default_context_config() noexcept {
    return BaziContextConfig();
}

Status initialize_context(
    BaziContext* out,
    const BaziContextConfig* config
) noexcept {
    if (!out || !config || !valid_earth_palace_mode(config->earth_palace_mode)
        || !valid_qiyun_direction_mode(config->qiyun_direction_mode)
        || !valid_qiyun_time_model(config->qiyun_time_model)
        || !valid_dayun_boundary_model(config->dayun_boundary_model)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = BaziContext();
    out->config = *config;
    return TAIYIN_STATUS_OK;
}

Status get_kong_wang(uint8_t ganzhi, uint8_t out_branches[2]) noexcept {
    return status_from_rule_result(
        rules::kong_wang(ganzhi, out_branches));
}

Status get_ten_god(
    uint8_t day_stem_id,
    uint8_t target_stem_id,
    uint8_t* out_ten_god_id
) noexcept {
    return status_from_rule_result(rules::ten_god(
        day_stem_id, target_stem_id, out_ten_god_id));
}

Status get_hidden_stems(
    uint8_t branch_id,
    uint8_t out_stems[kHiddenStemCapacity],
    uint8_t* out_count
) noexcept {
    return status_from_rule_result(rules::hidden_stems(
        branch_id, out_stems, out_count));
}

Status calculate_stem_relation(
    uint8_t stem_a,
    uint8_t stem_b,
    uint32_t* out_flags,
    uint8_t* out_combined_element_id
) noexcept {
    return status_from_rule_result(rules::stem_relation(
        stem_a, stem_b, out_flags, out_combined_element_id));
}

Status calculate_branch_relation(
    uint8_t branch_a,
    uint8_t branch_b,
    uint32_t* out_flags,
    uint8_t* out_combined_element_id
) noexcept {
    return status_from_rule_result(rules::branch_relation(
        branch_a, branch_b, out_flags, out_combined_element_id));
}

Status calculate_branch_triple_relation(
    uint8_t branch_a,
    uint8_t branch_b,
    uint8_t branch_c,
    uint32_t* out_flags,
    uint8_t* out_combined_element_id
) noexcept {
    return status_from_rule_result(rules::branch_triple_relation(
        branch_a, branch_b, branch_c, out_flags, out_combined_element_id));
}

Status get_life_stage(
    uint8_t stem_id,
    uint8_t branch_id,
    int32_t earth_palace_mode,
    uint8_t* out_life_stage_id
) noexcept {
    return status_from_rule_result(rules::life_stage(
        stem_id, branch_id, earth_palace_mode, out_life_stage_id));
}

Status calculate_chart(
    const BaziContext* context,
    const chinese_calendar::GanzhiFourPillars& pillars,
    BaziChart* out
) noexcept {
    if (!context || !out) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = BaziChart();
    out->pillars = pillars;
    return fill_chart_rule_data(out, context->config.earth_palace_mode);
}

Status calculate_flow_year(int32_t civil_year, uint8_t* out) noexcept {
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out = kInvalidGanzhi;
    int64_t index = (static_cast<int64_t>(civil_year) - 4) % 60;
    if (index < 0) index += 60;
    return chinese_calendar::make_ganzhi(
        static_cast<uint8_t>(index % 10),
        static_cast<uint8_t>(index % 12),
        out);
}

Status calculate_flow_month(
    uint8_t year_pillar,
    uint8_t month_branch_id,
    uint8_t* out
) noexcept {
    if (!out || month_branch_id >= 12u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = kInvalidGanzhi;
    uint8_t validated_year = kInvalidGanzhi;
    Status status = chinese_calendar::make_ganzhi(
        static_cast<uint8_t>(year_pillar >> 4),
        static_cast<uint8_t>(year_pillar & 0x0fu),
        &validated_year);
    if (status != TAIYIN_STATUS_OK) return status;
    const uint8_t month_index = static_cast<uint8_t>((month_branch_id + 10u) % 12u);
    return chinese_calendar::get_month_ganzhi(
        static_cast<uint8_t>(validated_year >> 4), month_index, out);
}

Status calculate_flow_day(
    const CalendarDateTime& civil_date,
    uint8_t* out
) noexcept {
    return chinese_calendar::calculate_day_pillar(civil_date, out);
}

Status calculate_flow_hour(
    uint8_t day_pillar,
    uint8_t hour_index,
    uint8_t* out
) noexcept {
    if (!out || hour_index >= 12u) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out = kInvalidGanzhi;
    uint8_t validated_day = kInvalidGanzhi;
    Status status = chinese_calendar::make_ganzhi(
        static_cast<uint8_t>(day_pillar >> 4),
        static_cast<uint8_t>(day_pillar & 0x0fu),
        &validated_day);
    if (status != TAIYIN_STATUS_OK) return status;
    return chinese_calendar::get_hour_ganzhi(
        static_cast<uint8_t>(validated_day >> 4), hour_index, out);
}

Status calculate_xiaoyun(
    const BaziChart* chart,
    int32_t direction,
    int32_t age,
    uint8_t* out
) noexcept {
    if (!chart || !out || (direction != -1 && direction != 1) || age < 1) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = kInvalidGanzhi;
    const int32_t delta = direction == 1 ? age : -age;
    return chinese_calendar::advance_ganzhi(chart->pillars.hour, delta, out);
}

Status fill_xiaoyun(
    const BaziChart* chart,
    int32_t direction,
    int32_t start_age,
    size_t requested_count,
    BaziXiaoYun* out,
    size_t capacity,
    size_t* out_count
) noexcept {
    if (out_count) *out_count = 0u;
    if (!chart || !out_count || (capacity != 0u && !out)
        || (direction != -1 && direction != 1) || start_age < 1
        || requested_count > static_cast<size_t>(std::numeric_limits<int32_t>::max())
        || (requested_count != 0u
            && static_cast<int64_t>(start_age)
                + static_cast<int64_t>(requested_count) - 1
                > std::numeric_limits<int32_t>::max())) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_count = requested_count;
    if (!out) {
        return capacity == 0u ? TAIYIN_STATUS_OK : TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (capacity < requested_count) return TAIYIN_ERROR_OUT_OF_MEMORY;
    for (size_t i = 0; i < requested_count; ++i) {
        BaziXiaoYun item;
        item.age = static_cast<uint32_t>(static_cast<int64_t>(start_age) + i);
        const Status status = calculate_xiaoyun(
            chart,
            direction,
            static_cast<int32_t>(item.age),
            &item.ganzhi);
        if (status != TAIYIN_STATUS_OK) {
            *out_count = 0u;
            return status;
        }
        out[i] = item;
    }
    return TAIYIN_STATUS_OK;
}

Status collect_chart_relations(
    const BaziChart* chart,
    uint32_t pillar_mask,
    uint32_t relation_mask,
    BaziRelation* out,
    size_t capacity,
    size_t* out_count
) noexcept {
    if (out_count) *out_count = 0u;
    if (!chart || !out_count || (capacity != 0u && !out)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const uint8_t pillars[8] = {
        chart->pillars.year,
        chart->pillars.month,
        chart->pillars.day,
        chart->pillars.hour,
        chart->extra.ming_gong,
        chart->extra.shen_gong,
        chart->extra.tai_yuan,
        chart->extra.tai_xi,
    };
    return status_from_collection_result(rules::collect_relations(
        pillars, pillar_mask, relation_mask, out, capacity, out_count));
}

Status collect_target_shen_sha(
    const BaziChart* chart,
    uint8_t target_ganzhi,
    int32_t target_kind,
    uint64_t* out_words,
    size_t word_capacity,
    size_t* out_word_count
) noexcept {
    if (out_word_count) *out_word_count = 0u;
    if (!chart || !out_word_count || (word_capacity != 0u && !out_words)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const uint8_t pillars[8] = {
        chart->pillars.year,
        chart->pillars.month,
        chart->pillars.day,
        chart->pillars.hour,
        chart->extra.ming_gong,
        chart->extra.shen_gong,
        chart->extra.tai_yuan,
        chart->extra.tai_xi,
    };
    return status_from_collection_result(rules::collect_shen_sha(
        pillars,
        target_ganzhi,
        target_kind,
        out_words,
        word_capacity,
        out_word_count));
}

Status collect_target_shen_sha_with_gender(
    const BaziChart* chart,
    uint8_t target_ganzhi,
    int32_t target_kind,
    int32_t gender,
    uint64_t* out_words,
    size_t word_capacity,
    size_t* out_word_count
) noexcept {
    if (out_word_count) *out_word_count = 0u;
    if (!chart || !out_word_count || (word_capacity != 0u && !out_words)
        || gender < BaziGenderFemale || gender > BaziGenderMale) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const uint8_t pillars[8] = {
        chart->pillars.year,
        chart->pillars.month,
        chart->pillars.day,
        chart->pillars.hour,
        chart->extra.ming_gong,
        chart->extra.shen_gong,
        chart->extra.tai_yuan,
        chart->extra.tai_xi,
    };
    return status_from_collection_result(
        rules::collect_shen_sha_with_gender(
            pillars,
            target_ganzhi,
            target_kind,
            gender,
            out_words,
            word_capacity,
            out_word_count));
}

Status calculate_qiyun(
    const BaziContext* context,
    const chinese_calendar::ChineseCalendarContext* calendar_context,
    const SplitJulianDate& birth_jd_ut,
    const CalendarDateTime& birth_civil_time,
    const BaziChart* chart,
    int32_t gender,
    BaziQiYunResult* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = BaziQiYunResult();
    if (!context || !calendar_context || !chart || !out
        || !split_julian_date_is_finite(birth_jd_ut)
        || !valid_datetime(birth_civil_time)
        || gender < BaziGenderFemale || gender > BaziGenderMale) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    int32_t direction = 0;
    Status status = status_from_rule_result(rules::qiyun_direction(
        chart->pillars.year,
        gender,
        context->config.qiyun_direction_mode,
        &direction));
    if (status != TAIYIN_STATUS_OK) return status;

    chinese_calendar::SolarTermEvent reference;
    status = chinese_calendar::getPrevJie(
        calendar_context, birth_jd_ut, &reference, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    double interval_days = birth_jd_ut - reference.jd_ut;
    if (!std::isfinite(interval_days)) return TAIYIN_ERROR_INTERNAL;

    if (std::fabs(interval_days)
            <= chinese_calendar::internal::kSolarTermRootEqualityToleranceDays) {
        interval_days = 0.0;
    } else if (direction > 0) {
        status = chinese_calendar::getNextJie(
            calendar_context, birth_jd_ut, &reference, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        interval_days = reference.jd_ut - birth_jd_ut;
    }
    if (!std::isfinite(interval_days)
        || interval_days < -chinese_calendar::internal::kSolarTermRootEqualityToleranceDays) {
        return TAIYIN_ERROR_INTERNAL;
    }
    interval_days = std::max(0.0, interval_days);

    const double scaled_days = interval_days * 120.0;
    const int32_t offset_years = static_cast<int32_t>(std::floor(scaled_days / 360.0));
    const double after_years = scaled_days - static_cast<double>(offset_years) * 360.0;
    const int32_t offset_months = static_cast<int32_t>(std::floor(after_years / 30.0));
    const double after_months = after_years - static_cast<double>(offset_months) * 30.0;
    const int32_t offset_days = static_cast<int32_t>(std::floor(after_months));
    double remaining_seconds = (after_months - static_cast<double>(offset_days))
        * SECONDS_PER_DAY;
    const int32_t offset_hours = static_cast<int32_t>(
        std::floor(remaining_seconds / 3600.0));
    remaining_seconds -= static_cast<double>(offset_hours) * 3600.0;
    const int32_t offset_minutes = static_cast<int32_t>(
        std::floor(remaining_seconds / 60.0));
    const double offset_seconds = remaining_seconds
        - static_cast<double>(offset_minutes) * 60.0;

    SplitJulianDate start_jd_ut;
    CalendarDateTime start_civil;
    if (context->config.qiyun_time_model == BaziQiYunTraditionalCalendar) {
        double elapsed_days = 0.0;
        if (!add_calendar_components(
                birth_civil_time,
                offset_years,
                offset_months,
                after_months,
                &start_civil, &elapsed_days)
            || !add_days_to_split_jd(birth_jd_ut, elapsed_days, &start_jd_ut)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
    } else {
        const double year_days = context->config.qiyun_time_model == BaziQiYunJulianYear
            ? DAYS_PER_JULIAN_YEAR
            : DAYS_PER_TROPICAL_YEAR;
        const double offset_days = interval_days * year_days / 3.0;
        if (!add_continuous_days(
                birth_jd_ut, birth_civil_time, offset_days,
                &start_jd_ut, &start_civil)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
    }

    out->direction = direction;
    out->time_model = context->config.qiyun_time_model;
    out->reference_jie_index = reference.index_from_winter_solstice;
    out->jie_interval_days = interval_days;
    out->start_age_years = interval_days / 3.0;
    out->offset_years = offset_years;
    out->offset_months = offset_months;
    out->offset_days = offset_days;
    out->offset_hours = offset_hours;
    out->offset_minutes = offset_minutes;
    out->offset_seconds = offset_seconds;
    out->reference_jie_jd_ut = reference.jd_ut;
    out->start_jd_ut = start_jd_ut;
    out->start_civil_time = start_civil;
    return TAIYIN_STATUS_OK;
}

Status fill_dayun(
    const BaziContext* context,
    const CalendarDateTime& birth_civil_time,
    const BaziChart* chart,
    const BaziQiYunResult* qiyun,
    size_t requested_count,
    BaziDaYun* out,
    size_t capacity,
    size_t* out_count
) noexcept {
    if (out_count) *out_count = 0u;
    if (!context || !chart || !qiyun || !out_count
        || (capacity != 0u && !out)
        || !valid_datetime(birth_civil_time)
        || !valid_datetime(qiyun->start_civil_time)
        || !split_julian_date_is_finite(qiyun->start_jd_ut)
        || (qiyun->direction != -1 && qiyun->direction != 1)
        || qiyun->time_model != context->config.qiyun_time_model
        || requested_count > std::numeric_limits<uint32_t>::max()) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_count = requested_count;
    if (!out) return capacity == 0u ? TAIYIN_STATUS_OK : TAIYIN_ERROR_INVALID_ARGUMENT;
    if (capacity < requested_count) return TAIYIN_ERROR_OUT_OF_MEMORY;

    const double boundary_year_days = context->config.dayun_boundary_model
            == BaziDaYunJulianYears
        ? DAYS_PER_JULIAN_YEAR
        : DAYS_PER_TROPICAL_YEAR;
    for (size_t i = 0; i < requested_count; ++i) {
        BaziDaYun item;
        item.index = static_cast<uint32_t>(i + 1u);
        Status status = status_from_rule_result(rules::dayun_ganzhi(
            chart->pillars.month,
            qiyun->direction,
            item.index,
            &item.ganzhi));
        if (status != TAIYIN_STATUS_OK) return status;

        const int64_t start_years = static_cast<int64_t>(i) * 10;
        const int64_t end_years = static_cast<int64_t>(i + 1u) * 10;
        bool valid = false;
        if (context->config.dayun_boundary_model == BaziDaYunCivilYears) {
            valid = add_civil_years(
                    qiyun->start_jd_ut, qiyun->start_civil_time, start_years,
                    &item.start_jd_ut, &item.start_civil_time)
                && add_civil_years(
                    qiyun->start_jd_ut, qiyun->start_civil_time, end_years,
                    &item.end_jd_ut, &item.end_civil_time);
        } else {
            valid = add_continuous_days(
                    qiyun->start_jd_ut, qiyun->start_civil_time,
                    static_cast<double>(start_years) * boundary_year_days,
                    &item.start_jd_ut, &item.start_civil_time)
                && add_continuous_days(
                    qiyun->start_jd_ut, qiyun->start_civil_time,
                    static_cast<double>(end_years) * boundary_year_days,
                    &item.end_jd_ut, &item.end_civil_time);
        }
        if (!valid) return TAIYIN_ERROR_INVALID_ARGUMENT;
        item.start_virtual_age = item.start_civil_time.year - birth_civil_time.year + 1;
        item.end_virtual_age = item.start_virtual_age + 9;
        out[i] = item;
    }
    return TAIYIN_STATUS_OK;
}

Status get_renyuan_siling_segments(
    uint8_t month_branch_id,
    int32_t table_model,
    BaziRenyuanSilingSegment* out,
    size_t capacity,
    size_t* out_count
) noexcept {
    if (out_count) *out_count = 0u;
    if (!out_count || (capacity != 0u && !out)
        || month_branch_id >= 12u
        || !valid_siling_table_model(table_model)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    uint8_t segment_count = 0u;
    uint8_t stem_id = kInvalidGanzhi;
    uint8_t origin_kind = BaziRenyuanSilingOriginStem;
    double duration_days = 0.0;
    Status status = status_from_rule_result(rules::siling_segment(
        table_model, month_branch_id, 0u, &segment_count, &stem_id,
        &origin_kind, &duration_days));
    if (status != TAIYIN_STATUS_OK) return status;
    *out_count = segment_count;
    if (!out) return capacity == 0u
        ? TAIYIN_STATUS_OK : TAIYIN_ERROR_INVALID_ARGUMENT;
    if (capacity < segment_count) return TAIYIN_ERROR_OUT_OF_MEMORY;

    double start_day = 0.0;
    for (uint8_t i = 0u; i < segment_count; ++i) {
        if (i != 0u) {
            status = status_from_rule_result(rules::siling_segment(
                table_model, month_branch_id, i, &segment_count, &stem_id,
                &origin_kind, &duration_days));
            if (status != TAIYIN_STATUS_OK) return status;
        }
        BaziRenyuanSilingSegment segment;
        segment.stem_id = stem_id;
        segment.origin_kind = origin_kind;
        segment.segment_index = i;
        segment.start_day = start_day;
        segment.end_day = start_day + duration_days;
        out[i] = segment;
        start_day = segment.end_day;
    }
    return TAIYIN_STATUS_OK;
}

Status calculate_renyuan_siling(
    const chinese_calendar::ChineseCalendarContext* calendar_context,
    const SplitJulianDate& instant_jd_ut,
    const BaziChart* chart,
    int32_t table_model,
    int32_t time_model,
    BaziRenyuanSilingResult* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = BaziRenyuanSilingResult();
    if (!calendar_context || !chart || !out
        || !split_julian_date_is_finite(instant_jd_ut)
        || !valid_siling_table_model(table_model)
        || !valid_siling_time_model(time_model)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    chinese_calendar::SolarTermEvent previous_jie;
    Status status = chinese_calendar::getPrevJie(
        calendar_context, instant_jd_ut, &previous_jie, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    double day_coordinate = 0.0;
    if (time_model == BaziRenyuanSilingElapsed24Hours) {
        day_coordinate = instant_jd_ut - previous_jie.jd_ut;
        const double nearest_integer_day = std::round(day_coordinate);
        if (std::fabs(day_coordinate - nearest_integer_day)
            <= chinese_calendar::internal::kSolarTermRootEqualityToleranceDays) {
            day_coordinate = nearest_integer_day;
        }
    } else {
        day_coordinate = static_cast<double>(
            calendar_civil_day_number(*calendar_context, instant_jd_ut)
            - calendar_civil_day_number(*calendar_context, previous_jie.jd_ut));
    }
    if (!std::isfinite(day_coordinate) || day_coordinate < 0.0) {
        return TAIYIN_ERROR_INTERNAL;
    }

    const uint8_t month_branch_id = static_cast<uint8_t>(
        chart->pillars.month & 0x0fu);
    uint8_t segment_index = 0xffu;
    uint8_t stem_id = kInvalidGanzhi;
    uint8_t origin_kind = BaziRenyuanSilingOriginStem;
    double segment_start_day = std::numeric_limits<double>::quiet_NaN();
    double segment_end_day = std::numeric_limits<double>::quiet_NaN();
    status = status_from_rule_result(rules::select_siling(
        table_model, month_branch_id, day_coordinate, &segment_index,
        &stem_id, &origin_kind, &segment_start_day, &segment_end_day));
    if (status != TAIYIN_STATUS_OK) return status;

    out->table_model = table_model;
    out->time_model = time_model;
    out->month_branch_id = month_branch_id;
    out->stem_id = stem_id;
    out->origin_kind = origin_kind;
    out->segment_index = segment_index;
    out->previous_jie_index = previous_jie.index_from_winter_solstice;
    out->days_since_jie = day_coordinate;
    out->segment_start_day = segment_start_day;
    out->segment_end_day = segment_end_day;
    out->previous_jie_jd_ut = previous_jie.jd_ut;
    return TAIYIN_STATUS_OK;
}

}  // namespace bazi
}  // namespace taiyin
