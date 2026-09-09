#include "taiyin/ziwei/reverse_lookup.h"
#include "calendar_adapter_internal.h"

#include "taiyin/ziwei/flow_calendar_adapter.h"

#include <new>
#include <utility>

namespace taiyin {
namespace ziwei {
namespace {

bool valid_requested_branch(int32_t value) noexcept {
    return value == kReverseUnspecified
        || (value >= 0 && value < static_cast<int32_t>(kBranchCount));
}

bool valid_query(const Tier1ReverseQuery& query) noexcept {
    return valid_requested_branch(query.lucun_branch)
        && valid_requested_branch(query.hongluan_branch)
        && valid_requested_branch(query.zuofu_branch)
        && valid_requested_branch(query.youbi_branch)
        && valid_requested_branch(query.wenchang_branch)
        && valid_requested_branch(query.wenqu_branch)
        && valid_requested_branch(query.santai_branch)
        && valid_requested_branch(query.bazuo_branch)
        && valid_requested_branch(query.ziwei_branch);
}

bool matches_position(
    const NatalChart& chart,
    StarId star,
    int32_t expected
) noexcept {
    if (expected == kReverseUnspecified) return true;
    if (star == kInvalidStarId) return false;
    for (std::size_t branch = 0u; branch < kBranchCount; ++branch) {
        if (chart.palaces[branch].stars.test(star)) {
            return static_cast<int32_t>(branch) == expected;
        }
    }
    return false;
}

bool matches_query(
    const NatalChart& chart,
    const StarRegistry& registry,
    const Tier1ReverseQuery& query
) noexcept {
    const char* const keys[9] = {
        "lucun", "hongluan", "zuofu", "youbi", "wenchang", "wenqu",
        "santai", "bazuo", "ziwei",
    };
    const int32_t expected[9] = {
        query.lucun_branch, query.hongluan_branch, query.zuofu_branch,
        query.youbi_branch, query.wenchang_branch, query.wenqu_branch,
        query.santai_branch, query.bazuo_branch, query.ziwei_branch,
    };
    for (std::size_t i = 0u; i < 9u; ++i) {
        if (expected[i] == kReverseUnspecified) continue;
        StarId star = kInvalidStarId;
        if (!registry.find(keys[i], &star)
            || !matches_position(chart, star, expected[i])) {
            return false;
        }
    }
    return true;
}

bool request_has_constraint(const Tier1ReverseQuery& query) noexcept {
    return query.lucun_branch != kReverseUnspecified
        || query.hongluan_branch != kReverseUnspecified
        || query.zuofu_branch != kReverseUnspecified
        || query.youbi_branch != kReverseUnspecified
        || query.wenchang_branch != kReverseUnspecified
        || query.wenqu_branch != kReverseUnspecified
        || query.santai_branch != kReverseUnspecified
        || query.bazuo_branch != kReverseUnspecified
        || query.ziwei_branch != kReverseUnspecified;
}

bool same_placement(const NatalChart& a, const NatalChart& b) noexcept {
    if (a.anchors.bureau != b.anchors.bureau
        || a.anchors.ziwei != b.anchors.ziwei || a.anchors.tianfu != b.anchors.tianfu
        || a.anchors.palace_positions != b.anchors.palace_positions
        || a.body_palace != b.body_palace || a.life_master != b.life_master
        || a.body_master != b.body_master || a.palace_stems != b.palace_stems
        || a.transformations.marks_by_star != b.transformations.marks_by_star) return false;
    for (std::size_t i = 0; i < kBranchCount; ++i) {
        if (a.palaces[i].stars != b.palaces[i].stars) return false;
    }
    return true;
}

RatHourSegment segment_for_clock(
    const CalendarDateTime& value,
    int32_t rat_hour_mode
) noexcept {
    if (value.hour < 0 || value.hour > 23) return RatHourSegment::None;
    if (rat_hour_mode == chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT) {
        return (value.hour == 0 || value.hour >= 23)
            ? RatHourSegment::Unified : RatHourSegment::None;
    }
    if (value.hour == 0) return RatHourSegment::Early;
    if (value.hour >= 23) return RatHourSegment::Late;
    return RatHourSegment::None;
}

}  // namespace

Tier1ReverseQuery::Tier1ReverseQuery() noexcept
    : lucun_branch(kReverseUnspecified),
      hongluan_branch(kReverseUnspecified),
      zuofu_branch(kReverseUnspecified),
      youbi_branch(kReverseUnspecified),
      wenchang_branch(kReverseUnspecified),
      wenqu_branch(kReverseUnspecified),
      santai_branch(kReverseUnspecified),
      bazuo_branch(kReverseUnspecified),
      ziwei_branch(kReverseUnspecified) {}

ReverseLookupRequest::ReverseLookupRequest() noexcept
    : start_instant_utc(),
      end_instant_utc(),
      start_virtual_time(),
      gender(Gender::Male),
      birth_options(default_birth_resolution_options()),
      query() {}

static Status reverse_lookup_impl(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const ReverseLookupRequest& request,
    const CompiledRules& rules,
    const StarRegistry& registry,
    std::vector<ReverseLookupCandidate>* out,
    runtime::EphemerisEvalDiagnostic* diagnostic,
    const ChartClock* clock
) noexcept {
    if (calendar == NULL || out == NULL
        || !split_julian_date_is_finite(request.start_instant_utc)
        || !split_julian_date_is_finite(request.end_instant_utc)
        || days_between_split_jd(
            request.start_instant_utc, request.end_instant_utc) < 0.0
        || !is_valid(request.gender) || !valid_query(request.query)
        || !request_has_constraint(request.query)
        || !compiled_rules_match_registry(rules, registry)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    try {
        std::vector<ReverseLookupCandidate> result;
        SplitJulianDate instant = request.start_instant_utc;
        CalendarDateTime virtual_time = request.start_virtual_time;
        if (clock) {
            const Status s = chart_time_from_ut1(calendar, *clock, instant, &virtual_time, diagnostic);
            if (s != TAIYIN_STATUS_OK) return s;
        }
        RatHourSegment segment = segment_for_clock(
            virtual_time, request.birth_options.rat_hour_mode);
        NatalChart previous_chart;
        bool intra_hour_probe = false;
        SplitJulianDate next_jie;
        bool have_next_jie = false;
        while (days_between_split_jd(instant, request.end_instant_utc) >= 0.0) {
            ResolvedBirth birth;
            Status status = clock ? resolve_birth_at_ut1(
                calendar, instant, *clock, request.gender, request.birth_options, &birth, diagnostic)
                : resolve_birth_from_calendar(
                calendar, instant, virtual_time, request.gender,
                request.birth_options, &birth, diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
            NatalChart chart;
            status = make_natal_chart(
                birth.facts, birth.anchors, birth.body_palace,
                request.birth_options.anchor_options.rules, rules, &chart);
            if (status != TAIYIN_STATUS_OK) return status;
            if ((!intra_hour_probe || !same_placement(previous_chart, chart))
                && matches_query(chart, registry, request.query)) {
                ReverseLookupCandidate candidate;
                candidate.instant_utc = instant;
                candidate.virtual_time = virtual_time;
                candidate.lunar_date = birth.facts.lunar_date;
                candidate.hour_branch = to_index(
                    birth.facts.solar_term_pillars.hour.branch);
                candidate.rat_hour_segment = segment;
                result.push_back(candidate);
            }
            previous_chart = std::move(chart);
            if (instant == request.end_instant_utc) break;
            SplitJulianDate next_instant;
            CalendarDateTime next_virtual;
            CalendarDateTime normalized;
            status = chinese_calendar::normalize_chart_virtual_time(virtual_time, &normalized);
            if (status != TAIYIN_STATUS_OK) return status;
            const bool split = request.birth_options.rat_hour_mode
                != chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT;
            const int next_hour = split && normalized.hour == 23
                ? 24 : ((normalized.hour + 1) / 2) * 2 + 1;
            const double seconds = (next_hour - normalized.hour) * 3600.0
                - normalized.minute * 60.0 - normalized.second;
            SplitJulianDate local;
            if (!julian_day_split(normalized, &local)
                || !add_seconds_to_split_jd(instant, seconds, &next_instant)) {
                return TAIYIN_ERROR_INVALID_ARGUMENT;
            }
            status = detail::shift_virtual_hours(normalized,
                next_hour - normalized.hour, &next_virtual);
            if (status != TAIYIN_STATUS_OK) return status;
            next_virtual.minute = 0;
            next_virtual.second = 0.0;
            if (clock) {
                status = chart_time_to_ut1(calendar, *clock, next_virtual, &next_instant, diagnostic);
                if (status != TAIYIN_STATUS_OK) return status;
            }
            if (!have_next_jie) {
                chinese_calendar::SolarTermEvent term;
                status = chinese_calendar::next_pillar_jie(
                    calendar, instant, &term, &next_jie, diagnostic);
                if (status != TAIYIN_STATUS_OK) return status;
                have_next_jie = true;
            }
            intra_hour_probe = next_jie < next_instant;
            if (next_jie <= next_instant) {
                next_instant = next_jie;
                if (clock) {
                    status = chart_time_from_ut1(calendar, *clock, next_instant, &next_virtual, diagnostic);
                    if (status != TAIYIN_STATUS_OK) return status;
                } else if (!reverse_julian_day_split(local + (next_instant - instant), &next_virtual)) {
                    return TAIYIN_ERROR_INVALID_ARGUMENT;
                }
                have_next_jie = false;
            }
            if (days_between_split_jd(instant, next_instant) <= 0.0) {
                return TAIYIN_ERROR_INTERNAL;
            }
            instant = next_instant;
            virtual_time = next_virtual;
            segment = segment_for_clock(virtual_time, request.birth_options.rat_hour_mode);
        }
        *out = std::move(result);
        return TAIYIN_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return TAIYIN_ERROR_INTERNAL;
    }
}

Status reverse_lookup_tier1_from_calendar(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const ReverseLookupRequest& request, const CompiledRules& rules,
    const StarRegistry& registry, std::vector<ReverseLookupCandidate>* out,
    runtime::EphemerisEvalDiagnostic* diagnostic) noexcept {
    return reverse_lookup_impl(calendar, request, rules, registry, out, diagnostic, NULL);
}

Status reverse_lookup_tier1_at_ut1(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const ReverseLookupRequest& request, const ChartClock& clock,
    const CompiledRules& rules, const StarRegistry& registry,
    std::vector<ReverseLookupCandidate>* out,
    runtime::EphemerisEvalDiagnostic* diagnostic) noexcept {
    return reverse_lookup_impl(calendar, request, rules, registry, out, diagnostic, &clock);
}

}  // namespace ziwei
}  // namespace taiyin
