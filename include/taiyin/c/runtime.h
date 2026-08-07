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

TAIYIN_C_API void TAIYIN_C_CALL taiyin_runtime_config_init(
    taiyin_runtime_config* config
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
TAIYIN_C_API size_t TAIYIN_C_CALL taiyin_runtime_cache_entry_count(void);

#ifdef __cplusplus
}
#endif

#endif
