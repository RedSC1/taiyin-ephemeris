#include "taiyin/internal/ephemeris_discovery.h"

#include "taiyin/internal/kepler_catalog_discovery.h"
#include "taiyin/internal/opm2_catalog_discovery.h"
#include "taiyin/internal/path_utils.h"
#include "taiyin/internal/spk_catalog_discovery.h"
#include "taiyin/internal/tkc1_catalog_discovery.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#if defined(_WIN32)
#include "taiyin/internal/win32_dirent.h"
#else
#include <dirent.h>
#endif
#include <limits>
#include <sys/stat.h>
#include <vector>

namespace taiyin {
namespace internal {
namespace {

bool collect_file_paths_recursive(const std::string& root, std::vector<std::string>* out) {
    if (!out) {
        return false;
    }

    DIR* dir = opendir(root.c_str());
    if (!dir) {
        return false;
    }

    bool ok = true;
    while (dirent* entry = readdir(dir)) {
        const char* name = entry->d_name;
        if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) {
            continue;
        }

        const std::string path = join_path(root, name);
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            ok = false;
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (!collect_file_paths_recursive(path, out)) {
                ok = false;
            }
        } else if (S_ISREG(st.st_mode)) {
            out->push_back(path);
        }
    }

    closedir(dir);
    return ok;
}

}  // namespace

void append_builtin_ephemeris_discoverers(
    std::vector<EphemerisDiscoverFileFn>* out
) noexcept {
    append_opm2_ephemeris_discoverer(out);
    append_spk_ephemeris_discoverer(out);
    append_kepler_ephemeris_discoverer(out);
    append_tkc1_ephemeris_discoverer(out);
}

bool cache_bucket_id_for_jd(
    const EphemerisBlockDescriptor& source,
    const SplitJulianDate& jd_tdb,
    int* out_bucket_id
) noexcept {
    if (!out_bucket_id
        || !split_julian_date_is_finite(jd_tdb)
        || !std::isfinite(source.jd_tdb_start)
        || !std::isfinite(source.jd_tdb_end)
        || source.jd_tdb_end <= source.jd_tdb_start
        || days_between_split_jd_and_double(jd_tdb, source.jd_tdb_start) > 0.0
        || days_between_split_jd_and_double(jd_tdb, source.jd_tdb_end) <= 0.0) {
        return false;
    }

    if (source.cache_policy.kind == CacheWholeEntry) {
        *out_bucket_id = 0;
        return true;
    }

    const double bucket_days = source.cache_policy.span_days;
    if (!std::isfinite(source.cache_policy.origin_jd)
        || !std::isfinite(bucket_days)
        || bucket_days <= 0.0) {
        return false;
    }
    const double bucket_double = std::floor(
        -days_between_split_jd_and_double(jd_tdb, source.cache_policy.origin_jd)
            / bucket_days);
    if (!std::isfinite(bucket_double)
        || bucket_double < static_cast<double>(source.cache_policy.first_index)
        || bucket_double >= static_cast<double>(source.cache_policy.first_index)
            + static_cast<double>(source.cache_policy.count)
        || bucket_double < static_cast<double>(std::numeric_limits<int>::min())
        || bucket_double > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }

    const int bucket_id = static_cast<int>(bucket_double);
    *out_bucket_id = bucket_id;
    return true;
}

bool make_cache_bucket_descriptor_for_jd(
    const EphemerisBlockDescriptor& source,
    const SplitJulianDate& jd_tdb,
    EphemerisBlockDescriptor* out
) noexcept {
    if (!out) {
        return false;
    }

    int bucket_id = 0;
    if (!cache_bucket_id_for_jd(source, jd_tdb, &bucket_id)) {
        return false;
    }

    if (source.cache_policy.kind == CacheWholeEntry) {
        *out = source;
        out->route_key = EphemerisRouteKey(
            source.target_id,
            source.center_id,
            source.method_id,
            bucket_id);
        return true;
    }

    const double bucket_days = source.cache_policy.span_days;
    const double bucket_start = source.cache_policy.origin_jd
        + static_cast<double>(bucket_id) * bucket_days;
    const double bucket_end = bucket_start + bucket_days;
    if (!std::isfinite(bucket_start) || !std::isfinite(bucket_end) || bucket_end <= bucket_start) {
        return false;
    }
    const double result_start = std::max(source.jd_tdb_start, bucket_start);
    const double result_end = std::min(source.jd_tdb_end, bucket_end);
    if (result_end <= result_start) {
        return false;
    }
    *out = source;
    out->jd_tdb_start = result_start;
    out->jd_tdb_end = result_end;
    if (!split_julian_date_from_double(result_start, &out->jd_tdb_start_split)
        || !split_julian_date_from_double(result_end, &out->jd_tdb_end_split)) {
        return false;
    }
    out->route_key = EphemerisRouteKey(
        source.target_id,
        source.center_id,
        source.method_id,
        bucket_id);
    return true;
}

bool discover_ephemeris_descriptors_from_directory(
    const std::string& root,
    const std::vector<EphemerisDiscoverFileFn>& discoverers,
    const EphemerisDiscoveryOptions& options,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept {
    if (!out || root.empty() || !directory_exists(root)) {
        return false;
    }

    try {
        out->clear();

        std::vector<std::string> paths;
        if (!collect_file_paths_recursive(root, &paths)) {
            return false;
        }
        std::sort(paths.begin(), paths.end());

        for (size_t path_index = 0; path_index < paths.size(); ++path_index) {
            for (size_t discoverer_index = 0; discoverer_index < discoverers.size(); ++discoverer_index) {
                EphemerisDiscoverFileFn discoverer = discoverers[discoverer_index];
                if (!discoverer) {
                    continue;
                }

                const size_t before = out->size();
                const EphemerisDiscoveryStatus status = discoverer(paths[path_index], options, out);
                if (status == DiscoveryNotApplicable) {
                    continue;
                }
                if (status == DiscoveryOk) {
                    if (out->size() == before && options.strict) {
                        return false;
                    }
                    break;
                }

                out->resize(before);
                if (options.strict) {
                    return false;
                }
                break;
            }
        }
    } catch (...) {
        out->clear();
        return false;
    }

    return !out->empty();
}

bool discover_ephemeris_catalog_from_directory(
    const std::string& root,
    const std::vector<EphemerisDiscoverFileFn>& discoverers,
    const EphemerisDiscoveryOptions& options,
    EphemerisBlockCatalog* out
) noexcept {
    if (!out) {
        return false;
    }

    std::vector<EphemerisBlockDescriptor> descriptors;
    if (!discover_ephemeris_descriptors_from_directory(root, discoverers, options, &descriptors)) {
        return false;
    }

    for (size_t i = 0; i < descriptors.size(); ++i) {
        if (!out->add(descriptors[i])) {
            return false;
        }
    }
    return true;
}

}  // namespace internal
}  // namespace taiyin
