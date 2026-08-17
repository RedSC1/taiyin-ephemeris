#include "taiyin/body_id.h"
#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"
#include "taiyin/ziwei/calendar_adapter.h"
#include "taiyin/ziwei/rules_loader.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifndef TAIYIN_ZIWEI_RULES_FILE
#define TAIYIN_ZIWEI_RULES_FILE "ziwei_astrology/rules/default.toml"
#endif

namespace {

bool encode_china_standard(
    const taiyin::CalendarDateTime& local,
    taiyin::SplitJulianDate* out
) {
    return taiyin::julian_day_split(local, out)
        && taiyin::add_seconds_to_split_jd(*out, -8.0 * 3600.0, out);
}

void append_ganzhi(
    const taiyin::ziwei::Ganzhi& value,
    std::vector<int64_t>* out
) {
    out->push_back(taiyin::ziwei::to_index(value.stem));
    out->push_back(taiyin::ziwei::to_index(value.branch));
}

void append_pillars(
    const taiyin::ziwei::Pillars& value,
    std::vector<int64_t>* out
) {
    append_ganzhi(value.year, out);
    append_ganzhi(value.month, out);
    append_ganzhi(value.day, out);
    append_ganzhi(value.hour, out);
}

void print_record(const std::vector<int64_t>& values) {
    for (std::size_t i = 0u; i < values.size(); ++i) {
        if (i != 0u) std::cout << ',';
        std::cout << values[i];
    }
    std::cout << '\n';
}

void append_palace_transform_masks(
    const taiyin::ziwei::NatalChart& chart,
    std::size_t star_count,
    std::vector<int64_t>* out
) {
    using namespace taiyin::ziwei;
    for (std::size_t star = 0u; star < star_count; ++star) {
        const StarTransformMask mask = star_transform_mask(
            chart, static_cast<StarId>(star));
        out->push_back(mask);
    }
}

taiyin::ziwei::Ganzhi cycle_ganzhi(int index) {
    return taiyin::ziwei::Ganzhi{
        static_cast<taiyin::ziwei::Stem>(index % 10),
        static_cast<taiyin::ziwei::Branch>(index % 12),
    };
}

int run_finite(int32_t rat_mode, int64_t max_records) {
    using namespace taiyin;
    using namespace taiyin::ziwei;
    if (rat_mode < chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT
        || rat_mode > chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TOMORROW_GAN) {
        return 64;
    }
    const LoadedRules loaded = load_rules_from_toml(TAIYIN_ZIWEI_RULES_FILE);
    const AnchorOptions anchor_options = default_anchor_options();
    int64_t record_count = 0;
    std::ios::sync_with_stdio(false);
    std::cout << "# taiyin-ziwei-finite-v2 mode=" << rat_mode << '\n';
    for (int year_cycle = 0; year_cycle < 60; ++year_cycle) {
        const Ganzhi year = cycle_ganzhi(year_cycle);
        for (uint8_t month = 1u; month <= 12u; ++month) {
            const int month_stem = ((to_index(year.stem) % 5u) * 2u + 2u
                + month - 1u) % 10u;
            const Ganzhi month_pillar = {
                static_cast<Stem>(month_stem),
                static_cast<Branch>((month + 1u) % 12u),
            };
            for (uint8_t day = 1u; day <= 30u; ++day) {
                const Ganzhi day_pillar = cycle_ganzhi(day - 1u);
                for (uint8_t hour = 0u; hour < 12u; ++hour) {
                    uint8_t hour_day_stem = to_index(day_pillar.stem);
                    if (hour == 0u
                        && rat_mode
                            == chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TOMORROW_GAN) {
                        hour_day_stem = static_cast<uint8_t>(
                            (hour_day_stem + 1u) % 10u);
                    }
                    const Ganzhi hour_pillar = {
                        static_cast<Stem>(
                            ((hour_day_stem % 5u) * 2u + hour) % 10u),
                        static_cast<Branch>(hour),
                    };
                    for (uint8_t gender = 0u; gender < 2u; ++gender) {
                        CalendarFacts facts = {};
                        facts.birth.gender = static_cast<Gender>(gender);
                        facts.lunar_date.year = 1984 + year_cycle;
                        facts.lunar_date.month = month;
                        facts.lunar_date.day = day;
                        facts.effective_lunar_year = 1984 + year_cycle;
                        facts.effective_lunar_month = month;
                        facts.solar_day_from_previous_jie = day;
                        facts.solar_term_pillars = Pillars{
                            year, month_pillar, day_pillar, hour_pillar,
                        };
                        facts.lunar_pillars = facts.solar_term_pillars;
                        Anchors anchors;
                        Branch body = Branch::Zi;
                        NatalChart chart;
                        Status status = compute_anchors(
                            facts, anchor_options, &anchors, &body);
                        if (status == TAIYIN_STATUS_OK) {
                            status = make_natal_chart(
                                facts,
                                anchors,
                                body,
                                anchor_options.rules,
                                loaded.compiled,
                                &chart);
                        }
                        std::vector<uint8_t> positions;
                        if (status == TAIYIN_STATUS_OK) {
                            status = dump_natal_star_positions(chart, &positions);
                        }
                        if (status != TAIYIN_STATUS_OK) return 3;
                        std::vector<int64_t> record;
                        record.reserve(271u);
                        record.push_back(1984 + year_cycle);
                        record.push_back(month);
                        record.push_back(day);
                        record.push_back(hour);
                        record.push_back(0);
                        record.push_back(gender);
                        record.push_back(facts.effective_lunar_year);
                        record.push_back(facts.effective_lunar_month);
                        append_pillars(anchors.solar_term, &record);
                        append_pillars(anchors.lunar, &record);
                        record.push_back(to_index(anchors.palace_positions[
                            to_index(PalaceId::Life)]));
                        record.push_back(to_index(body));
                        record.push_back(to_index(anchors.bureau));
                        for (std::size_t branch = 0u;
                             branch < chart.palace_stems.size(); ++branch) {
                            record.push_back(to_index(chart.palace_stems[branch]));
                        }
                        record.push_back(chart.life_master);
                        record.push_back(chart.body_master);
                        for (std::size_t star = 0u;
                             star < loaded.compiled.natal_star_count; ++star) {
                            record.push_back(positions[star]);
                        }
                        append_palace_transform_masks(
                            chart, loaded.compiled.natal_star_count, &record);
                        print_record(record);
                        ++record_count;
                        if (max_records >= 0 && record_count >= max_records) {
                            return 0;
                        }
                    }
                }
            }
        }
    }
    return record_count == 518400 ? 0 : 3;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace taiyin;
    using namespace taiyin::ziwei;
    if ((argc == 3 || argc == 4) && std::string(argv[1]) == "finite") {
        return run_finite(
            std::atoi(argv[2]), argc == 4 ? std::atoll(argv[3]) : -1);
    }
    if (argc != 4 && argc != 5) {
        std::cerr << "usage: dump_ziwei_exhaustive MODE START_YEAR END_YEAR"
                     " [MAX_RECORDS]\n"
                     "   or: dump_ziwei_exhaustive finite MODE [MAX_RECORDS]\n";
        return 64;
    }
    const int32_t rat_mode = std::atoi(argv[1]);
    const int32_t start_year = std::atoi(argv[2]);
    const int32_t end_year = std::atoi(argv[3]);
    const int64_t max_records = argc == 5 ? std::atoll(argv[4]) : -1;
    if (rat_mode < chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT
        || rat_mode > chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TOMORROW_GAN
        || end_year < start_year) {
        std::cerr << "invalid exhaustive range or Rat-hour mode\n";
        return 64;
    }

    runtime::EphemerisRuntimeConfig runtime_config;
    runtime_config.load_packaged_data = true;
    if (!runtime::initialize_global_ephemeris_runtime(runtime_config)) return 2;
    runtime::NativeCalcContext astronomy;
    if (runtime::native_context_set_geocentric_observer(
            &astronomy, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH)
            != TAIYIN_STATUS_OK
        || runtime::native_context_set_route_rule(
            &astronomy, runtime::TAIYIN_EPHEMERIS_ROUTE_AUTO)
            != TAIYIN_STATUS_OK) {
        return 2;
    }
    chinese_calendar::ChineseCalendarContext calendar;
    const chinese_calendar::ChineseCalendarConfig calendar_config =
        chinese_calendar::historical_china_config();
    if (chinese_calendar::initialize_context(
            &calendar, &astronomy, &calendar_config) != TAIYIN_STATUS_OK) {
        return 2;
    }
    const LoadedRules loaded = load_rules_from_toml(TAIYIN_ZIWEI_RULES_FILE);
    BirthResolutionOptions options = default_birth_resolution_options();
    options.rat_hour_mode = rat_mode;
    runtime::EphemerisEvalDiagnostic diagnostic;

    const CalendarDateTime start_clock = {start_year, 1, 1, 0, 0, 0.0};
    const CalendarDateTime end_clock = {end_year + 1, 1, 1, 0, 0, 0.0};
    SplitJulianDate start_jd;
    SplitJulianDate end_jd;
    if (!julian_day_split(start_clock, &start_jd)
        || !julian_day_split(end_clock, &end_jd)) {
        return 2;
    }
    const int64_t day_count = static_cast<int64_t>(
        days_between_split_jd(start_jd, end_jd));
    const bool split_rat = rat_mode
        != chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT;
    const int hours[13] = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 23};
    const std::size_t hour_count = split_rat ? 13u : 12u;

    std::ios::sync_with_stdio(false);
    std::cout << "# taiyin-ziwei-exhaustive-v2 mode=" << rat_mode
              << " years=" << start_year << ".." << end_year << '\n';
    int64_t record_count = 0;
    for (int64_t day_offset = 0; day_offset < day_count; ++day_offset) {
        CalendarDateTime date;
        if (!reverse_julian_day_split(start_jd + static_cast<double>(day_offset),
                &date)) {
            return 2;
        }
        for (std::size_t hour_slot = 0u; hour_slot < hour_count; ++hour_slot) {
            CalendarDateTime local = date;
            local.hour = hours[hour_slot];
            local.minute = (hours[hour_slot] == 0 || hours[hour_slot] == 23)
                ? 30 : 0;
            local.second = 0.0;
            SplitJulianDate instant;
            if (!encode_china_standard(local, &instant)) return 2;
            for (uint8_t gender_index = 0u; gender_index < 2u; ++gender_index) {
                const Gender gender = static_cast<Gender>(gender_index);
                ResolvedBirth birth;
                NatalChart chart;
                Status status = resolve_birth_from_calendar(
                    &calendar,
                    instant,
                    local,
                    gender,
                    options,
                    &birth,
                    &diagnostic);
                if (status == TAIYIN_STATUS_OK) {
                    status = make_natal_chart(
                        birth.facts,
                        birth.anchors,
                        birth.body_palace,
                        options.anchor_options.rules,
                        loaded.compiled,
                        &chart);
                }
                std::vector<uint8_t> positions;
                if (status == TAIYIN_STATUS_OK) {
                    status = dump_natal_star_positions(chart, &positions);
                }
                if (status != TAIYIN_STATUS_OK
                    || positions.size() < loaded.compiled.natal_star_count) {
                    std::cerr << "chart failure at " << local.year << '-'
                              << local.month << '-' << local.day << ' '
                              << local.hour << ':' << local.minute
                              << " gender=" << static_cast<int>(gender_index)
                              << " status=" << status << '\n';
                    return 3;
                }

                std::vector<int64_t> record;
                record.reserve(271u);
                record.push_back(local.year);
                record.push_back(local.month);
                record.push_back(local.day);
                record.push_back(local.hour);
                record.push_back(local.minute);
                record.push_back(gender_index);
                record.push_back(birth.facts.effective_lunar_year);
                record.push_back(birth.facts.effective_lunar_month);
                append_pillars(birth.anchors.solar_term, &record);
                append_pillars(birth.anchors.lunar, &record);
                record.push_back(to_index(birth.anchors.palace_positions[
                    to_index(PalaceId::Life)]));
                record.push_back(to_index(birth.body_palace));
                record.push_back(to_index(birth.anchors.bureau));
                for (std::size_t branch = 0u;
                     branch < chart.palace_stems.size(); ++branch) {
                    record.push_back(to_index(chart.palace_stems[branch]));
                }
                record.push_back(chart.life_master);
                record.push_back(chart.body_master);
                for (std::size_t star = 0u;
                     star < loaded.compiled.natal_star_count; ++star) {
                    record.push_back(positions[star]);
                }
                append_palace_transform_masks(
                    chart, loaded.compiled.natal_star_count, &record);
                print_record(record);
                ++record_count;
                if (max_records >= 0 && record_count >= max_records) return 0;
            }
        }
    }
    return 0;
}
