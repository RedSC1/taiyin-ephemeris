#ifndef TAIYIN_C_RUNTIME_H
#define TAIYIN_C_RUNTIME_H

#include "taiyin/c/base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct taiyin_runtime_config {
    uint32_t struct_size;
    uint32_t api_version;
    size_t segment_cache_max_entries;
    const char* const* source_paths;
    size_t source_path_count;
    const char* data_root;
    const char* eop_path;
    const char* lunar_limb_path;
    taiyin_bool load_packaged_data;
    taiyin_bool load_builtin_eop;
    taiyin_bool strict_discovery;
    uint8_t reserved0;
} taiyin_runtime_config;

enum taiyin_runtime_data_source_kind {
    TAIYIN_RUNTIME_DATA_SOURCE_EPHEMERIS = 1,
    TAIYIN_RUNTIME_DATA_SOURCE_EARTH_ORIENTATION = 2,
    TAIYIN_RUNTIME_DATA_SOURCE_LUNAR_LIMB = 3
};

enum taiyin_runtime_data_source_format {
    TAIYIN_RUNTIME_DATA_FORMAT_UNKNOWN = 0,
    TAIYIN_RUNTIME_DATA_FORMAT_OPM2 = 1,
    TAIYIN_RUNTIME_DATA_FORMAT_SPK = 2,
    TAIYIN_RUNTIME_DATA_FORMAT_KEPLER = 3,
    TAIYIN_RUNTIME_DATA_FORMAT_SEMI_ANALYTIC = 4,
    TAIYIN_RUNTIME_DATA_FORMAT_FIXED_STAR = 5,
    TAIYIN_RUNTIME_DATA_FORMAT_TSC1 = 6,
    TAIYIN_RUNTIME_DATA_FORMAT_TKC1 = 7,
    TAIYIN_RUNTIME_DATA_FORMAT_CUSTOM = 8,
    TAIYIN_RUNTIME_DATA_FORMAT_FINALS2000A = 100,
    TAIYIN_RUNTIME_DATA_FORMAT_BUILTIN_EOP = 101,
    TAIYIN_RUNTIME_DATA_FORMAT_TLL1 = 200,
    TAIYIN_RUNTIME_DATA_FORMAT_MEMORY = 1000
};

enum taiyin_runtime_data_source_flags {
    TAIYIN_RUNTIME_DATA_SOURCE_HAS_COVERAGE = 1u << 0,
    TAIYIN_RUNTIME_DATA_SOURCE_BUILTIN = 1u << 1,
    TAIYIN_RUNTIME_DATA_SOURCE_MEMORY = 1u << 2
};

typedef struct taiyin_runtime_registered_data_source {
    uint32_t struct_size;
    uint32_t kind;
    uint32_t format;
    uint32_t flags;
    uint64_t item_count;
    double jd_start;
    double jd_end;
} taiyin_runtime_registered_data_source;

TAIYIN_C_API void TAIYIN_C_CALL taiyin_runtime_config_init(
    taiyin_runtime_config* config
);
TAIYIN_C_API void TAIYIN_C_CALL
taiyin_runtime_registered_data_source_init(
    taiyin_runtime_registered_data_source* value
);
/*
 * This is a process-wide setup-time operation and must not overlap
 * calculations. The first successful initialization preserves native position
 * evaluators registered during setup. Every later valid initialization attempt
 * is a runtime reset and clears C-owned native position evaluators plus C-owned
 * astrology callbacks before initialization can fail, so stale language-runtime
 * callbacks cannot survive a hot restart. A C-owned house callback still used
 * as a fallback by a non-C-API C++ house model is retained until that dependent
 * C++ model is removed.
 */
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_runtime_initialize(
    const taiyin_runtime_config* config
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_runtime_add_source_path(
    const char* path
);
/* Setup-time selection override. `path_or_basename` may be the exact loaded
 * path or a bare filename such as "jup349.bsp". The supplied value replaces
 * that file's provider-default numeric priority. Higher values are preferred
 * inside the file's provider and, under AUTO, also reorder source-specific
 * product rules such as JUP365/JUP349 or DE442/DE441. Provider/method route
 * boundaries remain unchanged. Clearing an override restores the provider
 * default. Matching follows platform path-case convention: case-insensitive
 * on Windows, case-sensitive on POSIX. */
TAIYIN_C_API taiyin_status TAIYIN_C_CALL
taiyin_runtime_set_ephemeris_source_priority(
    const char* path_or_basename,
    int32_t priority
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL
taiyin_runtime_clear_ephemeris_source_priority(
    const char* path_or_basename
);
TAIYIN_C_API void TAIYIN_C_CALL
taiyin_runtime_clear_all_ephemeris_source_priorities(void);
/*
 * Global EOP and lunar-limb mutations are setup-time operations. Do not call
 * them concurrently with calculations.
 */
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_runtime_load_eop_table(
    const char* path
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL
taiyin_runtime_load_builtin_eop_table(void);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_runtime_clear_eop_table(void);
TAIYIN_C_API taiyin_bool TAIYIN_C_CALL taiyin_runtime_has_eop_table(void);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_runtime_load_lunar_limb_model(
    const char* path
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_runtime_clear_lunar_limb_model(void);
TAIYIN_C_API taiyin_bool TAIYIN_C_CALL
taiyin_runtime_has_lunar_limb_model(void);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_runtime_clear_ephemeris_cache(void);
TAIYIN_C_API size_t TAIYIN_C_CALL taiyin_runtime_catalog_size(void);
TAIYIN_C_API size_t TAIYIN_C_CALL
taiyin_runtime_registered_data_source_count(void);
/*
 * `source` is a physical path for file-backed data and a stable label such as
 * "builtin:semi-analytic" for built-in data. Call with source=NULL and
 * source_capacity=0 to query the required UTF-8 byte count, including NUL.
 */
TAIYIN_C_API taiyin_status TAIYIN_C_CALL
taiyin_runtime_get_registered_data_source(
    size_t index,
    taiyin_runtime_registered_data_source* out,
    char* source,
    size_t source_capacity,
    size_t* out_required_source_size
);
TAIYIN_C_API size_t TAIYIN_C_CALL taiyin_runtime_cache_entry_count(void);

#ifdef __cplusplus
}
#endif

#endif
