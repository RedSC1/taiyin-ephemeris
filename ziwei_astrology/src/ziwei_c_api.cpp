#include "taiyin/c/ziwei.h"

#include "c_api_internal.h"
#include "chinese_calendar_context_internal.h"
#include "taiyin/ziwei/ziweicore.h"

#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct taiyin_ziwei_data_catalog {
    taiyin::ziwei::ZiweiDataCatalog value;

    explicit taiyin_ziwei_data_catalog(const std::string& path)
        : value(path) {}
};

struct taiyin_ziwei_context {
    taiyin::ziwei::ZiweiContext value;

    explicit taiyin_ziwei_context(taiyin::ziwei::ZiweiContext context)
        : value(std::move(context)) {}
};

struct taiyin_ziwei_ruleset {
    taiyin::ziwei::ZiweiRuleset value;
};

struct taiyin_ziwei_chart {
    taiyin::ziwei::ResolvedBirth birth;
    taiyin::ziwei::Chart value;
    uint64_t registry_fingerprint;
};

namespace {

template <typename T>
void init_struct(T* value) noexcept {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

taiyin_status exception_status() noexcept {
    try {
        throw;
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (const taiyin::ziwei::RuleFileNotFoundError&) {
        return TAIYIN_FILE_ERROR_NOT_FOUND;
    } catch (const taiyin::ziwei::RuleLoadError&) {
        return TAIYIN_FILE_ERROR_BAD_FORMAT;
    } catch (const std::invalid_argument&) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    } catch (...) {
        return TAIYIN_ERROR_INTERNAL;
    }
}

bool valid_level(int32_t value) noexcept {
    return value >= TAIYIN_ZIWEI_FLOW_DECADE
        && value <= TAIYIN_ZIWEI_FLOW_HOUR;
}

bool valid_star_transform_mark(int32_t value) noexcept {
    return value >= TAIYIN_ZIWEI_BIRTH_YEAR_LU
        && value <= TAIYIN_ZIWEI_CENTRIPETAL_JI;
}

bool valid_pillar_boundary(int32_t value) noexcept {
    return value >= TAIYIN_ZIWEI_PILLAR_SOLAR_TERM
        && value <= TAIYIN_ZIWEI_PILLAR_LUNAR;
}

bool valid_birth_option_values(
    const taiyin_ziwei_birth_options& value
) noexcept {
    return value.rat_hour_mode
            >= taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT
        && value.rat_hour_mode
            <= taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TOMORROW_GAN
        && value.leap_month_strategy >= TAIYIN_ZIWEI_LEAP_AS_PREVIOUS
        && value.leap_month_strategy
            <= TAIYIN_ZIWEI_LEAP_SPLIT_AFTER_FIFTEENTH
        && value.chart_mode >= TAIYIN_ZIWEI_CHART_TIAN_PAN
        && value.chart_mode <= TAIYIN_ZIWEI_CHART_REN_PAN
        && valid_pillar_boundary(value.wu_hu_dun_year_boundary)
        && valid_pillar_boundary(value.sihua_year_boundary)
        && valid_pillar_boundary(value.body_master_year_boundary);
}

bool valid_flow_option_values(
    const taiyin_ziwei_flow_options& value
) noexcept {
    return valid_pillar_boundary(value.boundary)
        && value.rat_hour_mode
            >= taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT
        && value.rat_hour_mode
            <= taiyin::chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TOMORROW_GAN
        && value.childhood_strategy >= TAIYIN_ZIWEI_CHILDHOOD_SKIP
        && value.childhood_strategy <= TAIYIN_ZIWEI_CHILDHOOD_SEQUENTIAL
        && value.flow_month_palace_strategy
            >= TAIYIN_ZIWEI_FLOW_MONTH_PALACE_PHYSICAL_SEQUENCE
        && value.flow_month_palace_strategy
            <= TAIYIN_ZIWEI_FLOW_MONTH_PALACE_EFFECTIVE_MONTH;
}

taiyin::ziwei::BirthResolutionOptions to_cpp_birth_options(
    const taiyin_ziwei_birth_options& value
) noexcept {
    taiyin::ziwei::BirthResolutionOptions out;
    out.rat_hour_mode = value.rat_hour_mode;
    out.leap_month_strategy =
        static_cast<taiyin::ziwei::LeapMonthStrategy>(value.leap_month_strategy);
    out.anchor_options.chart_mode =
        static_cast<taiyin::ziwei::ZiweiChartMode>(value.chart_mode);
    out.anchor_options.rules.wu_hu_dun_year_boundary =
        static_cast<taiyin::ziwei::PillarBoundary>(
            value.wu_hu_dun_year_boundary);
    out.anchor_options.rules.sihua_year_boundary =
        static_cast<taiyin::ziwei::PillarBoundary>(value.sihua_year_boundary);
    out.anchor_options.rules.body_master_year_boundary =
        static_cast<taiyin::ziwei::PillarBoundary>(
            value.body_master_year_boundary);
    return out;
}

taiyin::ziwei::FlowResolutionOptions to_cpp_flow_options(
    const taiyin_ziwei_flow_options& value
) noexcept {
    taiyin::ziwei::FlowResolutionOptions out;
    out.boundary = static_cast<taiyin::ziwei::PillarBoundary>(value.boundary);
    out.rat_hour_mode = value.rat_hour_mode;
    out.childhood_strategy =
        static_cast<taiyin::ziwei::ChildhoodStrategy>(
            value.childhood_strategy);
    out.flow_month_palace_strategy =
        static_cast<taiyin::ziwei::FlowMonthPalaceStrategy>(
            value.flow_month_palace_strategy);
    return out;
}

taiyin::ziwei::Tier1ReverseQuery to_cpp_reverse_query(
    const taiyin_ziwei_reverse_query& value
) noexcept {
    taiyin::ziwei::Tier1ReverseQuery out;
    out.lucun_branch = value.lucun_branch;
    out.hongluan_branch = value.hongluan_branch;
    out.zuofu_branch = value.zuofu_branch;
    out.youbi_branch = value.youbi_branch;
    out.wenchang_branch = value.wenchang_branch;
    out.wenqu_branch = value.wenqu_branch;
    out.santai_branch = value.santai_branch;
    out.bazuo_branch = value.bazuo_branch;
    out.ziwei_branch = value.ziwei_branch;
    return out;
}

void copy_reverse_candidate(
    const taiyin::ziwei::ReverseLookupCandidate& source,
    taiyin_ziwei_reverse_candidate* out
) noexcept {
    init_struct(out);
    taiyin_calendar_datetime_init(&out->virtual_time);
    taiyin_c_internal::from_cpp_split_jd(source.instant_utc, &out->instant_utc);
    taiyin_c_internal::from_cpp_datetime(source.virtual_time, &out->virtual_time);
    out->lunar_year = source.lunar_date.year;
    out->lunar_month = source.lunar_date.month;
    out->lunar_day = source.lunar_date.day;
    out->lunar_is_leap = source.lunar_date.is_leap;
    out->hour_branch = source.hour_branch;
    out->rat_hour_segment = taiyin::ziwei::to_index(source.rat_hour_segment);
}

void copy_transforms(
    const taiyin::ziwei::TransformSet& source,
    taiyin_ziwei_transform_set* out
) noexcept {
    init_struct(out);
    out->lu = source.lu;
    out->quan = source.quan;
    out->ke = source.ke;
    out->ji = source.ji;
}

void copy_flow_summary(
    const taiyin::ziwei::ResolvedFlow& source,
    taiyin_ziwei_flow_summary* out
) noexcept {
    init_struct(out);
    out->effective_birth_year = source.effective_birth_year;
    out->effective_target_year = source.effective_target_year;
    out->target_month = source.target_month;
    out->target_month_sequence = source.target_month_sequence;
    out->target_month_building_branch =
        taiyin::ziwei::to_index(source.target_month_building_branch);
    out->target_day = source.target_day;
    out->target_hour_index = source.target_hour_index;
    out->target_rat_hour_segment =
        taiyin::ziwei::to_index(source.target_rat_hour_segment);
    out->target_month_is_leap = source.target_month_is_leap ? 1u : 0u;
    out->target_effective_month = source.month.effective_month;
    out->target_month_name = source.target_month_name;
    out->target_palace_month_index = source.month.palace_month_index;
    out->target_lunar_year = source.month.year;
    out->decade_index = source.decade.index;
    out->decade_start_age = source.decade.start_age;
    out->decade_end_age = source.decade.end_age;
    out->small_limit_virtual_age = source.small_limit.virtual_age;
    out->small_limit_stem =
        taiyin::ziwei::to_index(source.small_limit.coordinate.stem);
    out->small_limit_branch =
        taiyin::ziwei::to_index(source.small_limit.coordinate.branch);
}

bool apply_override(
    const taiyin_ziwei_option_override& source,
    taiyin::ziwei::ZiweiOptionSelection* out
) {
    if (!out || source.struct_size < sizeof(source)
        || !source.option || source.option[0] == '\0') {
        return false;
    }
    const std::string option(source.option);
    const bool has_key = source.key && source.key[0] != '\0';
    const std::string key = has_key ? std::string(source.key) : std::string();
    switch (source.component) {
    case TAIYIN_ZIWEI_OPTION_PLACEMENT:
        if (has_key) out->placement[key] = option;
        else out->placement_default = option;
        return true;
    case TAIYIN_ZIWEI_OPTION_BRIGHTNESS:
        if (has_key) out->brightness[key] = option;
        else out->brightness_default = option;
        return true;
    case TAIYIN_ZIWEI_OPTION_SIHUA:
        if (has_key) out->sihua[key] = option;
        else out->sihua_default = option;
        return true;
    case TAIYIN_ZIWEI_OPTION_MASTERS:
        if (has_key) return false;
        out->masters = option;
        return true;
    case TAIYIN_ZIWEI_OPTION_LONGEVITY:
        if (has_key) return false;
        out->longevity = option;
        return true;
    default:
        return false;
    }
}

const taiyin::ziwei::FlowLayer* find_flow_layer(
    const taiyin_ziwei_chart* chart,
    int32_t level
) noexcept {
    if (!chart || !valid_level(level)) return NULL;
    for (std::size_t i = 0u; i < chart->value.flow_stack.size(); ++i) {
        if (taiyin::ziwei::to_index(chart->value.flow_stack[i].level)
                == static_cast<uint8_t>(level)) {
            return &chart->value.flow_stack[i];
        }
    }
    return NULL;
}

bool chart_matches_context(
    const taiyin_ziwei_chart* chart,
    const taiyin_ziwei_context* context
) noexcept {
    return chart != NULL && context != NULL
        && chart->registry_fingerprint != 0u
        && chart->registry_fingerprint
            == context->value.compiled_tables().registry_fingerprint;
}

}  // namespace

extern "C" {

void TAIYIN_C_CALL taiyin_ziwei_option_override_init(
    taiyin_ziwei_option_override* value
) {
    init_struct(value);
}

void TAIYIN_C_CALL taiyin_ziwei_json_rule_module_init(
    taiyin_ziwei_json_rule_module* value
) {
    init_struct(value);
}

void TAIYIN_C_CALL taiyin_ziwei_birth_options_init(
    taiyin_ziwei_birth_options* value
) {
    init_struct(value);
    if (!value) return;
    const taiyin::ziwei::BirthResolutionOptions defaults =
        taiyin::ziwei::default_birth_resolution_options();
    value->rat_hour_mode = defaults.rat_hour_mode;
    value->leap_month_strategy =
        static_cast<int32_t>(defaults.leap_month_strategy);
    value->chart_mode = static_cast<int32_t>(
        defaults.anchor_options.chart_mode);
    value->wu_hu_dun_year_boundary = static_cast<int32_t>(
        defaults.anchor_options.rules.wu_hu_dun_year_boundary);
    value->sihua_year_boundary = static_cast<int32_t>(
        defaults.anchor_options.rules.sihua_year_boundary);
    value->body_master_year_boundary = static_cast<int32_t>(
        defaults.anchor_options.rules.body_master_year_boundary);
}

void TAIYIN_C_CALL taiyin_ziwei_flow_options_init(
    taiyin_ziwei_flow_options* value
) {
    init_struct(value);
    if (!value) return;
    const taiyin::ziwei::FlowResolutionOptions defaults =
        taiyin::ziwei::default_flow_resolution_options();
    value->boundary = static_cast<int32_t>(defaults.boundary);
    value->rat_hour_mode = defaults.rat_hour_mode;
    value->childhood_strategy =
        static_cast<int32_t>(defaults.childhood_strategy);
    value->flow_month_palace_strategy =
        static_cast<int32_t>(defaults.flow_month_palace_strategy);
}

void TAIYIN_C_CALL taiyin_ziwei_transform_set_init(
    taiyin_ziwei_transform_set* value
) {
    init_struct(value);
    if (!value) return;
    value->lu = TAIYIN_ZIWEI_INVALID_STAR_ID;
    value->quan = TAIYIN_ZIWEI_INVALID_STAR_ID;
    value->ke = TAIYIN_ZIWEI_INVALID_STAR_ID;
    value->ji = TAIYIN_ZIWEI_INVALID_STAR_ID;
}

void TAIYIN_C_CALL taiyin_ziwei_flow_summary_init(
    taiyin_ziwei_flow_summary* value
) {
    init_struct(value);
    if (!value) return;
    value->target_month = 0xffu;
    value->target_month_sequence = 0xffu;
    value->target_month_building_branch = 0xffu;
    value->target_effective_month = 0xffu;
    value->target_month_name = 0xffu;
    value->target_palace_month_index = 0xffu;
    value->target_day = 0xffu;
    value->target_hour_index = 0xffu;
    value->target_rat_hour_segment = 0xffu;
    value->small_limit_stem = 0xffu;
    value->small_limit_branch = 0xffu;
}

void TAIYIN_C_CALL taiyin_ziwei_reverse_query_init(
    taiyin_ziwei_reverse_query* value
) {
    init_struct(value);
    if (!value) return;
    value->lucun_branch = taiyin::ziwei::kReverseUnspecified;
    value->hongluan_branch = taiyin::ziwei::kReverseUnspecified;
    value->zuofu_branch = taiyin::ziwei::kReverseUnspecified;
    value->youbi_branch = taiyin::ziwei::kReverseUnspecified;
    value->wenchang_branch = taiyin::ziwei::kReverseUnspecified;
    value->wenqu_branch = taiyin::ziwei::kReverseUnspecified;
    value->santai_branch = taiyin::ziwei::kReverseUnspecified;
    value->bazuo_branch = taiyin::ziwei::kReverseUnspecified;
    value->ziwei_branch = taiyin::ziwei::kReverseUnspecified;
}

void TAIYIN_C_CALL taiyin_ziwei_reverse_request_init(
    taiyin_ziwei_reverse_request* value
) {
    init_struct(value);
    if (!value) return;
    taiyin_calendar_datetime_init(&value->start_virtual_time);
    value->gender = TAIYIN_ZIWEI_GENDER_MALE;
    taiyin_ziwei_birth_options_init(&value->birth_options);
    taiyin_ziwei_reverse_query_init(&value->query);
}

void TAIYIN_C_CALL taiyin_ziwei_reverse_candidate_init(
    taiyin_ziwei_reverse_candidate* value
) {
    init_struct(value);
    if (!value) return;
    taiyin_calendar_datetime_init(&value->virtual_time);
    value->hour_branch = TAIYIN_ZIWEI_INVALID_POSITION;
    value->rat_hour_segment = 0xffu;
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_data_catalog_create(
    const char* profile_path,
    taiyin_ziwei_data_catalog** out_catalog
) {
    if (out_catalog) *out_catalog = NULL;
    if (!profile_path || profile_path[0] == '\0' || !out_catalog) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    try {
        taiyin_ziwei_data_catalog* created =
            new taiyin_ziwei_data_catalog(profile_path);
        *out_catalog = created;
        return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
    } catch (...) {
        return taiyin_c_internal::pack_call_result(exception_status());
    }
}

void TAIYIN_C_CALL taiyin_ziwei_data_catalog_destroy(
    taiyin_ziwei_data_catalog* catalog
) {
    delete catalog;
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_data_catalog_reload(
    taiyin_ziwei_data_catalog* catalog
) {
    if (!catalog) return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    try {
        catalog->value.reload();
        return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
    } catch (...) {
        return taiyin_c_internal::pack_call_result(exception_status());
    }
}

uint64_t TAIYIN_C_CALL taiyin_ziwei_data_catalog_generation(
    const taiyin_ziwei_data_catalog* catalog
) {
    return catalog ? catalog->value.generation() : 0u;
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_ruleset_create(
    taiyin_ziwei_ruleset** out_ruleset
) {
    if (out_ruleset) *out_ruleset = NULL;
    if (!out_ruleset) return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    try {
        *out_ruleset = new taiyin_ziwei_ruleset();
        return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
    } catch (...) {
        return taiyin_c_internal::pack_call_result(exception_status());
    }
}

void TAIYIN_C_CALL taiyin_ziwei_ruleset_destroy(
    taiyin_ziwei_ruleset* ruleset
) {
    delete ruleset;
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_ruleset_add_json_module(
    taiyin_ziwei_ruleset* ruleset,
    const taiyin_ziwei_json_rule_module* module
) {
    if (!ruleset || !taiyin_c_internal::valid_struct(module)
        || !module->label || module->label[0] == '\0') {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    try {
        const char* empty = "";
        taiyin::ziwei::ZiweiJsonRuleModuleInput input;
        input.label = module->label;
        input.stars_json = module->stars_json ? module->stars_json : empty;
        input.brightness_json = module->brightness_json
            ? module->brightness_json : empty;
        input.sihua_json = module->sihua_json ? module->sihua_json : empty;
        input.flow_json = module->flow_json ? module->flow_json : empty;
        input.masters_json = module->masters_json ? module->masters_json : empty;
        const taiyin::ziwei::ZiweiRuleset replacement =
            taiyin::ziwei::ZiweiConfigLoader::add_json_module(
                ruleset->value, input);
        ruleset->value = replacement;
        return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
    } catch (...) {
        return taiyin_c_internal::pack_call_result(exception_status());
    }
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_ruleset_remove_module(
    taiyin_ziwei_ruleset* ruleset,
    const char* label
) {
    if (!ruleset || !label || label[0] == '\0') {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    try {
        ruleset->value = ruleset->value.remove_module(label);
        return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
    } catch (...) {
        return taiyin_c_internal::pack_call_result(exception_status());
    }
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_context_create(
    const taiyin_ziwei_data_catalog* catalog,
    const taiyin_ziwei_option_override* overrides,
    size_t override_count,
    taiyin_ziwei_context** out_context
) {
    return taiyin_ziwei_context_create_with_ruleset(
        catalog, overrides, override_count, NULL, out_context);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_context_create_with_ruleset(
    const taiyin_ziwei_data_catalog* catalog,
    const taiyin_ziwei_option_override* overrides,
    size_t override_count,
    const taiyin_ziwei_ruleset* ruleset,
    taiyin_ziwei_context** out_context
) {
    if (out_context) *out_context = NULL;
    if (!catalog || !out_context || (override_count != 0u && !overrides)) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    try {
        taiyin::ziwei::ZiweiOptionSelection selection;
        for (std::size_t i = 0u; i < override_count; ++i) {
            if (!apply_override(overrides[i], &selection)) {
                return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
            }
        }
        taiyin_ziwei_context* created = new taiyin_ziwei_context(
            ruleset
                ? catalog->value.create_context(selection, ruleset->value)
                : catalog->value.create_context(selection));
        *out_context = created;
        return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
    } catch (...) {
        return taiyin_c_internal::pack_call_result(exception_status());
    }
}

void TAIYIN_C_CALL taiyin_ziwei_context_destroy(
    taiyin_ziwei_context* context
) {
    delete context;
}

uint64_t TAIYIN_C_CALL taiyin_ziwei_context_generation(
    const taiyin_ziwei_context* context
) {
    return context ? context->value.catalog_generation() : 0u;
}

size_t TAIYIN_C_CALL taiyin_ziwei_star_count(
    const taiyin_ziwei_context* context
) {
    return context ? context->value.star_registry().size() : 0u;
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_find_star(
    const taiyin_ziwei_context* context,
    const char* key,
    uint16_t* out_star_id
) {
    if (out_star_id) *out_star_id = TAIYIN_ZIWEI_INVALID_STAR_ID;
    if (!context || !key || !out_star_id) return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    try {
        return taiyin_c_internal::pack_call_result(context->value.star_registry().find(key, out_star_id)
            ? TAIYIN_STATUS_OK : TAIYIN_ERROR_INVALID_ARGUMENT);
    } catch (...) {
        return taiyin_c_internal::pack_call_result(exception_status());
    }
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_get_star_metadata(
    const taiyin_ziwei_context* context,
    uint16_t star_id,
    int32_t* out_category,
    char* buffer,
    size_t capacity,
    size_t* out_required_size
) {
    if (out_required_size) *out_required_size = 0u;
    if (!context || !out_category || !out_required_size
        || star_id >= context->value.star_registry().size()
        || (capacity != 0u && !buffer)) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    try {
        const taiyin::ziwei::StarMetadata& metadata =
            context->value.star_registry().at(star_id);
        *out_category = static_cast<int32_t>(metadata.category);
        *out_required_size = metadata.key.size() + 1u;
        if (!buffer) return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
        if (capacity < *out_required_size) return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_OUT_OF_MEMORY);
        std::memcpy(buffer, metadata.key.c_str(), *out_required_size);
        return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
    } catch (...) {
        return taiyin_c_internal::pack_call_result(exception_status());
    }
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_star_is_natal(
    const taiyin_ziwei_context* context,
    uint16_t star_id,
    uint8_t* out_is_natal
) {
    if (out_is_natal) *out_is_natal = 0u;
    if (!context || !out_is_natal
        || star_id >= context->value.star_registry().size()) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    try {
        *out_is_natal = context->value.star_registry().at(star_id).natal ? 1u : 0u;
        return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
    } catch (...) {
        return taiyin_c_internal::pack_call_result(exception_status());
    }
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_create(
    const taiyin_ziwei_context* context,
    const taiyin_chinese_calendar_context* calendar_context,
    const taiyin_split_julian_date* instant_utc,
    const taiyin_calendar_datetime* virtual_time,
    int32_t gender,
    const taiyin_ziwei_birth_options* options,
    taiyin_ziwei_chart** out_chart,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (out_chart) *out_chart = NULL;
    if (!context || !calendar_context
        || !taiyin_c_internal::valid_split_jd(instant_utc)
        || !taiyin_c_internal::valid_struct(virtual_time)
        || !taiyin_c_internal::valid_struct(options)
        || !valid_birth_option_values(*options)
        || gender < TAIYIN_ZIWEI_GENDER_MALE
        || gender > TAIYIN_ZIWEI_GENDER_FEMALE
        || !out_chart
        || (diagnostic && !taiyin_c_internal::valid_struct(diagnostic))) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    try {
        taiyin_ziwei_chart* created = new taiyin_ziwei_chart();
        taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
        const taiyin::ziwei::BirthResolutionOptions cpp_options =
            to_cpp_birth_options(*options);
        taiyin_c_internal::TrackedCalendarContext tracked(calendar_context->value);
        taiyin::Status status = taiyin::ziwei::resolve_birth_from_calendar(
            &tracked.value,
            taiyin_c_internal::to_cpp_split_jd(*instant_utc),
            taiyin_c_internal::to_cpp_datetime(*virtual_time),
            static_cast<taiyin::ziwei::Gender>(gender),
            cpp_options,
            &created->birth,
            diagnostic ? &cpp_diagnostic : NULL);
        if (status == TAIYIN_STATUS_OK) {
            status = taiyin::ziwei::make_natal_chart(
                created->birth.facts,
                created->birth.anchors,
                created->birth.body_palace,
                cpp_options.anchor_options.rules,
                context->value.compiled_tables(),
                &created->value.natal);
        }
        if (diagnostic) {
            taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
        }
        if (status != TAIYIN_STATUS_OK) {
            delete created;
            return taiyin_c_internal::pack_call_result(status, tracked.flags);
        }
        created->registry_fingerprint =
            context->value.compiled_tables().registry_fingerprint;
        *out_chart = created;
        return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK, tracked.flags);
    } catch (...) {
        return taiyin_c_internal::pack_call_result(exception_status());
    }
}

void TAIYIN_C_CALL taiyin_ziwei_chart_destroy(taiyin_ziwei_chart* chart) {
    delete chart;
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_reverse_lookup_tier1(
    const taiyin_ziwei_context* context,
    const taiyin_chinese_calendar_context* calendar_context,
    const taiyin_ziwei_reverse_request* request,
    taiyin_ziwei_reverse_candidate* out_candidates,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (out_count) *out_count = 0u;
    if (!context || !calendar_context || !request || !out_count
        || !taiyin_c_internal::valid_struct(request)
        || !taiyin_c_internal::valid_split_jd(&request->start_instant_utc)
        || !taiyin_c_internal::valid_split_jd(&request->end_instant_utc)
        || !taiyin_c_internal::valid_struct(&request->start_virtual_time)
        || !taiyin_c_internal::valid_struct(&request->birth_options)
        || !valid_birth_option_values(request->birth_options)
        || !taiyin_c_internal::valid_struct(&request->query)
        || request->gender < TAIYIN_ZIWEI_GENDER_MALE
        || request->gender > TAIYIN_ZIWEI_GENDER_FEMALE
        || (capacity != 0u && !out_candidates)
        || (diagnostic && !taiyin_c_internal::valid_struct(diagnostic))) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    try {
        taiyin::ziwei::ReverseLookupRequest cpp_request;
        cpp_request.start_instant_utc = taiyin_c_internal::to_cpp_split_jd(
            request->start_instant_utc);
        cpp_request.end_instant_utc = taiyin_c_internal::to_cpp_split_jd(
            request->end_instant_utc);
        cpp_request.start_virtual_time = taiyin_c_internal::to_cpp_datetime(
            request->start_virtual_time);
        cpp_request.gender = static_cast<taiyin::ziwei::Gender>(request->gender);
        cpp_request.birth_options = to_cpp_birth_options(request->birth_options);
        cpp_request.query = to_cpp_reverse_query(request->query);
        std::vector<taiyin::ziwei::ReverseLookupCandidate> candidates;
        taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
        taiyin_c_internal::TrackedCalendarContext tracked(calendar_context->value);
        const taiyin::Status status =
            taiyin::ziwei::reverse_lookup_tier1_from_calendar(
                &tracked.value, cpp_request,
                context->value.compiled_tables(), context->value.star_registry(),
                &candidates, diagnostic ? &cpp_diagnostic : NULL);
        if (diagnostic) {
            taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
        }
        if (status != TAIYIN_STATUS_OK) return taiyin_c_internal::pack_call_result(status, tracked.flags);
        *out_count = candidates.size();
        if (!out_candidates) return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK, tracked.flags);
        if (capacity < candidates.size()) return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_OUT_OF_MEMORY, tracked.flags);
        for (std::size_t i = 0u; i < candidates.size(); ++i) {
            copy_reverse_candidate(candidates[i], &out_candidates[i]);
        }
        return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK, tracked.flags);
    } catch (...) {
        return taiyin_c_internal::pack_call_result(exception_status());
    }
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_anchors(
    const taiyin_ziwei_chart* chart,
    uint8_t out_anchors[TAIYIN_ZIWEI_ANCHOR_COUNT]
) {
    if (!chart || !out_anchors) return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    const std::array<uint8_t, taiyin::ziwei::kAnchorCount> anchors =
        taiyin::ziwei::flatten_anchors(chart->value.natal.anchors);
    std::memcpy(out_anchors, anchors.data(), anchors.size());
    return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_summary(
    const taiyin_ziwei_chart* chart,
    uint8_t* out_gender,
    uint8_t* out_bureau,
    uint8_t* out_body_palace,
    uint16_t* out_life_master,
    uint16_t* out_body_master,
    taiyin_ziwei_transform_set* out_transforms
) {
    if (!chart || !out_gender || !out_bureau || !out_body_palace
        || !out_life_master || !out_body_master
        || !taiyin_c_internal::valid_struct(out_transforms)) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    *out_gender = taiyin::ziwei::to_index(chart->value.natal.gender);
    *out_bureau = taiyin::ziwei::to_index(chart->value.natal.anchors.bureau);
    *out_body_palace =
        taiyin::ziwei::to_index(chart->value.natal.body_palace);
    *out_life_master = chart->value.natal.life_master;
    *out_body_master = chart->value.natal.body_master;
    copy_transforms(chart->value.natal.transformations.birth_year, out_transforms);
    return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_palace_branch(
    const taiyin_ziwei_chart* chart,
    uint8_t palace_id,
    uint8_t* out_branch
) {
    if (!chart || !out_branch || palace_id >= taiyin::ziwei::kPalaceCount) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    *out_branch = taiyin::ziwei::to_index(
        chart->value.natal.anchors.palace_positions[palace_id]);
    return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_palace_stem(
    const taiyin_ziwei_chart* chart,
    uint8_t branch,
    uint8_t* out_stem
) {
    if (!chart || !out_stem || branch >= taiyin::ziwei::kBranchCount) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    *out_stem = taiyin::ziwei::to_index(
        chart->value.natal.palace_stems[branch]);
    return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_star_position(
    const taiyin_ziwei_chart* chart,
    uint16_t star_id,
    uint8_t* out_branch
) {
    if (out_branch) *out_branch = TAIYIN_ZIWEI_INVALID_POSITION;
    if (!chart || !out_branch
        || star_id >= chart->value.natal.palaces[0].stars.size()) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    for (uint8_t branch = 0u; branch < taiyin::ziwei::kBranchCount; ++branch) {
        if (chart->value.natal.palaces[branch].stars.test(star_id)) {
            *out_branch = branch;
            return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
        }
    }
    return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_star_palace(
    const taiyin_ziwei_chart* chart,
    uint16_t star_id,
    uint8_t* out_palace_id
) {
    if (out_palace_id) *out_palace_id = TAIYIN_ZIWEI_INVALID_POSITION;
    if (!chart || !out_palace_id) return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    uint8_t branch = TAIYIN_ZIWEI_INVALID_POSITION;
    const taiyin_status status = taiyin_ziwei_chart_get_star_position(
        chart, star_id, &branch);
    if (status != TAIYIN_STATUS_OK
        || branch == TAIYIN_ZIWEI_INVALID_POSITION) {
        return taiyin_c_internal::pack_call_result(status);
    }
    for (uint8_t palace = 0u; palace < taiyin::ziwei::kPalaceCount; ++palace) {
        if (taiyin::ziwei::to_index(
                chart->value.natal.anchors.palace_positions[palace]) == branch) {
            *out_palace_id = palace;
            break;
        }
    }
    return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_brightness(
    const taiyin_ziwei_context* context,
    const taiyin_ziwei_chart* chart,
    uint16_t star_id,
    int32_t* out_brightness
) {
    if (!context || !chart || !out_brightness
        || !chart_matches_context(chart, context)) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    uint8_t branch = TAIYIN_ZIWEI_INVALID_POSITION;
    taiyin_status status = taiyin_ziwei_chart_get_star_position(
        chart, star_id, &branch);
    if (status != TAIYIN_STATUS_OK) return taiyin_c_internal::pack_call_result(status);
    if (branch == TAIYIN_ZIWEI_INVALID_POSITION) {
        *out_brightness = TAIYIN_ZIWEI_BRIGHTNESS_NONE;
        return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
    }
    taiyin::ziwei::Brightness brightness = taiyin::ziwei::Brightness::None;
    status = taiyin::ziwei::brightness_at(
        context->value.compiled_tables(), star_id,
        static_cast<taiyin::ziwei::Branch>(branch), &brightness);
    if (status == TAIYIN_STATUS_OK) {
        *out_brightness = static_cast<int32_t>(brightness);
    }
    return taiyin_c_internal::pack_call_result(status);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_has_star_transform_mark(
    const taiyin_ziwei_chart* chart,
    int32_t mark,
    uint16_t star_id,
    uint8_t* out_has_mark
) {
    if (out_has_mark) *out_has_mark = 0u;
    if (!chart || !out_has_mark || !valid_star_transform_mark(mark)
        || star_id >= chart->value.natal.palaces[0].stars.size()) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    *out_has_mark = taiyin::ziwei::has_star_transform_mark(
        chart->value.natal,
        static_cast<taiyin::ziwei::StarTransformMark>(mark), star_id) ? 1u : 0u;
    return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_star_transformation_mask(
    const taiyin_ziwei_chart* chart,
    uint16_t star_id,
    uint16_t* out_mask
) {
    if (out_mask) *out_mask = 0u;
    if (!chart || !out_mask
        || star_id >= chart->value.natal.palaces[0].stars.size()) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    *out_mask = taiyin::ziwei::star_transform_mask(
        chart->value.natal, star_id);
    return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_palace_stars(
    const taiyin_ziwei_chart* chart,
    uint8_t branch,
    uint16_t* out_star_ids,
    size_t capacity,
    size_t* out_count
) {
    if (out_count) *out_count = 0u;
    if (!chart || branch >= taiyin::ziwei::kBranchCount || !out_count
        || (capacity != 0u && !out_star_ids)) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    const taiyin::ziwei::DynamicBitset& stars =
        chart->value.natal.palaces[branch].stars;
    *out_count = stars.count();
    if (!out_star_ids) return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
    if (capacity < *out_count) return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_OUT_OF_MEMORY);
    std::size_t cursor = 0u;
    for (std::size_t id = 0u; id < stars.size(); ++id) {
        if (stars.test(id)) out_star_ids[cursor++] = static_cast<uint16_t>(id);
    }
    return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_set_flow(
    const taiyin_ziwei_context* context,
    const taiyin_chinese_calendar_context* calendar_context,
    const taiyin_split_julian_date* target_instant_utc,
    const taiyin_calendar_datetime* target_virtual_time,
    const taiyin_ziwei_flow_options* options,
    int32_t deepest_level,
    taiyin_ziwei_chart* chart,
    taiyin_ziwei_flow_summary* out_summary,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !calendar_context
        || !taiyin_c_internal::valid_split_jd(target_instant_utc)
        || !taiyin_c_internal::valid_struct(target_virtual_time)
        || !taiyin_c_internal::valid_struct(options)
        || !valid_flow_option_values(*options)
        || !valid_level(deepest_level)
        || !chart || !taiyin_c_internal::valid_struct(out_summary)
        || (diagnostic && !taiyin_c_internal::valid_struct(diagnostic))
        || !chart_matches_context(chart, context)) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    taiyin::ziwei::ResolvedFlow resolved;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        taiyin::ziwei::set_flow_stack_through_from_calendar(
            &calendar_context->value,
            chart->birth,
            taiyin_c_internal::to_cpp_split_jd(*target_instant_utc),
            taiyin_c_internal::to_cpp_datetime(*target_virtual_time),
            to_cpp_flow_options(*options),
            static_cast<taiyin::ziwei::FlowLevel>(deepest_level),
            context->value.compiled_tables(),
            &chart->value,
            &resolved,
            diagnostic ? &cpp_diagnostic : NULL);
    if (diagnostic) {
        taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    }
    if (status == TAIYIN_STATUS_OK) copy_flow_summary(resolved, out_summary);
    return taiyin_c_internal::pack_call_result(status);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_truncate_flow(
    taiyin_ziwei_chart* chart,
    int32_t first_removed_level
) {
    if (!chart || !valid_level(first_removed_level)) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    return taiyin_c_internal::pack_call_result(taiyin::ziwei::truncate_flow_stack(
        &chart->value,
        static_cast<taiyin::ziwei::FlowLevel>(first_removed_level)));
}

size_t TAIYIN_C_CALL taiyin_ziwei_chart_flow_layer_count(
    const taiyin_ziwei_chart* chart
) {
    return chart ? chart->value.flow_stack.size() : 0u;
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_flow_star_position(
    const taiyin_ziwei_chart* chart,
    int32_t level,
    uint16_t star_id,
    uint8_t* out_branch
) {
    if (out_branch) *out_branch = TAIYIN_ZIWEI_INVALID_POSITION;
    const taiyin::ziwei::FlowLayer* layer = find_flow_layer(chart, level);
    if (!layer || !out_branch || star_id >= layer->stars[0].size()) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    for (uint8_t branch = 0u; branch < taiyin::ziwei::kBranchCount; ++branch) {
        if (layer->stars[branch].test(star_id)) {
            *out_branch = branch;
            break;
        }
    }
    return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_flow_layer_summary(
    const taiyin_ziwei_chart* chart,
    int32_t level,
    uint8_t* out_life_palace,
    uint8_t* out_coordinate_stem,
    uint8_t* out_coordinate_branch
) {
    const taiyin::ziwei::FlowLayer* layer = find_flow_layer(chart, level);
    if (!layer || !out_life_palace || !out_coordinate_stem
        || !out_coordinate_branch) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    *out_life_palace = taiyin::ziwei::to_index(layer->life_palace);
    *out_coordinate_stem = taiyin::ziwei::to_index(layer->coordinate.stem);
    *out_coordinate_branch = taiyin::ziwei::to_index(layer->coordinate.branch);
    return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_flow_palace_stars(
    const taiyin_ziwei_chart* chart,
    int32_t level,
    uint8_t branch,
    uint16_t* out_star_ids,
    size_t capacity,
    size_t* out_count
) {
    if (out_count) *out_count = 0u;
    const taiyin::ziwei::FlowLayer* layer = find_flow_layer(chart, level);
    if (!layer || branch >= taiyin::ziwei::kBranchCount || !out_count
        || (capacity != 0u && !out_star_ids)) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    const taiyin::ziwei::DynamicBitset& stars = layer->stars[branch];
    *out_count = stars.count();
    if (!out_star_ids) return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
    if (capacity < *out_count) return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_OUT_OF_MEMORY);
    std::size_t cursor = 0u;
    for (std::size_t id = 0u; id < stars.size(); ++id) {
        if (stars.test(id)) out_star_ids[cursor++] = static_cast<uint16_t>(id);
    }
    return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_flow_transforms(
    const taiyin_ziwei_chart* chart,
    int32_t level,
    taiyin_ziwei_transform_set* out_transforms
) {
    const taiyin::ziwei::FlowLayer* layer = find_flow_layer(chart, level);
    if (!layer || !taiyin_c_internal::valid_struct(out_transforms)) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    copy_transforms(layer->transforms, out_transforms);
    return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_step_flow_hour_target(
    const taiyin_split_julian_date* current_instant_utc,
    const taiyin_calendar_datetime* current_virtual_time,
    int32_t rat_hour_mode,
    int32_t direction,
    taiyin_split_julian_date* out_instant_utc,
    taiyin_calendar_datetime* out_virtual_time,
    uint8_t* out_rat_hour_segment
) {
    if (!taiyin_c_internal::valid_split_jd(current_instant_utc)
        || !taiyin_c_internal::valid_struct(current_virtual_time)
        || !out_instant_utc
        || !taiyin_c_internal::valid_struct(out_virtual_time)
        || !out_rat_hour_segment) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    taiyin::SplitJulianDate cpp_instant;
    taiyin::CalendarDateTime cpp_time;
    taiyin::ziwei::RatHourSegment cpp_segment =
        taiyin::ziwei::RatHourSegment::None;
    const taiyin::Status status = taiyin::ziwei::step_flow_hour_target(
        taiyin_c_internal::to_cpp_split_jd(*current_instant_utc),
        taiyin_c_internal::to_cpp_datetime(*current_virtual_time),
        rat_hour_mode,
        direction,
        &cpp_instant,
        &cpp_time,
        &cpp_segment);
    if (status == TAIYIN_STATUS_OK) {
        taiyin_c_internal::from_cpp_split_jd(cpp_instant, out_instant_utc);
        taiyin_c_internal::from_cpp_datetime(cpp_time, out_virtual_time);
        *out_rat_hour_segment = taiyin::ziwei::to_index(cpp_segment);
    }
    return taiyin_c_internal::pack_call_result(status);
}

taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_step_flow_day_target(
    const taiyin_split_julian_date* current_instant_utc,
    const taiyin_calendar_datetime* current_virtual_time,
    int32_t direction,
    taiyin_split_julian_date* out_instant_utc,
    taiyin_calendar_datetime* out_virtual_time
) {
    if (!taiyin_c_internal::valid_split_jd(current_instant_utc)
        || !taiyin_c_internal::valid_struct(current_virtual_time)
        || !out_instant_utc
        || !taiyin_c_internal::valid_struct(out_virtual_time)) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    taiyin::SplitJulianDate cpp_instant;
    taiyin::CalendarDateTime cpp_time;
    const taiyin::Status status = taiyin::ziwei::step_flow_day_target(
        taiyin_c_internal::to_cpp_split_jd(*current_instant_utc),
        taiyin_c_internal::to_cpp_datetime(*current_virtual_time),
        direction,
        &cpp_instant,
        &cpp_time);
    if (status == TAIYIN_STATUS_OK) {
        taiyin_c_internal::from_cpp_split_jd(cpp_instant, out_instant_utc);
        taiyin_c_internal::from_cpp_datetime(cpp_time, out_virtual_time);
    }
    return taiyin_c_internal::pack_call_result(status);
}

}  // extern "C"
