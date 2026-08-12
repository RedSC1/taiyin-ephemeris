#include "taiyin/internal/opm2_catalog_discovery.h"

#include "taiyin/internal/ephemeris_file_loader.h"
#include "taiyin/internal/opm2.h"
#include "taiyin/internal/path_utils.h"

namespace taiyin {
namespace internal {
namespace {

EphemerisFrame frame_from_opm2_frame_id(uint32_t frame_id) noexcept {
    return frame_id == OPM2_FRAME_ECLIPTIC_J2000
        ? EphemerisFrame::IcrfJ2000Equatorial
        : EphemerisFrame::FrameUnknown;
}

bool make_descriptor_from_opm2_file(
    const std::string& path,
    uint64_t block_id,
    EphemerisBlockDescriptor* out
) noexcept {
    if (!out || path.empty()) {
        return false;
    }

    EphemerisFileView bytes;
    Opm2EpheSection ephe;
    Opm2GridSection grid;
    uint32_t header_source_id = OPM2_SOURCE_UNDEFINED;
    if (!bytes.open_readonly(path)
        || !parse_opm2_summary(
            bytes.data(), bytes.size(), &ephe, &grid, &header_source_id)) {
        return false;
    }

    const EphemerisFrame frame = frame_from_opm2_frame_id(ephe.frame_id);
    if (frame == EphemerisFrame::FrameUnknown) {
        return false;
    }

    const int center_id = ephe.center_id;
    EphemerisBlockDescriptor descriptor;
    descriptor.route_key = EphemerisRouteKey(
        ephe.target_id,
        center_id,
        static_cast<int>(OPM2_METHOD_ID),
        static_cast<int>(block_id));
    descriptor.source_key = EphemerisBlockKey(
        normalize_opm2_source_id(header_source_id),
        block_id,
        OPM2_CONTAINER_VERSION,
        0);
    descriptor.target_id = ephe.target_id;
    descriptor.center_id = center_id;
    descriptor.method_id = static_cast<int>(OPM2_METHOD_ID);
    descriptor.frame = frame;
    descriptor.format = EphemerisBlockFormat::Opm2;
    descriptor.jd_tdb_start = ephe.coverage_start_jd;
    descriptor.jd_tdb_end = ephe.coverage_end_jd;
    descriptor.path = path;
    descriptor.cache_policy.kind = CacheNaturalSegment;
    descriptor.cache_policy.origin_jd = grid.origin_jd;
    descriptor.cache_policy.span_days = grid.period_days;
    descriptor.cache_policy.first_index = grid.first_segment_index;
    descriptor.cache_policy.count = grid.segment_count;

    *out = descriptor;
    return true;
}

}  // namespace

EphemerisDiscoveryStatus discover_opm2_file(
    const std::string& path,
    const EphemerisDiscoveryOptions&,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept {
    if (!out || !has_suffix_case_insensitive(path, ".opm2")) {
        return DiscoveryNotApplicable;
    }

    try {
        EphemerisBlockDescriptor descriptor;
        if (!make_descriptor_from_opm2_file(path, static_cast<uint64_t>(out->size() + 1), &descriptor)) {
            return DiscoveryError;
        }
        out->push_back(descriptor);
    } catch (...) {
        return DiscoveryError;
    }

    return DiscoveryOk;
}

void append_opm2_ephemeris_discoverer(
    std::vector<EphemerisDiscoverFileFn>* out
) noexcept {
    if (!out) {
        return;
    }
    try {
        out->push_back(&discover_opm2_file);
    } catch (...) {
    }
}

bool collect_opm2_descriptors_from_directory(
    const std::string& root,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept {
    if (!out) {
        return false;
    }

    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_opm2_ephemeris_discoverer(&discoverers);
    EphemerisDiscoveryOptions options;
    options.strict = true;
    return discover_ephemeris_descriptors_from_directory(root, discoverers, options, out);
}

bool discover_opm2_catalog_from_directory(
    const std::string& root,
    EphemerisBlockCatalog* out
) noexcept {
    if (!out) {
        return false;
    }

    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_opm2_ephemeris_discoverer(&discoverers);
    EphemerisDiscoveryOptions options;
    options.strict = true;
    return discover_ephemeris_catalog_from_directory(root, discoverers, options, out);
}

}  // namespace internal
}  // namespace taiyin
