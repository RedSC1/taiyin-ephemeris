#include "taiyin/c/runtime.h"

#include "c_api_internal.h"
#include "position_lifecycle_internal.h"
#include "astrology_lifecycle_internal.h"
#include "taiyin/runtime/runtime.h"

#include <cstring>
#include <mutex>
#include <new>
#include <vector>

namespace {

bool g_c_runtime_initialized = false;

}  // namespace

extern "C" {

void TAIYIN_C_CALL taiyin_runtime_config_init(taiyin_runtime_config* config) {
    if (!config) return;
    std::memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->api_version = TAIYIN_C_ABI_VERSION;
    config->segment_cache_max_entries = 4096;
    config->load_packaged_data = 1u;
    config->load_builtin_eop = 1u;
}

void TAIYIN_C_CALL taiyin_runtime_registered_data_source_init(
    taiyin_runtime_registered_data_source* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

taiyin_status TAIYIN_C_CALL taiyin_runtime_initialize(
    const taiyin_runtime_config* config
) {
    if (!taiyin_c_internal::valid_struct(config)
        || config->api_version != TAIYIN_C_ABI_VERSION
        || (!config->source_paths && config->source_path_count != 0)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::runtime::EphemerisRuntimeConfig cpp;
    cpp.segment_cache_max_entries = config->segment_cache_max_entries;
    cpp.source_paths = config->source_paths;
    cpp.source_path_count = config->source_path_count;
    cpp.data_root = config->data_root;
    cpp.eop_path = config->eop_path;
    cpp.lunar_limb_path = config->lunar_limb_path;
    cpp.load_packaged_data = config->load_packaged_data != 0u;
    cpp.load_builtin_eop = config->load_builtin_eop != 0u;
    cpp.strict_discovery = config->strict_discovery != 0u;
    try {
        std::lock_guard<std::mutex> lifecycle_lock(
            taiyin_c_internal::native_position_lifecycle_mutex());
        std::lock_guard<std::mutex> astrology_lifecycle_lock(
            taiyin_c_internal::astrology_model_lifecycle_mutex());
        if (g_c_runtime_initialized) {
            // Clear before the reset can fail. Language runtimes may already
            // have discarded the isolate/group that owned the callbacks.
            taiyin_c_internal::clear_c_ayanamsha_models_locked();
            taiyin_c_internal::clear_c_house_system_models_locked();
            taiyin_c_internal::clear_native_position_evaluators_locked();
        }
        if (!taiyin::runtime::initialize_global_ephemeris_runtime(cpp)) {
            return taiyin::TAIYIN_ERROR_INTERNAL;
        }
        g_c_runtime_initialized = true;
        return taiyin::TAIYIN_STATUS_OK;
    } catch (...) {
        return taiyin::TAIYIN_ERROR_INTERNAL;
    }
}

taiyin_status TAIYIN_C_CALL taiyin_runtime_add_source_path(const char* path) {
    if (!path || path[0] == '\0') return taiyin_c_internal::invalid_argument();
    return taiyin::runtime::add_global_ephemeris_source_path(path)
        ? taiyin::TAIYIN_STATUS_OK
        : taiyin::TAIYIN_FILE_ERROR_DISCOVERY_FAILED;
}

taiyin_status TAIYIN_C_CALL taiyin_runtime_set_ephemeris_source_priority(
    const char* path_or_basename,
    int32_t priority
) {
    if (!path_or_basename || path_or_basename[0] == '\0') {
        return taiyin_c_internal::invalid_argument();
    }
    return taiyin::runtime::set_global_ephemeris_source_priority(
        path_or_basename,
        static_cast<int>(priority))
        ? TAIYIN_STATUS_OK
        : TAIYIN_ERROR_INTERNAL;
}

taiyin_status TAIYIN_C_CALL taiyin_runtime_clear_ephemeris_source_priority(
    const char* path_or_basename
) {
    if (!path_or_basename || path_or_basename[0] == '\0') {
        return taiyin_c_internal::invalid_argument();
    }
    return taiyin::runtime::clear_global_ephemeris_source_priority(
        path_or_basename)
        ? TAIYIN_STATUS_OK
        : TAIYIN_ERROR_INTERNAL;
}

void TAIYIN_C_CALL taiyin_runtime_clear_all_ephemeris_source_priorities(void) {
    taiyin::runtime::clear_all_global_ephemeris_source_priorities();
}

taiyin_status TAIYIN_C_CALL taiyin_runtime_load_eop_table(const char* path) {
    if (!path || path[0] == '\0') return taiyin_c_internal::invalid_argument();
    return taiyin::runtime::load_global_earth_orientation_table(path);
}

taiyin_status TAIYIN_C_CALL taiyin_runtime_load_builtin_eop_table(void) {
    return taiyin::runtime::load_global_builtin_earth_orientation_table();
}

void TAIYIN_C_CALL taiyin_runtime_clear_eop_table(void) {
    taiyin::runtime::set_global_earth_orientation_table(0);
}

taiyin_bool TAIYIN_C_CALL taiyin_runtime_has_eop_table(void) {
    return taiyin::runtime::global_earth_orientation_table() ? 1u : 0u;
}

taiyin_status TAIYIN_C_CALL taiyin_runtime_load_lunar_limb_model(
    const char* path
) {
    if (!path || path[0] == '\0') return taiyin_c_internal::invalid_argument();
    return taiyin::runtime::load_global_lunar_limb_model(path);
}

void TAIYIN_C_CALL taiyin_runtime_clear_lunar_limb_model(void) {
    taiyin::runtime::load_global_lunar_limb_model(0);
}

taiyin_bool TAIYIN_C_CALL taiyin_runtime_has_lunar_limb_model(void) {
    return taiyin::runtime::global_lunar_limb_model() ? 1u : 0u;
}

void TAIYIN_C_CALL taiyin_runtime_clear_ephemeris_cache(void) {
    taiyin::runtime::clear_global_ephemeris_cache();
}

size_t TAIYIN_C_CALL taiyin_runtime_catalog_size(void) {
    return taiyin::runtime::global_ephemeris_catalog_size();
}

size_t TAIYIN_C_CALL taiyin_runtime_registered_data_source_count(void) {
    std::vector<taiyin::runtime::RegisteredDataSource> sources;
    return taiyin::runtime::get_global_registered_data_sources(&sources)
        ? sources.size()
        : 0u;
}

taiyin_status TAIYIN_C_CALL taiyin_runtime_get_registered_data_source(
    size_t index,
    taiyin_runtime_registered_data_source* out,
    char* source,
    size_t source_capacity,
    size_t* out_required_source_size
) {
    if (!taiyin_c_internal::valid_struct(out)
        || (!source && source_capacity != 0u)
        || !out_required_source_size) {
        return taiyin_c_internal::invalid_argument();
    }
    try {
        std::vector<taiyin::runtime::RegisteredDataSource> sources;
        if (!taiyin::runtime::get_global_registered_data_sources(&sources)) {
            return taiyin::TAIYIN_ERROR_INTERNAL;
        }
        if (index >= sources.size()) {
            return taiyin_c_internal::invalid_argument();
        }
        const taiyin::runtime::RegisteredDataSource& item = sources[index];
        taiyin_runtime_registered_data_source_init(out);
        out->kind = static_cast<uint32_t>(item.kind);
        out->format = static_cast<uint32_t>(item.format);
        out->flags = item.flags;
        out->item_count = static_cast<uint64_t>(item.item_count);
        out->jd_start = item.jd_start;
        out->jd_end = item.jd_end;

        *out_required_source_size = item.source.size() + 1u;
        if (source && source_capacity > 0u) {
            const size_t copy_size = item.source.size() < source_capacity - 1u
                ? item.source.size()
                : source_capacity - 1u;
            std::memcpy(source, item.source.data(), copy_size);
            source[copy_size] = '\0';
        }
        return !source || source_capacity >= *out_required_source_size
            ? taiyin::TAIYIN_STATUS_OK
            : taiyin::TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (const std::bad_alloc&) {
        return taiyin::TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return taiyin::TAIYIN_ERROR_INTERNAL;
    }
}

size_t TAIYIN_C_CALL taiyin_runtime_cache_entry_count(void) {
    return taiyin::runtime::global_ephemeris_cache_entry_count();
}

}  // extern "C"
