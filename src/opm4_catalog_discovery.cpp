#include "taiyin/internal/opm4_catalog_discovery.h"

#include "taiyin/internal/ephemeris_file_loader.h"
#include "taiyin/internal/opm4.h"
#include "taiyin/internal/path_utils.h"

#include <vector>

namespace taiyin {
namespace internal {
namespace {

EphemerisFrame frame_from_opm4_frame_id(int frame_id) noexcept {
    return frame_id == static_cast<int>(EphemerisFrame::IcrfJ2000Equatorial)
        ? EphemerisFrame::IcrfJ2000Equatorial
        : EphemerisFrame::FrameUnknown;
}

bool make_descriptor_from_opm4_file(
    const std::string& path,
    uint64_t block_id,
    EphemerisBlockDescriptor* out
) noexcept {
    if (!out || path.empty()) {
        return false;
    }

    std::vector<uint8_t> bytes;
    OPM4Header header;
    if (!load_ephemeris_file_bytes(path, &bytes)
        || bytes.empty()
        || !parse_opm4_header(&bytes[0], bytes.size(), &header)) {
        return false;
    }

    const EphemerisFrame frame = frame_from_opm4_frame_id(header.frame_id);
    if (frame == EphemerisFrame::FrameUnknown) {
        return false;
    }

    EphemerisBlockDescriptor descriptor;
    descriptor.route_key = EphemerisRouteKey(
        header.target_id,
        header.center_id,
        header.method_id,
        static_cast<int>(block_id));
    descriptor.source_key = EphemerisBlockKey(1, block_id, 1, 0);
    descriptor.target_id = header.target_id;
    descriptor.center_id = header.center_id;
    descriptor.method_id = header.method_id;
    descriptor.frame = frame;
    descriptor.format = EphemerisBlockFormat::Opm4;
    descriptor.jd_tdb_start = header.jd_tdb_start;
    descriptor.jd_tdb_end = header.jd_tdb_end;
    descriptor.path = path;

    *out = descriptor;
    return true;
}

}  // namespace

EphemerisDiscoveryStatus discover_opm4_file(
    const std::string& path,
    const EphemerisDiscoveryOptions&,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept {
    if (!out || !has_suffix(path, ".opm4")) {
        return DiscoveryNotApplicable;
    }

    try {
        EphemerisBlockDescriptor descriptor;
        if (!make_descriptor_from_opm4_file(path, static_cast<uint64_t>(out->size() + 1), &descriptor)) {
            return DiscoveryError;
        }
        out->push_back(descriptor);
    } catch (...) {
        return DiscoveryError;
    }

    return DiscoveryOk;
}

void append_opm4_ephemeris_discoverer(
    std::vector<EphemerisDiscoverFileFn>* out
) noexcept {
    if (!out) {
        return;
    }
    try {
        out->push_back(&discover_opm4_file);
    } catch (...) {
    }
}

bool collect_opm4_descriptors_from_directory(
    const std::string& root,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept {
    if (!out) {
        return false;
    }

    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_opm4_ephemeris_discoverer(&discoverers);
    EphemerisDiscoveryOptions options;
    options.strict = true;
    return discover_ephemeris_descriptors_from_directory(root, discoverers, options, out);
}

bool discover_opm4_catalog_from_directory(
    const std::string& root,
    EphemerisBlockCatalog* out
) noexcept {
    if (!out) {
        return false;
    }

    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_opm4_ephemeris_discoverer(&discoverers);
    EphemerisDiscoveryOptions options;
    options.strict = true;
    return discover_ephemeris_catalog_from_directory(root, discoverers, options, out);
}

}  // namespace internal
}  // namespace taiyin
