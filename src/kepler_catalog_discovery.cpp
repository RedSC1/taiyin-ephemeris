#include "taiyin/internal/kepler_catalog_discovery.h"

#include "taiyin/internal/kepler_file.h"
#include "taiyin/internal/path_utils.h"

#include <vector>

namespace taiyin {
namespace internal {
EphemerisDiscoveryStatus discover_kepler_file(
    const std::string& path,
    const EphemerisDiscoveryOptions&,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept {
    if (!out || !has_suffix(path, ".tke1")) {
        return DiscoveryNotApplicable;
    }

    try {
        std::vector<KeplerElements> elements;
        EphemerisBlockDescriptor descriptor;
        if (!load_kepler_file_with_block_id(path, static_cast<uint64_t>(out->size() + 1), &elements, &descriptor)) {
            return DiscoveryError;
        }
        out->push_back(descriptor);
    } catch (...) {
        return DiscoveryError;
    }

    return DiscoveryOk;
}

void append_kepler_ephemeris_discoverer(
    std::vector<EphemerisDiscoverFileFn>* out
) noexcept {
    if (!out) {
        return;
    }
    try {
        out->push_back(&discover_kepler_file);
    } catch (...) {
    }
}

bool collect_kepler_descriptors_from_directory(
    const std::string& root,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept {
    if (!out) {
        return false;
    }

    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_kepler_ephemeris_discoverer(&discoverers);
    EphemerisDiscoveryOptions options;
    options.strict = true;
    return discover_ephemeris_descriptors_from_directory(root, discoverers, options, out);
}

bool discover_kepler_catalog_from_directory(
    const std::string& root,
    EphemerisBlockCatalog* out
) noexcept {
    if (!out) {
        return false;
    }

    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_kepler_ephemeris_discoverer(&discoverers);
    EphemerisDiscoveryOptions options;
    options.strict = true;
    return discover_ephemeris_catalog_from_directory(root, discoverers, options, out);
}

}  // namespace internal
}  // namespace taiyin
