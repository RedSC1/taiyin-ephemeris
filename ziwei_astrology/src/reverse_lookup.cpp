#include "taiyin/ziwei/reverse_lookup.h"

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

Status reverse_lookup_tier1_from_calendar(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const ReverseLookupRequest& request,
    const CompiledRules& rules,
    const StarRegistry& registry,
    std::vector<ReverseLookupCandidate>* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
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
        RatHourSegment segment = segment_for_clock(
            virtual_time, request.birth_options.rat_hour_mode);
        while (days_between_split_jd(instant, request.end_instant_utc) >= 0.0) {
            ResolvedBirth birth;
            Status status = resolve_birth_from_calendar(
                calendar, instant, virtual_time, request.gender,
                request.birth_options, &birth, diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
            NatalChart chart;
            status = make_natal_chart(
                birth.facts, birth.anchors, birth.body_palace,
                request.birth_options.anchor_options.rules, rules, &chart);
            if (status != TAIYIN_STATUS_OK) return status;
            if (matches_query(chart, registry, request.query)) {
                ReverseLookupCandidate candidate;
                candidate.instant_utc = instant;
                candidate.virtual_time = virtual_time;
                candidate.lunar_date = birth.facts.lunar_date;
                candidate.hour_branch = to_index(
                    birth.facts.solar_term_pillars.hour.branch);
                candidate.rat_hour_segment = segment;
                result.push_back(candidate);
            }
            SplitJulianDate next_instant;
            CalendarDateTime next_virtual;
            RatHourSegment next_segment = RatHourSegment::None;
            status = step_flow_hour_target(
                instant, virtual_time, request.birth_options.rat_hour_mode,
                1, &next_instant, &next_virtual, &next_segment);
            if (status != TAIYIN_STATUS_OK) return status;
            if (days_between_split_jd(instant, next_instant) <= 0.0) {
                return TAIYIN_ERROR_INTERNAL;
            }
            instant = next_instant;
            virtual_time = next_virtual;
            segment = next_segment;
        }
        *out = std::move(result);
        return TAIYIN_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return TAIYIN_ERROR_INTERNAL;
    }
}

}  // namespace ziwei
}  // namespace taiyin
