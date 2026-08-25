#include "taiyin/chinese_calendar/ganzhi.h"
#include "taiyin/body_id.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        ++failures;
    }
}

void expect_status(taiyin::Status status, const char* label) {
    if (status != taiyin::TAIYIN_STATUS_OK) {
        std::cerr << "FAIL: " << label << ": " << taiyin::status_name(status) << "\n";
        ++failures;
    }
}

void test_ganzhi_rules() {
    uint8_t value = taiyin::chinese_calendar::kInvalidGanzhi;
    expect_status(taiyin::chinese_calendar::make_ganzhi(4, 6, &value), "make Wu-Wu");
    expect(value == 0x46u, "Ganzhi uses stem high nibble and branch low nibble");
    expect_status(taiyin::chinese_calendar::advance_ganzhi(value, 1, &value),
        "advance Wu-Wu");
    expect(value == 0x57u, "Wu-Wu advances to Ji-Wei");
    expect(taiyin::chinese_calendar::make_ganzhi(0, 1, &value)
            == taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject incompatible Ganzhi parity");

    for (uint8_t stem = 0; stem < 10u; ++stem) {
        for (uint8_t month = 0; month < 12u; ++month) {
            expect_status(
                taiyin::chinese_calendar::get_month_ganzhi(stem, month, &value),
                "calculate Wu-Hu-Dun month");
            const uint8_t expected_stem = static_cast<uint8_t>(
                (((stem % 5u) * 2u + 2u) + month) % 10u);
            expect(value == static_cast<uint8_t>(
                    (expected_stem << 4) | ((month + 2u) % 12u)),
                "all Wu-Hu-Dun month pillars match the rule source");
        }
        for (uint8_t hour = 0; hour < 12u; ++hour) {
            expect_status(
                taiyin::chinese_calendar::get_hour_ganzhi(stem, hour, &value),
                "calculate Wu-Shu-Dun hour");
            const uint8_t expected_stem = static_cast<uint8_t>(
                ((stem % 5u) * 2u + hour) % 10u);
            expect(value == static_cast<uint8_t>((expected_stem << 4) | hour),
                "all Wu-Shu-Dun hour pillars match the rule source");
        }
    }

    uint8_t nayin_element = taiyin::chinese_calendar::kInvalidNaYin;
    expect_status(
        taiyin::chinese_calendar::get_nayin_element(0x00u, &nayin_element),
        "calculate Jia-Zi NaYin");
    expect(nayin_element == taiyin::chinese_calendar::GanzhiWuXingMetal,
        "Jia-Zi NaYin is metal");
    expect_status(
        taiyin::chinese_calendar::get_nayin_element(0x28u, &nayin_element),
        "calculate Bing-Shen NaYin");
    expect(nayin_element == taiyin::chinese_calendar::GanzhiWuXingFire,
        "Bing-Shen NaYin is fire");

    static const uint8_t expected_nayin_elements[60] = {
        2, 2, 4, 4, 1, 1, 3, 3, 2, 2,
        4, 4, 0, 0, 3, 3, 2, 2, 1, 1,
        0, 0, 3, 3, 4, 4, 1, 1, 0, 0,
        2, 2, 4, 4, 1, 1, 3, 3, 2, 2,
        4, 4, 0, 0, 3, 3, 2, 2, 1, 1,
        0, 0, 3, 3, 4, 4, 1, 1, 0, 0,
    };
    for (uint8_t index = 0; index < 60u; ++index) {
        uint8_t value = taiyin::chinese_calendar::kInvalidGanzhi;
        uint8_t nayin_id = taiyin::chinese_calendar::kInvalidNaYin;
        uint8_t element = taiyin::chinese_calendar::kInvalidNaYin;
        expect_status(
            taiyin::chinese_calendar::make_ganzhi(
                static_cast<uint8_t>(index % 10u),
                static_cast<uint8_t>(index % 12u),
                &value),
            "construct sexagenary cycle value");
        expect_status(
            taiyin::chinese_calendar::get_nayin_id(value, &nayin_id),
            "derive NaYin ID from sexagenary cycle value");
        expect(nayin_id == static_cast<uint8_t>(index / 2u),
            "GanZhi index formula preserves the 30 NaYin pairs");
        expect_status(
            taiyin::chinese_calendar::get_nayin_element(value, &element),
            "transcribe NayinHelper._nayinWuXing");
        expect(element == expected_nayin_elements[index],
            "all 60 NaYin elements match bazi_core table");
    }
}

void test_four_pillars() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    const std::string data_root = root && root[0] != '\0'
        ? std::string(root) + "/data/ephemerides/opm2/major-bodies/600y"
        : "../data/ephemerides/opm2/major-bodies/600y";
    const char* paths[] = {data_root.c_str()};
    taiyin::runtime::EphemerisRuntimeConfig runtime_config;
    runtime_config.source_paths = paths;
    runtime_config.source_path_count = 1;
    runtime_config.load_packaged_data = true;
    expect(taiyin::runtime::initialize_global_ephemeris_runtime(runtime_config),
        "initialize Ganzhi calendar runtime");

    taiyin::runtime::NativeCalcContext astronomy;
    expect_status(taiyin::runtime::native_context_set_geocentric_observer(
        &astronomy, taiyin::TAIYIN_BODY_EARTH, taiyin::TAIYIN_BODY_EARTH),
        "set geocentric observer");
    expect_status(taiyin::runtime::native_context_set_route_rule(
        &astronomy, taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_AUTO),
        "set ephemeris route");

    taiyin::chinese_calendar::ChineseCalendarContext calendar;
    const taiyin::chinese_calendar::ChineseCalendarConfig config =
        taiyin::chinese_calendar::fixed_utc_offset_config(8 * 60);
    expect_status(taiyin::chinese_calendar::initialize_context(
        &calendar, &astronomy, &config), "initialize Chinese calendar");

    struct Oracle {
        taiyin::CalendarDateTime clock;
        uint8_t year;
        uint8_t month;
        uint8_t day;
        uint8_t hour;
        const char* label;
    };
    const Oracle oracles[] = {
        {{1990, 5, 15, 14, 30, 0.0}, 0x66u, 0x75u, 0x64u, 0x97u,
            "bazi_core summer oracle"},
        {{1900, 1, 1, 12, 0, 0.0}, 0x5bu, 0x20u, 0x0au, 0x66u, "1900 Zi month"},
        {{2000, 1, 1, 0, 30, 0.0}, 0x53u, 0x20u, 0x46u, 0x80u, "J2000"},
        {{1985, 11, 20, 23, 30, 0.0}, 0x11u, 0x3bu, 0x00u, 0x00u,
            "bazi_core late-Rat oracle"},
        {{2025, 12, 15, 12, 0, 0.0}, 0x15u, 0x40u, 0x46u, 0x46u,
            "2025 Zi month"},
        {{2026, 1, 15, 12, 0, 0.0}, 0x15u, 0x51u, 0x51u, 0x66u, "Chou month"},
        {{2026, 3, 5, 10, 4, 0.0}, 0x26u, 0x62u, 0x42u, 0x35u, "2026 chart"},
        {{2026, 2, 19, 23, 28, 0.0}, 0x26u, 0x62u, 0x11u, 0x20u,
            "bazi_core chart-construction oracle"},
        {{2024, 2, 4, 16, 0, 0.0}, 0x93u, 0x11u, 0x4au, 0x68u,
            "bazi_core pre-Lichun oracle"},
        {{2024, 2, 4, 17, 0, 0.0}, 0x04u, 0x22u, 0x4au, 0x79u,
            "bazi_core post-Lichun oracle"},
    };
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    for (const Oracle& oracle : oracles) {
        taiyin::SplitJulianDate instant;
        expect(taiyin::julian_day_split(oracle.clock, &instant), oracle.label);
        expect(taiyin::add_seconds_to_split_jd(instant, -8.0 * 3600.0, &instant),
            oracle.label);
        taiyin::chinese_calendar::GanzhiFourPillars pillars;
        expect_status(taiyin::chinese_calendar::calculate_four_pillars(
            &calendar,
            instant,
            oracle.clock,
            taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
            &pillars,
            &diagnostic), oracle.label);
        expect(pillars.year == oracle.year && pillars.month == oracle.month
                && pillars.day == oracle.day && pillars.hour == oracle.hour,
            oracle.label);
    }

    const taiyin::CalendarDateTime late_rat = {2000, 1, 1, 23, 30, 0.0};
    taiyin::SplitJulianDate late_rat_utc;
    expect(taiyin::julian_day_split(late_rat, &late_rat_utc), "encode late Rat hour");
    expect(taiyin::add_seconds_to_split_jd(
        late_rat_utc, -8.0 * 3600.0, &late_rat_utc), "convert late Rat hour");
    const int32_t modes[] = {
        taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
        taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TOMORROW_GAN,
        taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TODAY_GAN,
    };
    const uint8_t expected_days[] = {0x57u, 0x46u, 0x46u};
    const uint8_t expected_hours[] = {0x00u, 0x00u, 0x80u};
    for (size_t i = 0; i < 3; ++i) {
        taiyin::chinese_calendar::GanzhiFourPillars pillars;
        expect_status(taiyin::chinese_calendar::calculate_four_pillars(
            &calendar, late_rat_utc, late_rat, modes[i], &pillars, &diagnostic),
            "calculate late Rat-hour convention");
        expect(pillars.day == expected_days[i] && pillars.hour == expected_hours[i],
            "late Rat-hour convention matches the bazi_core oracle");
    }

    struct SolarTermBoundaryOracle {
        int32_t year;
        uint8_t term_index_from_vernal_equinox;
        uint8_t before_year;
        uint8_t before_month;
        uint8_t after_year;
        uint8_t after_month;
        const char* label;
    };
    const SolarTermBoundaryOracle term_boundaries[] = {
        {2025, 17u, 0x15u, 0x3bu, 0x15u, 0x40u, "Daxue month boundary"},
        {2026, 19u, 0x15u, 0x40u, 0x15u, 0x51u, "Xiaohan month boundary"},
        {2024, 21u, 0x93u, 0x11u, 0x04u, 0x22u, "Lichun year/month boundary"},
    };
    for (const SolarTermBoundaryOracle& oracle : term_boundaries) {
        taiyin::chinese_calendar::SolarTermEvent term;
        expect_status(taiyin::chinese_calendar::getSpecificJieQi(
            &calendar, oracle.year, oracle.term_index_from_vernal_equinox,
            &term, &diagnostic), oracle.label);
        for (int direction = -1; direction <= 1; direction += 2) {
            taiyin::SplitJulianDate instant;
            expect(taiyin::add_seconds_to_split_jd(
                term.jd_ut, 0.5 * static_cast<double>(direction), &instant), oracle.label);
            taiyin::SplitJulianDate local;
            expect(taiyin::add_seconds_to_split_jd(instant, 8.0 * 3600.0, &local),
                oracle.label);
            taiyin::CalendarDateTime clock;
            expect(taiyin::reverse_julian_day_split(local, &clock), oracle.label);
            taiyin::chinese_calendar::GanzhiFourPillars pillars;
            expect_status(taiyin::chinese_calendar::calculate_four_pillars(
                &calendar, instant, clock,
                taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
                &pillars, &diagnostic), oracle.label);
            expect(pillars.year == (direction < 0 ? oracle.before_year : oracle.after_year),
                oracle.label);
            expect(pillars.month == (direction < 0 ? oracle.before_month : oracle.after_month),
                oracle.label);
        }

        // This is the shared numerical equality floor for independently refined
        // year and month boundaries. It belongs to the new term at the floor.
        const double equality_floor_seconds = 1.0e-10 * 86400.0;
        const double offsets_seconds[] = {
            -2.0 * equality_floor_seconds,
            -0.5 * equality_floor_seconds,
            0.0,
            0.5 * equality_floor_seconds,
        };
        for (size_t i = 0; i < sizeof(offsets_seconds) / sizeof(offsets_seconds[0]); ++i) {
            const bool before = offsets_seconds[i] < -equality_floor_seconds;
            taiyin::SplitJulianDate instant;
            expect(taiyin::add_seconds_to_split_jd(
                term.jd_ut, offsets_seconds[i], &instant), oracle.label);
            taiyin::SplitJulianDate local;
            expect(taiyin::add_seconds_to_split_jd(instant, 8.0 * 3600.0, &local),
                oracle.label);
            taiyin::CalendarDateTime clock;
            expect(taiyin::reverse_julian_day_split(local, &clock), oracle.label);
            taiyin::chinese_calendar::GanzhiFourPillars pillars;
            expect_status(taiyin::chinese_calendar::calculate_four_pillars(
                &calendar, instant, clock,
                taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
                &pillars, &diagnostic), oracle.label);
            expect(pillars.year == (before ? oracle.before_year : oracle.after_year),
                oracle.label);
            expect(pillars.month == (before ? oracle.before_month : oracle.after_month),
                oracle.label);
        }
    }

    const taiyin::CalendarDateTime wei_clock = {2026, 2, 18, 13, 0, 0.0};
    taiyin::SplitJulianDate wei_utc;
    expect(taiyin::julian_day_split(wei_clock, &wei_utc), "encode exact Wei-hour boundary");
    expect(taiyin::add_seconds_to_split_jd(wei_utc, -8.0 * 3600.0, &wei_utc),
        "convert exact Wei-hour boundary to UTC");
    taiyin::chinese_calendar::GanzhiFourPillars wei_pillars;
    expect_status(taiyin::chinese_calendar::calculate_four_pillars(
        &calendar, wei_utc, wei_clock,
        taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
        &wei_pillars, &diagnostic), "calculate exact 13:00 pillars");
    expect((wei_pillars.hour & 0x0fu) == 7u, "exact 13:00 enters Wei hour");

    const uint8_t expected_boundary_branches[] = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 0u,
    };
    for (int hour = 0; hour < 24; ++hour) {
        const taiyin::CalendarDateTime source = {
            2026, 4, 8, hour, 0, 0.0};
        taiyin::SplitJulianDate split_jd;
        taiyin::CalendarDateTime split_spelling;
        taiyin::CalendarDateTime split_normalized;
        expect(taiyin::julian_day_split(source, &split_jd)
            && taiyin::reverse_julian_day_split(
                split_jd, &split_spelling),
            "spell an exact civil-hour boundary through Split-JD");
        expect_status(taiyin::chinese_calendar::normalize_chart_virtual_time(
            split_spelling, &split_normalized),
            "normalize a Split-JD civil-hour spelling");
        expect(split_normalized.year == source.year
            && split_normalized.month == source.month
            && split_normalized.day == source.day
            && split_normalized.hour == source.hour
            && split_normalized.minute == 0
            && split_normalized.second == 0.0,
            "all Split-JD civil-hour spellings canonicalize exactly");

        const taiyin::CalendarDateTime scalar_spelling =
            taiyin::reverse_julian_day(taiyin::julian_day(source));
        taiyin::CalendarDateTime scalar_normalized;
        expect_status(taiyin::chinese_calendar::normalize_chart_virtual_time(
            scalar_spelling, &scalar_normalized),
            "normalize a scalar-JD civil-hour spelling");
        expect(scalar_normalized.year == source.year
            && scalar_normalized.month == source.month
            && scalar_normalized.day == source.day
            && scalar_normalized.hour == source.hour
            && scalar_normalized.minute == 0
            && scalar_normalized.second == 0.0,
            "all scalar-JD civil-hour spellings canonicalize exactly");
    }
    for (int hour = 1; hour <= 23; hour += 2) {
        const taiyin::CalendarDateTime source = {
            2026, 4, 8, hour, 0, 0.0};
        taiyin::SplitJulianDate local_jd;
        taiyin::CalendarDateTime roundtrip;
        expect(
            taiyin::julian_day_split(source, &local_jd)
                && taiyin::reverse_julian_day_split(local_jd, &roundtrip),
            "round-trip exact Shi-Chen boundary");
        taiyin::SplitJulianDate instant = local_jd;
        expect(taiyin::add_seconds_to_split_jd(
            instant, -8.0 * 3600.0, &instant),
            "convert Shi-Chen boundary to UTC");
        taiyin::chinese_calendar::GanzhiFourPillars pillars;
        expect_status(taiyin::chinese_calendar::calculate_four_pillars(
            &calendar, instant, roundtrip,
            taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
            &pillars, &diagnostic),
            "calculate round-tripped Shi-Chen boundary");
        expect(
            (pillars.hour & 0x0fu)
                == expected_boundary_branches[(hour - 1) / 2],
            "exact Shi-Chen boundary enters the new branch");
    }

    const taiyin::CalendarDateTime before_wu_source = {
        2026, 4, 8, 10, 59, 59.999};
    taiyin::CalendarDateTime before_wu_normalized;
    taiyin::SplitJulianDate before_wu_local;
    expect_status(taiyin::chinese_calendar::normalize_chart_virtual_time(
        before_wu_source, &before_wu_normalized),
        "normalize a real pre-Wu virtual time");
    expect(before_wu_normalized.hour == 10
        && before_wu_normalized.minute == 59
        && before_wu_normalized.second == before_wu_source.second,
        "normalization does not swallow a real pre-boundary millisecond");
    expect(taiyin::julian_day_split(before_wu_source, &before_wu_local),
        "encode a real pre-Wu virtual time");
    taiyin::SplitJulianDate before_wu_instant = before_wu_local;
    expect(taiyin::add_seconds_to_split_jd(
        before_wu_instant, -8.0 * 3600.0, &before_wu_instant),
        "convert a real pre-Wu virtual time to UTC");
    taiyin::chinese_calendar::GanzhiFourPillars before_wu_pillars;
    expect_status(taiyin::chinese_calendar::calculate_four_pillars(
        &calendar,
        before_wu_instant,
        before_wu_source,
        taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
        &before_wu_pillars,
        &diagnostic),
        "calculate a real pre-Wu four-pillar boundary");
    expect((before_wu_pillars.hour & 0x0fu) == 5u,
        "a real pre-boundary millisecond remains in Si hour");

    struct DayPillarOracle {
        taiyin::CalendarDateTime clock;
        uint8_t day;
        const char* label;
    };
    const DayPillarOracle day_oracles[] = {
        {{2000, 1, 7, 12, 0, 0.0}, 0x00u, "Jia-Zi day anchor"},
        {{2000, 1, 9, 12, 0, 0.0}, 0x22u, "Bing-Yin day anchor"},
    };
    for (const DayPillarOracle& oracle : day_oracles) {
        taiyin::SplitJulianDate instant;
        expect(taiyin::julian_day_split(oracle.clock, &instant), oracle.label);
        expect(taiyin::add_seconds_to_split_jd(instant, -8.0 * 3600.0, &instant),
            oracle.label);
        taiyin::chinese_calendar::GanzhiFourPillars pillars;
        expect_status(taiyin::chinese_calendar::calculate_four_pillars(
            &calendar, instant, oracle.clock,
            taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
            &pillars, &diagnostic), oracle.label);
        expect(pillars.day == oracle.day, oracle.label);
    }
}

}  // namespace

int main() {
    test_ganzhi_rules();
    test_four_pillars();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
