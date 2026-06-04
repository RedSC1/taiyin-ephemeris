#include "taiyin/internal/tkc1_catalog_discovery.h"

#include "taiyin/internal/kepler_catalog_tkc1.h"
#include "taiyin/internal/path_utils.h"

#include <vector>

namespace taiyin {
namespace internal {
EphemerisDiscoveryStatus discover_tkc1_file(
    const std::string& path,
    const EphemerisDiscoveryOptions&,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept {
    if (!out || !has_suffix(path, ".tkc1")) {
        return DiscoveryNotApplicable;
    }

    try {
        Tkc1Catalog catalog;
        if (!tkc1_catalog_load_from_file(&catalog, path)) {
            return DiscoveryError;
        }
        const size_t before = out->size();
        for (uint32_t i = 0; catalog.header && i < catalog.header->object_count; ++i) {
            EphemerisBlockDescriptor descriptor;
            if (!tkc1_make_descriptor_for_object(path, &catalog, i, &descriptor)) {
                out->resize(before);
                tkc1_catalog_destroy(&catalog);
                return DiscoveryError;
            }
            out->push_back(descriptor);
        }
        tkc1_catalog_destroy(&catalog);
    } catch (...) {
        return DiscoveryError;
    }

    return DiscoveryOk;
}

void append_tkc1_ephemeris_discoverer(
    std::vector<EphemerisDiscoverFileFn>* out
) noexcept {
    if (!out) {
        return;
    }
    try {
        out->push_back(&discover_tkc1_file);
    } catch (...) {
    }
}

bool collect_tkc1_descriptors_from_directory(
    const std::string& root,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept {
    if (!out) {
        return false;
    }

    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_tkc1_ephemeris_discoverer(&discoverers);
    EphemerisDiscoveryOptions options;
    options.strict = true;
    return discover_ephemeris_descriptors_from_directory(root, discoverers, options, out);
}

bool discover_tkc1_catalog_from_directory(
    const std::string& root,
    EphemerisBlockCatalog* out
) noexcept {
    if (!out) {
        return false;
    }

    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_tkc1_ephemeris_discoverer(&discoverers);
    EphemerisDiscoveryOptions options;
    options.strict = true;
    return discover_ephemeris_catalog_from_directory(root, discoverers, options, out);
}

}  // namespace internal
}  // namespace taiyin
