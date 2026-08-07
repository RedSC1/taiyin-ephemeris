#include "taiyin/bazi/bazi.h"
#include "taiyin/body_id.h"
#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int fail(const char* message) {
    std::cerr << "FAIL: " << message << "\n";
    return EXIT_FAILURE;
}

bool expect_status(taiyin::Status status, const char* message) {
    if (status == taiyin::TAIYIN_STATUS_OK) return true;
    std::cerr << "FAIL: " << message << ": " << taiyin::status_name(status) << "\n";
    return false;
}

bool encode_beijing_time(
    const taiyin::CalendarDateTime& clock,
    taiyin::SplitJulianDate* out
) {
    return taiyin::julian_day_split(clock, out)
        && taiyin::add_seconds_to_split_jd(*out, -8.0 * 3600.0, out);
}

double clock_difference_seconds(
    const taiyin::CalendarDateTime& a,
    const taiyin::CalendarDateTime& b
) {
    taiyin::SplitJulianDate a_jd;
    taiyin::SplitJulianDate b_jd;
    if (!taiyin::julian_day_split(a, &a_jd)
        || !taiyin::julian_day_split(b, &b_jd)) {
        return INFINITY;
    }
    return (a_jd - b_jd) * taiyin::SECONDS_PER_DAY;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return fail("missing ephemeris data root");

    const std::string data_root = std::string(argv[1])
        + "/ephemerides/opm2/major-bodies/600y";
    const char* paths[] = {data_root.c_str()};
    taiyin::runtime::EphemerisRuntimeConfig runtime_config;
    runtime_config.source_paths = paths;
    runtime_config.source_path_count = 1;
    runtime_config.load_packaged_data = false;
    if (!taiyin::runtime::initialize_global_ephemeris_runtime(runtime_config)) {
        return fail("initialize ephemeris runtime");
    }

    taiyin::runtime::NativeCalcContext astronomy;
    if (!expect_status(taiyin::runtime::native_context_set_geocentric_observer(
            &astronomy, taiyin::TAIYIN_BODY_EARTH, taiyin::TAIYIN_BODY_EARTH),
            "set geocentric observer")
        || !expect_status(taiyin::runtime::native_context_set_route_rule(
            &astronomy, taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_AUTO),
            "set ephemeris route")) {
        return EXIT_FAILURE;
    }

    taiyin::chinese_calendar::ChineseCalendarContext calendar;
    const taiyin::chinese_calendar::ChineseCalendarConfig calendar_config =
        taiyin::chinese_calendar::fixed_utc_offset_config(8 * 60);
    if (!expect_status(taiyin::chinese_calendar::initialize_context(
            &calendar, &astronomy, &calendar_config), "initialize Chinese calendar")) {
        return EXIT_FAILURE;
    }

    taiyin::bazi::BaziContext bazi;
    const taiyin::bazi::BaziContextConfig bazi_config =
        taiyin::bazi::default_context_config();
    if (!expect_status(taiyin::bazi::initialize_context(&bazi, &bazi_config),
            "initialize BaZi")) {
        return EXIT_FAILURE;
    }

    const taiyin::CalendarDateTime beijing_clock = {2026, 3, 5, 10, 4, 0.0};
    taiyin::SplitJulianDate instant_utc;
    if (!taiyin::julian_day_split(beijing_clock, &instant_utc)
        || !taiyin::add_seconds_to_split_jd(
            instant_utc, -8.0 * 3600.0, &instant_utc)) {
        return fail("encode Beijing civil time as UTC split-JD");
    }

    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    taiyin::chinese_calendar::GanzhiFourPillars pillars;
    if (!expect_status(taiyin::chinese_calendar::calculate_four_pillars(
            &calendar,
            instant_utc,
            beijing_clock,
            taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
            &pillars,
            &diagnostic), "calculate four pillars")) {
        return EXIT_FAILURE;
    }
    if (pillars.year != 0x26u || pillars.month != 0x62u
        || pillars.day != 0x42u || pillars.hour != 0x35u) {
        return fail("calendar four-pillar oracle");
    }

    taiyin::bazi::BaziChart chart;
    if (!expect_status(taiyin::bazi::calculate_chart(&bazi, pillars, &chart),
            "interpret calendar-produced four pillars")) {
        return EXIT_FAILURE;
    }
    if (chart.pillars.year != pillars.year || chart.pillars.month != pillars.month
        || chart.pillars.day != pillars.day || chart.pillars.hour != pillars.hour
        || chart.nayin_ids[0] != 21u || chart.nayin_ids[1] != 13u
        || chart.nayin_ids[2] != 7u || chart.nayin_ids[3] != 26u) {
        return fail("BaZi chart preserves calendar pillars and NaYin IDs");
    }

    const taiyin::CalendarDateTime fortune_birth = {2026, 2, 19, 23, 28, 0.0};
    taiyin::SplitJulianDate fortune_birth_jd;
    if (!encode_beijing_time(fortune_birth, &fortune_birth_jd)) {
        return fail("encode fortune oracle birth time");
    }
    taiyin::chinese_calendar::GanzhiFourPillars fortune_pillars;
    if (!expect_status(taiyin::chinese_calendar::calculate_four_pillars(
            &calendar,
            fortune_birth_jd,
            fortune_birth,
            taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
            &fortune_pillars,
            &diagnostic), "calculate fortune oracle pillars")
        || fortune_pillars.year != 0x26u
        || fortune_pillars.month != 0x62u
        || fortune_pillars.day != 0x11u
        || fortune_pillars.hour != 0x20u) {
        return fail("fortune oracle four pillars");
    }
    taiyin::bazi::BaziChart fortune_chart;
    taiyin::bazi::BaziQiYunResult traditional_qiyun;
    if (!expect_status(taiyin::bazi::calculate_chart(
            &bazi, fortune_pillars, &fortune_chart), "build fortune oracle chart")
        || !expect_status(taiyin::bazi::calculate_qiyun(
            &bazi,
            &calendar,
            fortune_birth_jd,
            fortune_birth,
            &fortune_chart,
            taiyin::bazi::BaziGenderMale,
            &traditional_qiyun,
            &diagnostic), "calculate traditional qi-yun")) {
        return EXIT_FAILURE;
    }

    taiyin::chinese_calendar::SolarTermEvent previous_jie;
    if (!expect_status(taiyin::chinese_calendar::getPrevJie(
            &calendar, fortune_birth_jd, &previous_jie, &diagnostic),
            "find Renyuan Siling previous Jie")) {
        return EXIT_FAILURE;
    }
    taiyin::bazi::BaziRenyuanSilingResult siling_before_boundary;
    taiyin::bazi::BaziRenyuanSilingResult siling_at_boundary;
    taiyin::SplitJulianDate before_boundary;
    taiyin::SplitJulianDate at_boundary;
    if (!taiyin::add_days_to_split_jd(
            previous_jie.jd_ut, 5.0 - 1.0e-6, &before_boundary)
        || !taiyin::add_days_to_split_jd(
            previous_jie.jd_ut, 5.0, &at_boundary)
        || !expect_status(taiyin::bazi::calculate_renyuan_siling(
            &calendar,
            before_boundary,
            &fortune_chart,
            taiyin::bazi::BaziRenyuanSilingSanMingTongHui,
            taiyin::bazi::BaziRenyuanSilingElapsed24Hours,
            &siling_before_boundary,
            &diagnostic), "calculate Renyuan Siling before a segment boundary")
        || !expect_status(taiyin::bazi::calculate_renyuan_siling(
            &calendar,
            at_boundary,
            &fortune_chart,
            taiyin::bazi::BaziRenyuanSilingSanMingTongHui,
            taiyin::bazi::BaziRenyuanSilingElapsed24Hours,
            &siling_at_boundary,
            &diagnostic), "calculate Renyuan Siling at a segment boundary")) {
        return EXIT_FAILURE;
    }
    if (siling_before_boundary.month_branch_id != 2u
        || siling_before_boundary.stem_id != 4u
        || siling_before_boundary.origin_kind
            != taiyin::bazi::BaziRenyuanSilingOriginGenEarth
        || siling_before_boundary.segment_index != 0u
        || siling_at_boundary.stem_id != 2u
        || siling_at_boundary.segment_index != 1u
        || siling_at_boundary.segment_start_day != 5.0
        || std::fabs(siling_at_boundary.previous_jie_jd_ut - previous_jie.jd_ut)
            > 1.0e-9) {
        return fail("Renyuan Siling uses half-open segment boundaries");
    }

    taiyin::SplitJulianDate previous_jie_local;
    taiyin::CalendarDateTime previous_jie_clock;
    taiyin::SplitJulianDate next_local_midnight;
    if (!taiyin::add_seconds_to_split_jd(
            previous_jie.jd_ut, 8.0 * 3600.0, &previous_jie_local)
        || !taiyin::reverse_julian_day_split(
            previous_jie_local, &previous_jie_clock)) {
        return fail("decode Renyuan Siling previous Jie in Beijing time");
    }
    const taiyin::CalendarDateTime local_midnight = {
        previous_jie_clock.year,
        previous_jie_clock.month,
        previous_jie_clock.day,
        0,
        0,
        0.0,
    };
    if (!taiyin::julian_day_split(local_midnight, &next_local_midnight)
        || !taiyin::add_days_to_split_jd(
            next_local_midnight, 1.0, &next_local_midnight)
        || !taiyin::add_seconds_to_split_jd(
            next_local_midnight, -8.0 * 3600.0, &next_local_midnight)) {
        return fail("encode next Beijing civil midnight");
    }
    taiyin::bazi::BaziRenyuanSilingResult elapsed_siling;
    taiyin::bazi::BaziRenyuanSilingResult civil_siling;
    if (!expect_status(taiyin::bazi::calculate_renyuan_siling(
            &calendar,
            next_local_midnight,
            &fortune_chart,
            taiyin::bazi::BaziRenyuanSilingSanMingTongHui,
            taiyin::bazi::BaziRenyuanSilingElapsed24Hours,
            &elapsed_siling,
            &diagnostic), "calculate elapsed-day Renyuan Siling")
        || !expect_status(taiyin::bazi::calculate_renyuan_siling(
            &calendar,
            next_local_midnight,
            &fortune_chart,
            taiyin::bazi::BaziRenyuanSilingSanMingTongHui,
            taiyin::bazi::BaziRenyuanSilingLocalCivilDays,
            &civil_siling,
            &diagnostic), "calculate civil-day Renyuan Siling")) {
        return EXIT_FAILURE;
    }
    if (!(elapsed_siling.days_since_jie >= 0.0
            && elapsed_siling.days_since_jie < 1.0)
        || civil_siling.days_since_jie != 1.0
        || elapsed_siling.time_model
            != taiyin::bazi::BaziRenyuanSilingElapsed24Hours
        || civil_siling.time_model
            != taiyin::bazi::BaziRenyuanSilingLocalCivilDays) {
        return fail("Renyuan Siling keeps elapsed and civil-day models distinct");
    }
    const taiyin::CalendarDateTime dart_qiyun_oracle = {
        2030, 10, 12, 13, 27, 49.0};
    const double offset_seconds_of_day =
        traditional_qiyun.offset_hours * 3600.0
        + traditional_qiyun.offset_minutes * 60.0
        + traditional_qiyun.offset_seconds;
    if (traditional_qiyun.direction != 1
        || traditional_qiyun.time_model
            != taiyin::bazi::BaziQiYunTraditionalCalendar
        || !(traditional_qiyun.jie_interval_days > 13.0
            && traditional_qiyun.jie_interval_days < 15.0)
        || std::fabs(traditional_qiyun.start_age_years
            - traditional_qiyun.jie_interval_days / 3.0) > 1.0e-13
        || std::fabs(clock_difference_seconds(
            traditional_qiyun.start_civil_time, dart_qiyun_oracle)) > 120.0
        || traditional_qiyun.offset_years != 4
        || traditional_qiyun.offset_months != 7
        || traditional_qiyun.offset_days != 22
        || std::fabs(offset_seconds_of_day - (13.0 * 3600.0 + 59.0 * 60.0 + 49.0))
            > 120.0) {
        return fail("traditional qi-yun Dart oracle");
    }

    taiyin::bazi::BaziDaYun dayun[8];
    size_t dayun_count = 0;
    const uint8_t expected_dayun[8] = {
        0x73u, 0x84u, 0x95u, 0x06u, 0x17u, 0x28u, 0x39u, 0x4au,
    };
    if (!expect_status(taiyin::bazi::fill_dayun(
            &bazi,
            fortune_birth,
            &fortune_chart,
            &traditional_qiyun,
            8,
            nullptr,
            0,
            &dayun_count), "count da-yun")
        || dayun_count != 8
        || !expect_status(taiyin::bazi::fill_dayun(
            &bazi,
            fortune_birth,
            &fortune_chart,
            &traditional_qiyun,
            8,
            dayun,
            8,
            &dayun_count), "fill da-yun")) {
        return EXIT_FAILURE;
    }
    for (size_t i = 0; i < 8; ++i) {
        if (dayun[i].index != i + 1u || dayun[i].ganzhi != expected_dayun[i]
            || dayun[i].start_civil_time.year != 2030 + static_cast<int>(i) * 10
            || dayun[i].end_civil_time.year != 2040 + static_cast<int>(i) * 10
            || dayun[i].start_virtual_age != 5 + static_cast<int>(i) * 10
            || dayun[i].end_virtual_age != 14 + static_cast<int>(i) * 10) {
            return fail("da-yun Dart oracle");
        }
    }

    taiyin::bazi::BaziContextConfig alternate_config = bazi_config;
    alternate_config.qiyun_time_model = taiyin::bazi::BaziQiYunJulianYear;
    alternate_config.dayun_boundary_model = taiyin::bazi::BaziDaYunJulianYears;
    taiyin::bazi::BaziContext julian_bazi;
    taiyin::bazi::BaziQiYunResult julian_qiyun;
    if (!expect_status(taiyin::bazi::initialize_context(
            &julian_bazi, &alternate_config), "initialize Julian qi-yun")
        || !expect_status(taiyin::bazi::calculate_qiyun(
            &julian_bazi,
            &calendar,
            fortune_birth_jd,
            fortune_birth,
            &fortune_chart,
            taiyin::bazi::BaziGenderMale,
            &julian_qiyun,
            &diagnostic), "calculate Julian qi-yun")
        || std::fabs((julian_qiyun.start_jd_ut - fortune_birth_jd)
            - julian_qiyun.jie_interval_days
                * taiyin::DAYS_PER_JULIAN_YEAR / 3.0) > 1.0e-11) {
        return fail("Julian qi-yun duration");
    }
    taiyin::bazi::BaziDaYun julian_dayun[2];
    if (!expect_status(taiyin::bazi::fill_dayun(
            &julian_bazi,
            fortune_birth,
            &fortune_chart,
            &julian_qiyun,
            2,
            julian_dayun,
            2,
            &dayun_count), "fill Julian-year da-yun")
        || std::fabs((julian_dayun[1].start_jd_ut
            - julian_dayun[0].start_jd_ut)
            - 10.0 * taiyin::DAYS_PER_JULIAN_YEAR) > 1.0e-11) {
        return fail("Julian-year da-yun boundary");
    }
    alternate_config.qiyun_time_model = taiyin::bazi::BaziQiYunTropicalYear;
    alternate_config.dayun_boundary_model = taiyin::bazi::BaziDaYunTropicalYears;
    taiyin::bazi::BaziContext tropical_bazi;
    taiyin::bazi::BaziQiYunResult tropical_qiyun;
    if (!expect_status(taiyin::bazi::initialize_context(
            &tropical_bazi, &alternate_config), "initialize tropical qi-yun")
        || !expect_status(taiyin::bazi::calculate_qiyun(
            &tropical_bazi,
            &calendar,
            fortune_birth_jd,
            fortune_birth,
            &fortune_chart,
            taiyin::bazi::BaziGenderMale,
            &tropical_qiyun,
            &diagnostic), "calculate tropical qi-yun")
        || std::fabs((tropical_qiyun.start_jd_ut - fortune_birth_jd)
            - tropical_qiyun.jie_interval_days
                * taiyin::DAYS_PER_TROPICAL_YEAR / 3.0) > 1.0e-11
        || !(julian_qiyun.start_jd_ut > tropical_qiyun.start_jd_ut)) {
        return fail("tropical qi-yun duration");
    }
    taiyin::bazi::BaziDaYun tropical_dayun[2];
    if (!expect_status(taiyin::bazi::fill_dayun(
            &tropical_bazi,
            fortune_birth,
            &fortune_chart,
            &tropical_qiyun,
            2,
            tropical_dayun,
            2,
            &dayun_count), "fill tropical-year da-yun")
        || std::fabs((tropical_dayun[1].start_jd_ut
            - tropical_dayun[0].start_jd_ut)
            - 10.0 * taiyin::DAYS_PER_TROPICAL_YEAR) > 1.0e-11) {
        return fail("tropical-year da-yun boundary");
    }

    taiyin::bazi::BaziQiYunResult reverse_qiyun;
    if (!expect_status(taiyin::bazi::calculate_qiyun(
            &bazi,
            &calendar,
            fortune_birth_jd,
            fortune_birth,
            &fortune_chart,
            taiyin::bazi::BaziGenderFemale,
            &reverse_qiyun,
            &diagnostic), "calculate reverse qi-yun")
        || reverse_qiyun.direction != -1
        || !(reverse_qiyun.reference_jie_jd_ut < fortune_birth_jd)) {
        return fail("female reverse qi-yun direction");
    }
    taiyin::bazi::BaziDaYun reverse_dayun;
    if (!expect_status(taiyin::bazi::fill_dayun(
            &bazi,
            fortune_birth,
            &fortune_chart,
            &reverse_qiyun,
            1,
            &reverse_dayun,
            1,
            &dayun_count), "fill reverse da-yun")
        || reverse_dayun.ganzhi != 0x51u) {
        return fail("reverse da-yun advances before natal month");
    }

    taiyin::chinese_calendar::SolarTermEvent exact_jie;
    if (!expect_status(taiyin::chinese_calendar::getNextJie(
            &calendar, fortune_birth_jd, &exact_jie, &diagnostic),
            "find exact qi-yun boundary")) {
        return EXIT_FAILURE;
    }
    taiyin::SplitJulianDate exact_civil_jd;
    taiyin::CalendarDateTime exact_civil;
    if (!taiyin::add_seconds_to_split_jd(
            exact_jie.jd_ut, 8.0 * 3600.0, &exact_civil_jd)
        || !taiyin::reverse_julian_day_split(exact_civil_jd, &exact_civil)) {
        return fail("decode exact qi-yun boundary");
    }
    taiyin::bazi::BaziQiYunResult exact_qiyun;
    if (!expect_status(taiyin::bazi::calculate_qiyun(
            &bazi,
            &calendar,
            exact_jie.jd_ut,
            exact_civil,
            &fortune_chart,
            taiyin::bazi::BaziGenderMale,
            &exact_qiyun,
            &diagnostic), "calculate exact-boundary qi-yun")
        || exact_qiyun.jie_interval_days != 0.0
        || exact_qiyun.offset_years != 0
        || exact_qiyun.offset_months != 0
        || exact_qiyun.offset_days != 0
        || exact_qiyun.offset_hours != 0
        || exact_qiyun.offset_minutes != 0
        || exact_qiyun.offset_seconds != 0.0
        || exact_qiyun.start_jd_ut != exact_jie.jd_ut
        || std::fabs(clock_difference_seconds(
            exact_qiyun.start_civil_time, exact_civil)) > 1.0e-8) {
        return fail("exact Jie must start qi-yun immediately");
    }

    const taiyin::CalendarDateTime leap_qiyun_civil = {
        2032, 2, 29, 12, 0, 0.0};
    taiyin::bazi::BaziQiYunResult leap_qiyun;
    leap_qiyun.direction = 1;
    leap_qiyun.time_model = taiyin::bazi::BaziQiYunTraditionalCalendar;
    leap_qiyun.start_civil_time = leap_qiyun_civil;
    if (!encode_beijing_time(leap_qiyun_civil, &leap_qiyun.start_jd_ut)) {
        return fail("encode leap-day qi-yun fixture");
    }
    taiyin::bazi::BaziDaYun leap_dayun[2];
    if (!expect_status(taiyin::bazi::fill_dayun(
            &bazi,
            fortune_birth,
            &fortune_chart,
            &leap_qiyun,
            2,
            leap_dayun,
            2,
            &dayun_count), "fill leap-day da-yun")
        || leap_dayun[1].start_civil_time.year != 2042
        || leap_dayun[1].start_civil_time.month != 3
        || leap_dayun[1].start_civil_time.day != 1) {
        return fail("civil da-yun preserves Dart leap-day rollover");
    }
    return EXIT_SUCCESS;
}
