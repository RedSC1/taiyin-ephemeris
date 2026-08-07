#ifndef TAIYIN_BAZI_BAZI_H
#define TAIYIN_BAZI_BAZI_H

#include "taiyin/chinese_calendar/ganzhi.h"
#include "taiyin/status.h"

#include <cstddef>
#include <cstdint>

namespace taiyin {
namespace bazi {

// C++-only spellings avoid colliding with the public C ABI macros.
constexpr uint8_t kInvalidGanzhi = chinese_calendar::kInvalidGanzhi;
constexpr uint8_t kHiddenStemCapacity = 3u;
constexpr uint8_t kInvalidWuXing = 0xffu;
constexpr uint8_t kInvalidNaYin = chinese_calendar::kInvalidNaYin;
constexpr std::size_t kRenyuanSilingMaxSegments = 3u;

enum BaziWuXing {
    BaziWuXingWater = 0,
    BaziWuXingWood = 1,
    BaziWuXingMetal = 2,
    BaziWuXingEarth = 3,
    BaziWuXingFire = 4,
};

enum BaziStemRelationFlags {
    BaziStemRelationCombination = 1u << 0,
    BaziStemRelationClash = 1u << 1,
    BaziStemRelationRestraint = 1u << 2,
};

enum BaziBranchRelationFlags {
    BaziBranchRelationCombination = 1u << 0,
    BaziBranchRelationClash = 1u << 1,
    BaziBranchRelationHarm = 1u << 2,
    BaziBranchRelationDestruction = 1u << 3,
    BaziBranchRelationPunishment = 1u << 4,
    BaziBranchRelationSelfPunishment = 1u << 5,
    BaziBranchRelationHiddenCombination = 1u << 6,
    BaziBranchRelationSeverance = 1u << 7,
};

enum BaziBranchTripleRelationFlags {
    BaziBranchTripleRelationCombination = 1u << 0,
    BaziBranchTripleRelationDirection = 1u << 1,
    BaziBranchTripleRelationPunishment = 1u << 2,
};

// These values intentionally inherit bazi_core's BaziInteraction ordering.
enum BaziRelationKind {
    BaziRelationStemCombination = 0,
    BaziRelationStemClash = 1,
    BaziRelationStemRestraint = 2,
    BaziRelationBranchCombination = 3,
    BaziRelationBranchClash = 4,
    BaziRelationBranchHarm = 5,
    BaziRelationBranchDestruction = 6,
    BaziRelationBranchTriplePunishment = 7,
    BaziRelationBranchPunishment = 8,
    BaziRelationBranchSelfPunishment = 9,
    BaziRelationBranchTripleCombination = 10,
    BaziRelationBranchTripleDirection = 11,
    BaziRelationBranchHalfCombination = 12,
    BaziRelationBranchArchingCombination = 13,
    BaziRelationBranchHiddenCombination = 14,
    BaziRelationBranchSeverance = 15,
    BaziRelationKindCount = 16,
};

enum BaziRelationPillarFlags {
    BaziRelationPillarYear = 1u << 0,
    BaziRelationPillarMonth = 1u << 1,
    BaziRelationPillarDay = 1u << 2,
    BaziRelationPillarHour = 1u << 3,
    BaziRelationPillarMingGong = 1u << 4,
    BaziRelationPillarShenGong = 1u << 5,
    BaziRelationPillarTaiYuan = 1u << 6,
    BaziRelationPillarTaiXi = 1u << 7,
    BaziRelationPillarPrimary = BaziRelationPillarYear
        | BaziRelationPillarMonth
        | BaziRelationPillarDay
        | BaziRelationPillarHour,
    BaziRelationPillarExtra = BaziRelationPillarMingGong
        | BaziRelationPillarShenGong
        | BaziRelationPillarTaiYuan
        | BaziRelationPillarTaiXi,
    BaziRelationPillarAll = BaziRelationPillarPrimary | BaziRelationPillarExtra,
};

constexpr uint32_t kBaziRelationKindMaskAll =
    (1u << static_cast<uint32_t>(BaziRelationKindCount)) - 1u;

enum BaziShenShaTargetKind {
    BaziShenShaTargetYear = 0,
    BaziShenShaTargetMonth = 1,
    BaziShenShaTargetDay = 2,
    BaziShenShaTargetHour = 3,
    BaziShenShaTargetMingGong = 4,
    BaziShenShaTargetShenGong = 5,
    BaziShenShaTargetTaiYuan = 6,
    BaziShenShaTargetTaiXi = 7,
    BaziShenShaTargetDaYun = 8,
    BaziShenShaTargetFlowYear = 9,
    BaziShenShaTargetFlowMonth = 10,
    BaziShenShaTargetFlowDay = 11,
    BaziShenShaTargetFlowHour = 12,
};

enum BaziShenShaId {
    BaziShenShaTianYiGuiRen = 0,
    BaziShenShaYiMa = 1,
    BaziShenShaKongWang = 11,
    BaziShenShaTianChuGuiRenXun = 12,
    BaziShenShaXianChiTaoHua = 2,
    BaziShenShaHongLuan = 3,
    BaziShenShaTianXi = 4,
    BaziShenShaYangRen = 5,
    BaziShenShaFeiRen = 6,
    BaziShenShaFuXingGuiRen = 7,
    BaziShenShaZaiSha = 8,
    BaziShenShaJieSha = 9,
    BaziShenShaWangShen = 10,
    BaziShenShaTianChuGuiRen = 13,
    BaziShenShaDeXiuGuiRen = 14,
    BaziShenShaTianYiMedicine = 15,
    BaziShenShaXueRen = 16,
    BaziShenShaGouSha = 18,
    BaziShenShaJiaoSha = 19,
    BaziShenShaYuanChen = 20,
    BaziShenShaGuChen = 21,
    BaziShenShaGuaSu = 22,
    BaziShenShaHongYanSha = 23,
    BaziShenShaJinYu = 24,
    BaziShenShaJinShen = 25,
    BaziShenShaTongZi = 31,
    BaziShenShaTianDeHe = 32,
    BaziShenShaSanQiTian = 33,
    BaziShenShaSanQiDi = 34,
    BaziShenShaSanQiRen = 35,
    BaziShenShaYueDeHe = 17,
    BaziShenShaTianSheDay = 26,
    BaziShenShaLiuXia = 27,
    BaziShenShaSangMen = 28,
    BaziShenShaDiaoKe = 29,
    BaziShenShaPiMa = 30,
    BaziShenShaJiangXing = 36,
    BaziShenShaHuaGai = 37,
    BaziShenShaDiZhuan = 50,
    BaziShenShaTianZhuan = 51,
    BaziShenShaKuiGang = 38,
    BaziShenShaShiLingDay = 39,
    BaziShenShaBaZhuanDay = 40,
    BaziShenShaLiuXiuDay = 41,
    BaziShenShaJiuChouDay = 42,
    BaziShenShaSiFeiDay = 43,
    BaziShenShaShiEDaBai = 44,
    BaziShenShaYinChaYangCuo = 46,
    BaziShenShaGuLuanSha = 47,
    BaziShenShaTianLuoDiWang = 45,
    BaziShenShaGongLu = 48,
    BaziShenShaGongGui = 49,
    BaziShenShaTaiJiGuiRen = 52,
    BaziShenShaWenChangGuiRen = 53,
    BaziShenShaGuoYinGuiRen = 54,
    BaziShenShaYueDeGuiRen = 56,
    BaziShenShaLuShen = 57,
    BaziShenShaRiGanXueTang = 58,
    BaziShenShaRiGanCiGuan = 59,
    BaziShenShaTianDeGuiRen = 55,
    BaziShenShaZhengXueTang = 60,
    BaziShenShaZhengCiGuan = 61,
    BaziShenShaGuanGuiXueTang = 62,
    BaziShenShaGuanGuiCiGuan = 63,
    BaziShenShaGuanXingXueTang = 64,
    BaziShenShaXueTangHuiGui = 65,
};

constexpr std::size_t kBaziShenShaStableIdCount = 66u;
constexpr std::size_t kBaziShenShaWordCount =
    (kBaziShenShaStableIdCount + 63u) / 64u;

struct BaziRelation {
    int32_t kind;
    uint32_t pillar_mask;
    uint8_t combined_element_id;
    uint8_t reserved[3];
};

enum BaziEarthPalaceMode {
    BaziEarthPalaceFireEarth = 0,
    BaziEarthPalaceWaterEarth = 1,
};

enum BaziGender {
    BaziGenderFemale = 0,
    BaziGenderMale = 1,
};

enum BaziQiYunDirectionMode {
    // Yang-year men and yin-year women advance; the other combinations reverse.
    BaziQiYunDirectionYearStemGender = 0,
};

enum BaziQiYunTimeModel {
    // Three days per year, decomposed with 360-day years and 30-day months,
    // then added as civil calendar components.
    BaziQiYunTraditionalCalendar = 0,
    // Three days per 365.25-day Julian year, added as continuous duration.
    BaziQiYunJulianYear = 1,
    // Three days per Taiyin's shared mean tropical year, added as duration.
    BaziQiYunTropicalYear = 2,
};

enum BaziDaYunBoundaryModel {
    // Each da-yun boundary advances ten civil calendar years.
    BaziDaYunCivilYears = 0,
    // Each da-yun boundary advances ten 365.25-day Julian years.
    BaziDaYunJulianYears = 1,
    // Each da-yun boundary advances ten mean tropical years.
    BaziDaYunTropicalYears = 2,
};

enum BaziRenyuanSilingTableModel {
    // The month-command tables recorded in San Ming Tong Hui.
    BaziRenyuanSilingSanMingTongHui = 0,
    // A commonly circulated modern table retained for compatibility.
    BaziRenyuanSilingCommon = 1,
};

enum BaziRenyuanSilingTimeModel {
    // Continuous elapsed 24-hour days since the previous Jie.
    BaziRenyuanSilingElapsed24Hours = 0,
    // Whole local civil-day boundaries crossed since the previous Jie.
    BaziRenyuanSilingLocalCivilDays = 1,
};

enum BaziRenyuanSilingOriginKind {
    BaziRenyuanSilingOriginStem = 0,
    BaziRenyuanSilingOriginGenEarth = 1,
    BaziRenyuanSilingOriginKunEarth = 2,
};

struct BaziContextConfig {
    int32_t earth_palace_mode;
    int32_t qiyun_direction_mode;
    int32_t qiyun_time_model;
    int32_t dayun_boundary_model;

    BaziContextConfig() noexcept;
};

struct BaziContext {
    BaziContextConfig config;

    BaziContext() noexcept;
};

struct BaziExtraPillars {
    uint8_t ming_gong;
    uint8_t shen_gong;
    uint8_t tai_yuan;
    uint8_t tai_xi;

    BaziExtraPillars() noexcept;
};

struct BaziChart {
    chinese_calendar::GanzhiFourPillars pillars;
    BaziExtraPillars extra;
    uint8_t hidden_stem_count[4];
    uint8_t hidden_stems[4][kHiddenStemCapacity];
    uint8_t visible_ten_gods[4];
    uint8_t hidden_ten_gods[4][kHiddenStemCapacity];
    uint8_t life_stages[4];
    uint8_t nayin_ids[4];

    BaziChart() noexcept;
};

struct BaziQiYunResult {
    int32_t direction;
    int32_t time_model;
    uint8_t reference_jie_index;
    uint8_t reserved[3];
    double jie_interval_days;
    double start_age_years;
    int32_t offset_years;
    int32_t offset_months;
    int32_t offset_days;
    int32_t offset_hours;
    int32_t offset_minutes;
    double offset_seconds;
    SplitJulianDate reference_jie_jd_ut;
    SplitJulianDate start_jd_ut;
    CalendarDateTime start_civil_time;

    BaziQiYunResult() noexcept;
};

struct BaziDaYun {
    uint32_t index;
    uint8_t ganzhi;
    uint8_t reserved[3];
    int32_t start_virtual_age;
    int32_t end_virtual_age;
    SplitJulianDate start_jd_ut;
    SplitJulianDate end_jd_ut;
    CalendarDateTime start_civil_time;
    CalendarDateTime end_civil_time;

    BaziDaYun() noexcept;
};

struct BaziXiaoYun {
    uint32_t age;
    uint8_t ganzhi;
    uint8_t reserved[3];

    BaziXiaoYun() noexcept;
};

struct BaziRenyuanSilingSegment {
    uint8_t stem_id;
    uint8_t origin_kind;
    uint8_t segment_index;
    uint8_t reserved;
    double start_day;
    double end_day;

    BaziRenyuanSilingSegment() noexcept;
};

struct BaziRenyuanSilingResult {
    int32_t table_model;
    int32_t time_model;
    uint8_t month_branch_id;
    uint8_t stem_id;
    uint8_t origin_kind;
    uint8_t segment_index;
    uint8_t previous_jie_index;
    uint8_t reserved[3];
    double days_since_jie;
    double segment_start_day;
    double segment_end_day;
    SplitJulianDate previous_jie_jd_ut;

    BaziRenyuanSilingResult() noexcept;
};

BaziContextConfig default_context_config() noexcept;

Status initialize_context(
    BaziContext* out,
    const BaziContextConfig* config
) noexcept;

// Returns the two empty branches in the ordering used by bazi_core.
Status get_kong_wang(
    uint8_t ganzhi,
    uint8_t out_branches[2]
) noexcept;

Status get_ten_god(
    uint8_t day_stem_id,
    uint8_t target_stem_id,
    uint8_t* out_ten_god_id
) noexcept;

Status get_hidden_stems(
    uint8_t branch_id,
    uint8_t out_stems[kHiddenStemCapacity],
    uint8_t* out_count
) noexcept;

Status calculate_stem_relation(
    uint8_t stem_a,
    uint8_t stem_b,
    uint32_t* out_flags,
    uint8_t* out_combined_element_id
) noexcept;

Status calculate_branch_relation(
    uint8_t branch_a,
    uint8_t branch_b,
    uint32_t* out_flags,
    uint8_t* out_combined_element_id
) noexcept;

Status calculate_branch_triple_relation(
    uint8_t branch_a,
    uint8_t branch_b,
    uint8_t branch_c,
    uint32_t* out_flags,
    uint8_t* out_combined_element_id
) noexcept;

Status get_life_stage(
    uint8_t stem_id,
    uint8_t branch_id,
    int32_t earth_palace_mode,
    uint8_t* out_life_stage_id
) noexcept;

Status calculate_chart(
    const BaziContext* context,
    const chinese_calendar::GanzhiFourPillars& pillars,
    BaziChart* out
) noexcept;

// Flow-year/month/day/hour primitives follow bazi_core's stable IDs. The
// month argument is a branch ID (2=Yin, ..., 0=Zi, 1=Chou), while the hour
// argument is an index in the same 0=Zi, ..., 11=Hai order as Ganzhi APIs.
Status calculate_flow_year(
    int32_t civil_year,
    uint8_t* out
) noexcept;

Status calculate_flow_month(
    uint8_t year_pillar,
    uint8_t month_branch_id,
    uint8_t* out
) noexcept;

Status calculate_flow_day(
    const CalendarDateTime& civil_date,
    uint8_t* out
) noexcept;

Status calculate_flow_hour(
    uint8_t day_pillar,
    uint8_t hour_index,
    uint8_t* out
) noexcept;

// age is the one-based virtual age used by the Dart fortune table. direction
// must be +1 or -1, and the natal hour pillar is the xiao-yun base.
Status calculate_xiaoyun(
    const BaziChart* chart,
    int32_t direction,
    int32_t age,
    uint8_t* out
) noexcept;

// Fills a contiguous virtual-age range. A count-only call supplies out=nullptr
// and capacity=0.
Status fill_xiaoyun(
    const BaziChart* chart,
    int32_t direction,
    int32_t start_age,
    size_t requested_count,
    BaziXiaoYun* out,
    size_t capacity,
    size_t* out_count
) noexcept;

// Collects the merged relation graph defined by bazi_core's
// BaziInteractionCalculator. pillar_mask selects the participating columns;
// relation_mask selects relation kinds by (1u << BaziRelationKind). A
// count-only call supplies out=nullptr and capacity=0.
Status collect_chart_relations(
    const BaziChart* chart,
    uint32_t pillar_mask,
    uint32_t relation_mask,
    BaziRelation* out,
    size_t capacity,
    size_t* out_count
) noexcept;

// Returns a bitset indexed by BaziShenShaId. The natal chart supplies source
// pillars while target_ganzhi/target_kind may describe a natal or transit
// pillar. A count-only call supplies out_words=nullptr and word_capacity=0.
Status collect_target_shen_sha(
    const BaziChart* chart,
    uint8_t target_ganzhi,
    int32_t target_kind,
    uint64_t* out_words,
    size_t word_capacity,
    size_t* out_word_count
) noexcept;

// Same bitset as collect_target_shen_sha, with the gender-dependent legacy
// Dart rules enabled. gender uses BaziGenderFemale/BaziGenderMale.
Status collect_target_shen_sha_with_gender(
    const BaziChart* chart,
    uint8_t target_ganzhi,
    int32_t target_kind,
    int32_t gender,
    uint64_t* out_words,
    size_t word_capacity,
    size_t* out_word_count
) noexcept;

// birth_civil_time is the caller-selected civil clock corresponding to
// birth_jd_ut. It is used only by the traditional calendar-component model
// and to render the returned civil start time.
Status calculate_qiyun(
    const BaziContext* context,
    const chinese_calendar::ChineseCalendarContext* calendar_context,
    const SplitJulianDate& birth_jd_ut,
    const CalendarDateTime& birth_civil_time,
    const BaziChart* chart,
    int32_t gender,
    BaziQiYunResult* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

// index is one-based: index 1 is the first da-yun after the natal month.
// Time boundaries form [start_jd_ut, end_jd_ut); virtual ages retain the
// traditional inclusive display range inherited from bazi_core.
// A count-only call supplies out=nullptr and capacity=0.
Status fill_dayun(
    const BaziContext* context,
    const CalendarDateTime& birth_civil_time,
    const BaziChart* chart,
    const BaziQiYunResult* qiyun,
    size_t requested_count,
    BaziDaYun* out,
    size_t capacity,
    size_t* out_count
) noexcept;

// Returns the selected profile's ordered [start_day, end_day) segments for a
// month branch. A count-only call supplies out=nullptr and capacity=0.
Status get_renyuan_siling_segments(
    uint8_t month_branch_id,
    int32_t table_model,
    BaziRenyuanSilingSegment* out,
    size_t capacity,
    size_t* out_count
) noexcept;

// Selects the active month command at instant_jd_ut. The local-civil-day model
// uses the day boundary configured by calendar_context.
Status calculate_renyuan_siling(
    const chinese_calendar::ChineseCalendarContext* calendar_context,
    const SplitJulianDate& instant_jd_ut,
    const BaziChart* chart,
    int32_t table_model,
    int32_t time_model,
    BaziRenyuanSilingResult* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace bazi
}  // namespace taiyin

#endif
