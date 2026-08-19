// Historical lunar-conversion note: exceptional-month naming and historical
// qi-shuo civil-day data reference Shouxing Astronomical Calendar (sxwnl /
// 寿星天文历). See the repository NOTICE for provenance and terms.
#include "taiyin/chinese_calendar/calendar.h"

#include "chinese_calendar/historical_calendar_data.h"
#include "chinese_calendar/solar_term_internal.h"

#include "taiyin/angle.h"
#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/runtime/event_search.h"
#include "taiyin/time.h"

#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>

namespace taiyin {
namespace chinese_calendar {
namespace {

constexpr double kModernChinaMeridianDeg = 120.0;
constexpr int32_t kModernChinaUtcOffsetMinutes = 8 * 60;
constexpr double kDaysPerSolarTerm = 15.2184;
constexpr double kDaysPerSynodicMonth = 29.5306;
const double kSolarTermStepRad = TAIYIN_PI / 12.0;
constexpr double kSolarTermSearchLeadDays = 40.0;
constexpr int kSolarTermCount = 24;
constexpr double kJ2000 = 2451545.0;

enum HistoricalEventKind {
    HistoricalNewMoon,
    HistoricalSolarTerm,
};

double quiet_nan() noexcept {
    return std::numeric_limits<double>::quiet_NaN();
}

int normalized_index(int value, int modulus) noexcept {
    int result = value % modulus;
    if (result < 0) result += modulus;
    return result;
}

SplitJulianDate invalid_split_jd() noexcept {
    return SplitJulianDate(0, quiet_nan());
}

SplitJulianDate split_jd_from_parts(
    int64_t day_number,
    double day_fraction
) noexcept {
    SplitJulianDate out;
    return normalize_split_julian_date(day_number, day_fraction, &out)
        ? out : invalid_split_jd();
}

bool finite_meridian(double longitude_deg) noexcept {
    return std::isfinite(longitude_deg)
        && longitude_deg >= -180.0
        && longitude_deg <= 180.0;
}

bool valid_calendar_mode(int32_t mode) noexcept {
    return mode == TAIYIN_CHINESE_CALENDAR_CHINA_STANDARD_HISTORICAL
        || mode == TAIYIN_CHINESE_CALENDAR_LOCAL_ASTRONOMICAL
        || mode == TAIYIN_CHINESE_CALENDAR_CHINA_STANDARD_ASTRONOMICAL;
}

bool valid_day_boundary_mode(int32_t mode) noexcept {
    return mode == TAIYIN_CHINESE_CALENDAR_FIXED_UTC_OFFSET
        || mode == TAIYIN_CHINESE_CALENDAR_MEAN_SOLAR_MERIDIAN;
}

bool valid_utc_offset_minutes(int32_t offset_minutes) noexcept {
    return offset_minutes >= -14 * 60 && offset_minutes <= 14 * 60;
}

bool valid_pillar_historical_mode(int32_t mode) noexcept {
    return mode == TAIYIN_GANZHI_PILLAR_HISTORICAL_FOLLOW_CALENDAR
        || mode == TAIYIN_GANZHI_PILLAR_HISTORICAL_OFF
        || mode == TAIYIN_GANZHI_PILLAR_HISTORICAL_ON;
}

bool valid_config(const ChineseCalendarConfig& config) noexcept {
    if (!valid_calendar_mode(config.mode)
        || !valid_day_boundary_mode(config.day_boundary_mode)
        || !valid_pillar_historical_mode(config.pillar_historical_mode)) {
        return false;
    }
    if (config.day_boundary_mode
        == TAIYIN_CHINESE_CALENDAR_FIXED_UTC_OFFSET) {
        return valid_utc_offset_minutes(config.utc_offset_minutes);
    }
    return finite_meridian(config.calendar_meridian_deg);
}

double local_day_offset(const ChineseCalendarContext& context) noexcept {
    if (context.config.day_boundary_mode
        == TAIYIN_CHINESE_CALENDAR_FIXED_UTC_OFFSET) {
        return static_cast<double>(context.config.utc_offset_minutes)
            / (24.0 * 60.0);
    }
    return context.config.calendar_meridian_deg / 360.0;
}

double structure_day_offset(
    const ChineseCalendarContext& context
) noexcept {
    if (context.config.mode
        != TAIYIN_CHINESE_CALENDAR_LOCAL_ASTRONOMICAL) {
        return static_cast<double>(kModernChinaUtcOffsetMinutes)
            / (24.0 * 60.0);
    }
    return local_day_offset(context);
}

bool uses_historical_china_profile(
    const ChineseCalendarContext& context
) noexcept {
    return context.config.mode
        == TAIYIN_CHINESE_CALENDAR_CHINA_STANDARD_HISTORICAL;
}

int64_t civil_day_number(
    SplitJulianDate jd_ut,
    double day_offset
) noexcept {
    const SplitJulianDate shifted = jd_ut + day_offset + 0.5;
    return shifted.day_number;
}

SolarDate solar_date_from_day_number(int64_t day_number) noexcept {
    CalendarDateTime date = {};
    if (!reverse_julian_day_split(
            split_jd_from_parts(day_number, -0.5), &date)) {
        return SolarDate();
    }
    SolarDate out;
    out.year = date.year;
    out.month = static_cast<uint8_t>(date.month);
    out.day = static_cast<uint8_t>(date.day);
    return out;
}

bool valid_solar_date(const SolarDate& value) noexcept {
    if (value.month < 1 || value.month > 12 || value.day < 1 || value.day > 31) {
        return false;
    }
    const CalendarDateTime input = {
        value.year,
        static_cast<int>(value.month),
        static_cast<int>(value.day),
        0,
        0,
        0.0,
    };
    SplitJulianDate jd;
    CalendarDateTime roundtrip;
    if (!julian_day_split(input, &jd)
        || !reverse_julian_day_split(jd, &roundtrip)) {
        return false;
    }
    return roundtrip.year == input.year
        && roundtrip.month == input.month
        && roundtrip.day == input.day;
}

int64_t solar_date_day_number(const SolarDate& value) noexcept {
    const CalendarDateTime date = {
        value.year,
        static_cast<int>(value.month),
        static_cast<int>(value.day),
        0,
        0,
        0.0,
    };
    SplitJulianDate jd;
    if (!julian_day_split(date, &jd)) return 0;
    return (jd + 0.5).day_number;
}

uint32_t popcount64(uint64_t value) noexcept {
    uint32_t count = 0;
    while (value != 0) {
        value &= value - 1;
        ++count;
    }
    return count;
}

template <std::size_t N>
bool packed_bit_at(
    const std::array<uint64_t, N>& words,
    std::size_t bit_index
) noexcept {
    return ((words[bit_index / 64u] >> (bit_index % 64u))
        & UINT64_C(1)) != 0;
}

template <std::size_t WordCount, std::size_t PrefixCount>
uint32_t packed_rank_in_block(
    const std::array<uint64_t, WordCount>& words,
    const std::array<uint16_t, PrefixCount>& prefixes,
    std::size_t bit_index
) noexcept {
    const std::size_t block_index = bit_index
        / internal::kHistoricalResidualRankBlockEvents;
    const std::size_t block_word = block_index
        * (internal::kHistoricalResidualRankBlockEvents / 64u);
    const std::size_t word_index = bit_index / 64u;
    uint32_t rank = prefixes[block_index];
    for (std::size_t i = block_word; i < word_index; ++i) {
        rank += popcount64(words[i]);
    }
    const unsigned bit_in_word = static_cast<unsigned>(bit_index % 64u);
    if (bit_in_word != 0u) {
        rank += popcount64(words[word_index]
            & ((UINT64_C(1) << bit_in_word) - UINT64_C(1)));
    }
    return rank;
}

int64_t rounded_linear_civil_day(
    const internal::CivilDayLinearSegment& segment,
    std::size_t event_index,
    int64_t phase_ticks
) noexcept {
    const std::size_t local_index = event_index - segment.first_event_index;
    const int64_t ticks = segment.base_ticks
        + segment.step_ticks * static_cast<int64_t>(local_index)
        + phase_ticks;
    return (ticks + internal::kHistoricalCivilDayScale / 2)
        / internal::kHistoricalCivilDayScale;
}

template <std::size_t SegmentCount, std::size_t MaskWords,
          std::size_t SignWords, std::size_t PrefixCount>
int64_t historical_profile_civil_day(
    std::size_t event_index,
    const std::array<internal::CivilDayLinearSegment, SegmentCount>& exact_segments,
    const internal::CivilDayLinearSegment& tail,
    const std::array<int64_t, 24>* phase_ticks,
    const std::array<uint64_t, MaskWords>& residual_mask,
    const std::array<uint64_t, SignWords>& residual_signs,
    const std::array<uint16_t, PrefixCount>& residual_rank
) noexcept {
    for (std::size_t i = 0; i < SegmentCount; ++i) {
        const internal::CivilDayLinearSegment& segment = exact_segments[i];
        if (event_index >= segment.first_event_index
            && event_index < segment.first_event_index + segment.event_count) {
            return rounded_linear_civil_day(segment, event_index, 0);
        }
    }

    const std::size_t local_index = event_index - tail.first_event_index;
    const int64_t phase = phase_ticks
        ? (*phase_ticks)[local_index % phase_ticks->size()] : 0;
    int64_t day = rounded_linear_civil_day(tail, event_index, phase);
    if (!packed_bit_at(residual_mask, local_index)) return day;
    const uint32_t rank = packed_rank_in_block(
        residual_mask, residual_rank, local_index);
    return day + (packed_bit_at(residual_signs, rank) ? 1 : -1);
}

bool historical_profile_event_index(
    HistoricalEventKind kind,
    double estimate_jd_ut,
    std::size_t* event_index
) noexcept {
    if (!event_index || estimate_jd_ut >= internal::kHistoricalProfileEndJd) {
        return false;
    }

    const double phase_index = kind == HistoricalSolarTerm
        ? std::floor((estimate_jd_ut + 7.0 - 2451259.0)
            / DAYS_PER_TROPICAL_YEAR * 24.0)
        : std::floor((estimate_jd_ut + 14.0 - 2451551.0)
            / kDaysPerSynodicMonth);
    const int64_t first_phase_index = kind == HistoricalSolarTerm
        ? internal::kHistoricalSolarTermFirstPhaseIndex
        : internal::kHistoricalNewMoonFirstPhaseIndex;
    const std::size_t event_count = kind == HistoricalSolarTerm
        ? internal::kHistoricalSolarTermEventCount
        : internal::kHistoricalNewMoonEventCount;
    const double ordinal = phase_index - static_cast<double>(first_phase_index);
    if (!std::isfinite(ordinal) || ordinal < 0.0
        || ordinal >= static_cast<double>(event_count)) {
        return false;
    }
    *event_index = static_cast<std::size_t>(ordinal);
    return true;
}

int64_t historical_calendar_day(
    HistoricalEventKind kind,
    double estimate_jd_ut,
    SplitJulianDate precise_jd_ut,
    double day_offset
) noexcept {
    std::size_t event_index = 0;
    if (!historical_profile_event_index(kind, estimate_jd_ut, &event_index)) {
        return civil_day_number(precise_jd_ut, day_offset);
    }

    if (kind == HistoricalSolarTerm) {
        return historical_profile_civil_day(
            event_index,
            internal::kHistoricalSolarTermExactSegments,
            internal::kHistoricalSolarTermTail,
            &internal::kSolarTermPhaseTicks,
            internal::kHistoricalSolarTermResidualMask,
            internal::kHistoricalSolarTermResidualSigns,
            internal::kHistoricalSolarTermResidualRank);
    }
    return historical_profile_civil_day(
        event_index,
        internal::kHistoricalNewMoonExactSegments,
        internal::kHistoricalNewMoonTail,
        0,
        internal::kHistoricalNewMoonResidualMask,
        internal::kHistoricalNewMoonResidualSigns,
        internal::kHistoricalNewMoonResidualRank);
}

int64_t assigned_event_day(
    const ChineseCalendarContext& context,
    HistoricalEventKind kind,
    SplitJulianDate estimate_jd_ut,
    SplitJulianDate precise_jd_ut
) noexcept {
    if (uses_historical_china_profile(context)) {
        const double estimate = split_julian_date_to_double(estimate_jd_ut);
        std::size_t event_index = 0;
        if (historical_profile_event_index(kind, estimate, &event_index)) {
            runtime::set_operation_flag(
                &context.astronomy,
                kResultFlagHistoricalEventAssignmentApplied);
        }
        return historical_calendar_day(
            kind,
            estimate,
            precise_jd_ut,
            structure_day_offset(context));
    }
    return civil_day_number(precise_jd_ut, structure_day_offset(context));
}

Status evaluate_solar_term(
    const ChineseCalendarContext& context,
    std::size_t index,
    SplitJulianDate estimate_jd_ut,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out || index >= TAIYIN_CHINESE_CALENDAR_TERM_COUNT) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double longitude = normalize_radians(
        (270.0 + 15.0 * static_cast<double>(index)) * TAIYIN_DEG_TO_RAD);
    SplitJulianDate event_jd_ut = invalid_split_jd();
    Status status = runtime::search_solar_longitude_ut(
        &context.astronomy,
        longitude,
        estimate_jd_ut - 4.0,
        0u,
        &event_jd_ut,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    out->index_from_winter_solstice = static_cast<uint8_t>(index);
    out->target_longitude_rad = longitude;
    out->jd_ut = event_jd_ut;
    out->civil_day_number = assigned_event_day(
        context, HistoricalSolarTerm, estimate_jd_ut, event_jd_ut);
    return TAIYIN_STATUS_OK;
}

enum SolarTermFilter {
    SolarTermAny,
    SolarTermJie,
    SolarTermQi,
};

int positive_mod(int value, int modulus) noexcept {
    const int remainder = value % modulus;
    return remainder < 0 ? remainder + modulus : remainder;
}

int solar_term_id_from_longitude_step(int longitude_step) noexcept {
    // The canonical Chinese-calendar order is Xiaohan=0 ... Dongzhi=23,
    // whereas ecliptic longitude step 0 is Chunfen (term 5).
    return positive_mod(longitude_step + 5, kSolarTermCount);
}

uint8_t winter_solstice_index_from_solar_term_id(int term_id) noexcept {
    return static_cast<uint8_t>(positive_mod(
        term_id + 1, kSolarTermCount));
}

bool matches_solar_term_filter(int term_id, SolarTermFilter filter) noexcept {
    switch (filter) {
    case SolarTermAny:
        return true;
    case SolarTermJie:
        return (term_id & 1) == 0;
    case SolarTermQi:
        return (term_id & 1) != 0;
    }
    return false;
}

Status evaluate_standalone_solar_term(
    const ChineseCalendarContext& context,
    int term_id,
    SplitJulianDate search_start_jd_ut,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out || term_id < 0 || term_id >= kSolarTermCount) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const int longitude_step = positive_mod(term_id - 5, kSolarTermCount);
    const double longitude = static_cast<double>(longitude_step)
        * kSolarTermStepRad;
    SplitJulianDate event_jd_ut = invalid_split_jd();
    const Status status = runtime::search_solar_longitude_ut(
        &context.astronomy,
        longitude,
        search_start_jd_ut,
        0u,
        &event_jd_ut,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    out->index_from_winter_solstice =
        winter_solstice_index_from_solar_term_id(term_id);
    out->target_longitude_rad = longitude;
    out->jd_ut = event_jd_ut;
    out->civil_day_number = assigned_event_day(
        context, HistoricalSolarTerm, event_jd_ut, event_jd_ut);
    return TAIYIN_STATUS_OK;
}

Status find_winter_solstice(
    const ChineseCalendarContext& context,
    SplitJulianDate target_jd_ut,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status get_specific_solar_term(
    const ChineseCalendarContext* context,
    int32_t civil_year,
    uint8_t term_index_from_vernal_equinox,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = SolarTermEvent();
    if (diagnostic) *diagnostic = runtime::EphemerisEvalDiagnostic();
    if (!context || !out || term_index_from_vernal_equinox >= kSolarTermCount
        || !valid_config(context->config)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    // Match calcY's anchor chain: first locate the actual preceding winter
    // solstice, then refine one of its following 15-degree terms. Indices
    // 19..23 name January-through-March terms earlier in civil_year, not
    // terms in the following year.
    const CalendarDateTime anchor = {
        civil_year, 6, 1, 0, 0, 0.0,
    };
    SplitJulianDate anchor_jd_ut;
    if (!julian_day_split(anchor, &anchor_jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SolarTermEvent winter_solstice;
    Status status = find_winter_solstice(
        *context, anchor_jd_ut, &winter_solstice, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    const std::size_t winter_solstice_index =
        term_index_from_vernal_equinox >= 19u
            ? static_cast<std::size_t>(term_index_from_vernal_equinox) - 18u
            : static_cast<std::size_t>(term_index_from_vernal_equinox) + 6u;
    const SplitJulianDate estimate_jd_ut = winter_solstice.jd_ut
        + static_cast<double>(winter_solstice_index) * kDaysPerSolarTerm;
    // This is the same evaluator and bounded local estimate used by calcY().
    // It is never seeded from an accumulated tropical-year phase.
    status = evaluate_solar_term(
        *context, winter_solstice_index, estimate_jd_ut, out, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    out->index_from_winter_solstice = static_cast<uint8_t>(
        winter_solstice_index % kSolarTermCount);
    return TAIYIN_STATUS_OK;
}

Status find_solar_term(
    const ChineseCalendarContext* context,
    SplitJulianDate jd_ut,
    bool next,
    SolarTermFilter filter,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = SolarTermEvent();
    if (diagnostic) *diagnostic = runtime::EphemerisEvalDiagnostic();
    if (!context || !out || !split_julian_date_is_finite(jd_ut)
        || !valid_config(context->config)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    double position[6] = {};
    Status status = runtime::calc_position_ut(
        &context->astronomy,
        TAIYIN_BODY_SUN,
        jd_ut,
        runtime::TAIYIN_NATIVE_POSITION_RADIANS,
        position,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    const int approximate_step = static_cast<int>(std::floor(
        normalize_radians(position[0]) / kSolarTermStepRad));
    const SplitJulianDate search_start_jd_ut =
        jd_ut - kSolarTermSearchLeadDays;
    SolarTermEvent best;
    bool found = false;
    // The filter can skip one term. Keep an extra candidate on each side so
    // a root evaluated infinitesimally before a boundary still finds the next
    // same-kind jie/qi.
    for (int offset = -3; offset <= 3; ++offset) {
        const int term_id = solar_term_id_from_longitude_step(
            approximate_step + offset);
        if (!matches_solar_term_filter(term_id, filter)) continue;

        SolarTermEvent candidate;
        status = evaluate_standalone_solar_term(
            *context, term_id, search_start_jd_ut, &candidate, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        // Split JD preserves the event instant, so no wall-clock serialization
        // tolerance is needed here. Only absorb the numerical floor between
        // independently refined copies of the same root.
        const double difference_days = candidate.jd_ut - jd_ut;
        const bool matches_direction = next
            ? difference_days > internal::kSolarTermRootEqualityToleranceDays
            : difference_days <= internal::kSolarTermRootEqualityToleranceDays;
        if (!matches_direction
            || (found && (next ? candidate.jd_ut >= best.jd_ut
                                : candidate.jd_ut <= best.jd_ut))) {
            continue;
        }
        best = candidate;
        found = true;
    }
    if (!found) return TAIYIN_EVENT_ERROR_NOT_FOUND;
    *out = best;
    return TAIYIN_STATUS_OK;
}

Status find_winter_solstice(
    const ChineseCalendarContext& context,
    SplitJulianDate target_jd_ut,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out || !split_julian_date_is_finite(target_jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const int64_t target_day = civil_day_number(
        target_jd_ut, structure_day_offset(context));

    // Search a complete tropical year instead of accumulating a linear
    // solstice seed from J2000. The extra days cover civil-day assignment at
    // both ends of the interval.
    const double day_offset = structure_day_offset(context);
    const SplitJulianDate end_jd_ut = split_jd_from_parts(
        target_day, -day_offset + 0.5);
    const SplitJulianDate start_jd_ut = end_jd_ut - 371.0;
    SplitJulianDate candidates[3] = {};
    std::size_t candidate_count = 0;
    const Status status = runtime::search_body_longitude_crossings_ut(
        &context.astronomy,
        TAIYIN_BODY_SUN,
        270.0 * TAIYIN_DEG_TO_RAD,
        start_jd_ut,
        end_jd_ut,
        20.0,
        0u,
        candidates,
        sizeof(candidates) / sizeof(candidates[0]),
        &candidate_count,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    bool found = false;
    for (std::size_t i = 0; i < candidate_count; ++i) {
        const int64_t assigned_day = assigned_event_day(
            context, HistoricalSolarTerm, candidates[i], candidates[i]);
        if (assigned_day > target_day) break;
        out->index_from_winter_solstice = 0;
        out->target_longitude_rad = 270.0 * TAIYIN_DEG_TO_RAD;
        out->jd_ut = candidates[i];
        out->civil_day_number = assigned_day;
        found = true;
    }
    return found ? TAIYIN_STATUS_OK : TAIYIN_EVENT_ERROR_NOT_FOUND;
}

Status fill_solar_terms(
    const ChineseCalendarContext& context,
    ChineseCalendarYear* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    for (std::size_t i = 1; i < TAIYIN_CHINESE_CALENDAR_TERM_COUNT; ++i) {
        const SplitJulianDate estimate_jd =
            out->solar_terms[i - 1].jd_ut + kDaysPerSolarTerm;
        const Status status = evaluate_solar_term(
            context,
            i,
            estimate_jd,
            &out->solar_terms[i],
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
    }
    out->solar_term_count =
        static_cast<uint8_t>(TAIYIN_CHINESE_CALENDAR_TERM_COUNT);
    out->first_winter_solstice_day_number =
        out->solar_terms[0].civil_day_number;
    out->second_winter_solstice_day_number =
        out->solar_terms[24].civil_day_number;
    return TAIYIN_STATUS_OK;
}

Status fill_new_moons(
    const ChineseCalendarContext& context,
    ChineseCalendarYear* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    SplitJulianDate candidates[20] = {};
    std::size_t candidate_count = 0;
    const SplitJulianDate winter_jd = out->solar_terms[0].jd_ut;
    const Status status = runtime::search_lunar_phase_crossings_default_step_ut(
        &context.astronomy,
        0.0,
        winter_jd - 40.0,
        winter_jd + 430.0,
        0u,
        candidates,
        sizeof(candidates) / sizeof(candidates[0]),
        &candidate_count,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    std::size_t first_index = candidate_count;
    for (std::size_t i = 0; i < candidate_count; ++i) {
        const int64_t day = assigned_event_day(
            context, HistoricalNewMoon, candidates[i], candidates[i]);
        if (day <= out->solar_terms[0].civil_day_number) {
            first_index = i;
        } else {
            break;
        }
    }
    if (first_index == candidate_count
        || first_index + TAIYIN_CHINESE_CALENDAR_NEW_MOON_COUNT
            > candidate_count) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    for (std::size_t i = 0;
         i < TAIYIN_CHINESE_CALENDAR_NEW_MOON_COUNT;
         ++i) {
        const SplitJulianDate jd_ut = candidates[first_index + i];
        out->new_moons[i].jd_ut = jd_ut;
        out->new_moons[i].civil_day_number = assigned_event_day(
            context, HistoricalNewMoon, jd_ut, jd_ut);
    }
    out->new_moon_count =
        static_cast<uint8_t>(TAIYIN_CHINESE_CALENDAR_NEW_MOON_COUNT);
    return TAIYIN_STATUS_OK;
}

uint8_t month_number_from_sequence(int sequence) noexcept {
    static const uint8_t kMonthNumbers[12] = {
        11, 12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    };
    int normalized = sequence % 12;
    if (normalized < 0) normalized += 12;
    return kMonthNumbers[normalized];
}

int resolve_physical_month_sequences(
    const ChineseCalendarYear& year,
    int* out_sequences
) noexcept {
    if (out_sequences == NULL) return -1;
    for (std::size_t i = 0u;
         i < TAIYIN_CHINESE_CALENDAR_MONTH_COUNT;
         ++i) {
        out_sequences[i] = static_cast<int>(i);
    }

    // A physical month-building sequence is always anchored to the lunar
    // month containing the first winter solstice (Zi).  When this solstice
    // interval holds thirteen months, the first month without a Zhong-Qi
    // repeats its predecessor; this is deliberately independent from any
    // historical written month-number convention.
    if (year.new_moons[13].civil_day_number
        > year.solar_terms[24].civil_day_number) {
        return -1;
    }
    int leap_index = 1;
    while (leap_index < 13
        && year.new_moons[leap_index + 1].civil_day_number
            > year.solar_terms[2 * leap_index].civil_day_number) {
        ++leap_index;
    }
    for (int i = leap_index;
         i < static_cast<int>(TAIYIN_CHINESE_CALENDAR_MONTH_COUNT);
         ++i) {
        --out_sequences[i];
    }
    return leap_index;
}

void assign_lunar_years(ChineseCalendarYear* out) noexcept {
    int year_starts[TAIYIN_CHINESE_CALENDAR_MONTH_COUNT] = {};
    int year_start_count = 0;
    for (std::size_t i = 0; i < TAIYIN_CHINESE_CALENDAR_MONTH_COUNT; ++i) {
        const ChineseCalendarMonth& month = out->months[i];
        if (month.month == 1 && month.is_leap == 0u
            && month.month_name != TAIYIN_CHINESE_MONTH_NAME_ALT_ONE) {
            year_starts[year_start_count++] = static_cast<int>(i);
        }
    }
    if (year_start_count > 0) {
        int32_t first_year = 0;
        for (int boundary = 0; boundary < year_start_count; ++boundary) {
            const int first_month_index = year_starts[boundary];
            const int next_month_index = boundary + 1 < year_start_count
                ? year_starts[boundary + 1]
                : static_cast<int>(TAIYIN_CHINESE_CALENDAR_MONTH_COUNT);
            const int64_t start =
                out->months[first_month_index].first_civil_day_number;
            const int64_t end = boundary + 1 < year_start_count
                ? out->months[next_month_index].first_civil_day_number
                : start + 180;
            const int32_t year =
                solar_date_from_day_number((start + end) / 2).year;
            if (boundary == 0) first_year = year;
            for (int i = first_month_index; i < next_month_index; ++i) {
                out->months[i].lunar_year = year;
            }
        }
        for (int i = 0; i < year_starts[0]; ++i) {
            out->months[i].lunar_year = first_year - 1;
        }
        return;
    }

    for (std::size_t i = 0; i < TAIYIN_CHINESE_CALENDAR_MONTH_COUNT; ++i) {
        const SolarDate date = solar_date_from_day_number(
            out->months[i].first_civil_day_number);
        out->months[i].lunar_year =
            out->months[i].month >= 11 ? date.year - 1 : date.year;
    }
}

Status assign_early_historical_months(
    const ChineseCalendarContext& context,
    int year_hint,
    ChineseCalendarYear* out
) noexcept {
    int physical_sequences[TAIYIN_CHINESE_CALENDAR_MONTH_COUNT] = {};
    (void) resolve_physical_month_sequences(*out, physical_sequences);
    int64_t year_starts[3] = {};
    uint8_t base_months[3] = {};
    uint8_t special_names[3] = {};
    for (int i = 0; i < 3; ++i) {
        const int year = year_hint + i - 1;
        double estimate = quiet_nan();
        if (year >= -721) {
            estimate = 1457698.0
                + std::floor(0.342 + (year + 721) * 12.368422)
                    * kDaysPerSynodicMonth;
            base_months[i] = 2;
            special_names[i] = TAIYIN_CHINESE_MONTH_NAME_THIRTEEN;
        }
        if (year >= -479) {
            estimate = 1546083.0
                + std::floor(0.500 + (year + 479) * 12.368422)
                    * kDaysPerSynodicMonth;
            base_months[i] = 2;
            special_names[i] = TAIYIN_CHINESE_MONTH_NAME_THIRTEEN;
        }
        if (year >= -220) {
            estimate = 1640641.0
                + std::floor(0.866 + (year + 220) * 12.369000)
                    * kDaysPerSynodicMonth;
            base_months[i] = 11;
            special_names[i] = TAIYIN_CHINESE_MONTH_NAME_LATER_NINE;
        }
        if (!std::isfinite(estimate)) return TAIYIN_ERROR_INTERNAL;
        SplitJulianDate precise_estimate;
        if (!split_julian_date_from_double(estimate, &precise_estimate)) {
            return TAIYIN_ERROR_INTERNAL;
        }
        year_starts[i] = historical_calendar_day(
            HistoricalNewMoon,
            estimate,
            precise_estimate,
            structure_day_offset(context));
    }

    for (std::size_t i = 0; i < TAIYIN_CHINESE_CALENDAR_MONTH_COUNT; ++i) {
        int era_index = 2;
        while (era_index > 0
            && out->new_moons[i].civil_day_number < year_starts[era_index]) {
            --era_index;
        }
        const int month_offset = static_cast<int>(std::floor(
            (out->new_moons[i].civil_day_number - year_starts[era_index] + 15.0)
            / kDaysPerSynodicMonth));
        ChineseCalendarMonth& month = out->months[i];
        // The historical year-start table already identifies the calendar
        // year containing this month. Deriving the label from the midpoint of
        // the local winter-solstice window is not stable across adjacent
        // calcY() calls at a reform boundary: the same Gregorian year can then
        // receive two distinct months with an identical LunarDate identity.
        // The Zhuanxu/Qin-Han branch begins its numbered year in the winter
        // preceding the Gregorian year used by the approximation's epoch
        // index. Keep that one-year shift explicit instead of letting a
        // midpoint heuristic change it between adjacent winter-solstice
        // windows.
        const int winter_year_shift = base_months[era_index] == 11 ? 1 : 0;
        month.lunar_year = year_hint + era_index - 1 - winter_year_shift;
        month.month_building_branch = static_cast<uint8_t>(
            normalized_index(physical_sequences[i], 12));
        if (month_offset < 12) {
            month.month = month_number_from_sequence(
                month_offset + base_months[era_index]);
        } else {
            month.month_name = special_names[era_index];
            month.month = special_names[era_index]
                    == TAIYIN_CHINESE_MONTH_NAME_THIRTEEN
                ? 13
                : 9;
            month.is_leap = 1u;
        }
    }
    out->leap_month_index = -1;
    out->month_count =
        static_cast<uint8_t>(TAIYIN_CHINESE_CALENDAR_MONTH_COUNT);
    (void) context;
    return TAIYIN_STATUS_OK;
}

Status assign_months(
    const ChineseCalendarContext& context,
    ChineseCalendarYear* out
) noexcept {
    int sequence[TAIYIN_CHINESE_CALENDAR_MONTH_COUNT] = {};
    for (std::size_t i = 0; i < TAIYIN_CHINESE_CALENDAR_MONTH_COUNT; ++i) {
        const int64_t day_count =
            out->new_moons[i + 1].civil_day_number
            - out->new_moons[i].civil_day_number;
        // 237 CE Jingchu-calendar transition: the recorded month starting on
        // civil day 1807696 is 28 days. Only the historical profile carries
        // this; an astronomical 28-day month would be a genuine error.
        const bool jingchu_transition_month =
            uses_historical_china_profile(context)
            && day_count == 28
            && out->new_moons[i].civil_day_number == INT64_C(1807696);
        if (jingchu_transition_month) {
            runtime::set_operation_flag(
                &context.astronomy,
                kResultFlagHistoricalCalendarRulesApplied);
        }
        if (!jingchu_transition_month && (day_count < 29 || day_count > 30)) {
            return TAIYIN_ERROR_INTERNAL;
        }
        ChineseCalendarMonth& month = out->months[i];
        month.day_count = static_cast<uint8_t>(day_count);
        month.first_civil_day_number =
            out->new_moons[i].civil_day_number;
        month.astronomical_new_moon_jd_ut = out->new_moons[i].jd_ut;
        sequence[i] = static_cast<int>(i);
    }

    const int year_hint = static_cast<int>(std::floor(
        (static_cast<double>(out->solar_terms[0].civil_day_number)
            - kJ2000 + 190.0)
        / DAYS_PER_TROPICAL_YEAR)) + 2000;
    if (uses_historical_china_profile(context)
        && year_hint >= -721 && year_hint <= -104) {
        runtime::set_operation_flag(
            &context.astronomy,
            kResultFlagHistoricalCalendarRulesApplied);
        return assign_early_historical_months(context, year_hint, out);
    }

    const int leap_index = resolve_physical_month_sequences(*out, sequence);
    out->leap_month_index = static_cast<int8_t>(leap_index);

    for (std::size_t i = 0; i < TAIYIN_CHINESE_CALENDAR_MONTH_COUNT; ++i) {
        ChineseCalendarMonth& month = out->months[i];
        int month_sequence = sequence[i];
        month.month_building_branch = static_cast<uint8_t>(
            normalized_index(month_sequence, 12));
        month.month = month_number_from_sequence(month_sequence);
        month.is_leap = leap_index >= 0
                && static_cast<int>(i) == leap_index
            ? 1u
            : 0u;

        if (!uses_historical_china_profile(context)) {
            continue;
        }
        const int64_t first_day = month.first_civil_day_number;
        if ((first_day >= 1724360 && first_day <= 1729794)
            || (first_day >= 1807724 && first_day <= 1808699)) {
            month.month = month_number_from_sequence(month_sequence + 1);
            runtime::set_operation_flag(
                &context.astronomy,
                kResultFlagHistoricalCalendarRulesApplied);
        } else if (first_day >= 1999349 && first_day <= 1999467) {
            month.month = month_number_from_sequence(month_sequence + 2);
            runtime::set_operation_flag(
                &context.astronomy,
                kResultFlagHistoricalCalendarRulesApplied);
        } else if (first_day >= 1973067 && first_day <= 1977052) {
            if (month_sequence % 12 == 0) {
                month.month = 1;
                runtime::set_operation_flag(
                    &context.astronomy,
                    kResultFlagHistoricalCalendarRulesApplied);
            }
            if (month_sequence == 2) {
                month.month = 1;
                month.month_name = TAIYIN_CHINESE_MONTH_NAME_ALT_ONE;
                runtime::set_operation_flag(
                    &context.astronomy,
                    kResultFlagHistoricalCalendarRulesApplied);
            }
        }
        if (first_day == 1729794 || first_day == 1808699) {
            month.month = 12;
            month.month_name = TAIYIN_CHINESE_MONTH_NAME_ALT_TWELVE;
            runtime::set_operation_flag(
                &context.astronomy,
                kResultFlagHistoricalCalendarRulesApplied);
        }
        // At the Wu-Zetian and 761/762 restoration boundaries, the profile
        // contains two months in one lunar year with exactly the same written
        // numeric name. Tag the later occurrence without presenting it as a
        // leap month or changing its localized display name.
        if (first_day == 1977112 || first_day == 1999526) {
            month.month_name =
                TAIYIN_CHINESE_MONTH_NAME_LATER_SAME_NAME;
            runtime::set_operation_flag(
                &context.astronomy,
                kResultFlagHistoricalCalendarRulesApplied);
        }
    }
    assign_lunar_years(out);
    out->month_count =
        static_cast<uint8_t>(TAIYIN_CHINESE_CALENDAR_MONTH_COUNT);
    return TAIYIN_STATUS_OK;
}

bool matching_lunar_month(
    const ChineseCalendarMonth& month,
    const LunarDate& lunar
) noexcept {
    return month.lunar_year == lunar.year
        && month.month == lunar.month
        && month.is_leap == lunar.is_leap
        && month.month_name == lunar.month_name;
}

Status calc_year_for_lunar_search(
    const ChineseCalendarContext* context,
    int32_t year,
    ChineseCalendarYear* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const CalendarDateTime local_noon = {
        year, 6, 1, 12, 0, 0.0,
    };
    SplitJulianDate jd_ut;
    if (!julian_day_split(local_noon, &jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    jd_ut -= structure_day_offset(*context);
    return calcY(context, jd_ut, out, diagnostic);
}

}  // namespace

namespace internal {

bool historical_profile_term_day(
    const ChineseCalendarContext& context,
    const SolarTermEvent& term,
    int64_t* out_civil_day_number
) noexcept {
    if (!out_civil_day_number) return false;
    const double estimate = split_julian_date_to_double(term.jd_ut);
    std::size_t event_index = 0;
    if (!historical_profile_event_index(
            HistoricalSolarTerm, estimate, &event_index)) {
        return false;
    }
    // The pillar historical switch is independent of the calendar
    // arrangement mode, so answer from the profile even when this context
    // does not use the historical profile for its own month layout.
    *out_civil_day_number = historical_calendar_day(
        HistoricalSolarTerm, estimate, term.jd_ut,
        structure_day_offset(context));
    return true;
}

}  // namespace internal

ChineseCalendarConfig::ChineseCalendarConfig() noexcept
    : mode(TAIYIN_CHINESE_CALENDAR_CHINA_STANDARD_HISTORICAL),
      day_boundary_mode(TAIYIN_CHINESE_CALENDAR_FIXED_UTC_OFFSET),
      utc_offset_minutes(kModernChinaUtcOffsetMinutes),
      pillar_historical_mode(TAIYIN_GANZHI_PILLAR_HISTORICAL_FOLLOW_CALENDAR),
      calendar_meridian_deg(kModernChinaMeridianDeg) {}

ChineseCalendarContext::ChineseCalendarContext() noexcept
    : astronomy(), config() {}

ChineseCalendarContext::ChineseCalendarContext(
    const ChineseCalendarContext& other
) noexcept
    : astronomy(other.astronomy), config(other.config) {
    astronomy.apparent_options.model_context = &astronomy.model_context;
}

ChineseCalendarContext& ChineseCalendarContext::operator=(
    const ChineseCalendarContext& other
) noexcept {
    if (this == &other) return *this;
    astronomy = other.astronomy;
    config = other.config;
    astronomy.apparent_options.model_context = &astronomy.model_context;
    return *this;
}

SolarDate::SolarDate() noexcept
    : year(0), month(0), day(0), reserved{} {}

LunarDate::LunarDate() noexcept
    : year(0),
      month(0),
      day(0),
      is_leap(0),
      month_days(0),
      month_name(TAIYIN_CHINESE_MONTH_NAME_NORMAL),
      reserved{} {}

SolarTermEvent::SolarTermEvent() noexcept
    : index_from_winter_solstice(0),
      reserved{},
      target_longitude_rad(quiet_nan()),
      jd_ut(invalid_split_jd()),
      civil_day_number(0) {}

NewMoonEvent::NewMoonEvent() noexcept
    : jd_ut(invalid_split_jd()), civil_day_number(0) {}

ChineseCalendarMonth::ChineseCalendarMonth() noexcept
    : lunar_year(0),
      month(0),
      is_leap(0),
      day_count(0),
      month_name(TAIYIN_CHINESE_MONTH_NAME_NORMAL),
      month_building_branch(0xffu),
      first_civil_day_number(0),
      astronomical_new_moon_jd_ut(invalid_split_jd()) {}

ChineseCalendarYear::ChineseCalendarYear() noexcept
    : solar_terms{},
      new_moons{},
      months{},
      solar_term_count(0),
      new_moon_count(0),
      month_count(0),
      leap_month_index(-1),
      first_winter_solstice_day_number(0),
      second_winter_solstice_day_number(0) {}

ChineseCalendarConfig historical_china_config() noexcept {
    return ChineseCalendarConfig();
}

ChineseCalendarConfig china_standard_historical_config(
    int32_t local_utc_offset_minutes
) noexcept {
    ChineseCalendarConfig config;
    config.mode = TAIYIN_CHINESE_CALENDAR_CHINA_STANDARD_HISTORICAL;
    config.day_boundary_mode = TAIYIN_CHINESE_CALENDAR_FIXED_UTC_OFFSET;
    config.utc_offset_minutes = local_utc_offset_minutes;
    config.calendar_meridian_deg =
        static_cast<double>(local_utc_offset_minutes) / 4.0;
    return config;
}

ChineseCalendarConfig china_standard_astronomical_config(
    int32_t local_utc_offset_minutes
) noexcept {
    ChineseCalendarConfig config =
        china_standard_historical_config(local_utc_offset_minutes);
    config.mode = TAIYIN_CHINESE_CALENDAR_CHINA_STANDARD_ASTRONOMICAL;
    return config;
}

ChineseCalendarConfig local_astronomical_utc_offset_config(
    int32_t utc_offset_minutes
) noexcept {
    ChineseCalendarConfig config;
    config.mode = TAIYIN_CHINESE_CALENDAR_LOCAL_ASTRONOMICAL;
    config.day_boundary_mode = TAIYIN_CHINESE_CALENDAR_FIXED_UTC_OFFSET;
    config.utc_offset_minutes = utc_offset_minutes;
    config.calendar_meridian_deg = static_cast<double>(utc_offset_minutes) / 4.0;
    return config;
}

ChineseCalendarConfig local_astronomical_meridian_config(
    double longitude_deg
) noexcept {
    ChineseCalendarConfig config;
    config.mode = TAIYIN_CHINESE_CALENDAR_LOCAL_ASTRONOMICAL;
    config.day_boundary_mode = TAIYIN_CHINESE_CALENDAR_MEAN_SOLAR_MERIDIAN;
    config.utc_offset_minutes = 0;
    config.calendar_meridian_deg = longitude_deg;
    return config;
}

ChineseCalendarConfig fixed_utc_offset_config(
    int32_t utc_offset_minutes
) noexcept {
    return local_astronomical_utc_offset_config(utc_offset_minutes);
}

ChineseCalendarConfig fixed_meridian_config(double longitude_deg) noexcept {
    return local_astronomical_meridian_config(longitude_deg);
}

Status initialize_context(
    ChineseCalendarContext* out,
    const runtime::NativeCalcContext* astronomy,
    const ChineseCalendarConfig* config
) noexcept {
    if (!out || !astronomy || !config
        || !valid_config(*config)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = ChineseCalendarContext();
    out->astronomy = *astronomy;
    out->config = *config;
    out->astronomy.apparent_options.model_context =
        &out->astronomy.model_context;
    const Status observer_status = runtime::native_context_set_geocentric_observer(
        &out->astronomy, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH);
    if (observer_status != TAIYIN_STATUS_OK) return observer_status;
    out->astronomy.apparent_options.flags =
        TAIYIN_APPARENT_SPHERICAL
        | TAIYIN_APPARENT_LIGHT_TIME
        | TAIYIN_APPARENT_ABERRATION
        | TAIYIN_APPARENT_DEFLECTION;
    out->astronomy.apparent_options.output_frame_id =
        TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    return runtime::native_context_use_solar_deflector(&out->astronomy);
}

Status calcY(
    const ChineseCalendarContext* context,
    SplitJulianDate jd_ut,
    ChineseCalendarYear* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = ChineseCalendarYear();
    if (diagnostic) *diagnostic = runtime::EphemerisEvalDiagnostic();
    if (!context || !out || !split_julian_date_is_finite(jd_ut)
        || !valid_config(context->config)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    Status status = find_winter_solstice(
        *context, jd_ut, &out->solar_terms[0], diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = fill_solar_terms(*context, out, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = fill_new_moons(*context, out, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    return assign_months(*context, out);
}

Status getSpecificJieQi(
    const ChineseCalendarContext* context,
    int32_t civil_year,
    uint8_t term_index_from_vernal_equinox,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return get_specific_solar_term(
        context, civil_year, term_index_from_vernal_equinox, out, diagnostic);
}

Status getPrevJieQi(
    const ChineseCalendarContext* context,
    SplitJulianDate jd_ut,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return find_solar_term(
        context, jd_ut, false, SolarTermAny, out, diagnostic);
}

Status getNextJieQi(
    const ChineseCalendarContext* context,
    SplitJulianDate jd_ut,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return find_solar_term(
        context, jd_ut, true, SolarTermAny, out, diagnostic);
}

Status getPrevJie(
    const ChineseCalendarContext* context,
    SplitJulianDate jd_ut,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return find_solar_term(
        context, jd_ut, false, SolarTermJie, out, diagnostic);
}

Status getNextJie(
    const ChineseCalendarContext* context,
    SplitJulianDate jd_ut,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return find_solar_term(
        context, jd_ut, true, SolarTermJie, out, diagnostic);
}

Status getPrevQi(
    const ChineseCalendarContext* context,
    SplitJulianDate jd_ut,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return find_solar_term(
        context, jd_ut, false, SolarTermQi, out, diagnostic);
}

Status getNextQi(
    const ChineseCalendarContext* context,
    SplitJulianDate jd_ut,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return find_solar_term(
        context, jd_ut, true, SolarTermQi, out, diagnostic);
}

Status fromSolar(
    const ChineseCalendarContext* context,
    const SolarDate* solar,
    LunarDate* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = LunarDate();
    if (!context || !solar || !out || !valid_solar_date(*solar)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const int64_t target_day = solar_date_day_number(*solar);
    const SplitJulianDate target_jd_ut = split_jd_from_parts(
        target_day, -structure_day_offset(*context));
    ChineseCalendarYear year;
    Status status = calcY(context, target_jd_ut, &year, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    for (std::size_t i = 0; i < TAIYIN_CHINESE_CALENDAR_MONTH_COUNT; ++i) {
        const int64_t start = year.months[i].first_civil_day_number;
        if (start >= year.second_winter_solstice_day_number) break;
        const int64_t end = start + year.months[i].day_count;
        if (target_day < start || target_day >= end) continue;
        out->year = year.months[i].lunar_year;
        out->month = year.months[i].month;
        out->day = static_cast<uint8_t>(target_day - start + 1);
        out->is_leap = year.months[i].is_leap;
        out->month_days = year.months[i].day_count;
        out->month_name = year.months[i].month_name;
        return TAIYIN_STATUS_OK;
    }
    return TAIYIN_EVENT_ERROR_NOT_FOUND;
}

Status fromInstant(
    const ChineseCalendarContext* context,
    SplitJulianDate jd_ut,
    LunarDate* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = LunarDate();
    if (!context || !out || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const SolarDate local_date = solar_date_from_day_number(
        civil_day_number(jd_ut, local_day_offset(*context)));
    return fromSolar(context, &local_date, out, diagnostic);
}

Status fromLunar(
    const ChineseCalendarContext* context,
    const LunarDate* lunar,
    SolarDate* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = SolarDate();
    if (!context || !lunar || !out
        || lunar->month < 1 || lunar->month > 13
        || lunar->day < 1 || lunar->day > 30
        || lunar->is_leap > 1
        || lunar->month_name
            > TAIYIN_CHINESE_MONTH_NAME_LATER_SAME_NAME) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    for (int offset = 0; offset <= 1; ++offset) {
        ChineseCalendarYear year;
        const Status status = calc_year_for_lunar_search(
            context, lunar->year + offset, &year, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        for (std::size_t i = 0; i < TAIYIN_CHINESE_CALENDAR_MONTH_COUNT; ++i) {
            const ChineseCalendarMonth& month = year.months[i];
            if (month.first_civil_day_number
                >= year.second_winter_solstice_day_number) {
                break;
            }
            if (!matching_lunar_month(month, *lunar)) continue;
            if (lunar->day > month.day_count) {
                return TAIYIN_ERROR_INVALID_ARGUMENT;
            }
            *out = solar_date_from_day_number(
                month.first_civil_day_number + lunar->day - 1);
            return TAIYIN_STATUS_OK;
        }
    }
    return TAIYIN_EVENT_ERROR_NOT_FOUND;
}

Status getLunarMonthNum(
    const ChineseCalendarContext* context,
    int32_t lunar_year,
    uint8_t month,
    bool is_leap,
    uint8_t* out_day_count,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out_day_count) *out_day_count = 0;
    if (!context || !out_day_count || month < 1 || month > 13) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const uint8_t leap = is_leap ? 1u : 0u;
    bool has_exceptional_match = false;
    uint8_t exceptional_day_count = 0;
    for (int offset = 0; offset <= 1; ++offset) {
        ChineseCalendarYear year;
        const Status status = calc_year_for_lunar_search(
            context, lunar_year + offset, &year, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        for (std::size_t i = 0;
             i < TAIYIN_CHINESE_CALENDAR_MONTH_COUNT;
             ++i) {
            const ChineseCalendarMonth& candidate = year.months[i];
            if (candidate.first_civil_day_number
                >= year.second_winter_solstice_day_number) {
                break;
            }
            if (candidate.lunar_year != lunar_year
                || candidate.month != month
                || candidate.is_leap != leap) {
                continue;
            }
            // This API predates the structured historical month-name field.
            // Preserve its ordinary-month preference, but still let callers
            // query a month (such as the leap thirteenth month of -456) that
            // exists only under an exceptional historical name.
            if (candidate.month_name == TAIYIN_CHINESE_MONTH_NAME_NORMAL) {
                *out_day_count = candidate.day_count;
                return TAIYIN_STATUS_OK;
            }
            if (!has_exceptional_match) {
                exceptional_day_count = candidate.day_count;
                has_exceptional_match = true;
            }
        }
    }
    if (has_exceptional_match) {
        *out_day_count = exceptional_day_count;
        return TAIYIN_STATUS_OK;
    }
    return TAIYIN_EVENT_ERROR_NOT_FOUND;
}

}  // namespace chinese_calendar
}  // namespace taiyin
