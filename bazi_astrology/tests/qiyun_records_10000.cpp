#include "taiyin/bazi/bazi.h"
#include "taiyin/body_id.h"
#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

constexpr std::size_t kSampleCount = 10000u;

bool expect_ok(taiyin::Status status) {
    return status == taiyin::TAIYIN_STATUS_OK;
}

uint64_t double_bits(double value) {
    uint64_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void print_record(std::size_t index, const taiyin::bazi::BaziQiYunResult& value) {
    const taiyin::CalendarDateTime& civil = value.start_civil_time;
    std::cout << index
              << ' ' << value.direction
              << ' ' << value.time_model
              << ' ' << static_cast<unsigned int>(value.reference_jie_index)
              << ' ' << std::hex
              << double_bits(value.jie_interval_days)
              << ' ' << double_bits(value.start_age_years)
              << ' ' << value.offset_years
              << ' ' << value.offset_months
              << ' ' << value.offset_days
              << ' ' << value.offset_hours
              << ' ' << value.offset_minutes
              << ' ' << double_bits(value.offset_seconds)
              << ' ' << value.reference_jie_jd_ut.day_number
              << ' ' << double_bits(value.reference_jie_jd_ut.day_fraction)
              << ' ' << value.start_jd_ut.day_number
              << ' ' << double_bits(value.start_jd_ut.day_fraction)
              << ' ' << civil.year
              << ' ' << civil.month
              << ' ' << civil.day
              << ' ' << civil.hour
              << ' ' << civil.minute
              << ' ' << double_bits(civil.second)
              << std::dec << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: qiyun_records_10000 <data-root>\n";
        return 2;
    }

    const std::string data_root = std::string(argv[1])
        + "/ephemerides/opm2/major-bodies/600y";
    const char* source_paths[] = {data_root.c_str()};
    taiyin::runtime::EphemerisRuntimeConfig runtime_config;
    runtime_config.source_paths = source_paths;
    runtime_config.source_path_count = 1u;
    runtime_config.load_packaged_data = false;
    if (!taiyin::runtime::initialize_global_ephemeris_runtime(runtime_config)) return 3;

    taiyin::runtime::NativeCalcContext astronomy;
    if (!expect_ok(taiyin::runtime::native_context_set_geocentric_observer(
            &astronomy, taiyin::TAIYIN_BODY_EARTH, taiyin::TAIYIN_BODY_EARTH))
        || !expect_ok(taiyin::runtime::native_context_set_route_rule(
            &astronomy, taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_AUTO))) return 4;

    taiyin::chinese_calendar::ChineseCalendarContext calendar;
    const taiyin::chinese_calendar::ChineseCalendarConfig calendar_config =
        taiyin::chinese_calendar::fixed_utc_offset_config(8 * 60);
    if (!expect_ok(taiyin::chinese_calendar::initialize_context(
            &calendar, &astronomy, &calendar_config))) return 5;

    taiyin::bazi::BaziContext bazi;
    const taiyin::bazi::BaziContextConfig bazi_config =
        taiyin::bazi::default_context_config();
    if (!expect_ok(taiyin::bazi::initialize_context(&bazi, &bazi_config))) return 6;

    const taiyin::CalendarDateTime first_local = {1900, 1, 1, 0, 0, 0.0};
    taiyin::SplitJulianDate first_local_jd;
    if (!taiyin::julian_day_split(first_local, &first_local_jd)) return 7;

    for (std::size_t i = 0u; i < kSampleCount; ++i) {
        taiyin::SplitJulianDate local_jd;
        const double seconds_of_day = static_cast<double>((i * 7331u) % 86400u);
        const double elapsed_days = static_cast<double>(i * 7u)
            + seconds_of_day / taiyin::SECONDS_PER_DAY;
        if (!taiyin::add_days_to_split_jd(first_local_jd, elapsed_days, &local_jd)) return 8;
        const taiyin::CalendarDateTime local_civil = taiyin::reverse_julian_day(
            taiyin::split_julian_date_to_double(local_jd));

        taiyin::SplitJulianDate birth_jd_ut;
        if (!taiyin::add_seconds_to_split_jd(
                local_jd, -8.0 * 3600.0, &birth_jd_ut)) return 9;

        taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
        taiyin::chinese_calendar::GanzhiFourPillars pillars;
        if (!expect_ok(taiyin::chinese_calendar::calculate_four_pillars(
                &calendar, birth_jd_ut, local_civil,
                taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
                &pillars, &diagnostic))) return 10;

        taiyin::bazi::BaziChart chart;
        if (!expect_ok(taiyin::bazi::calculate_chart(&bazi, pillars, &chart))) return 11;

        taiyin::bazi::BaziQiYunResult qiyun;
        const int32_t gender = (i & 1u) == 0u
            ? taiyin::bazi::BaziGenderFemale : taiyin::bazi::BaziGenderMale;
        if (!expect_ok(taiyin::bazi::calculate_qiyun(
                &bazi, &calendar, birth_jd_ut, local_civil, &chart, gender,
                &qiyun, &diagnostic))) return 12;
        print_record(i, qiyun);
    }
    return 0;
}
