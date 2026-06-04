#include "taiyin/internal/tsc1_catalog_discovery.h"

#include "taiyin/star_catalog_tsc1.h"
#include "taiyin/internal/path_utils.h"

#include <cmath>
#include <vector>

namespace taiyin {
namespace internal {
namespace {

const uint64_t TSC1_DISCOVERY_SOURCE_ID = 3;
const uint32_t TSC1_DISCOVERY_GENERATION = 1;
const uint32_t TSC1_DISCOVERY_PURPOSE = 0;

double epoch_year_to_jd_tdb(double epoch_year) noexcept {
    return EPHEMERIS_DISCOVERY_J2000_JD
        + (epoch_year - 2000.0) * DAYS_PER_JULIAN_YEAR;
}

bool catalog_epoch_range_jd(const Tsc1Header* header, double* out_start, double* out_end) noexcept {
    if (!out_start || !out_end) {
        return false;
    }

    double start = TSC1_DEFAULT_JD_TDB_START;
    double end = TSC1_DEFAULT_JD_TDB_END;
    if (header
        && std::isfinite(header->catalog_min_epoch)
        && std::isfinite(header->catalog_max_epoch)
        && header->catalog_max_epoch > header->catalog_min_epoch) {
        start = epoch_year_to_jd_tdb(header->catalog_min_epoch);
        end = epoch_year_to_jd_tdb(header->catalog_max_epoch);
    }

    if (!std::isfinite(start) || !std::isfinite(end) || end <= start) {
        return false;
    }

    *out_start = start;
    *out_end = end;
    return true;
}

bool make_descriptor_from_tsc1_file(
    const std::string& path,
    uint64_t block_id,
    EphemerisBlockDescriptor* out
) noexcept {
    if (!out || path.empty()) {
        return false;
    }

    Tsc1Catalog catalog;
    if (!tsc1_catalog_load_from_file(&catalog, path)) {
        return false;
    }

    double jd_tdb_start = 0.0;
    double jd_tdb_end = 0.0;
    const bool has_epoch_range = catalog_epoch_range_jd(catalog.header, &jd_tdb_start, &jd_tdb_end);
    tsc1_catalog_destroy(&catalog);
    if (!has_epoch_range) {
        return false;
    }

    const int bucket_id = static_cast<int>(block_id);
    EphemerisBlockDescriptor descriptor;
    descriptor.route_key = EphemerisRouteKey(
        0,
        TSC1_DEFAULT_CENTER_ID,
        TSC1_STAR_METHOD_ID,
        bucket_id);
    descriptor.source_key = EphemerisBlockKey(
        TSC1_DISCOVERY_SOURCE_ID,
        block_id,
        TSC1_DISCOVERY_GENERATION,
        TSC1_DISCOVERY_PURPOSE);
    descriptor.target_id = 0;
    descriptor.center_id = TSC1_DEFAULT_CENTER_ID;
    descriptor.method_id = TSC1_STAR_METHOD_ID;
    descriptor.frame = EphemerisFrame::IcrfJ2000Equatorial;
    descriptor.format = EphemerisBlockFormat::Tsc1;
    descriptor.jd_tdb_start = jd_tdb_start;
    descriptor.jd_tdb_end = jd_tdb_end;
    descriptor.path = path;

    *out = descriptor;
    return true;
}

}  // namespace

EphemerisDiscoveryStatus discover_tsc1_file(
    const std::string& path,
    const EphemerisDiscoveryOptions&,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept {
    if (!out || !has_suffix(path, ".tsc1")) {
        return DiscoveryNotApplicable;
    }

    try {
        EphemerisBlockDescriptor descriptor;
        if (!make_descriptor_from_tsc1_file(path, static_cast<uint64_t>(out->size() + 1), &descriptor)) {
            return DiscoveryError;
        }
        out->push_back(descriptor);
    } catch (...) {
        return DiscoveryError;
    }

    return DiscoveryOk;
}

void append_tsc1_ephemeris_discoverer(
    std::vector<EphemerisDiscoverFileFn>* out
) noexcept {
    if (!out) {
        return;
    }
    try {
        out->push_back(&discover_tsc1_file);
    } catch (...) {
    }
}

bool collect_tsc1_descriptors_from_directory(
    const std::string& root,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept {
    if (!out) {
        return false;
    }

    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_tsc1_ephemeris_discoverer(&discoverers);
    EphemerisDiscoveryOptions options;
    options.strict = true;
    return discover_ephemeris_descriptors_from_directory(root, discoverers, options, out);
}

}  // namespace internal
}  // namespace taiyin
